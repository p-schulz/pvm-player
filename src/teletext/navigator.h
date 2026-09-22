#pragma once

#include <string>

#include "page.h"

namespace teletext {

// Teletext-decoder navigation state: which page (and, within it, which
// sub-page) is showing, which of that page's selectable links (RowLink, see
// page.h) is highlighted, plus the digit-entry buffer. GLFW-free on purpose
// -- App maps keys onto these calls, and the self-test drives them directly.
//
// Controls (same across NEWS/TAGESSCHAU/ARD):
//   selectLink()   Up/Down: move the highlighted link, clamped (no wrap --
//                  a page's link list is short and fixed)
//   stepPage()     Left/Right: turns to the next/previous *sub-page* of the
//                  current page number if it has one; once its sub-pages
//                  are exhausted, falls through to the next/previous
//                  populated *page number* instead (wrapping), landing on
//                  that number's own first sub-page either way.
//   digit()        direct page entry; jumps to sub-page 0 on the 3rd digit.
//                  An entry that stalls with fewer than 3 digits is
//                  abandoned after kEntryTimeoutSeconds.
// Enter is not this class's job: App reads page->links[selectedLink()] and
// either calls goTo() (a page link) or starts playback (a play link).
class Navigator {
public:
    static constexpr double kEntryTimeoutSeconds = 3.0;

    int currentPage() const { return current_; }
    int currentSubPage() const { return currentSubPage_; }
    int selectedLink() const { return selectedLink_; }
    bool entering() const { return !entry_.empty(); }

    // The header's "target page" field: the current page ("111") normally;
    // "1--" / "12-" while a page number is being typed. Never shows the
    // sub-page -- only the 3-digit number is ever typed.
    std::string targetLabel() const;

    // Jumps straight to `page`'s own first sub-page, resetting the link
    // selection to the first (top) selectable row.
    void goTo(int page);

    // Left/Right, direction -1 or +1 -- see the class comment.
    void stepPage(const PageStore& store, int direction);

    // Up/Down: moves the highlighted link by `direction` (-1 or +1),
    // clamped to [0, linkCount). A no-op if linkCount is 0. `linkCount` is
    // the current page's links.size(), which the caller already has.
    void selectLink(int direction, int linkCount);

    void digit(int value, double now);

    // Call once per frame: abandons a stalled digit entry.
    void update(double now);

    // Abandons a partial entry. Returns true if there was one to abandon.
    bool cancelEntry();

    // "Back": abandons a partial entry if there is one, else climbs one
    // level (article -> its headline page -> index). Returns false when
    // already at the top (index), i.e. the caller should leave this section.
    bool back(const PageStore& store);

private:
    int current_ = kIndexPage;
    int currentSubPage_ = 0;
    int selectedLink_ = 0;
    std::string entry_;
    double lastDigitTime_ = 0.0;
};

}  // namespace teletext
