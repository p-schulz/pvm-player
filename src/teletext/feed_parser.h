#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace teletext {

struct FeedItem {
    std::string title;      // plain text, single line
    std::string link;
    std::string category;   // first <category> if the item has one, else ""
    std::string body;       // plain text (HTML stripped), paragraphs separated by "\n\n"
    std::time_t published = 0;  // UTC epoch seconds; 0 if absent or unparseable
};

struct Feed {
    bool ok = false;
    std::string error;      // set when !ok
    std::string title;
    std::vector<FeedItem> items;
};

// Parses an RSS 2.0, RSS 1.0 (RDF) or Atom document. For each item the body
// is whichever of the available fields (<content:encoded>, Atom <content>,
// <description>, <summary>) yields the most text -- feeds vary wildly in
// which one carries the full article. Text is UTF-8; run it through
// toTeletextAscii() before laying it out on the grid.
//
// Real-world feeds are messy, so on an XML error this retries once after
// stripping illegal control characters and bare '&'s.
Feed parseFeed(const std::string& xml);

// RFC 822/2822 ("Sun, 20 Sep 2026 10:58:11 +0200", "GMT", "EST", ...) and
// ISO 8601 / RFC 3339 ("2026-09-20T08:58:11Z", with offset or fractional
// seconds). Returns 0 when the text matches neither.
std::time_t parseFeedDate(const std::string& text);

}  // namespace teletext
