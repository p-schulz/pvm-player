#include "feed_parser.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "html_strip.h"

namespace teletext {

namespace {

// ---- Dates -----------------------------------------------------------------

std::time_t utcFromTm(std::tm tm) {
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

int monthFromName(const char* name) {
    static const char* const kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                          "jul", "aug", "sep", "oct", "nov", "dec"};
    std::string lower(name, 3);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (int i = 0; i < 12; ++i) {
        if (lower == kMonths[i]) {
            return i;
        }
    }
    return -1;
}

// Offset in seconds east of UTC for an RFC 822 zone token ("+0200", "GMT", "EST").
bool zoneOffset(const std::string& zone, long& seconds) {
    if (zone.empty()) {
        seconds = 0;
        return true;
    }
    if ((zone[0] == '+' || zone[0] == '-') && zone.size() >= 5) {
        const int hh = std::atoi(zone.substr(1, 2).c_str());
        const int mm = std::atoi(zone.substr(zone.size() == 5 ? 3 : 4, 2).c_str());
        seconds = (hh * 3600L + mm * 60L) * (zone[0] == '-' ? -1 : 1);
        return true;
    }
    static const struct {
        const char* name;
        int hours;
    } kZones[] = {{"UT", 0}, {"UTC", 0}, {"GMT", 0}, {"Z", 0},  {"EST", -5}, {"EDT", -4},
                  {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7},
                  {"CET", 1}, {"CEST", 2}, {"BST", 1}};
    for (const auto& z : kZones) {
        if (zone == z.name) {
            seconds = z.hours * 3600L;
            return true;
        }
    }
    seconds = 0;
    return true;  // unknown zone: treat as UTC rather than dropping the date
}

}  // namespace

std::time_t parseFeedDate(const std::string& raw) {
    const std::string text = collapseWhitespace(raw);
    if (text.empty()) {
        return 0;
    }

    // ISO 8601: 2026-09-20T08:58:11[.123][Z|+02:00|+0200]
    int year, month, day, hour = 0, minute = 0;
    double second = 0;
    int consumed = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d%n", &year, &month, &day, &consumed) == 3 && year > 1900) {
        const char* rest = text.c_str() + consumed;
        if (*rest == 'T' || *rest == 't' || *rest == ' ') {
            int n = 0;
            if (std::sscanf(rest + 1, "%d:%d:%lf%n", &hour, &minute, &second, &n) < 2) {
                hour = minute = 0;
                second = 0;
            } else if (n > 0) {
                rest += 1 + n;
            }
        }
        std::string zone;
        if (rest && *rest != '\0') {
            zone = rest;
            zone.erase(std::remove(zone.begin(), zone.end(), ':'), zone.end());
        }
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = static_cast<int>(second);
        long offset = 0;
        zoneOffset(zone, offset);
        return utcFromTm(tm) - offset;
    }

    // RFC 822: [Sun, ]20 Sep 2026 10:58[:11] +0200
    const char* p = text.c_str();
    if (const char* comma = std::strchr(p, ',')) {
        p = comma + 1;
    }
    char monthName[16] = {0};
    char zoneBuf[16] = {0};
    int y = 0, d = 0, h = 0, mi = 0, s = 0;
    int used = 0;
    if (std::sscanf(p, " %d %15s %d %d:%d%n", &d, monthName, &y, &h, &mi, &used) >= 5 && std::strlen(monthName) >= 3) {
        p += used;
        if (*p == ':') {  // optional seconds
            int secondsUsed = 0;
            if (std::sscanf(p + 1, "%d%n", &s, &secondsUsed) == 1) {
                p += 1 + secondsUsed;
            }
        }
        std::sscanf(p, " %15s", zoneBuf);
        const int mon = monthFromName(monthName);
        if (mon < 0) {
            return 0;
        }
        if (y < 100) {
            y += y < 70 ? 2000 : 1900;
        }
        std::tm tm{};
        tm.tm_year = y - 1900;
        tm.tm_mon = mon;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = s;
        long offset = 0;
        zoneOffset(zoneBuf, offset);
        return utcFromTm(tm) - offset;
    }
    return 0;
}

