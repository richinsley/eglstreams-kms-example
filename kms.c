/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#include <xf86drmMode.h>
#include <xf86drm.h>

#include "kms.h"
#include "utils.h"

// Config, PropertyIDs, and PropertyIDAddresses structs remain the same...
struct Config {
    uint32_t connectorID;
    uint32_t crtcID;
    int crtcIndex;
    uint32_t planeID;
    drmModeModeInfo mode;
    uint16_t width;
    uint16_t height;
};

struct PropertyIDs {

    struct {
        uint32_t mode_id;
        uint32_t active;
    } crtc;

    struct {
        uint32_t src_x;
        uint32_t src_y;
        uint32_t src_w;
        uint32_t src_h;
        uint32_t crtc_x;
        uint32_t crtc_y;
        uint32_t crtc_w;
        uint32_t crtc_h;
        uint32_t fb_id;
        uint32_t crtc_id;
    } plane;

    struct {
        uint32_t crtc_id;
    } connector;
};

struct PropertyIDAddresses {
    const char *name;
    uint32_t *ptr;
};


/*
 * Pick the first connected connector we find with usable modes and
 * CRTC.
 */
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

                // If a full spec is given (width, height, refresh)
                if (desired_width > 0 && desired_height > 0 && desired_refresh > 0) {
                    if (current_mode->hdisplay == desired_width &&
                        current_mode->vdisplay == desired_height &&
                        current_mode->vrefresh == (uint32_t)desired_refresh) {
                        best_mode = current_mode;
                        break; // Exact match found
                    }
                }
                // If only resolution is given
                else if (desired_width > 0 && desired_height > 0) {
                     if (current_mode->hdisplay == desired_width &&
                         current_mode->vdisplay == desired_height) {
                         // Take the first matching resolution if refresh isn't specified
                         if (!best_mode) {
                            best_mode = current_mode;
                         }
                     }
                }
            }

            // If no specific mode was found, take the preferred one (first in the list)
            if (!best_mode) {
                 best_mode = &pConnector->modes[0];
                 if (desired_width > 0) { // Only print if a selection was attempted
                     printf("Desired mode (%dx%d @ %dHz) not found. Using default: %dx%d @ %dHz.\n",
                            desired_width, desired_height, desired_refresh,
                            best_mode->hdisplay, best_mode->vdisplay, best_mode->vrefresh);
                 }
            }

            pConfig->mode = *best_mode;
            pConfig->connectorID = pModeRes->connectors[i];

            // Find a suitable CRTC for this connector
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
                return; // Found a complete setup
            }
        }
        drmModeFreeConnector(pConnector);
    }

    Fatal("Could not find a suitable connector.\n");
}


// GetPropertyValue and PickPlane remain the same...
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
        Fatal("Unable to find value for property \'%s\'.\n", propName);
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


/*
 * Pick a connector, CRTC, and plane to use for the modeset.
 */
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


// CreateModeID, CreateFb, AssignPropertyIDs, and AssignAtomicRequest remain the same...
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

static void AssignPropertyIDsOneType(int drmFd,
                                     uint32_t objectID,
                                     uint32_t objectType,
                                     struct PropertyIDAddresses *table,
                                     size_t tableLen)
{
    uint32_t i;
    drmModeObjectPropertiesPtr pModeObjectProperties =
        drmModeObjectGetProperties(drmFd, objectID, objectType);

    if (pModeObjectProperties == NULL) {
        Fatal("Unable to query mode object properties.\n");
    }

    for (i = 0; i < pModeObjectProperties->count_props; i++) {

        uint32_t j;
        drmModePropertyPtr pProperty =
            drmModeGetProperty(drmFd, pModeObjectProperties->props[i]);

        if (pProperty == NULL) {
            Fatal("Unable to query property.\n");
        }

        for (j = 0; j < tableLen; j++) {
            if (strcmp(table[j].name, pProperty->name) == 0) {
                *(table[j].ptr) = pProperty->prop_id;
                break;
            }
        }

        drmModeFreeProperty(pProperty);
    }

    drmModeFreeObjectProperties(pModeObjectProperties);

    for (i = 0; i < tableLen; i++) {
        if (*(table[i].ptr) == 0) {
            Fatal("Unable to find property ID for \'%s\'.\n", table[i].name);
        }
    }
}

static void AssignPropertyIDs(int drmFd,
                              const struct Config *pConfig,
                              struct PropertyIDs *pPropertyIDs)
{
    struct PropertyIDAddresses crtcTable[] = {
        { "MODE_ID", &pPropertyIDs->crtc.mode_id      },
        { "ACTIVE",  &pPropertyIDs->crtc.active       },
    };

