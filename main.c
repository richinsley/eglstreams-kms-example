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

#include "utils.h"
#include "egl.h"
#include "kms.h"
#include "eglgears.h"
#include <stdlib.h> // For atoi
#include <stdio.h>  // For printf
#include <string.h> // For strcmp

/*
 * Example code demonstrating how to connect EGL to DRM KMS using
 * EGLStreams.
 */

int main(int argc, char *argv[])
{
    EGLDisplay eglDpy;
    EGLDeviceEXT eglDevice;
    int drmFd, width, height;
    int desired_width = 0, desired_height = 0, desired_refresh = 0;
    int hdr_enabled = 0;
    uint32_t planeID = 0;
    EGLSurface eglSurface;

    // Argument parsing
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--hdr") == 0) {
            hdr_enabled = 1;
        } else if (i + 2 < argc && desired_width == 0) {
            desired_width = atoi(argv[i]);
            desired_height = atoi(argv[++i]);
            desired_refresh = atoi(argv[++i]);
        }
    }

    if (hdr_enabled) {
        printf("HDR mode requested.\n");
    }

    GetEglExtensionFunctionPointers();
    eglDevice = GetEglDevice();
    drmFd = GetDrmFd(eglDevice);

    SetMode(drmFd, desired_width, desired_height, desired_refresh, hdr_enabled,
            &planeID, &width, &height);

    eglDpy = GetEglDisplay(eglDevice, drmFd);
    eglSurface = SetUpEgl(eglDpy, planeID, width, height, hdr_enabled);

    InitGears(width, height);

    while (1) {
        DrawGears();
        eglSwapBuffers(eglDpy, eglSurface);
        PrintFps();
    }

    return 0;
}