#pragma once

#include <string>

// Directory containing the running executable (no trailing slash).
// Used so bundled assets (shaders, fonts) resolve correctly regardless of
// the process's current working directory.
std::string exeDir();
