# Third-party licenses

PVM Player (GPL-3.0-or-later, see [LICENSE](LICENSE)) is built on the
libretro API and on the libraries below. None of them are modified beyond
what their own build/vendoring scripts do; each keeps its own upstream
license, reproduced here in full for the ones short enough to quote, or
summarized with a link for the long ones (the GPL itself). Nothing here
changes what license *this project's own code* is under -- that is GPL-3.0-or-later,
chosen specifically because it is compatible with every license below,
including the copyleft ones.

## At a glance

| Component | Used for | License |
|---|---|---|
| [libretro.h](#libretroh) | the core-loading API (`src/retro/`) | MIT |
| [gambatte](#gambatte-libretro-core) | Game Boy / Color core (fetched, not bundled in source) | GPL-2.0-only |
| [bsnes](#bsnes-libretro-core) | SNES core (fetched, not bundled in source) | GPL-3.0-or-later (core); ISC-style (nall/ruby/hiro/libco) |
| [libmpv + FFmpeg](#libmpv--ffmpeg-android-prebuilt) (Android prebuilt) | video/audio playback | LGPL-2.1-or-later |
| [libmpv](#libmpv-desktop) (desktop, system/vendored) | video/audio playback | GPL-2.0-or-later by default, or LGPL-2.1-or-later -- depends on how your copy was built; see below |
| [Dear ImGui](#dear-imgui) | UI | MIT |
| [nlohmann/json](#nlohmannjson) | Mediathek API parsing | MIT |
| [pugixml](#pugixml) | RSS/Atom parsing | MIT |
| [GLFW](#glfw) | desktop windowing/input | Zlib |
| [glad](#glad) | desktop OpenGL loader (generated) | MIT (loader code); Apache-2.0 (Khronos specs it was generated from) |
| [miniaudio](#miniaudio) | game-core audio output | Public domain (Unlicense) / MIT-0, your choice |
| Android GameActivity, AndroidX | Android platform glue | Apache-2.0 |

Cores are never included in this repository's source and are not built from
it: `scripts/fetch_cores.sh` downloads prebuilt binaries from the libretro
buildbot for you to run locally, and `scripts/build_apk.sh` optionally
bundles them into an APK you build yourself (see the GPL note under
[gambatte](#gambatte-libretro-core)/[bsnes](#bsnes-libretro-core) about what
that means if you pass such an APK on to someone else).

---

## libretro.h

The API `src/retro/retro_core.{h,cpp}` implements against, from
[libretro/libretro-common](https://github.com/libretro/libretro-common)
(vendored at `thirdparty/libretro/libretro.h`, pinned by commit in
`scripts/fetch_thirdparty.sh`). MIT, header only:

```
Copyright (C) 2010-2024 The RetroArch team

Permission is hereby granted, free of charge,
to any person obtaining a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## gambatte (libretro core)

[libretro/gambatte-libretro](https://github.com/libretro/gambatte-libretro),
fetched by `scripts/fetch_cores.sh` (not built from or vendored in this
repository). **GPL-2.0-only** -- every `libgambatte` source file's own header
says "version 2" without an "or later" clause, so unlike this project's own
code it cannot be upgraded to GPL-3. Full text:
<https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>. It is loaded at run
time as its own separate shared library through the (MIT) libretro API above,
never linked into PVM Player's own binary, exactly like every other libretro
frontend (RetroArch itself is GPL-3.0 and loads this same GPL-2.0-only core
the same way).

## bsnes (libretro core)

[libretro/bsnes-libretro](https://github.com/libretro/bsnes-libretro), fetched
by `scripts/fetch_cores.sh` (not built from or vendored in this repository).
The emulation core itself is **GPL-3.0-or-later**:

```
bsnes - Super Nintendo emulator
Copyright (c) 2004-2020 byuu et al -- https://byuu.org/bsnes/

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version. [...] See <https://www.gnu.org/licenses/>.
```

Its bundled support libraries (`libco`, `nall`, `ruby`, `hiro`) are under a
separate, permissive ISC-style license (also byuu et al., 2006-2020):
"Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies," with the
usual "AS IS" disclaimer.

**If you redistribute an APK you built with `scripts/build_apk.sh` and its
default cores:** bundling gambatte and/or bsnes into the same APK as PVM
Player's own compiled code is why this project is GPL-3.0-or-later rather
than a permissive license -- so the combination is squarely GPL-compliant
either way, the same position RetroArch's own distribution is in. `--no-cores`
leaves them out entirely if you'd rather not carry GPL binaries in your build
at all.

## libmpv + FFmpeg (Android prebuilt)

`android/fetch_libmpv.sh` downloads a prebuilt `libmpv.so` (mpv 0.36 with
FFmpeg built in) from
[media-kit/libmpv-android-video-build](https://github.com/media-kit/libmpv-android-video-build)'s
"full" release flavor, pinned by checksum. Checked directly against that
project's own build scripts for the exact flavor used here: FFmpeg is
configured with `--disable-gpl --disable-nonfree --enable-version3`, and mpv
itself with meson's `-Dgpl=false` -- both **LGPL-2.1-or-later**, not GPL. The
wrapper/packaging itself carries this notice (Ilya Zhuravlev and sfan5, 2016):

```
Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions: [...]
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND [...]
```

(mpv: <https://github.com/mpv-player/mpv/blob/master/LICENSE.LGPL>; FFmpeg:
<https://github.com/FFmpeg/FFmpeg/blob/master/COPYING.LGPLv2.1>.) LGPL permits
linking from a project under any license, including this one, provided LGPL's
own terms are met for the library itself (source availability, re-linking) --
already the case here, since it's fetched from its own public upstream
project rather than modified and re-distributed by PVM Player.

## libmpv (desktop)

On macOS/Linux, `CMakeLists.txt` links a vendored SDK under `thirdparty/libmpv/`
if present, else your system's own `libmpv` (e.g. Homebrew's `mpv` formula, or
your distro's package). mpv's own default build is **GPL-2.0-or-later**;
building it with `-Dgpl=false` instead (as the Android prebuilt above does)
makes it LGPL-2.1-or-later. Which one you actually have depends on how your
particular copy was built -- `mpv --version` and your package manager's
license metadata for it (`brew info mpv`, `apt show libmpv2`, ...) will say.
This is outside PVM Player's own control (it isn't vendored, fetched, or
modified by this repository on desktop), and this project's own
GPL-3.0-or-later licensing is compatible with either case: an LGPL system mpv
imposes no extra terms on the combination; a GPL system mpv means that
particular build of the *combination* carries GPL's terms too, same as it
already would for any GPL app on your system linking that same library.

## Dear ImGui

[ocornut/imgui](https://github.com/ocornut/imgui) v1.92.8, vendored at
`thirdparty/imgui/` (see its `LICENSE.txt`). MIT:

```
Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## nlohmann/json

[nlohmann/json](https://github.com/nlohmann/json) v3.11.3, vendored at
`thirdparty/json/` (see its `LICENSE.txt`). MIT:

```
Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## pugixml

[zeux/pugixml](https://github.com/zeux/pugixml), fetched by CMake if not
found as a system package. MIT:

```
Copyright (c) 2006-2026 Arseny Kapoulkine

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
```

## GLFW

[glfw/glfw](https://github.com/glfw/glfw) 3.4, fetched by CMake on desktop
(`src/platform/glfw`). Zlib/libpng license:

```
Copyright (c) 2002-2006 Marcus Geelnard
Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
```

## glad

[Dav1dde/glad](https://github.com/Dav1dde/glad) (glad2), generated once via
the CLI into `thirdparty/glad/` (`scripts/fetch_thirdparty.sh`, no
network/Python needed at build time). Its own loader code is MIT (David
Herberth); the Khronos API specifications it was generated from are
Apache-2.0, per glad's own `LICENSE`:

```
The glad source code: MIT License, Copyright (c) 2013-2022 David Herberth.
The Khronos Specifications: Copyright (c) 2013-2020 The Khronos Group Inc.,
Licensed under the Apache License, Version 2.0.
```

## miniaudio

[mackron/miniaudio](https://github.com/mackron/miniaudio) 0.11.25, vendored
single header at `thirdparty/miniaudio/miniaudio.h` (game-core audio output,
`src/retro/audio_out.cpp`). Dual-licensed, your choice of either -- public
domain (Unlicense) or MIT-0 (MIT, no attribution required):

```
Copyright 2026 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

(or, at your option, the Unlicense -- see the header for its full text.)

## Android GameActivity / AndroidX

`androidx.games:games-activity`, `androidx.core:core-ktx`,
`androidx.appcompat:appcompat` (Gradle dependencies, `android/app/build.gradle.kts`).
Apache-2.0, Google: <https://source.android.com/setup/start/licenses> /
<https://www.apache.org/licenses/LICENSE-2.0>. Apache-2.0 code may be included
in a GPL-3.0-or-later work (GPLv3's own license-compatibility list explicitly
permits this); it is not compatible the other way around with GPL-2.0-only
code, which is exactly why nothing under Apache-2.0 here is ever combined into
the same binary as gambatte.
