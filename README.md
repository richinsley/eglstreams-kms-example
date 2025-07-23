eglstreams-kms-example
======================
Forked from:
https://github.com/aritger/eglstreams-kms-example

This is a simple example program demonstrating the use of EGLStreams in conjunction with DRM KMS.  Build, and then run as root from a console, without an X server running.


Requirements to Use
-------------------

* Use NVIDIA Linux GPU driver release 364.12 or later.
* Use a Linux 4.1 or later kernel with DRM support enabled.
* Enable NVIDIA's DRM KMS support:
    modprobe -r nvidia-drm ; modprobe nvidia-drm modeset=1
* Stop the X server, or any other potential DRM client.  eglstreams-kms-example requires the ability to become DRM master in order to do modesets.
* **libdrm-dev** (or equivalent for your distribution) needs to be installed.
* **pkg-config** needs to be installed.
* **CMake 3.10** or later.

This example program demonstrates:

* Using the EGL_EXT_device_base EGL extension to enumerate the GPUs in the system, discovering an EGLDevice for each.

* Using the EGL_EXT_device_drm EGL extension to find the DRM device file of an EGLDevice.

* Using the DRM KMS atomic API to set a mode on a CRTC, and find a DRM KMS plane usable with that CRTC.

* Using the EGL_EXT_platform_base and EGL_EXT_platform_device EGL extensions to create and EGLDisplay from an EGLDevice.

* Using EGL_EXT_output_base and EGL_EXT_output_drm to find the EGLOutputLayer that corresponds to the DRM KMS plane found above.

* Using EGL_KHR_stream, EGL_EXT_stream_consumer_egloutput, and EGL_KHR_stream_producer_eglsurface to create an EGLStream that connects an EGLSurface to the EGLOutputLayer found above.

With the above, calling eglSwapBuffers() on the EGLSurface producer of the EGLStream presents the final frames to the DRM KMS plane.

Dependencies
------------

* This relies on NVIDIA Linux GPU driver release 364.12 or later, which includes support for all of the above extensions.

* The Makefile links eglstreams-kms-example against libEGL.so and libOpenGL.so, the latter of which is provided by GLVND (enabled by default in NVIDIA driver releases 364.12 or later).

* The use of libdrm requires your distribution's libdrm development package.  E.g., on recent Fedora versions, the needed package is "libdrm-devel".

## How to Build

1.  **Create a build directory:**
    ```bash
    mkdir build && cd build
    ```

2.  **Run CMake:**
    ```bash
    cmake ..
    ```

3.  **Compile the project:**
    ```bash
    make
    ```

The executable `eglstreams-kms-example` will be created in the `build` directory.

## How to Run

Run the program as root from a console where no other display server (like X11 or Wayland) is running.

To use the default resolution and refresh rate:
```bash
sudo ./build/eglstreams-kms-example
```

To specify a resolution (e.g., 1920x1080):
```bash
sudo ./build/eglstreams-kms-example 1920 1080
```

To specify a resolution and refresh rate (e.g., 1920x1080 at 120Hz):
```bash
sudo ./build/eglstreams-kms-example 1920 1080 120
```

Concerns
--------

* The Mesa EGL implementation achieves comparable functionality with libgbm instead of the EGLStream-based solution described above.  With libgbm, the application uses libgbm both for bootstrapping the EGLDisplay and for allocating surfaces which can be presented using the DRM KMS atomic API.  However, we feel an EGLStream-based solution is ultimately better because it gives the EGL implementation:

  * Explicit information about how the surface is going to be used (the consumer and producer are defined at surface creation time).

  * An opportunity, as part of eglSwapBuffers() to do any final graphics work that is required before the surface is displayed (e.g., downsampling, decompressing, etc).

* As-is, the EGLStream-based solution described above does not fit well with DRM KMS atomic: there isn't currently a way for a frame of an EGLStream to be consumed as part of an DRM_IOCTL_MODE_ATOMIC request.

EGL Extension Specs Used Here
-----------------------------

[EGL_EXT_device_base](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_device_base)  
[EGL_EXT_device_enumeration](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_device_enumeration)  
[EGL_EXT_device_query](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_device_query)  
[EGL_EXT_device_drm](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_device_drm)  

[EGL_EXT_platform_base](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_platform_base)  
[EGL_EXT_platform_device](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_platform_device)  
[EGL_EXT_output_base](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_output_base)  

[EGL_KHR_stream](https://www.khronos.org/registry/egl/extensions/KHR/EGL_KHR_stream)  
[EGL_EXT_stream_consumer_egloutput](https://www.khronos.org/registry/egl/extensions/EXT/EGL_EXT_stream_consumer_egloutput)  
[EGL_KHR_stream_producer_eglsurface](https://www.khronos.org/registry/egl/extensions/KHR/EGL_KHR_stream_producer_eglsurface)  
