#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace teletext {

struct MvwItem {
    std::string title;   // UTF-8, as received (e.g. "Folge 7 · Staffel 1 | Babylon Berlin (S01/E07)")
    std::string topic;    // the show/program name, e.g. "Babylon Berlin"
    std::string channel;  // MediathekViewWeb `channel`, e.g. "ARD" or a regional broadcaster like "WDR"
    std::string description;
    std::time_t timestamp = 0;
    int durationSeconds = 0;
    std::string videoUrl;  // direct https stream URL; items without one are dropped during parsing
    int season = 0;         // parsed from the title, 0 if absent
    int episode = 0;        // parsed from the title, 0 if absent
    bool accessibilityVariant = false;  // title says "(Audiodeskription)" or "(Gebärdensprache)"
};

struct MvwQueryResult {
    bool ok = false;
    std::string error;
    std::vector<MvwItem> items;
};

// One MediathekViewWeb query, filtered to `channel` (exact match -- most
// shows air on exactly one: "ARD" for Das Erste, or a regional "Dritte"
// broadcaster's own name, e.g. "WDR", "SWR", "BR"; check with
// teletext_probe if unsure) and optionally further to one show's `topic`.
// An empty `channel` searches every broadcaster MediathekViewWeb indexes,
// not just ARD -- useful for finding which channel a show's real episodes
// actually carry (see MvwItem::channel), but never used for the generated
// A-Z index, which stays deliberately scoped to "ARD". `size` is clamped
// to a sane range -- MediathekViewWeb itself is happy with a few thousand
// per query (measured offset=9990 still answers in ~15ms), but this app
// never needs that many at once. Blocking; call off the render thread,
// like httpGet/httpPostJson.
MvwQueryResult queryMediathek(const std::string& channel, const std::string& topicFilter, int size,
                        bool newestFirst = true);

// Parses "(S02/E04)" out of a title. {0, 0} if the pattern isn't present.
struct SeasonEpisode {
    int season = 0;
    int episode = 0;
};
SeasonEpisode parseSeasonEpisode(const std::string& title);

// True if the title marks this as an accessibility variant of another item
// -- spoken-narration "(Audiodeskription)" or sign-language-interpreted
// "(Gebärdensprache)".
bool isAccessibilityVariantTitle(const std::string& title);

// A short display title for an episode row, with the boilerplate a raw
// MediathekViewWeb title carries removed once season/episode is already
// shown as its own badge: "Folge 4 · Staffel 2 | Babylon Berlin (S02/E04)
// (Audiodeskription)" -> "Folge 4". Falls back to the original title,
// whitespace-collapsed, if nothing sensible survives stripping.
std::string shortEpisodeTitle(const std::string& title, const std::string& showTopic);

// Drops the accessibility-variant twin of an item ("(Audiodeskription)" or
// "(Gebärdensprache)") when a plain version of the same episode (matched by
// season/episode, or by title once the marker is stripped) also exists;
// keeps it if it is the only version available. Relative order of the
// items that remain is preserved.
std::vector<MvwItem> dropAccessibilityDuplicates(std::vector<MvwItem> items);

}  // namespace teletext
