#pragma once

#include <string>

#include "page.h"

namespace teletext {

constexpr int kBannerFirstRow = 1;
constexpr int kBannerRows = 4;

// Draws a "logo" banner: `kBannerRows` rows of solid blue starting at
// kBannerFirstRow with `text` in big white block letters, centered. The
// letters are built from 2x3 block-graphics cells (a 5x7 pixel font, drawn
// twice as wide when the text is short enough to allow it), exactly how real
// teletext services drew their title art. Characters the font lacks are
// drawn as blanks; text longer than kMaxLogoChars is cut.
void drawBanner(TeletextPage& page, const std::string& text);

constexpr int kMaxLogoChars = 13;

}  // namespace teletext