namespace {

// ---- XML helpers -----------------------------------------------------------

// All text under `node`: character data and CDATA concatenated, and child
// elements (e.g. Atom's type="xhtml" content) serialized so stripHtml() can
// deal with them like any other markup.
std::string nodeText(const pugi::xml_node& node) {
    std::string out;
    for (pugi::xml_node child : node.children()) {
        switch (child.type()) {
            case pugi::node_pcdata:
            case pugi::node_cdata:
                out += child.value();
                break;
            case pugi::node_element: {
                std::ostringstream ss;
                child.print(ss, "", pugi::format_raw);
                out += ss.str();
                break;
            }
            default:
                break;
        }
    }
    return out;
}

// The element text is HTML iff it says so or contains markup; either way
// stripHtml() is a no-op on plain text, so always run it.
std::string plainText(const pugi::xml_node& node) {
    return stripHtml(nodeText(node));
}

std::string singleLine(const std::string& text) {
    return collapseWhitespace(text);
}

// Footer lines feeds append to every excerpt ("Continue reading...", "Read
// full article", "Comments", WordPress's "The post X appeared first on Y").
std::string dropBoilerplate(std::string body) {
    auto trimEnd = [&body] {
        while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) {
            body.pop_back();
        }
    };
    const size_t post = body.rfind("The post ");
    if (post != std::string::npos && body.find(" appeared first on ", post) != std::string::npos) {
        body.erase(post);
    }
    trimEnd();

    static const char* const kTrailers[] = {"continue reading...", "continue reading", "read full article",
                                            "read more", "read more...", "comments", "read the full story"};
    for (bool removed = true; removed;) {
        removed = false;
        const size_t cut = body.rfind("\n\n");
        const std::string last = cut == std::string::npos ? body : body.substr(cut + 2);
        std::string lower = last;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        for (const char* trailer : kTrailers) {
            if (lower == trailer) {
                body.erase(cut == std::string::npos ? 0 : cut);
                trimEnd();
                removed = true;
                break;
            }
        }
    }
    return body;
}

std::string atomLink(const pugi::xml_node& entry) {
    std::string fallback;
    for (pugi::xml_node link : entry.children("link")) {
        const std::string href = link.attribute("href").as_string();
        const std::string rel = link.attribute("rel").as_string("alternate");
        if (href.empty()) {
            continue;
        }
        if (rel == "alternate") {
            return href;
        }
        if (fallback.empty()) {
            fallback = href;
        }
    }
    return fallback;
}

FeedItem parseRssItem(const pugi::xml_node& item) {
    FeedItem out;
    out.title = singleLine(plainText(item.child("title")));
    out.link = collapseWhitespace(nodeText(item.child("link")));
    out.category = singleLine(plainText(item.child("category")));
    if (out.category.empty()) {
        out.category = singleLine(plainText(item.child("dc:subject")));
    }

    std::string date = nodeText(item.child("pubDate"));
    if (date.empty()) date = nodeText(item.child("dc:date"));
    if (date.empty()) date = nodeText(item.child("published"));
    out.published = parseFeedDate(date);

    // Take whichever field carries the most text.
    std::string best = plainText(item.child("content:encoded"));
    const std::string description = plainText(item.child("description"));
    if (description.size() > best.size()) {
        best = description;
    }
    out.body = dropBoilerplate(best);
    return out;
}

FeedItem parseAtomEntry(const pugi::xml_node& entry) {
    FeedItem out;
    out.title = singleLine(plainText(entry.child("title")));
    out.link = atomLink(entry);
    out.category = entry.child("category").attribute("term").as_string();

    std::string date = nodeText(entry.child("published"));
    if (date.empty()) date = nodeText(entry.child("updated"));
    out.published = parseFeedDate(date);

    std::string best = plainText(entry.child("content"));
    const std::string summary = plainText(entry.child("summary"));
    if (summary.size() > best.size()) {
        best = summary;
    }
    out.body = dropBoilerplate(best);
    return out;
}

