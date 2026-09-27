# PVM Player

A fullscreen media player prototype styled after a Sony PVM broadcast
monitor: a flat black, monospace, keyboard/remote-only on-screen menu, and
an optional CRT post-process pass (scanlines, vignette, bloom, color tear)
over whatever's playing.
The platform abstraction branch for supporting Android was branched by Claude Code. Honestly, this allowed me to reuse almost all of the code. 

<p align="center">
  <img src="docs/screenshot-menu-current.png" alt="PVM-style root menu" width="420">
  <img src="docs/screenshot-playback.jpg" alt="Video playback with the CRT effect" width="380">
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
- **NEWS**, **TAGESSCHAU**, **ARD** and **ZDF**: teletext-style readers --
  numbered 3-digit pages on a 40x24 monospace grid. NEWS/TAGESSCHAU are fed
  by RSS/Atom sources you configure; ARD/ZDF each browse and play their own
  Mediathek's videos via the MediathekViewWeb API. All four refresh in the
  background and cache to disk (see [NEWS](#news-teletext) below)
- **GAMES**: runs [libretro](https://www.libretro.com/) cores (emulators) --
  Game Boy / Color with gambatte, SNES with bsnes, and any other
  software-rendered core -- through the same CRT pass, with battery saves,
  save states and a game menu (see [Games](#games-libretro-cores) below)
- **CRT post-process pass**: scanlines, vignette, bloom, and a chromatic
  "color tear" effect, each independently tunable, plus always-on
  brightness/contrast/saturation controls — toggle the whole effect with
  <kbd>C</kbd>
- **Configurable look**: pick any font dropped into `assets/fonts/`
  (ships with a clean default plus an authentic teletext-style option),
  uncapped font size, 5 menu screen positions, 3 selection-highlight
  styles, and independent X/Y stretch for the menu panel and for text
- Settings persist to a plain-text `config.cfg` next to the executable
- **Stays awake during playback**: the display's idle-sleep timer is
  held off while a video is actively playing (not while paused, or on any
  other screen), so the screen doesn't blank mid-movie on battery power

## Building

Requires CMake 3.20+, a C++17 compiler, [libmpv](https://mpv.io/)
(e.g. `brew install mpv` on macOS, or vendor a prebuilt Windows SDK under
`thirdparty/libmpv/`), and libcurl (bundled with macOS; `libcurl4-openssl-dev`
on Debian/Raspberry Pi OS). [pugixml](https://pugixml.org/) is used from the
system if installed (`brew install pugixml` / `apt install libpugixml-dev`)
and otherwise fetched and built automatically. GLFW, glad, Dear ImGui and
[nlohmann/json](https://github.com/nlohmann/json) (the ARD section's JSON
parser) are vendored/fetched automatically.

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

The root menu reads: Play Media, TV, Tagesschau, News, Games, ARD, ZDF,
Settings, Quit.

**TV** opens a list of live channels (ARD, ZDF, tagesschau24, ONE, ARD alpha),
HLS streams played by mpv; the info overlay shows LIVE instead of a time, and
stopping returns to the list. The stream addresses are in `kTvChannels` in
`src/app.cpp`. ARD's first address (`mcdn.daserste.de`) does not resolve on
every network, so a second one is tried automatically if it fails to load.
A stream that never connects (a blocked or dead address) is given 15 seconds
(mpv's `network-timeout`) before it's treated as failed and the app returns
to the list with a toast, rather than sitting on a black screen forever; while
actually connecting or rebuffering, the overlay reads BUFFERING instead of
PLAYING. Reaching a stream still depends on your network actually being able
to route to it -- if a channel never gets past BUFFERING, check that first.

| Key(s)              | Action                                    |
|----------------------|--------------------------------------------|
| ↑ / ↓                | Move selection                             |
| Enter                | Select / open / play                       |
| Esc / Backspace       | Back / stop playback / exit                |
| Space                 | Play / pause                               |
| ← / →                 | Seek ±5s (playback) or adjust a setting     |
| C                     | Toggle the CRT effect                      |

Keys are remappable: copy `conf/keys.example.cfg` to `keys.cfg` next to
the executable and change the lines you need. Input goes through a
platform-free action layer (`src/input/`), so the same bindings file
format serves keyboards, remotes and, later, gamepads.

## Games (libretro cores)

The **GAMES** entry of the root menu runs game ROMs with libretro cores that
are loaded at run time. No emulator is built into or shipped with the player;
fetch the ones you want:

```sh
scripts/fetch_cores.sh                 # gambatte + bsnes for this machine
scripts/fetch_cores.sh mgba            # or any other core from the libretro buildbot
```

(`scripts\fetch_cores.ps1` on Windows.) In a development build the cores go
in `cores/` of the source tree; a copy of the player finds them in a `cores`
folder next to the executable, or in `$PVM_CORES_DIR`. The cores are the
buildbot's "latest" builds, each with its own licence (gambatte and bsnes are
GPL) -- you download them for your own use.

**GAMES** opens a small menu: **Browse ROMs** or **Favorites**. Browse ROMs is
yours to bring: it browses folders like *Play Media* (starting where you last
left off, or at Settings > *ROM Start Directory*), listing the file types your
cores handle, and starts the core that lists the ROM's extension (the more
specialised core if several do, e.g. gambatte for `.gb`). **Favorites** lists
ROMs you've marked, for launching without digging back through folders.

Add or remove the selected ROM in Browse ROMs, or the current one while
playing, with the **ToggleFavorite** hotkey (**F** by default, or the
gamepad's left stick click); a favorited ROM shows a `*` in the browser.
Inside Favorites the same hotkey removes the selected entry. The in-game menu
(below) also has a FAVORITE row that does the same thing. Favorites persist to
`game_favorites.cfg` next to `config.cfg` -- one absolute path per line,
editable by hand.

Only software-rendered cores work (most 2D systems); cores that need an
OpenGL or Vulkan context are refused. Files live in `retro/` next to the
executable: `saves/` (battery saves `<rom>.srm`, save states `<rom>.state[N]`),
`system/` (BIOS files a core asks for) and `options/<core>.cfg` (`key = value`
lines overriding a core's options). 

| Keyboard           | RetroPad                          |
|--------------------|------------------------------------|
| Arrow keys         | D-pad                              |
| X / Z              | A / B                              |
| S / A              | X / Y                              |
| Q / W, E / T       | L / R, L2 / R2                     |
| Enter / Right Shift or Tab | Start / Select             |
| Esc, M or I        | Game menu                          |

On a gamepad, the face buttons map by position (bottom = RetroPad B, right =
A, ...), the triggers are L2/R2, and **Start + Select together** open the game
menu: resume, save/load state (slot 0-9), reset, favorite, scale (fit /
integer / stretch), aspect (core / 4:3 / square pixels), filter and close
game. Every binding can be changed in `keys.cfg` (`retro_a`, `retro_start`,
`toggle_favorite`, ...).

**Android.** The cores are packaged into the APK as native libraries -- Android
only loads code from the app's own library folder, so cores cannot be dropped
in afterwards. `scripts/build_apk.sh` (`build_apk.ps1` on Windows) builds a
complete APK, cores included: it fetches whatever's missing (the vendored
headers, libmpv, and gambatte + bsnes for arm64 via `fetch_cores.sh --android`)
and runs Gradle, ending with `dist/pvm-player-release.apk`. Needs the Android
SDK/NDK already set up (`scripts/setup.sh --android` does that once). Options:

```sh
scripts/build_apk.sh                  # release APK with gambatte + bsnes
scripts/build_apk.sh --debug          # a debug build instead
scripts/build_apk.sh mgba             # specific cores instead of the default two
scripts/build_apk.sh --no-cores       # no game cores at all (smaller APK, no bundled GPL code)
scripts/build_apk.sh --force          # redo every fetch step, not just what's missing
```

GAMES then browses your storage for ROMs like *Play Media* does. The bundled
cores are GPL: an APK containing them is a GPL-covered combination if you pass
it on, so keep it for your own devices or comply with the licences (`--no-cores`
sidesteps this if you don't want any GPL code in the APK at all). A running
game survives the GL context being lost (it is resumed from an automatic save
state) and is paused, with its battery save written, while the app is in the
background.

Timing follows the display: when it refreshes at about the core's rate one
emulated frame is shown per refresh and the audio speed is adjusted by a
fraction of a percent to match; otherwise frames are paced against the clock.

## NEWS (teletext)

The root menu's **NEWS**, **TAGESSCHAU** and **ARD** entries each open a
teletext-style reader: a 40x24 character grid (the World System Teletext
geometry) in the eight teletext colors, using whichever font is selected in
Settings (the teletext-style fonts under `assets/fonts/` suit it best). 

**Pages.** 100 is the index. Each configured source owns a block of pages:
its first page is a headline list, the following pages hold articles
(newest first, long ones continue across pages, capped at
`max_article_pages`). When a block is full the oldest articles are dropped.

| Key(s)                | Action                                                        |
|------------------------|----------------------------------------------------------------|
| ↑ / ↓                  | Move the highlighted selection among this page's links (a headline, a category, a show, an episode) |
| Enter                  | Activate the selected link: jump to a page, or (ARD) play an episode |
| ← / →                  | Previous / next *page number* (skipping unpopulated numbers) -- on ARD, turns a page's own sub-pages first (see [ARD](#ard)) |
| 0-9 (or keypad 0-9)    | Type a page number; jumps on the 3rd digit                    |
| Red / Green (F1 / F2, or R / G) | `-` previous page / `+` next page, same as ←/→        |
| Yellow (F3 or Y), M    | `News`: the index, page 100                                   |
| Blue (F4 or B)         | `Refresh`: re-fetch this section right now, bypassing its normal timer/cache |
| Esc / Backspace        | Back: cancels a half-typed number, else climbs a level, else leaves the section |

A page number nobody has typed a third digit for is abandoned after 3 seconds.
A number with no page behind it shows a "PAGE NOT FOUND" placeholder.

**Sources** are listed in `news.cfg` next to the executable (the build ships
a starter copy as `news.default.cfg`, from [`conf/news.cfg`](conf/news.cfg);
copy it to `news.cfg` to edit it):

```
refresh_minutes=15
block_size=10
max_article_pages=3
source=110 | WORLD | https://feeds.bbci.co.uk/news/world/rss.xml | bbc.co.uk | BBC NEWS
```

A source is `start page | CATEGORY | feed URL`, optionally followed by
`| provider` (the cyan header text; defaults to the URL's host) and
`| LOGO` (the title-art text, up to 13 letters/digits; defaults to the
category).

**Refreshing and caching.** Feeds are fetched on a background thread (never
on the render loop, so video playback is unaffected), one source at a time,
no more often than every 5 minutes. The last good copy of every feed is
kept in `cache/news/` next to the executable, so the section has pages
immediately at startup and keeps working offline. The page-100 footer shows
when the news was last updated, and flags sources that are failing.

Feed quality varies a lot: some feeds carry only a one-line teaser per
story, others (e.g. Ars Technica) the first several paragraphs. Check a
candidate with the probe tool before adding it:

```sh
./build/teletext_probe https://example.com/feed.xml --limit 3
./build/teletext_probe --news build/news.default.cfg 100 110   # dump pages as text
```

Only the feed URLs you configure are fetched (no page scraping, no
images), with an identifying `User-Agent`. Logic tests run with
`ctest --test-dir build`.

<a id="tagesschau"></a>
### TAGESSCHAU

<p align="center">
  <img src="docs/screenshot-tagesschau-overview.png" alt="TAGESSCHAU page 100: title art, top stories with page numbers, section list" width="380">
  <img src="docs/screenshot-tagesschau-section.png" alt="TAGESSCHAU page 510: the Ausland section index" width="380">
</p>
<p align="center"><em>Page 100 (overview) and page 510 (the Ausland section index)</em></p>

The root menu's **TAGESSCHAU** entry is a second, larger teletext service of
the same kind, built from 14 tagesschau.de feeds (configured in
[`conf/tagesschau.cfg`](conf/tagesschau.cfg), shipped as
`tagesschau.default.cfg`; copy to `tagesschau.cfg` to edit). It has its own
config, cache (`cache/tagesschau/`) and current page, and the same keys as NEWS.

| Pages   | Content                                                          |
|---------|-------------------------------------------------------------------|
| 100-199 | Meldungen (110-149), Startseite (150-199)                         |
| 200-399 | Inland (210), Innenpolitik (260), Gesellschaft (310), Regional (350) |
| 400-499 | Baden-Württemberg (410)                                           |
| 500-699 | Ausland (510)                                                     |
| 700-799 | Wirtschaft (710)                                                  |
| 800-999 | Wissen (810), Gesundheit (860), Klima & Umwelt (910), Forschung (940), Technologie (970) |

Every **hundred page** (100, 200, ... 900) is an overview: title art, the six
newest articles located in its 100-page range with their page numbers, and
the sections that have pages there. Each feed's first page is its section
index (logo banner plus headlines); article pages follow, newest first, and
never use the hundred pages -- a range like 510-699 simply passes over 600
(whose overview lists the same newest stories if the feed did not reach that
far). Esc/Backspace climbs article → section index → hundred page → page 100
→ leave.

Both sections use the same file format: `source=` lines take a page range
(`210-259`) instead of a start page, and `overview_pages=hundreds`,
`service_name=` and `service_logo=` switch a config into this overview style.

<a id="ard"></a>
### ARD / ZDF

<p align="center">
  <img src="docs/screenshot-playback-ard.png" alt="Video playback with the CRT effect" width="420">
</p>

The root menu's **ARD** and **ZDF** entries are each a browsable, playable
catalog of currently-available Mediathek videos -- one broadcaster's
`channel` per entry -- built from the
[MediathekViewWeb](https://mediathekviewweb.de/) API, a community-run,
publicly documented aggregator across the German public broadcasters (chosen
over each broadcaster's own private, undocumented backend API). Both entries
are the exact same code (`teletext/mvw_*`) running against two different
config files, `ard.cfg` and `zdf.cfg` -- everything below applies to either,
substituting `zdf.cfg`/`ZDF` for `ard.cfg`/`ARD` as needed. Unlike
NEWS/TAGESSCHAU, a selected row can *play* a video directly instead of only
linking to another page:

Configured in `ard.cfg`/`zdf.cfg` (shipped as `ard.default.cfg`/
`zdf.default.cfg`, from [`conf/ard.cfg`](conf/ard.cfg)/
[`conf/zdf.cfg`](conf/zdf.cfg)):

```
channel=ARD
service_name=ARD Mediathek
service_logo=ARD MEDIATHEK
refresh_minutes=60
max_episode_pages=30
az_start_page=200
az_window_days=14
favorite=110 | Babylon Berlin | BABYLON BERLIN
```

