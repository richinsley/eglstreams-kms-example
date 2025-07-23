#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <drm/drm_mode.h>

#include "kms.h"
#include "utils.h"

// --- Fallback definitions for older libdrm versions ---
#ifndef HDR_METADATA_TYPE1
#define HDR_METADATA_TYPE1 1
#endif
#ifndef HDMI_EOTF_SMPTE_ST2084
#define HDMI_EOTF_TRADITIONAL_GAMMA_SDR    0
#define HDMI_EOTF_TRADITIONAL_GAMMA_HDR    1
#define HDMI_EOTF_SMPTE_ST2084             2
#define HDMI_EOTF_HLG                      3
#endif
#ifndef DRM_MODE_COLORIMETRY_BT2020_YCC
// Found in recent drm_mode.h, but define it for older versions
#define DRM_MODE_COLORIMETRY_BT2020_YCC 10
#endif
// --- End of fallback definitions ---

struct Config {
    uint32_t connectorID;
    uint32_t crtcID;
    int crtcIndex;
    uint32_t planeID;
    drmModeModeInfo mode;
    uint16_t width;
    uint16_t height;
};

// This struct now stores the object ID where the property was found
typedef struct {
    uint32_t id;
    uint32_t object_id;
} DrmProperty;

struct PropertyIDs {
    DrmProperty mode_id, active;
    DrmProperty fb_id, crtc_id;
    DrmProperty src_x, src_y, src_w, src_h;
    DrmProperty crtc_x, crtc_y, crtc_w, crtc_h;
    DrmProperty connector_crtc_id;
    DrmProperty hdr_output_metadata;
    DrmProperty colorspace;
    DrmProperty eotf; // Will hold NV_CRTC_REGAMMA_TF
};

static void FindProperty(int drmFd, uint32_t object_id, uint32_t object_type, const char *prop_name, DrmProperty *property)
{
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drmFd, object_id, object_type);
    if (!props) return;

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(drmFd, props->props[i]);
        if (prop) {
            if (strcmp(prop->name, prop_name) == 0) {
                property->id = prop->prop_id;
                property->object_id = object_id;
                drmModeFreeProperty(prop);
                break;
            }
            drmModeFreeProperty(prop);
        }
    }
    drmModeFreeObjectProperties(props);
}

// Helper to get an enum's value from its string name
static uint64_t GetEnumValue(int drmFd, uint32_t prop_id, const char* enum_name) {
    drmModePropertyPtr prop = drmModeGetProperty(drmFd, prop_id);
    if (!prop) return 0;

    for (int i = 0; i < prop->count_enums; i++) {
        if (strcmp(prop->enums[i].name, enum_name) == 0) {
            uint64_t value = prop->enums[i].value;
            drmModeFreeProperty(prop);
            return value;
        }
    }

    drmModeFreeProperty(prop);
    return 0; // Not found
}


static void AssignPropertyIDs(int drmFd, const struct Config *pConfig, struct PropertyIDs *pPropertyIDs)
{
    // Find CRTC properties
    FindProperty(drmFd, pConfig->crtcID, DRM_MODE_OBJECT_CRTC, "MODE_ID", &pPropertyIDs->mode_id);
    FindProperty(drmFd, pConfig->crtcID, DRM_MODE_OBJECT_CRTC, "ACTIVE", &pPropertyIDs->active);

    // Find Plane properties
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "FB_ID", &pPropertyIDs->fb_id);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &pPropertyIDs->crtc_id);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "SRC_X", &pPropertyIDs->src_x);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "SRC_Y", &pPropertyIDs->src_y);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "SRC_W", &pPropertyIDs->src_w);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "SRC_H", &pPropertyIDs->src_h);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "CRTC_X", &pPropertyIDs->crtc_x);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "CRTC_Y", &pPropertyIDs->crtc_y);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "CRTC_W", &pPropertyIDs->crtc_w);
    FindProperty(drmFd, pConfig->planeID, DRM_MODE_OBJECT_PLANE, "CRTC_H", &pPropertyIDs->crtc_h);

    // Find Connector properties
    FindProperty(drmFd, pConfig->connectorID, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &pPropertyIDs->connector_crtc_id);

    // Find HDR properties
    FindProperty(drmFd, pConfig->connectorID, DRM_MODE_OBJECT_CONNECTOR, "Colorspace", &pPropertyIDs->colorspace);
    FindProperty(drmFd, pConfig->connectorID, DRM_MODE_OBJECT_CONNECTOR, "HDR_OUTPUT_METADATA", &pPropertyIDs->hdr_output_metadata);
    
    // NVIDIA specific EOTF property on the CRTC
    FindProperty(drmFd, pConfig->crtcID, DRM_MODE_OBJECT_CRTC, "NV_CRTC_REGAMMA_TF", &pPropertyIDs->eotf);
}

