#pragma once

#include <imgui.h>

#include <string>

#include "page.h"

namespace teletext {

// User scaling of the teletext screen (the "Teletext ..." settings rows).
// `menu*` resizes the whole grid -- cells, block graphics and glyphs alike --
// about the screen centre; `text*` additionally stretches just the glyphs
// inside their cells. 1.0 = the auto-fit size. Not capped: a grid larger
// than the screen simply runs off the edges, like the menu panels do.
struct TeletextScale {
    float menuX = 1.0f;
    float menuY = 1.0f;
    float textX = 1.0f;
    float textY = 1.0f;
};

// Where the 40x24 grid lands on screen. Cell sizes and the origin are whole
// numbers in ImGui logical coordinates, so cells fall on whole framebuffer
// pixels (exactly, for any integer display scale factor).
struct GridLayout {
    int cellW = 0;           // scaled cell size (menu scale applied)
    int cellH = 0;
    int originX = 0;         // top-left of the (centered) grid; negative if it overflows
    int originY = 0;
    float fontSize = 0.0f;   // font size that makes a glyph fill an *unscaled* cell
    float advanceRatio = 0.0f;  // glyph advance / font size
    float lineRatio = 0.0f;     // font line height / font size
    float glyphScaleX = 1.0f;   // total glyph stretch relative to fontSize: cell growth * text scale
    float glyphScaleY = 1.0f;
};

// Fits the largest whole-pixel cell size for `font` such that the unscaled
// grid stays within a small margin of `display` on both axes, then applies
// `scale` and centers the result.
GridLayout computeGridLayout(ImFont* font, ImVec2 display, const TeletextScale& scale = {});

// Draws `page` on ImGui's background draw list: full-screen black, then the
// grid with per-cell colors and block graphics. The header row is generated
// here, not taken from the page: current page number (green), `targetLabel`
// (the page being typed as "1--", else the current page number), the page's
// provider name (cyan), then `date` ("DD.MM.") and `time` ("HH:MM:SS").
// `selectedLink` is an index into `page.links` (-1 for none) whose row gets
// its colors swapped (fg<->bg) so it reads as highlighted, independent of
// whatever colors that row was drawn in. Call between ImGui::NewFrame() and
// ImGui::Render().
void drawPage(const TeletextPage& page, const std::string& targetLabel, const std::string& date,
              const std::string& time, const TeletextScale& scale = {}, int selectedLink = -1);

}  // namespace teletext
