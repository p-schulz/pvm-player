#pragma once

#include <string>

// Compiles+links a vertex/fragment shader pair loaded from disk.
// Returns the GL program name, or 0 on failure (details logged to stderr).
unsigned int loadShaderProgram(const std::string& vertPath, const std::string& fragPath);
