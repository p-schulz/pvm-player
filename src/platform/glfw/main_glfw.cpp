#include <cstdio>
#include <vector>

#include "app.h"
#include "platform/glfw/glfw_platform.h"

int main(int argc, char** argv) {
    // Declared first, destroyed last: App tears its GL and mpv state down
    // against the platform's still-live window.
    GlfwPlatform platform;
    if (!platform.init(1280, 720, "PVM Player")) {
        return 1;
    }

    App app;
    if (!app.init(platform)) {
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

    std::vector<input::InputEvent> events;
    while (!platform.quitRequested()) {
        events.clear();
        platform.pollEvents(events);
        app.frame(events);
        platform.swapBuffers();
    }
    return 0;
}
