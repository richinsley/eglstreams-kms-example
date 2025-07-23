cmake_minimum_required(VERSION 3.10)

project(eglstreams-kms-example C)

# Set C standard and compiler flags
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# Find packages using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(OPENGL REQUIRED gl)
pkg_check_modules(LIBDRM REQUIRED libdrm)

# Add executable
add_executable(eglstreams-kms-example
    main.c
    egl.c
    kms.c
    utils.c
    eglgears.c
)

# Add include directories
target_include_directories(eglstreams-kms-example PRIVATE
    # "${PROJECT_SOURCE_DIR}/khronos"
    ${EGL_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS}
    ${LIBDRM_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(eglstreams-kms-example
    PRIVATE
        ${EGL_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${LIBDRM_LIBRARIES}
        m
)