    struct PropertyIDAddresses planeTable[] = {
        { "SRC_X",   &pPropertyIDs->plane.src_x       },
        { "SRC_Y",   &pPropertyIDs->plane.src_y       },
        { "SRC_W",   &pPropertyIDs->plane.src_w       },
        { "SRC_H",   &pPropertyIDs->plane.src_h       },
        { "CRTC_X",  &pPropertyIDs->plane.crtc_x      },
        { "CRTC_Y",  &pPropertyIDs->plane.crtc_y      },
        { "CRTC_W",  &pPropertyIDs->plane.crtc_w      },
        { "CRTC_H",  &pPropertyIDs->plane.crtc_h      },
        { "FB_ID",   &pPropertyIDs->plane.fb_id       },
        { "CRTC_ID", &pPropertyIDs->plane.crtc_id     },
    };

    struct PropertyIDAddresses connectorTable[] = {
        { "CRTC_ID", &pPropertyIDs->connector.crtc_id },
    };

    AssignPropertyIDsOneType(drmFd, pConfig->crtcID,
                             DRM_MODE_OBJECT_CRTC,
                             crtcTable, ARRAY_LEN(crtcTable));
    AssignPropertyIDsOneType(drmFd, pConfig->planeID,
                             DRM_MODE_OBJECT_PLANE,
                             planeTable, ARRAY_LEN(planeTable));
    AssignPropertyIDsOneType(drmFd, pConfig->connectorID,
                             DRM_MODE_OBJECT_CONNECTOR,
                             connectorTable, ARRAY_LEN(connectorTable));
}

static void AssignAtomicRequest(int drmFd,
                                drmModeAtomicReqPtr pAtomic,
                                const struct Config *pConfig,
                                uint32_t modeID, uint32_t fb)
{
    struct PropertyIDs propertyIDs = { 0 };

    AssignPropertyIDs(drmFd, pConfig, &propertyIDs);


    /* Specify the mode to use on the CRTC, and make the CRTC active. */

    drmModeAtomicAddProperty(pAtomic, pConfig->crtcID,
                             propertyIDs.crtc.mode_id, modeID);
    drmModeAtomicAddProperty(pAtomic, pConfig->crtcID,
                             propertyIDs.crtc.active, 1);

    /* Tell the connector to receive pixels from the CRTC. */

    drmModeAtomicAddProperty(pAtomic, pConfig->connectorID,
                             propertyIDs.connector.crtc_id, pConfig->crtcID);

    /*
     * Specify the region of source surface to display (i.e., the
     * "ViewPortIn").  Note these values are in 16.16 format, so shift
     * up by 16.
     */

    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.src_x, 0);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.src_y, 0);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.src_w, pConfig->width << 16);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.src_h, pConfig->height << 16);

    /*
     * Specify the region within the mode where the image should be
     * displayed (i.e., the "ViewPortOut").
     */

    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.crtc_x, 0);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.crtc_y, 0);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.crtc_w, pConfig->width);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.crtc_h, pConfig->height);

    /*
     * Specify the surface to display in the plane, and connect the
     * plane to the CRTC.
     *
     * XXX for EGLStreams purposes, it would be nice to have the
     * option of not specifying a surface at this point, as well as to
     * be able to have the KMS atomic modeset consume a frame from an
     * EGLStream.
     */

    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.fb_id, fb);
    drmModeAtomicAddProperty(pAtomic, pConfig->planeID,
                             propertyIDs.plane.crtc_id, pConfig->crtcID);
}


/*
 * Use the atomic DRM KMS API to set a mode on a CRTC.
 *
 * On success, return the non-zero ID of a DRM plane to which to
 * present, and its dimensions.  On failure, exit with a fatal error
 * message.
 */
void SetMode(int drmFd, int desired_width, int desired_height, int desired_refresh,
             uint32_t *pPlaneID, int *pWidth, int *pHeight)
{
    struct Config config = { 0 };
    drmModeAtomicReqPtr pAtomic;
    uint32_t modeID, fb;
    int ret;
    const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

    PickConfig(drmFd, desired_width, desired_height, desired_refresh, &config);

    modeID = CreateModeID(drmFd, &config);
    fb = CreateFb(drmFd, &config);

    pAtomic = drmModeAtomicAlloc();

    AssignAtomicRequest(drmFd, pAtomic, &config, modeID, fb);

    ret = drmModeAtomicCommit(drmFd, pAtomic, flags, NULL /* user_data */);

    drmModeAtomicFree(pAtomic);

    if (ret != 0) {
        Fatal("Failed to set mode.\n");
    }

    *pPlaneID = config.planeID;
    *pWidth = config.width;
    *pHeight = config.height;

    printf("Mode set to %dx%d @ %dHz\n", config.width, config.height, config.mode.vrefresh);
}