// All other functions (CreateHdrMetadataBlob, PickConnector, etc.) remain the same.
// ... (Insert the rest of the kms.c code from the previous response here) ...
static void PickConnector(int drmFd,
                          drmModeResPtr pModeRes,
                          int desired_width, int desired_height, int desired_refresh,
                          struct Config *pConfig)
{
    int i, j, k;

    // Find a connected connector
    for (i = 0; i < pModeRes->count_connectors; i++) {
        drmModeConnectorPtr pConnector = drmModeGetConnector(drmFd, pModeRes->connectors[i]);
        if (!pConnector) continue;

        if (pConnector->connection == DRM_MODE_CONNECTED && pConnector->count_modes > 0) {
            drmModeModeInfo *best_mode = NULL;

            // Iterate through modes to find the best match
            for (k = 0; k < pConnector->count_modes; k++) {
                drmModeModeInfo *current_mode = &pConnector->modes[k];

                if (desired_width > 0 && desired_height > 0) {
                     if (current_mode->hdisplay == desired_width &&
                         current_mode->vdisplay == desired_height) {
                         if (desired_refresh > 0) {
                             if(current_mode->vrefresh == desired_refresh) {
                                best_mode = current_mode;
                                break; // Exact match found
                             }
                         } else {
                            if (!best_mode) {
                                best_mode = current_mode;
                            }
                         }
                     }
                }
            }

            if (!best_mode) {
                 best_mode = &pConnector->modes[0];
                 if (desired_width > 0) {
                     printf("Desired mode (%dx%d @ %dHz) not found. Using default: %dx%d @ %dHz.\n",
                            desired_width, desired_height, desired_refresh,
                            best_mode->hdisplay, best_mode->vdisplay, best_mode->vrefresh);
                 }
            }

            pConfig->mode = *best_mode;
            pConfig->connectorID = pModeRes->connectors[i];

            drmModeEncoderPtr pEncoder = drmModeGetEncoder(drmFd, pConnector->encoders[0]);
            if (pEncoder) {
                for (j = 0; j < pModeRes->count_crtcs; j++) {
                    if ((pEncoder->possible_crtcs & (1 << j))) {
                        pConfig->crtcID = pModeRes->crtcs[j];
                        pConfig->crtcIndex = j;
                        break;
                    }
                }
                drmModeFreeEncoder(pEncoder);
            }

            drmModeFreeConnector(pConnector);

            if (pConfig->crtcID) {
                return;
            }
        }
        drmModeFreeConnector(pConnector);
    }

