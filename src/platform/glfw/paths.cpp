#include "paths.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdlib>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

std::string exeDir() {
    std::string path;

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        path.assign(buf, len);
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char real[PATH_MAX];
        if (realpath(buf, real)) {
            path = real;
        } else {
            path = buf;
        }
    }
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        path = buf;
    }
#endif

    if (path.empty()) {
        return ".";
    }
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? "." : path.substr(0, pos);
}
