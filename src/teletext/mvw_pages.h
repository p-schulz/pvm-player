#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "mvw_api.h"
#include "mvw_config.h"
#include "page.h"

namespace teletext {

enum class MvwFetchStatus {
    Pending,  // nothing fetched yet
    Ok,       // most recent fetch succeeded
    Failed,   // most recent fetch failed (items may still hold a stale result)
};

// What's known about one query: a favorite's episode list, or the generated
// "all shows" window.
struct MvwShowData {
    MvwFetchStatus status = MvwFetchStatus::Pending;
    std::vector<MvwItem> items;  // already deduped (see dropAccessibilityDuplicates); layout-ready
    std::time_t fetchedAt = 0;
};

// Builds a whole MediathekViewWeb browsing section for one broadcaster
// (`config.channel` -- ARD, ZDF, ...): page 100 (a FAVORITES list for quick
// selection, plus a link into the generated A-Z index), each favorite's own
// page (its episode list spread across that one page's sub-pages, playing
// an item on Enter -- see TeletextPage::subPage), and the generated
// "ALL SHOWS (A-Z)" index with its shows' own pages likewise. A show never
// needs more than the single page number it was given, however many
// episodes it has, so the shared 100-999 space only has to hold one entry
// per show (plus a handful of index-list pages), not one block each.
//
// `favoritesData[i]` belongs to `config.favorites[i]`; `azData` is the
// generated-window fetch, grouped by show and laid out alphabetically here
// (not pre-grouped by the caller) so the page budget and A-Z ordering stay
// in one place.
std::shared_ptr<const PageStore> buildMvwPageStore(const MvwConfig& config,
                                                    const std::vector<MvwShowData>& favoritesData,
                                                    const MvwShowData& azData);

}  // namespace teletext
