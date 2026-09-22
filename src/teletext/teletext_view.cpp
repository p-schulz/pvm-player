#include "teletext_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace teletext {

namespace {

// Fraction of each screen axis the grid may occupy; the rest is the margin a
// real set's overscan would have hidden anyway (and keeps the CRT vignette
// from eating the outer characters).
constexpr float kScreenFill = 0.92f;

// Reference size the font's proportions are measured at. Any size works --
// only the advance/line-height *ratios* matter -- but measuring at a large
// size keeps rounding error in those ratios negligible.
constexpr float kReferenceSize = 64.0f;

// Cell height as a multiple of the font's own line height. Teletext cells
// are taller than they are wide; fonts whose line box hugs the glyphs
// (Bedstead, EuropeanTeletext) would otherwise render rows touching.
constexpr float kRowLeading = 1.3f;

}  // namespace

GridLayout computeGridLayout(ImFont* font, ImVec2 display, const TeletextScale& scale) {
    ImFontBaked* baked = font->GetFontBaked(kReferenceSize);
    const float advanceRatio = baked->GetCharAdvance('M') / kReferenceSize;
    const float lineRatio = (baked->Ascent - baked->Descent) / kReferenceSize;
    const float cellRatio = lineRatio * kRowLeading;

    const float sizeFromWidth = display.x * kScreenFill / (kCols * advanceRatio);
    const float sizeFromHeight = display.y * kScreenFill / (kRows * cellRatio);

    GridLayout layout;
    layout.fontSize = std::max(1.0f, std::floor(std::min(sizeFromWidth, sizeFromHeight)));
    layout.advanceRatio = advanceRatio;
    layout.lineRatio = lineRatio;

    const int baseCellW = std::max(1, static_cast<int>(std::lround(advanceRatio * layout.fontSize)));
    const int baseCellH = std::max(1, static_cast<int>(std::lround(cellRatio * layout.fontSize)));

    // Menu scale resizes the cells (kept whole numbers); glyphs follow the
    // *rounded* cell so they always match it, then text scale stretches them
    // on top.
    layout.cellW = std::max(1, static_cast<int>(std::lround(static_cast<float>(baseCellW) * scale.menuX)));
    layout.cellH = std::max(1, static_cast<int>(std::lround(static_cast<float>(baseCellH) * scale.menuY)));
    layout.glyphScaleX = static_cast<float>(layout.cellW) / static_cast<float>(baseCellW) * scale.textX;
    layout.glyphScaleY = static_cast<float>(layout.cellH) / static_cast<float>(baseCellH) * scale.textY;

    layout.originX = (static_cast<int>(display.x) - kCols * layout.cellW) / 2;
    layout.originY = (static_cast<int>(display.y) - kRows * layout.cellH) / 2;
    return layout;
}

