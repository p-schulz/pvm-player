#pragma once

#include <imgui.h>

#include <string>

#include "page.h"

namespace teletext {

// Where the 40x24 grid lands on screen. All values are ImGui logical
// coordinates, chosen as whole numbers so glyph cells fall on whole
// framebuffer pixels (exactly, for any integer display scale factor).
struct GridLayout {
    int cellW = 0;
    int cellH = 0;
    int originX = 0;
    int originY = 0;
    int textOffsetY = 0;  // glyph top inside a cell (centers the font line box vertically)
    float fontSize = 0.0f;
};

// Fits the largest whole-pixel cell size for `font` such that the grid stays
// within a small margin of `display` on both axes, centered.
GridLayout computeGridLayout(ImFont* font, ImVec2 display);

// Draws `page` on ImGui's background draw list: full-screen black, then the
// grid with per-cell colors and block graphics. The header row is generated
// here, not taken from the page: current page number (green), `targetLabel`
// (the page being typed as "1--", else the current page number), the page's
// provider name (cyan), then `date` ("DD.MM.") and `time` ("HH:MM:SS").
// Call between ImGui::NewFrame() and ImGui::Render().
void drawPage(const TeletextPage& page, const std::string& targetLabel, const std::string& date,
              const std::string& time);

}  // namespace teletext
