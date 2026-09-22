#pragma once

#include <array>
#include <cstdint>
#include <ctime>
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
//   23       colored "fastext" bar: red "-", green "+", yellow "News", blue "Refresh"
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

// A selectable row: Up/Down move a highlighted cursor among a page's links
// (in the order they were added), Enter activates whichever is selected.
// Exactly one of `gotoPage`/`playUrl` is set: a navigational link (a
// headline, a category, a show) jumps to another page; a playable link (an
// episode, a stream) loads `playUrl` into the player instead. `playTitle`
// is what the playback HUD shows for a play link -- a stream URL's own
// last path segment is usually an opaque CDN filename, not a title.
struct RowLink {
    int row = 0;
    int col = 0;
    int length = kCols;
    int gotoPage = 0;
    std::string playUrl;
    std::string playTitle;
};

struct TeletextPage {
    int number = 0;
    // 0 = this page's own main/first address (what digit entry, page links
    // and Left/Right's "step to a different page number" all land on); >=1
    // = a sub-page of `number` -- an authentic teletext concept: several
    // screens sharing one page number, "turned" without spending any of
    // the shared 100-999 number space (real decoders auto-rotate or use a
    // dedicated key; here Left/Right turns them, falling through to the
    // next/previous *page number* once a page's sub-pages are exhausted --
    // see Navigator::stepPage()). Used by the MediathekViewWeb sections'
    // (ARD, ZDF) per-show episode lists (mvw_pages.h) so a show with many
    // episodes never needs more than the one page number it was given;
    // NEWS/TAGESSCHAU never set this.
    int subPage = 0;
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
    // an article carries the number of the article's first page; 0 for
    // non-article pages (index, headline lists, placeholders).
    int prevPage = 0;
    int nextPage = 0;
    int articleFirstPage = 0;

    // The page "back" leads to: an article's category headline page, a
    // headline page's index (100). 0 = top level (the index itself).
    int parentPage = 0;

    // Selectable rows, in the order Up/Down cycle through them -- see
    // RowLink. Builders add one per link as they lay out the page (a
    // headline, a category, an episode, ...); the view highlights
    // whichever the Navigator has selected.
    std::vector<RowLink> links;

    // A blank black page with the colored fastext bar already on row 23.
    TeletextPage();

    // Registers a link covering columns [col, col+length) of `row`: Enter
    // jumps to `gotoPage` (addPageLink) or loads `url` into the player,
    // showing `title` in the playback HUD instead of the URL (addPlayLink).
    // Purely bookkeeping -- callers still draw the row's text/color
    // themselves; these don't touch the grid.
    void addPageLink(int row, int gotoPage, int col = 0, int length = kCols);
    void addPlayLink(int row, const std::string& url, const std::string& title, int col = 0, int length = kCols);

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
    // `page.subPage == 0` replaces that number's main address (as before);
    // `page.subPage >= 1` appends a sub-page to that number's carousel --
    // callers must add a number's sub-pages in order, 1, then 2, then 3...
    void add(TeletextPage page);
    // Marks the store as done being built and ready to publish/read. A
    // no-op today (nextPage()/prevPage() need no precomputation), kept as
    // the builder-pattern's clear "I'm finished" call for whichever future
    // query needs one.
    void finalize();

    // A number's main address (sub-page 0) -- unaffected by, and unaware
    // of, any sub-pages it may have.
    const TeletextPage* find(int number) const;
    // Any sub-page (0 = the same as find(number)); nullptr if `number`
    // doesn't exist, or exists but doesn't have that many sub-pages.
    const TeletextPage* find(int number, int subPage) const;
    // How many addresses `number` has: 0 if it doesn't exist, else 1 plus
    // however many sub-pages (1..N) were added beyond its main address.
    int subPageCount(int number) const;

    // Applies `fn` to every page's *main* address. Only valid before
    // finalize()/publication. Sub-pages are reached only through the
    // number they belong to (see find(number, subPage)), not enumerated
    // here -- they aren't independently linked to from anywhere.
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
    // unchanged for an empty store. This is what Left/Right fall through to
    // once the current page's own sub-pages (if any) are exhausted.
    int nextPage(int from) const;
    int prevPage(int from) const;

    const std::map<int, TeletextPage>& pages() const { return pages_; }

private:
    std::map<int, TeletextPage> pages_;             // each number's main (sub-page 0) address
    std::map<int, std::vector<TeletextPage>> subPages_;  // subPages_[n][i] is n's sub-page (i+1)
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

// "Sun 20 Sep 08:41"-style local time, thread-safe. Shared by every
// section's page builder for "Updated ..." status lines and the live
// header clock.
std::string formatLocalTime(std::time_t t, const char* format);

}  // namespace teletext
