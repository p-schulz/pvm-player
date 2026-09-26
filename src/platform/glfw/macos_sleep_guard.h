#pragma once

// Prevents macOS from idle-sleeping the display while media is actively
// playing, so the screen doesn't blank mid-playback on battery power (the
// default idle-sleep timer applies regardless of what's on screen -- it has
// no way to know a video is playing).
//
// mpv normally handles this itself (its "stop-screensaver" option), but
// that logic lives in mpv's own native video output window on macOS; this
// app renders through the libmpv *render API* (vo=libmpv) into its own
// GLFW/OpenGL window instead of letting mpv own a window, so mpv's own
// inhibition never engages. Hence a small IOKit assertion of our own --
// Apple-only, hence the separate header with no IOKit types leaking into
// the rest of the (portable) app; only the GLFW platform
// (platform/glfw) uses this, behind #ifdef __APPLE__.
namespace macos_sleep_guard {

// Holds (or releases) a display-idle-sleep assertion so it matches
// `active`. Cheap to call every frame with the current playing-and-
// unpaused state -- a call that doesn't change the state is a no-op, only
// an actual transition touches IOKit.
void setActive(bool active);

// Releases any held assertion. Safe to call even if setActive(true) was
// never called.
void shutdown();

}  // namespace macos_sleep_guard