namespace {

// Saturated primaries, as on a real teletext decoder.
ImU32 colorU32(Color c) {
    switch (c) {
        case Color::Black: return IM_COL32(0, 0, 0, 255);
        case Color::Red: return IM_COL32(255, 0, 0, 255);
        case Color::Green: return IM_COL32(0, 255, 0, 255);
        case Color::Yellow: return IM_COL32(255, 255, 0, 255);
        case Color::Blue: return IM_COL32(0, 0, 255, 255);
        case Color::Magenta: return IM_COL32(255, 0, 255, 255);
        case Color::Cyan: return IM_COL32(0, 255, 255, 255);
        case Color::White: return IM_COL32(255, 255, 255, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

}  // namespace

void drawPage(const TeletextPage& source, const std::string& targetLabel, const std::string& date,
              const std::string& time, const TeletextScale& scale, int selectedLink) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImFont* font = ImGui::GetFont();

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, colorU32(Color::Black));

    TeletextPage page = source;
    composeHeader(page, targetLabel, date, time);

    if (selectedLink >= 0 && selectedLink < static_cast<int>(page.links.size())) {
        const RowLink& link = page.links[static_cast<size_t>(selectedLink)];
        const auto r = static_cast<size_t>(link.row);
        for (int col = std::max(link.col, 0); col < std::min(link.col + link.length, kCols); ++col) {
            std::swap(page.fg[r][static_cast<size_t>(col)], page.bg[r][static_cast<size_t>(col)]);
        }
    }

    const GridLayout grid = computeGridLayout(font, display, scale);

    for (int row = 0; row < kRows; ++row) {
        const auto r = static_cast<size_t>(row);
        const float y = static_cast<float>(grid.originY + row * grid.cellH);
        const float yNext = static_cast<float>(grid.originY + (row + 1) * grid.cellH);

        // Backgrounds first, merged into runs of equal color so a solid
        // band is one rectangle (no seams between cells).
        for (int col = 0; col < kCols;) {
            const Color bg = page.bg[r][static_cast<size_t>(col)];
            int end = col + 1;
            while (end < kCols && page.bg[r][static_cast<size_t>(end)] == bg) {
                ++end;
            }
            if (bg != Color::Black) {
                draw->AddRectFilled(ImVec2(static_cast<float>(grid.originX + col * grid.cellW), y),
                                    ImVec2(static_cast<float>(grid.originX + end * grid.cellW), yNext), colorU32(bg));
            }
            col = end;
        }

        // One draw call per glyph, each at its own whole-pixel cell origin,
        // so spacing is exactly cellW no matter how the font's own advance
        // rounds -- that's what keeps columns pixel-aligned.
        for (int col = 0; col < kCols; ++col) {
            const auto c = static_cast<size_t>(col);
            const float x = static_cast<float>(grid.originX + col * grid.cellW);
            const ImU32 color = colorU32(page.fg[r][c]);

            if (page.sixel[r][c] & kSixelFlag) {
                // 2x3 block graphics: split the cell on integer boundaries.
                for (int bit = 0; bit < 6; ++bit) {
                    if (!(page.sixel[r][c] & (1 << bit))) {
                        continue;
                    }
                    const int px = bit % 2;
                    const int py = bit / 2;
                    const float x0 = x + static_cast<float>(grid.cellW * px / 2);
                    const float x1 = x + static_cast<float>(grid.cellW * (px + 1) / 2);
                    const float y0 = y + static_cast<float>(grid.cellH * py / 3);
                    const float y1 = y + static_cast<float>(grid.cellH * (py + 1) / 3);
                    draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
                }
                continue;
            }

            const char ch = page.lines[r][c];
            if (ch == ' ') {
                continue;
            }

            // Rasterize at the larger of the two stretch factors (so a
            // magnified glyph stays crisp), centered in the cell, then squash
            // the other axis with a vertex transform about the cell centre --
            // ImGui fonts have no independent X/Y scale. With no scaling this
            // is exactly the plain whole-pixel placement.
            const float gx = grid.glyphScaleX;
            const float gy = grid.glyphScaleY;
            const float size = std::max(1.0f, grid.fontSize * std::max(gx, gy));
            const int boxW = static_cast<int>(std::lround(grid.advanceRatio * size));
            const int boxH = static_cast<int>(std::lround(grid.lineRatio * size));
            const ImVec2 pos(x + static_cast<float>((grid.cellW - boxW) / 2),
                             y + static_cast<float>((grid.cellH - boxH) / 2));

            const int firstVertex = draw->VtxBuffer.Size;
            draw->AddText(font, size, pos, color, &ch, &ch + 1);

            const float kx = gx / std::max(gx, gy);
            const float ky = gy / std::max(gx, gy);
            if (kx != 1.0f || ky != 1.0f) {
                const ImVec2 anchor(x + static_cast<float>(grid.cellW) * 0.5f, y + static_cast<float>(grid.cellH) * 0.5f);
                for (int i = firstVertex; i < draw->VtxBuffer.Size; ++i) {
                    ImDrawVert& v = draw->VtxBuffer[i];
                    v.pos.x = anchor.x + (v.pos.x - anchor.x) * kx;
                    v.pos.y = anchor.y + (v.pos.y - anchor.y) * ky;
                }
            }
        }
    }
}

}  // namespace teletext
