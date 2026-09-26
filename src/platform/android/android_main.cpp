// Entry point for the Android build: the GameActivity's native thread runs
// android_main(), which owns the AndroidPlatform (EGL) and the App, pumps the
// activity's event loop and renders a frame per iteration while a window
// exists and the activity is resumed.

#include <android/keycodes.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "platform/android/android_platform.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PVM", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PVM", __VA_ARGS__)

namespace {

struct Session {
    android_app* app = nullptr;
    std::unique_ptr<AndroidPlatform> platform;
    std::unique_ptr<App> pvm;
    bool resumed = false;

    // Rendering needs everything at once: an app, a surface, and an activity
    // in the foreground (a surface can outlive the foreground briefly, e.g.
    // while the screen is turning off).
    bool canRender() const { return pvm && platform->hasWindow() && resumed; }

    // (Re)builds the App on the platform's current context.
    bool startApp() {
        pvm = std::make_unique<App>();
        if (!pvm->init(*platform)) {
            LOGE("App::init failed");
            pvm.reset();
            return false;
        }
        return true;
    }

    // The GL context is gone for good: drop everything that lived in it and
    // start over on a fresh one.
    void recoverFromContextLoss() {
        LOGI("Rebuilding after EGL context loss");
        pvm.reset();
        platform->destroyContext();
        if (platform->attachWindow()) {
            startApp();
        }
    }
};

void onAppCmd(android_app* app, int32_t cmd) {
    auto* session = static_cast<Session*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("window created");
            if (app->window && session->platform->attachWindow() && !session->pvm) {
                if (!session->startApp()) {
                    GameActivity_finish(app->activity);
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("window destroyed");
            session->platform->detachWindow();
            break;
        case APP_CMD_RESUME:
            LOGI("resumed");
            session->resumed = true;
            break;
        case APP_CMD_PAUSE:
            LOGI("paused");
            session->resumed = false;
            break;
        default:
            break;
    }
}

// Which key events the app takes for itself. Everything the D-pad, face
// buttons and keyboard produce is ours (BACK included, so the Activity is not
// finished behind the app's back); volume and the like stay with the system.
bool keyEventFilter(const GameActivityKeyEvent* event) {
    switch (event->keyCode) {
        case AKEYCODE_VOLUME_UP:
        case AKEYCODE_VOLUME_DOWN:
        case AKEYCODE_VOLUME_MUTE:
        case AKEYCODE_POWER:
            return false;
        default:
            return true;
    }
}

// The app reports through stdout/stderr (settings warnings, test-hook stats);
// Android drops both, so pipe them into logcat under the tag "PVM-stdio".
void redirectStdioToLogcat() {
    static int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        return;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    std::thread([] {
        std::string pending;
        char buffer[512];
        ssize_t n;
        while ((n = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
            pending.append(buffer, static_cast<size_t>(n));
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                __android_log_write(ANDROID_LOG_INFO, "PVM-stdio", pending.substr(0, newline).c_str());
                pending.erase(0, newline + 1);
            }
        }
    }).detach();
}

// Android has no environment to set per launch, so the PVM_TEST_* hooks
// (frame stats, autoclose, screenshots, simulated keys...) are read from
// <dataDir>/test_env.txt instead: one KEY=VALUE per line. Absent in normal
// use.
void loadTestEnvironment(const std::string& dataDir) {
    std::ifstream file(dataDir + "/test_env.txt");
    std::string line;
    while (std::getline(file, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line.compare(0, 9, "PVM_TEST_") == 0) {
            setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 1);
            LOGI("test hook: %s", line.c_str());
        }
    }
}

void drainInput(Session& session) {
    android_input_buffer* buffer = android_app_swap_input_buffers(session.app);
    if (!buffer) {
        return;
    }
    for (uint64_t i = 0; i < buffer->keyEventsCount; ++i) {
        const GameActivityKeyEvent& key = buffer->keyEvents[i];
        session.platform->queueKeyEvent(key.keyCode, key.action, key.repeatCount);
    }
    android_app_clear_key_events(buffer);
    // No touch UI (out of scope); just don't let the queue fill up.
    android_app_clear_motion_events(buffer);
}

}  // namespace

extern "C" void android_main(android_app* app) {
    static bool stdioRedirected = false;  // android_main can run again in one process
    if (!stdioRedirected) {
        redirectStdioToLogcat();
        stdioRedirected = true;
    }

    Session session;
    session.app = app;
    session.platform = std::make_unique<AndroidPlatform>(app);
    loadTestEnvironment(session.platform->dataDir());

    app->userData = &session;
    app->onAppCmd = onAppCmd;
    android_app_set_key_event_filter(app, keyEventFilter);

    std::vector<input::InputEvent> events;
    while (true) {
        // Block for events while there is nothing to draw; otherwise just
        // drain what is pending (vsync in swapBuffers() paces the loop).
        int pollEvents = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(session.canRender() ? 0 : -1, nullptr, &pollEvents,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                break;
            }
        }
        if (app->destroyRequested) {
            break;
        }

        drainInput(session);
        events.clear();
        session.platform->pollEvents(events);

        if (!session.canRender()) {
            continue;  // input that arrives while backgrounded is dropped
        }
        session.pvm->frame(events);
        session.platform->swapBuffers();
        if (session.platform->contextLost()) {
            session.recoverFromContextLoss();
        }
        if (session.platform->quitRequested()) {
            // App asked to quit: the Activity finishes and APP_CMD_DESTROY
            // follows; stop drawing meanwhile.
            session.pvm.reset();
        }
    }

    // GL objects must be freed while the context is still current.
    session.pvm.reset();
    session.platform.reset();
    app->userData = nullptr;
}
