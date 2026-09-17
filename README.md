# PVM Player

A fullscreen media player prototype styled after a Sony PVM broadcast
monitor: a flat black, monospace, keyboard/remote-only on-screen menu, and
an optional CRT post-process pass (scanlines, vignette, bloom, color tear)
over whatever's playing.

<p align="center">
  <img src="docs/screenshot-menu.png" alt="PVM-style root menu" width="420">  <img src="docs/screenshot-playback.png" alt="Video playback with the CRT effect" width="420">
</p>

## Features

- **Playback** for local video and audio files via [libmpv](https://mpv.io/),
  rendered into an OpenGL texture (not a native player window)
- **PVM-style OSD menu** — root menu → file browser, fully keyboard-driven
  (no mouse required)
- **File browser**: one or more configured root folders, nested navigation
  (including *above* the configured root, up to the filesystem root), a
  configurable start directory, automatic "resume where you left off", and
  a hidden-files toggle
- **CRT post-process pass**: scanlines, vignette, bloom, and a chromatic
  "color tear" effect, each independently tunable, plus always-on
  brightness/contrast/saturation controls — toggle the whole effect with
  <kbd>C</kbd>
- **Configurable look**: pick any font dropped into `assets/fonts/`
  (ships with a clean default plus an authentic teletext-style option),
  uncapped font size, 5 menu screen positions, 3 selection-highlight
  styles, and independent X/Y stretch for the menu panel and for text
- Settings persist to a plain-text `config.cfg` next to the executable

## Building

Requires CMake 3.20+, a C++17 compiler, and [libmpv](https://mpv.io/)
(e.g. `brew install mpv` on macOS, or vendor a prebuilt Windows SDK under
`thirdparty/libmpv/`). GLFW, glad, and Dear ImGui are fetched/vendored
automatically.

```sh
cmake -S . -B build
cmake --build build -j
```

## Usage

```sh
./build/pvm_player [directory ...]
```

Pass one or more directories to browse via the "Play Media" menu entry;
defaults to the current directory. Multiple directories show a root
picker; the "Start Directory" setting is a simpler single-folder default
for everyday use.

| Key(s)              | Action                                    |
|----------------------|--------------------------------------------|
| ↑ / ↓                | Move selection                             |
| Enter                | Select / open / play                       |
| Esc / Backspace       | Back / stop playback / exit                |
| Space                 | Play / pause                               |
| ← / →                 | Seek ±5s (playback) or adjust a setting     |
| C                     | Toggle the CRT effect                      |

Explicitly out of scope for this prototype: subtitles, network streaming,
playlist persistence, metadata/artwork scraping, and audio visualizations.
