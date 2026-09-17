# Fonts

This directory is scanned at startup (`App::init()`) to populate the
Settings screen's "Font" row. Drop any `.ttf`/`.otf` file in here and it
becomes selectable in-app -- no rebuild needed.

`JetBrainsMono-Regular.ttf` is the bundled default (used when "Font" is
set to "Default", and as the fallback if a selected font file fails to
load or has since been removed).

## Bundled fonts

- **JetBrains Mono** (`JetBrainsMono-Regular.ttf`) -- OFL-1.1 licensed,
  see `JetBrainsMono-LICENSE.txt`. https://github.com/JetBrains/JetBrainsMono
- **Bedstead** (`Bedstead.otf`) -- CC0-1.0 (public domain), see
  `Bedstead-LICENSE.txt`. An outline font family reproducing the
  character designs of the Mullard SAA5050 Teletext Character Generator
  used in 1970s/80s UK Teletext (Ceefax/Oracle) and the BBC Micro's
  "Mode 7" display -- the authentic old-TV-teletext look.
  https://bjh21.me.uk/bedstead/ / https://github.com/glxxyz/bedstead