// pugixml only auto-detects Unicode encodings; legacy 8-bit ones must be
// requested explicitly, based on the XML declaration.
pugi::xml_encoding sniffEncoding(const std::string& xml) {
    std::string head = xml.substr(0, 200);
    for (char& c : head) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const size_t pos = head.find("encoding");
    if (pos == std::string::npos) {
        return pugi::encoding_auto;
    }
    if (head.find("iso-8859-1", pos) != std::string::npos || head.find("latin1", pos) != std::string::npos ||
        head.find("windows-1252", pos) != std::string::npos || head.find("cp1252", pos) != std::string::npos) {
        return pugi::encoding_latin1;
    }
    return pugi::encoding_auto;
}

// Removes characters XML 1.0 forbids outright and escapes '&' that doesn't
// start a valid reference -- the two most common reasons a "real" feed is
// rejected by a strict parser.
std::string sanitizeXml(const std::string& xml) {
    std::string out;
    out.reserve(xml.size());
    for (size_t i = 0; i < xml.size(); ++i) {
        const auto c = static_cast<unsigned char>(xml[i]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            continue;
        }
        if (c == '&') {
            const size_t semi = xml.find(';', i + 1);
            bool valid = false;
            if (semi != std::string::npos && semi - i <= 10 && semi > i + 1) {
                valid = true;
                for (size_t k = i + 1; k < semi; ++k) {
                    const auto ch = static_cast<unsigned char>(xml[k]);
                    if (!(std::isalnum(ch) || ch == '#')) {
                        valid = false;
                        break;
                    }
                }
            }
            if (!valid) {
                out += "&amp;";
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

bool loadDocument(pugi::xml_document& doc, const std::string& xml, std::string& error) {
    const unsigned int flags = pugi::parse_default | pugi::parse_declaration;
    pugi::xml_parse_result result = doc.load_buffer(xml.data(), xml.size(), flags, sniffEncoding(xml));
    if (result) {
        return true;
    }
    const std::string cleaned = sanitizeXml(xml);
    pugi::xml_parse_result retry = doc.load_buffer(cleaned.data(), cleaned.size(), flags, sniffEncoding(xml));
    if (retry) {
        return true;
    }
    error = std::string("XML parse error: ") + result.description() + " at offset " +
            std::to_string(result.offset);
    return false;
}

}  // namespace

Feed parseFeed(const std::string& xml) {
    Feed feed;
    pugi::xml_document doc;
    if (!loadDocument(doc, xml, feed.error)) {
        return feed;
    }

    if (pugi::xml_node rss = doc.child("rss")) {
        pugi::xml_node channel = rss.child("channel");
        feed.title = singleLine(plainText(channel.child("title")));
        for (pugi::xml_node item : channel.children("item")) {
            feed.items.push_back(parseRssItem(item));
        }
    } else if (pugi::xml_node rdf = doc.child("rdf:RDF")) {
        feed.title = singleLine(plainText(rdf.child("channel").child("title")));
        for (pugi::xml_node item : rdf.children("item")) {
            feed.items.push_back(parseRssItem(item));
        }
    } else if (pugi::xml_node atom = doc.child("feed")) {
        feed.title = singleLine(plainText(atom.child("title")));
        for (pugi::xml_node entry : atom.children("entry")) {
            feed.items.push_back(parseAtomEntry(entry));
        }
    } else {
        feed.error = "not an RSS or Atom document";
        return feed;
    }

    // An item with no title is unusable as a headline.
    feed.items.erase(std::remove_if(feed.items.begin(), feed.items.end(),
                                    [](const FeedItem& item) { return item.title.empty(); }),
                     feed.items.end());
    feed.ok = true;
    return feed;
}

}  // namespace teletext
