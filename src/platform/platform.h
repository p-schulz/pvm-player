#pragma once

// Everything App needs from the host it runs on -- window, GL context, input
// delivery, storage locations, power and display management -- behind one
// interface, so app.cpp contains no windowing-toolkit or OS #ifdefs. The
// desktop implementation is platform/glfw; the Android one arrives with the
// Android skeleton.
//
// Lifetime: the Platform is created first and destroyed last. App and
// MpvPlayer tear down their GL resources against a still-live context, then
// the platform destroys the window.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "input/input_action.h"

// One physical input caught by beginInputCapture() (Settings > Controls):
// `code` is this platform's own KeyMap code (bind()/actionsFor() use it
// directly), `name` is what keys.cfg would call it (round-trips through the
// platform's own codeFromName()).
struct CapturedInput {
    int code = 0;
    std::string name;
};

class Platform {
public:
    virtual ~Platform() = default;

    // --- Time -----------------------------------------------------------
    // Seconds from an arbitrary but fixed origin; monotonic.
    virtual double now() const = 0;

    // --- Window and GL --------------------------------------------------
    // Size of the drawable in pixels (not logical units; they differ on
    // HiDPI screens).
    virtual void framebufferSize(int& width, int& height) const = 0;
    // For libmpv's render context (and any GL loader that needs one).
    virtual void* glProcAddress(const char* name) const = 0;
    // The `#version` line for the ImGui GL backend's shaders.
    virtual const char* glslVersion() const = 0;
    virtual void swapBuffers() = 0;

    // The ImGui platform backend (display size, mouse, clipboard...). The
    // renderer backend is GL on every platform and stays in App. imguiInit()
    // runs after the ImGui context exists; input is *not* routed through it
    // (see pollEvents()).
    virtual bool imguiInit() = 0;
    virtual void imguiNewFrame() = 0;
    virtual void imguiShutdown() = 0;

    // --- Input ----------------------------------------------------------
    // Pumps the OS event loop and appends the actions that arrived since the
    // last call, already translated through the platform's key/button map.
    virtual void pollEvents(std::vector<input::InputEvent>& out) = 0;
    // The events a press of the key called `name` (keys.cfg spelling, e.g.
    // "ENTER", "F1", "7") would produce on this platform. Returns false if
    // the name isn't a key here. Lets PVM_TEST_SIMULATE_KEYS exercise the
    // real key map without App knowing key codes.
    virtual bool translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const = 0;
    // Test-only twin of translateKeyName(), for PVM_TEST_SIMULATE_KEYS to
    // exercise Settings > Controls without a real keyboard/gamepad: if a
    // capture is active (see beginInputCapture()), completes it exactly as
    // if `name` had been pressed for real. False (and no effect) if nothing
    // is capturing or `name` isn't a key here.
    virtual bool simulateCapturedInput(std::string_view name) = 0;
    // The left and right stick's current position (-1..1, dead zone already
    // applied). Unlike everything else here this is read once a frame while
    // a libretro game has focus and forwarded straight to retro::Session::
    // setAnalogStick(), instead of going through pollEvents() -- an analog
    // stick needs a continuous position, not a one-shot/held action. The
    // left stick is a RetroPad's/N64 pad's analog stick; the right is
    // offered too since some cores' own options (e.g. parallel_n64's
    // "C Button Mode") can read C buttons off a second stick instead of
    // digital buttons.
    virtual void gamepadStick(float& leftX, float& leftY, float& rightX, float& rightY) const = 0;

