#include "title_art.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <vector>

namespace teletext {

namespace {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kPixelsPerCellX = 2;
constexpr int kPixelsPerCellY = 3;

// 5x7 glyphs, rows separated by '/', '#' = lit pixel.
const std::map<char, const char*>& font() {
    static const std::map<char, const char*> glyphs = {
        {'A', ".###./#...#/#...#/#####/#...#/#...#/#...#"}, {'B', "####./#...#/#...#/####./#...#/#...#/####."},
        {'C', ".####/#..../#..../#..../#..../#..../.####"}, {'D', "####./#...#/#...#/#...#/#...#/#...#/####."},
        {'E', "#####/#..../#..../####./#..../#..../#####"}, {'F', "#####/#..../#..../####./#..../#..../#...."},
        {'G', ".####/#..../#..../#.###/#...#/#...#/.####"}, {'H', "#...#/#...#/#...#/#####/#...#/#...#/#...#"},
        {'I', "#####/..#../..#../..#../..#../..#../#####"}, {'J', "..###/...#./...#./...#./...#./#..#./.##.."},
        {'K', "#...#/#..#./#.#../##.../#.#../#..#./#...#"}, {'L', "#..../#..../#..../#..../#..../#..../#####"},
        {'M', "#...#/##.##/#.#.#/#.#.#/#...#/#...#/#...#"}, {'N', "#...#/##..#/#.#.#/#..##/#...#/#...#/#...#"},
        {'O', ".###./#...#/#...#/#...#/#...#/#...#/.###."}, {'P', "####./#...#/#...#/####./#..../#..../#...."},
        {'Q', ".###./#...#/#...#/#...#/#.#.#/#..#./.##.#"}, {'R', "####./#...#/#...#/####./#.#../#..#./#...#"},
        {'S', ".####/#..../#..../.###./....#/....#/####."}, {'T', "#####/..#../..#../..#../..#../..#../..#.."},
        {'U', "#...#/#...#/#...#/#...#/#...#/#...#/.###."}, {'V', "#...#/#...#/#...#/#...#/#...#/.#.#./..#.."},
        {'W', "#...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#"}, {'X', "#...#/#...#/.#.#./..#../.#.#./#...#/#...#"},
        {'Y', "#...#/#...#/.#.#./..#../..#../..#../..#.."}, {'Z', "#####/....#/...#./..#../.#.../#..../#####"},
        {'0', ".###./#...#/#..##/#.#.#/##..#/#...#/.###."}, {'1', "..#../.##../..#../..#../..#../..#../.###."},
        {'2', ".###./#...#/....#/...#./..#../.#.../#####"}, {'3', "####./....#/....#/.###./....#/....#/####."},
        {'4', "...#./..##./.#.#./#..#./#####/...#./...#."}, {'5', "#####/#..../####./....#/....#/#...#/.###."},
        {'6', ".###./#..../#..../####./#...#/#...#/.###."}, {'7', "#####/....#/...#./..#../.#.../.#.../.#..."},
        {'8', ".###./#...#/#...#/.###./#...#/#...#/.###."}, {'9', ".###./#...#/#...#/.####/....#/....#/.###."},
        {'.', "...../...../...../...../...../...../..#.."}, {'-', "...../...../...../#####/...../...../....."},
        {'+', "...../..#../..#../#####/..#../..#../....."}, {'&', ".##../#..#./.##../.##.#/#..##/#..#./.##.#"},
        {'!', "..#../..#../..#../..#../..#../...../..#.."}, {' ', "...../...../...../...../...../...../....."},
    };
    return glyphs;
}

bool glyphPixel(char c, int x, int y) {
    const auto& glyphs = font();
    auto it = glyphs.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (it == glyphs.end()) {
        return false;
    }
    // Row y starts after y '/' separators; every row is exactly kGlyphW long.
    return it->second[y * (kGlyphW + 1) + x] == '#';
}

}  // namespace

void drawBanner(TeletextPage& page, const std::string& rawText) {
    const std::string text = rawText.substr(0, static_cast<size_t>(kMaxLogoChars));
    const int n = static_cast<int>(text.size());

    // Wide letters (2 pixels per font pixel) when they fit, else normal width.
    const int scaleX = (n * (kGlyphW * 2 + 2) - 2 <= kCols * kPixelsPerCellX) ? 2 : 1;
    const int advance = (kGlyphW + 1) * scaleX;  // glyph plus a one-font-pixel gap, scaled
    const int textW = n > 0 ? n * advance - scaleX : 0;

    const int canvasW = kCols * kPixelsPerCellX;
    const int canvasH = kBannerRows * kPixelsPerCellY;
    const int originX = (canvasW - textW) / 2;
    const int originY = (canvasH - kGlyphH) / 2;

    std::vector<std::vector<bool>> canvas(static_cast<size_t>(canvasH), std::vector<bool>(static_cast<size_t>(canvasW), false));
    for (int i = 0; i < n; ++i) {
        for (int gy = 0; gy < kGlyphH; ++gy) {
            for (int gx = 0; gx < kGlyphW; ++gx) {
                if (!glyphPixel(text[static_cast<size_t>(i)], gx, gy)) {
                    continue;
                }
                for (int sx = 0; sx < scaleX; ++sx) {
                    const int x = originX + i * advance + gx * scaleX + sx;
                    const int y = originY + gy;
                    if (x >= 0 && x < canvasW && y >= 0 && y < canvasH) {
                        canvas[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
                    }
                }
            }
        }
    }

    page.fillRows(kBannerFirstRow, kBannerRows, Color::Blue);
    for (int row = 0; row < kBannerRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            unsigned mask = 0;
            for (int py = 0; py < kPixelsPerCellY; ++py) {
                for (int px = 0; px < kPixelsPerCellX; ++px) {
                    if (canvas[static_cast<size_t>(row * kPixelsPerCellY + py)][static_cast<size_t>(col * kPixelsPerCellX + px)]) {
                        mask |= 1u << (py * kPixelsPerCellX + px);
                    }
                }
            }
            if (mask) {
                page.setSixel(kBannerFirstRow + row, col, static_cast<std::uint8_t>(mask), Color::White, Color::Blue);
            }
        }
    }
}

}  // namespace teletext
