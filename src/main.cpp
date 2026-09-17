#include <cstdio>
#include <vector>

#include "app.h"

int main(int argc, char** argv) {
    App app;
    if (!app.init(1280, 720, "PVM Player")) {
        return 1;
    }

    // Any argv entries are media root directories the root menu's
    // "Play Media" entry browses (one or more configured folders, each
    // nested-navigable, and browsable above the root too -- see
    // FileBrowser). Defaults to the current working directory when none
    // are given.
    std::vector<std::string> roots(argv + 1, argv + argc);
    if (roots.empty()) {
        std::fprintf(stdout,
                      "No media roots given; browsing the current directory. "
                      "Pass one or more directories as arguments to browse elsewhere.\n");
    }
    app.setMediaRoots(roots);

    app.run();
    return 0;
}