    // --- Remapping (Settings > Controls) ---------------------------------
    // Display names ("PAD_A", "F", ...) of what's currently bound to
    // `action`, in binding order; empty if unbound.
    virtual std::vector<std::string> bindingNames(input::Action action) const = 0;
    // Rebinds `action` to exactly this one physical input (dropping
    // whatever it was bound to before), effective immediately, and persists
    // the change to keys.cfg (its own line only -- see input::saveKeyBinding)
    // so it survives a restart. `code`/`name` come from takeCapturedInput().
    virtual void rebindAction(input::Action action, int code, const std::string& name) = 0;
    // Restores `action`'s compiled-in default binding(s), discarding any
    // keys.cfg override (including ones from before this session).
    virtual void resetActionBinding(input::Action action) = 0;
    // Starts listening for the next physical press (a key or gamepad
    // button) so the Controls menu can capture it for rebindAction(); while
    // active, that press does not also reach pollEvents() as a normal
    // action, so binding a button doesn't also activate whatever it's
    // asking to be bound to. One-shot: ends itself once something nameable
    // is captured (an unnameable key, e.g. a bare punctuation key with no
    // keys.cfg spelling, is silently ignored and capture keeps waiting).
    virtual void beginInputCapture() = 0;
    // Ends capture without having caught anything (the user backed out).
    virtual void cancelInputCapture() = 0;
    // Non-null once beginInputCapture() has caught something; polled once a
    // frame while capturing. Consumes the result (reset to nullopt) once read.
    virtual std::optional<CapturedInput> takeCapturedInput() = 0;

    // --- Storage --------------------------------------------------------
    // Where config.cfg, user-edited *.cfg, keys.cfg and caches live
    // (writable, no trailing slash).
    virtual std::string dataDir() const = 0;
    // Where the shipped read-only files live: shaders/, assets/ and the
    // *.default.cfg files (no trailing slash).
    virtual std::string assetDir() const = 0;
    // Where the teletext and Mediathek page caches live (no trailing slash).
    // The OS may clear it at any time, which the services tolerate; on
    // desktop it is a subfolder of dataDir().
    virtual std::string cacheDir() const { return dataDir() + "/cache"; }
    // Directories searched for libretro cores (shared libraries named
    // *_libretro*), in priority order.
    virtual std::vector<std::string> coreDirs() const { return {dataDir() + "/cores"}; }
    // Media folders to browse when none were given explicitly.
    virtual std::vector<std::string> defaultMediaRoots() const = 0;

    // Whether the app may read the user's media folders. Always true on
    // desktop; on Android it is the "all files access" permission, which the
    // user grants in a system settings screen.
    virtual bool storageAccessGranted() const { return true; }
    // Opens that system screen (no-op where there is nothing to ask for).
    virtual void requestStorageAccess() {}

    // --- Lifecycle and power ---------------------------------------------
    // Keeps the display from idle-sleeping while true (called every frame
    // with the current playing-and-unpaused state, so implementations must
    // make a no-change call cheap).
    virtual void setKeepAwake(bool on) = 0;
    virtual void requestQuit() = 0;
    virtual bool quitRequested() const = 0;
    // Whether Back on the root menu quits the app. Desktop yes (Esc); on a
    // handheld it is ignored -- B is pressed constantly while backing out of
    // screens, and the menu's EXIT entry (or Home) leaves deliberately.
    virtual bool backQuitsAtRootMenu() const { return true; }

    // --- Display --------------------------------------------------------
    // False where the app always fills one fixed display (Android): the
    // settings screen then hides the Fullscreen and Monitor rows.
    virtual bool supportsWindowModes() const = 0;
    // Choices for the Monitor setting; index 0 is always the primary display
    // ("Primary"), 1..N specific displays.
    virtual std::vector<std::string> displayNames() const = 0;
    virtual void setFullscreen(bool on, int displayIndex) = 0;
    // Whether the platform can start the app on a display other than the one
    // the user should see it on (a dual-screen handheld), and so has a
    // "launch on the top screen" preference to offer. See App's
    // launchOnTopScreen_; the platform's launcher reads the saved value.
    virtual bool hasLaunchDisplaySetting() const { return false; }
    // Whether video is hardware-decoded unless the user changes it
    // (Settings > Hardware Decoding). Off everywhere for now.
    virtual bool defaultHardwareDecoding() const { return false; }
};
