#pragma once

// The one place that decides where GL declarations come from: the glad
// loader (GL 3.3 core) on desktop, the system GLES 3 headers on Android.
// Every GL-using file includes this instead of <glad/gl.h>. Desktop function
// pointers are loaded by the platform layer (see platform/glfw), so nothing
// outside it ever calls the loader.
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif
