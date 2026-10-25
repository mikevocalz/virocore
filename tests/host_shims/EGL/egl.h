// Host-test stand-in for <EGL/egl.h>: opaque handles used by OpenXR's GLES
// graphics-binding structs.
#pragma once
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef unsigned int EGLenum;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef void *EGLClientBuffer;
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
