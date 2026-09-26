#pragma once

#include <functional>

// Hooks macOS's hardware media keys (the Play/Pause key on many keyboards'
// Fn-row, or a dedicated media key on some Apple keyboards) in addition to
// the in-window Space hotkey. GLFW has no concept of these -- they arrive
// as NSSystemDefined events, a completely different delivery path from
// ordinary NSEvent key-downs, which GLFW's Cocoa backend never looks at.
// Reaching them means going around GLFW with a global CGEventTap (see
// macos_media_keys.mm); Apple-only, hence the separate header with no
// Objective-C types leaking into the rest of the (portable) app -- only the GLFW platform
// (platform/glfw) uses this, behind #ifdef __APPLE__.
namespace macos_media_keys {

// Installs a system-wide event tap watching for the hardware Play/Pause
// media key and calls `onPlayPause` (on the main thread) each time it's
// pressed. Requires the app to have Accessibility ("Input Monitoring" on
// newer macOS) permission granted in System Settings; if that hasn't been
// granted, or the tap can't be created for any other reason, this logs a
// message to stderr and does nothing further -- media-key support is
// strictly an addition to the regular hotkeys, never a requirement to run.
void install(std::function<void()> onPlayPause);

// Tears down the tap installed by install(), if any. Safe to call even if
// install() was never called or failed.
void shutdown();

}  // namespace macos_media_keys
