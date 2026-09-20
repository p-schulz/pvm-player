#pragma once

#include <string>

#include "page.h"

namespace teletext {

// Teletext-decoder navigation state: which page is showing, plus the
// digit-entry buffer ("P1--" while typing). GLFW-free on purpose -- App maps
// keys onto these calls, and the self-test drives them directly.
//
// Key semantics (see TELETEXT_PLAN.md "Conventions"):
//   up()/down()      next / previous page number
//   left()/right()   previous / next article (skips continuation pages)
//   digit()          direct page entry; jumps on the 3rd digit. An entry
//                    that stalls with fewer than 3 digits is abandoned after
//                    kEntryTimeoutSeconds (there is no page number it could
//                    sensibly mean), leaving the current page showing.
class Navigator {
public:
    static constexpr double kEntryTimeoutSeconds = 3.0;

    int currentPage() const { return current_; }
    bool entering() const { return !entry_.empty(); }

    // The header's "target page" field: the current page ("111") normally;
    // "1--" / "12-" while a page number is being typed.
    std::string targetLabel() const;

    void goTo(int page);
    void up(const PageStore& store);
    void down(const PageStore& store);
    void left(const PageStore& store);
    void right(const PageStore& store);
    void digit(int value, double now);

    // Call once per frame: abandons a stalled digit entry.
    void update(double now);

    // Abandons a partial entry. Returns true if there was one to abandon.
    bool cancelEntry();

    // "Back": abandons a partial entry if there is one, else climbs one
    // level (article -> its headline page -> index). Returns false when
    // already at the top (index), i.e. the caller should leave NEWS.
    bool back(const PageStore& store);

private:
    int current_ = kIndexPage;
    std::string entry_;
    double lastDigitTime_ = 0.0;
};

}  // namespace teletext
