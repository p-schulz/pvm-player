#pragma once

#include <string>
#include <vector>

namespace teletext {

// A pinned show for quick access from page 100's FAVORITES list -- exactly
// the "favorites section for quick selection" ask: a fixed page number, so
// it's always in the same place even while the generated A-Z index churns.
// Its episode list lives entirely on *sub-pages* of that one number (see
// mvw_pages.h) -- a favorite never needs more than the single page number
// below, however many episodes it has.
struct MvwFavorite {
    int page = 0;         // this favorite's fixed page number, e.g. 110
    std::string topic;    // MediathekViewWeb `topic` query value, e.g. "Babylon Berlin"
    std::string label;    // title-art/header text (defaults to `topic`, folded+upper-cased)
    // MediathekViewWeb `channel` query value. Empty means "use this
    // section's own MvwConfig::channel" (resolved once the whole file is
    // parsed, since `channel=` may appear before or after `favorite=`
    // lines). Most shows air on exactly one channel -- many regional
    // productions (e.g. a WDR- or SWR-produced series) are tagged with that
    // broadcaster's own name even when branded under the national one;
    // check with teletext_probe if a favorite otherwise turns up almost
    // empty.
    std::string channel;
};

struct MvwConfig {
    std::vector<MvwFavorite> favorites;  // validated: sorted by page, no two share a page number
    // Cap on how many *sub-pages* one show's episode list may use (not a
    // count of distinct 100-999 page numbers -- see mvw_pages.h). Generous
    // by default since sub-pages are effectively free: they don't compete
    // with any other show or section for the shared page-number space.
    int maxEpisodePages = 30;
    int azStartPage = 200;     // the generated "ALL SHOWS (A-Z)" section runs from here to page 999
    int azWindowDays = 14;     // how far back the generated index looks for "available now" shows
    int azQuerySize = 1500;    // how many recent items the generated index is built from
    // Cap on distinct shows in the generated index. Each now costs exactly
    // one page number (plus its sub-pages, which are free), so this is a
    // sanity guard against a huge window, not the tight page-budget limit
    // it used to be -- 200 comfortably covers a full A-Z alphabet's worth
    // of shows for a two-week window (~110 distinct ARD shows, measured).
    int azMaxShows = 200;
    int refreshMinutes = 60;   // how long a fetched page set is considered fresh before auto re-fetching

    // What broadcaster this whole section is scoped to. The generated A-Z
    // index always queries MediathekViewWeb's `channel` field for exactly
    // this value (unlike a favorite, which may override its own channel --
    // see MvwFavorite::channel); this is what makes the same service code
    // serve either ARD or ZDF from two different config files.
    std::string channel = "ARD";
    std::string serviceName = "ARD Mediathek";  // root-menu label and page-100 header text
    std::string serviceLogo = "ARD MEDIATHEK";  // title-art banner text, up to 13 characters
};

// Personal-use, on-demand browsing rather than a ticker, but still capped:
// nothing in the config can make requestRefresh() (the blue key) fire more
// often than this against a single show/the A-Z window.
constexpr int kMvwMinRefreshMinutes = 5;

// ard.cfg/zdf.cfg are flat text files, one setting per line ('#' starts a
// comment):
//
//   channel=ARD
//   service_name=ARD Mediathek
//   service_logo=ARD MEDIATHEK
//   max_episode_pages=30
//   az_start_page=200
//   az_window_days=14
//   az_query_size=1500
//   az_max_shows=200
//   refresh_minutes=60
//   favorite=110 | Babylon Berlin | BABYLON BERLIN
//   favorite=130 | Mord mit Aussicht | MORD/AUSSICHT | WDR
//
// `channel` is the MediathekViewWeb `channel` this whole section (and its
// generated A-Z index) is scoped to -- "ARD" (Das Erste) or "ZDF" are the
// two national broadcasters this app ships config for, but any channel
// MediathekViewWeb indexes works. `service_name`/`service_logo` are the
// root-menu label and the title-art banner text (up to 13 characters) --
// they exist so the very same page-building code can present itself as
// "ARD MEDIATHEK" or "ZDF MEDIATHEK" depending only on which config file
// was loaded.
//
// A favorite is `page | topic [| LABEL [| CHANNEL]]`: `topic` is matched
// against MediathekViewWeb's `topic` field (usually the show's exact name
// -- check with teletext_probe first); LABEL is the title-art and header
// text, defaulting to `topic` (ASCII-folded, upper-cased); CHANNEL is the
// MediathekViewWeb `channel` to search, defaulting to this config's own
// `channel` setting -- many shows are actually produced by, and tagged
// under, one specific regional broadcaster (WDR, SWR, BR, ...) instead of
// the national one they air under, in which case the default finds little
// or nothing for them: if a favorite turns up almost empty, check
// `teletext_probe --ard "<topic>" --all-channels` for what channel its real
// episodes carry, and set it here. The generated A-Z index is deliberately
// not configurable this way -- it always stays scoped to this config's own
// `channel`, a single manageable feed, rather than pulling in every
// regional broadcaster's entire catalog at once. Each favorite gets exactly
// one page number regardless of channel -- its episodes are laid out across
// that page's own sub-pages (Left/Right turns them, like a real teletext
// decoder's page carousel), so favorites never collide with the generated
// A-Z section (az_start_page..999) over page-number space; they only
// collide if two favorites are given the very same page number.
//
// Problems (bad lines, a page reused twice, ...) are reported through
// `warnings` and the offending line skipped -- one typo never disables the
// whole section. Returns false only if the file could not be opened.
bool loadMvwConfig(const std::string& path, MvwConfig& config, std::vector<std::string>* warnings);

// Same, from text already in memory (used by the self-test).
void parseMvwConfig(const std::string& text, MvwConfig& config, std::vector<std::string>* warnings);

}  // namespace teletext
