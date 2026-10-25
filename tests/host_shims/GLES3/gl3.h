// Host-test stand-in for <GLES3/gl3.h>: the types and the two calls
// VROOpenXRPanelCompositor.cpp makes. The test defines the functions.
#pragma once
#include <cstdint>
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA8 0x8058
void glFlush();
void glFinish();
