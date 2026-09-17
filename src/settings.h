#pragma once

#include <string>

// Persisted user settings (font size, menu position, screen appearance,
// CRT/bloom stylization, ...). Deliberately a flat, hand-rolled key=value
// text file rather than a library-backed format -- there are only a
// couple dozen scalar fields and no nesting.
struct AppSettings {
    int fontSizePx = 22;
    // Filename (not a full path) of the selected font within the fonts
    // directory, or "" to use the bundled default.
    std::string fontFile;
    int menuPositionIndex = 0;
    int selectionStyleIndex = 0;
    bool showHiddenFiles = false;

    // Directory the file browser starts at (user-configurable, via a
    // folder-picker settings row) and the directory it was last in when
    // media playback stopped (auto-tracked; takes priority over
    // startDirectory when set). Empty means "unset".
    std::string startDirectory;
    std::string lastUsedDirectory;

    // Screen appearance -- always applied, independent of crtEnabled.
    float brightness = 0.0f;  // additive, -0.5..0.5
    float contrast = 1.0f;    // multiplier around the mid-gray pivot
    float saturation = 1.0f;  // 0 = grayscale, 1 = normal

    // CRT/bloom stylization -- gated by crtEnabled.
    bool crtEnabled = true;
    float crtEffectStrength = 1.0f;
    float bloomStrength = 1.0f;
    int scanlineCount = 480;
    float vignetteStrength = 0.35f;
    float colorTear = 0.0f;  // channel-shift amount, in pixels
};

// Reads `path` and overwrites the matching fields of `settings` for any
// key found. Fields with no corresponding key in the file (including the
// whole file not existing, e.g. first run) are left untouched, so callers
// should pre-populate `settings` with defaults before calling this.
// Returns false if the file couldn't be opened at all (not an error for a
// first run); parsing issues on individual lines are logged and skipped.
bool loadSettings(const std::string& path, AppSettings& settings);

// Writes `settings` to `path`, overwriting any existing file. Returns
// false (and logs to stderr) if the file couldn't be written.
bool saveSettings(const std::string& path, const AppSettings& settings);