    Fatal("Could not find a suitable connector.\n");
}
static uint64_t GetPropertyValue(
    int drmFd,
    uint32_t objectID,
    uint32_t objectType,
    const char *propName)
{
    uint32_t i;
    int found = 0;
    uint64_t value = 0;
    drmModeObjectPropertiesPtr pModeObjectProperties =
        drmModeObjectGetProperties(drmFd, objectID, objectType);

    for (i = 0; i < pModeObjectProperties->count_props; i++) {

        drmModePropertyPtr pProperty =
            drmModeGetProperty(drmFd, pModeObjectProperties->props[i]);

        if (pProperty == NULL) {
            Fatal("Unable to query property.\n");
        }

        if (strcmp(propName, pProperty->name) == 0) {
            value = pModeObjectProperties->prop_values[i];
            found = 1;
        }

        drmModeFreeProperty(pProperty);

        if (found) {
            break;
        }
    }

    drmModeFreeObjectProperties(pModeObjectProperties);

    if (!found) {
        // This is not always a fatal error, some properties are optional.
        // The caller must handle the zero return value.
    }

    return value;
}
static void PickPlane(int drmFd, struct Config *pConfig)
{
    drmModePlaneResPtr pPlaneRes = drmModeGetPlaneResources(drmFd);
    uint32_t i;

    if (pPlaneRes == NULL) {
        Fatal("Unable to query DRM-KMS plane resources\n");
    }

    for (i = 0; i < pPlaneRes->count_planes; i++) {
        drmModePlanePtr pPlane = drmModeGetPlane(drmFd, pPlaneRes->planes[i]);
        uint32_t crtcs;
        uint64_t type;

        if (pPlane == NULL) {
            Fatal("Unable to query DRM-KMS plane %d\n", i);
        }

        crtcs = pPlane->possible_crtcs;

        drmModeFreePlane(pPlane);

        if ((crtcs & (1 << pConfig->crtcIndex)) == 0) {
            continue;
        }

        type = GetPropertyValue(drmFd, pPlaneRes->planes[i],
                                DRM_MODE_OBJECT_PLANE, "type");

        if (type == DRM_PLANE_TYPE_PRIMARY) {
            pConfig->planeID = pPlaneRes->planes[i];
            break;
        }
    }

    drmModeFreePlaneResources(pPlaneRes);

    if (pConfig->planeID == 0) {
        Fatal("Could not find a suitable plane.\n");
    }
}
static uint32_t CreateHdrMetadataBlob(int drmFd)
{
    struct hdr_output_metadata metadata = { 0 };
    uint32_t blob_id = 0;

    metadata.metadata_type = HDR_METADATA_TYPE1;
    metadata.hdmi_metadata_type1.eotf = HDMI_EOTF_SMPTE_ST2084;

    metadata.hdmi_metadata_type1.display_primaries[0].x = 15000;
    metadata.hdmi_metadata_type1.display_primaries[0].y = 35000;
    metadata.hdmi_metadata_type1.display_primaries[1].x = 7500;
    metadata.hdmi_metadata_type1.display_primaries[1].y = 3000;
    metadata.hdmi_metadata_type1.display_primaries[2].x = 34000;
    metadata.hdmi_metadata_type1.display_primaries[2].y = 16000;

    metadata.hdmi_metadata_type1.white_point.x = 15635;
    metadata.hdmi_metadata_type1.white_point.y = 16450;

    // Corrected value: max luminance is in cd/m^2
    metadata.hdmi_metadata_type1.max_display_mastering_luminance = 1000;
    // min luminance is in 0.0001 cd/m^2
    metadata.hdmi_metadata_type1.min_display_mastering_luminance = 1;

    metadata.hdmi_metadata_type1.max_cll = 1000;
    metadata.hdmi_metadata_type1.max_fall = 400;

    if (drmModeCreatePropertyBlob(drmFd, &metadata, sizeof(metadata), &blob_id) != 0) {
        Warning("Failed to create HDR metadata blob.\n");
        return 0;
    }

    return blob_id;
}

