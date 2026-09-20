#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Teletext page model: the World System Teletext grid (40 columns x 24 rows)
// and an immutable page collection ("store") the renderer and navigator read
// from. Nothing in here touches ImGui, GLFW or the network -- see
// teletext_view.h for drawing and news_service.h for how stores get built.
namespace teletext {

constexpr int kCols = 40;
constexpr int kRows = 24;

// Layout of the 24 rows:
//   0        header  -- drawn live by the view: current page (green), target
//                       page, provider name (cyan), date and clock
//   1..21    content (articles use rows 2..21, leaving row 1 as a margin)
//   22       info line: "1/3  MORE 112", "Updated 20 Sep 11:23", ...
//   23       colored "fastext" bar: red "-", green "+", yellow "News", blue "Weather"
constexpr int kHeaderRow = 0;
constexpr int kBodyFirstRow = 1;
constexpr int kInfoRow = kRows - 2;
constexpr int kFooterRow = kRows - 1;
constexpr int kContentFirstRow = 2;
constexpr int kContentRows = kInfoRow - kContentFirstRow;  // 20

// Header columns: "111 111 provider.name      20.09. 12:34:56"
constexpr int kHeaderCurrentCol = 0;   // 3 digits, green
constexpr int kHeaderTargetCol = 4;    // 3 characters: the page being typed, else the current page
constexpr int kHeaderServiceCol = 8;   // provider name, cyan, up to kHeaderServiceWidth
constexpr int kHeaderServiceWidth = 16;
constexpr int kHeaderDateCol = 25;     // "DD.MM." (6) + space + "HH:MM:SS" (8)

constexpr int kIndexPage = 100;
constexpr int kMinPage = 100;
constexpr int kMaxPage = 999;

// The eight teletext colors.
enum class Color : std::uint8_t { Black, Red, Green, Yellow, Blue, Magenta, Cyan, White };

// Block-graphics cells ("sixels"): each character cell is a 2x3 grid of
// pixels, bit 0 = top-left, 1 = top-right, 2 = middle-left, 3 = middle-right,
// 4 = bottom-left, 5 = bottom-right. A cell's `sixel` value is 0 for an
// ordinary text character, else kSixelFlag | mask.
constexpr std::uint8_t kSixelFlag = 0x80;

struct TeletextPage {
    int number = 0;
    std::string category;
    std::string service;  // provider name shown in the header, e.g. "tagesschau.de"

    // Text plane: always exactly kCols characters per row (ASCII only -- see
    // html_strip.h's toTeletextAscii()); use the setters below rather than
    // assigning directly. Sixel cells hold ' ' here.
    std::array<std::string, kRows> lines;
    // Color planes, one entry per cell.
    std::array<std::array<Color, kCols>, kRows> fg;
    std::array<std::array<Color, kCols>, kRows> bg;
    std::array<std::array<std::uint8_t, kCols>, kRows> sixel;

    // Continuation chain for multi-page articles (0 = none). Every page of
    // an article carries the number of the article's first page, which is
    // what Left/Right navigation skips over; 0 for non-article pages
    // (index, headline lists, placeholders).
    int prevPage = 0;
    int nextPage = 0;
    int articleFirstPage = 0;

    // The page "back" leads to: an article's category headline page, a
    // headline page's index (100). 0 = top level (the index itself).
    int parentPage = 0;

    // A blank black page with the colored fastext bar already on row 23.
    TeletextPage();

    // Replaces a whole row: `text` padded/truncated to kCols, colored
    // `foreground` on `background` across the full width.
    void setLine(int row, const std::string& text, Color foreground = Color::White,
                 Color background = Color::Black);
    // Overwrites text at a column, keeping the cells' background.
    void putText(int row, int col, const std::string& text, Color foreground);
    // Recolors a span without changing its text.
    void paint(int row, int col, int length, Color foreground, Color background);
    // Blanks `count` rows starting at `firstRow` to a solid background.
    void fillRows(int firstRow, int count, Color background);
    void setSixel(int row, int col, std::uint8_t mask, Color foreground, Color background);

    bool isArticlePage() const { return articleFirstPage != 0; }
};

// An immutable-once-published set of pages. Builders call add() then
// finalize(); after that the store is only ever read (from the render
// thread, through a shared_ptr<const PageStore>), so no locking is needed.
class PageStore {
public:
    void add(TeletextPage page);
    // Computes the derived article index. Must be called once after the last
    // add() and before any navigation query below.
    void finalize();

    const TeletextPage* find(int number) const;
    // Applies `fn` to every page. Only valid before finalize()/publication.
    template <typename Fn>
    void forEachPage(Fn fn) {
        for (auto& entry : pages_) {
            fn(entry.second);
        }
    }
    bool empty() const { return pages_.empty(); }
    size_t size() const { return pages_.size(); }

    // Next/previous *populated* page number after/before `from` (which need
    // not itself exist), wrapping around at the ends. Returns `from`
    // unchanged for an empty store.
    int nextPage(int from) const;
    int prevPage(int from) const;

    // First page of the next/previous article relative to `from`, wrapping.
    // "Article" means a chain of continuation pages; `from` may be any page
    // (including the index or a page that doesn't exist).
    int nextArticle(int from) const;
    int prevArticle(int from) const;

    const std::map<int, TeletextPage>& pages() const { return pages_; }

private:
    std::map<int, TeletextPage> pages_;
    std::vector<int> articleStarts_;  // sorted first-page numbers
};

// Writes the header row (row 0) onto `page`: current page number (green),
// `targetLabel` (white -- the page being typed as "1--", else the current
// page), the page's provider name (cyan), then `date` ("DD.MM.") and `time`
// ("HH:MM:SS") in white. The view calls this every frame so the clock is live.
void composeHeader(TeletextPage& page, const std::string& targetLabel, const std::string& date,
                   const std::string& time);

// "PAGE NOT FOUND" placeholder for a number with no page behind it.
// `service` is the provider name shown in the header.
TeletextPage makeNotFoundPage(int number, const std::string& service = "PVM NEWS");

}  // namespace teletext