static void AssignAtomicRequest(int drmFd,
                                drmModeAtomicReqPtr pAtomic,
                                const struct Config *pConfig,
                                const struct PropertyIDs *pPropertyIDs,
                                uint32_t modeID, uint32_t fb, int hdr_enabled)
{
    // --- THIS IS THE CRITICAL FIX for the "Invalid argument" error ---
    // The previous code was using the property's object_id as the value.
    // We need to use the actual width and height from the mode config.
    // Source coordinates are in 16.16 fixed point.
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->src_w.object_id, pPropertyIDs->src_w.id, (uint64_t)pConfig->width << 16);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->src_h.object_id, pPropertyIDs->src_h.id, (uint64_t)pConfig->height << 16);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->crtc_w.object_id, pPropertyIDs->crtc_w.id, pConfig->width);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->crtc_h.object_id, pPropertyIDs->crtc_h.id, pConfig->height);
    // ----------------------------------------------------------------

    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->mode_id.object_id, pPropertyIDs->mode_id.id, modeID);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->active.object_id, pPropertyIDs->active.id, 1);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->connector_crtc_id.object_id, pPropertyIDs->connector_crtc_id.id, pConfig->crtcID);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->fb_id.object_id, pPropertyIDs->fb_id.id, fb);
    drmModeAtomicAddProperty(pAtomic, pPropertyIDs->crtc_id.object_id, pPropertyIDs->crtc_id.id, pConfig->crtcID);
    
    if (hdr_enabled) {
        if (pPropertyIDs->eotf.id) {
            // Get the enum value for "PQ (Perceptual Quantizer)"
            uint64_t pq_value = GetEnumValue(drmFd, pPropertyIDs->eotf.id, "PQ (Perceptual Quantizer)");
            if (pq_value) {
                drmModeAtomicAddProperty(pAtomic, pPropertyIDs->eotf.object_id, pPropertyIDs->eotf.id, pq_value);
            } else {
                 Warning("Could not find 'PQ (Perceptual Quantizer)' enum for NV_CRTC_REGAMMA_TF.\n");
            }
        } else {
            Warning("EOTF property (NV_CRTC_REGAMMA_TF) not found.\n");
        }

        if (pPropertyIDs->colorspace.id) {
            // The standard BT.2020 YCC value should work here
            drmModeAtomicAddProperty(pAtomic, pPropertyIDs->colorspace.object_id, pPropertyIDs->colorspace.id, DRM_MODE_COLORIMETRY_BT2020_YCC);
        } else {
            Warning("Colorspace property not found.\n");
        }

        if (pPropertyIDs->hdr_output_metadata.id) {
            uint32_t blob_id = CreateHdrMetadataBlob(drmFd);
            if (blob_id) {
                drmModeAtomicAddProperty(pAtomic, pPropertyIDs->hdr_output_metadata.object_id, pPropertyIDs->hdr_output_metadata.id, blob_id);
            }
        } else {
            Warning("HDR_OUTPUT_METADATA property not found.\n");
        }
    }
}
static void PickConfig(int drmFd, int desired_width, int desired_height, int desired_refresh, struct Config *pConfig)
{
    drmModeResPtr pModeRes;
    int ret;

    ret = drmSetClientCap(drmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    if (ret != 0) {
        Fatal("DRM_CLIENT_CAP_UNIVERSAL_PLANES not available.\n");
    }

    ret = drmSetClientCap(drmFd, DRM_CLIENT_CAP_ATOMIC, 1);

    if (ret != 0) {
        Fatal("DRM_CLIENT_CAP_ATOMIC not available.\n");
    }

    pModeRes = drmModeGetResources(drmFd);

    if (pModeRes == NULL) {
        Fatal("Unable to query DRM-KMS resources.\n");
    }

    PickConnector(drmFd, pModeRes, desired_width, desired_height, desired_refresh, pConfig);

    PickPlane(drmFd, pConfig);

    drmModeFreeResources(pModeRes);

    pConfig->width = pConfig->mode.hdisplay;
    pConfig->height = pConfig->mode.vdisplay;
}
static uint32_t CreateFb(int drmFd, const struct Config *pConfig)
{
    struct drm_mode_create_dumb createRequest = { 0 };
    struct drm_mode_map_dumb mapRequest = { 0 };
    uint8_t *map;
    uint32_t fb = 0;
    int ret;

    createRequest.width = pConfig->width;
    createRequest.height = pConfig->height;
    createRequest.bpp = 32;

    ret = drmIoctl(drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &createRequest);
    if (ret < 0) {
        Fatal("Unable to create dumb buffer.\n");
    }

    ret = drmModeAddFB(drmFd, pConfig->width, pConfig->height, 24, 32,
                       createRequest.pitch, createRequest.handle, &fb);
    if (ret) {
        Fatal("Unable to add fb.\n");
    }

    mapRequest.handle = createRequest.handle;

    ret = drmIoctl(drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mapRequest);
    if (ret) {
        Fatal("Unable to map dumb buffer.\n");
    }

    map = mmap(0, createRequest.size, PROT_READ | PROT_WRITE, MAP_SHARED,
               drmFd, mapRequest.offset);
    if (map == MAP_FAILED) {
        Fatal("Failed to mmap(2) fb.\n");
    }

    memset(map, 0, createRequest.size);

    return fb;
}
static uint32_t CreateModeID(int drmFd, const struct Config *pConfig)
{
    uint32_t modeID = 0;
    int ret = drmModeCreatePropertyBlob(drmFd,
                                        &pConfig->mode, sizeof(pConfig->mode),
                                        &modeID);
    if (ret != 0) {
        Fatal("Failed to create mode property.\n");
    }

    return modeID;
}
void SetMode(int drmFd, int desired_width, int desired_height, int desired_refresh, int hdr_enabled,
             uint32_t *pPlaneID, int *pWidth, int *pHeight)
{
    struct Config config = { 0 };
    struct PropertyIDs propertyIDs = { 0 };
    drmModeAtomicReqPtr pAtomic;
    uint32_t modeID, fb;
    int ret;
    const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_NONBLOCK;

    PickConfig(drmFd, desired_width, desired_height, desired_refresh, &config);
    AssignPropertyIDs(drmFd, &config, &propertyIDs);

    modeID = CreateModeID(drmFd, &config);
    fb = CreateFb(drmFd, &config);
    pAtomic = drmModeAtomicAlloc();
    
    AssignAtomicRequest(drmFd, pAtomic, &config, &propertyIDs, modeID, fb, hdr_enabled);

    ret = drmModeAtomicCommit(drmFd, pAtomic, flags, NULL);
    drmModeAtomicFree(pAtomic);

    if (ret != 0) {
        Fatal("Failed to set mode. Error: %s\n", strerror(-ret));
    }

    *pPlaneID = config.planeID;
    *pWidth = config.width;
    *pHeight = config.height;

    printf("Mode set to %dx%d @ %dHz\n", config.width, config.height, config.mode.vrefresh);
}