// Logic tests for the teletext core (no window, no network). Run via ctest or
// directly: ./teletext_selftest -- prints each failure and exits non-zero.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "teletext/article_pages.h"
#include "teletext/feed_parser.h"
#include "teletext/html_strip.h"
#include "teletext/navigator.h"
#include "teletext/news_config.h"
#include "teletext/news_pages.h"
#include "teletext/news_service.h"
#include "teletext/page.h"
#include "teletext/title_art.h"

namespace {

int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const auto va = (a);                                                                              \
        const auto vb = (b);                                                                              \
        if (!(va == vb)) {                                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n  got:      [%s]\n  expected: [%s]\n", __FILE__,   \
                         __LINE__, #a, #b, std::string(va).c_str(), std::string(vb).c_str());             \
            ++failures;                                                                                   \
        }                                                                                                 \
    } while (0)

using namespace teletext;

// A source with an explicit page range (default: 10 pages, like block_size=10).
NewsSource mkSource(int start, const std::string& category, const std::string& url, const std::string& provider = "",
                    const std::string& logo = "", int end = 0) {
    NewsSource s;
    s.startPage = start;
    s.endPage = end ? end : start + 9;
    s.category = category;
    s.url = url;
    s.provider = provider;
    s.logo = logo.empty() ? category : logo;
    return s;
}

void testHtmlStrip() {
    CHECK_EQ(stripHtml("<p>Hello <b>world</b></p><p>Second</p>"), "Hello world\n\nSecond");
    CHECK_EQ(stripHtml("a<br>b<br/>c"), "a\nb\nc");
    CHECK_EQ(stripHtml("Tom &amp; Jerry &lt;3 &#233; &#x41; &nbsp;x"), "Tom & Jerry <3 \xC3\xA9 A x");
    CHECK_EQ(stripHtml("keep<script>var x = '<p>'; </script> this<style>p{}</style>"), "keep this");
    CHECK_EQ(stripHtml("x<!-- <p>hidden</p> -->y"), "xy");
    CHECK_EQ(stripHtml("<a href=\"a>b\">link</a> text"), "link text");
    CHECK_EQ(stripHtml("1 < 2 and 3 > 2"), "1 < 2 and 3 > 2");
    CHECK_EQ(stripHtml("<ul><li>one</li><li>two</li></ul>"), "* one\n* two");
    CHECK_EQ(stripHtml("  \n  lots \t of \n\n\n\n space  "), "lots of\n\nspace");
    CHECK_EQ(stripHtml("&amp;bogus; &unknown; AT&T"), "&bogus; &unknown; AT&T");
    // Double-escaped markup (&lt;p&gt; arriving as literal "<p>" after XML decoding).
    CHECK_EQ(stripHtml("&lt;p&gt;inner&lt;/p&gt;"), "inner");
    CHECK_EQ(stripHtml("<p>unclosed <b>bold"), "unclosed bold");
    CHECK_EQ(stripHtml(""), "");
}

void testAscii() {
    CHECK_EQ(toTeletextAscii("K\xC3\xBCnstliche Intelligenz f\xC3\xBCr Gro\xC3\x9F"), "Kuenstliche Intelligenz fuer Gross");
    CHECK_EQ(toTeletextAscii("\xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x98s\xE2\x80\x99 a\xE2\x80\x93" "b\xE2\x80\xA6"),
             "\"quoted\" 's' a-b...");
    CHECK_EQ(toTeletextAscii("caf\xC3\xA9 \xE2\x82\xAC" "5"), "cafe EUR5");
    CHECK_EQ(toTeletextAscii("smile \xF0\x9F\x98\x80 done"), "smile  done");  // emoji dropped
    CHECK_EQ(toTeletextAscii("a\tb\x01" "c"), "a b c");
    CHECK_EQ(toTeletextAscii("bad \xFF\xFE bytes"), "bad ?? bytes");
    CHECK_EQ(toTeletextAscii("trunc \xC3"), "trunc ?");
}

void testDates() {
    CHECK_EQ(std::to_string(parseFeedDate("Sun, 20 Sep 2026 10:58:11 +0200")),
             std::to_string(parseFeedDate("2026-09-20T08:58:11Z")));
    CHECK_EQ(std::to_string(parseFeedDate("Sun, 20 Sep 2026 08:58:11 GMT")),
             std::to_string(parseFeedDate("2026-09-20T10:58:11+02:00")));
    CHECK_EQ(std::to_string(parseFeedDate("2026-09-20T08:58:11.123Z")), std::to_string(parseFeedDate("2026-09-20T08:58:11Z")));
    CHECK_EQ(std::to_string(parseFeedDate("2026-09-20")), std::to_string(parseFeedDate("2026-09-20T00:00:00Z")));
    CHECK_EQ(std::to_string(parseFeedDate("20 Sep 2026 08:58 EST")), std::to_string(parseFeedDate("2026-09-20T13:58:00Z")));
    CHECK_EQ(std::to_string(parseFeedDate("garbage")), "0");
    CHECK_EQ(std::to_string(parseFeedDate("")), "0");
    CHECK(parseFeedDate("2026-09-20T08:58:11Z") > 1'700'000'000);
}

void testFeedParser() {
    const char* rss = R"(<?xml version="1.0"?>
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
 <channel><title>Test &amp; Co</title>
  <item><title><![CDATA[First <b>story</b>]]></title><link>http://x/1</link>
   <pubDate>Sun, 20 Sep 2026 10:58:11 +0200</pubDate><category>World</category>
   <description>&lt;p&gt;short&lt;/p&gt;</description>
   <content:encoded><![CDATA[<p>The long full text.</p><p>Second para.</p>]]></content:encoded></item>
  <item><title></title><description>no title, dropped</description></item>
  <item><title>Second</title><description>only a teaser &amp; more</description></item>
 </channel></rss>)";
    Feed f = parseFeed(rss);
    CHECK(f.ok);
    CHECK_EQ(f.title, "Test & Co");
    CHECK_EQ(std::to_string(f.items.size()), "2");
    if (f.items.size() == 2) {
        CHECK_EQ(f.items[0].title, "First story");
        CHECK_EQ(f.items[0].body, "The long full text.\n\nSecond para.");  // content:encoded beats description
        CHECK_EQ(f.items[0].category, "World");
        CHECK_EQ(f.items[0].link, "http://x/1");
        CHECK_EQ(f.items[1].body, "only a teaser & more");
    }

    const char* atom = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom"><title>Atom Feed</title>
 <entry><title>Entry one</title><link rel="self" href="http://x/self"/><link rel="alternate" href="http://x/alt"/>
  <updated>2026-09-20T08:58:11Z</updated><category term="tech"/>
  <summary>sum</summary><content type="html">&lt;p&gt;Full &lt;i&gt;content&lt;/i&gt;&lt;/p&gt;</content></entry>
 <entry><title>Entry two</title><content type="xhtml"><div xmlns="http://www.w3.org/1999/xhtml"><p>xhtml body</p></div></content></entry>
</feed>)";
    f = parseFeed(atom);
    CHECK(f.ok);
    CHECK_EQ(std::to_string(f.items.size()), "2");
    if (f.items.size() == 2) {
        CHECK_EQ(f.items[0].link, "http://x/alt");
        CHECK_EQ(f.items[0].body, "Full content");
        CHECK_EQ(f.items[0].category, "tech");
        CHECK(f.items[0].published > 0);
        CHECK_EQ(f.items[1].body, "xhtml body");
    }

    // Malformed: bare ampersand and a control character, recovered by the retry.
    f = parseFeed("<rss><channel><title>T</title><item><title>AT&T \x01 news</title></item></channel></rss>");
    CHECK(f.ok);
    if (!f.items.empty()) {
        CHECK_EQ(f.items[0].title, "AT&T news");
    }

    CHECK(!parseFeed("<html><body>not a feed</body></html>").ok);
    CHECK(!parseFeed("").ok);
    CHECK(!parseFeed("<rss><channel><item>").ok);

    // Latin-1 declared encoding.
    f = parseFeed("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><rss><channel><title>T</title><item><title>K\xFC" "he</title></item></channel></rss>");
    CHECK(f.ok);
    if (!f.items.empty()) {
        CHECK_EQ(toTeletextAscii(f.items[0].title), "Kuehe");
    }

    // Trailing boilerplate is removed.
    f = parseFeed("<rss><channel><item><title>T</title><description>&lt;p&gt;Body text.&lt;/p&gt;&lt;p&gt;Continue reading...&lt;/p&gt;</description></item></channel></rss>");
    if (!f.items.empty()) {
        CHECK_EQ(f.items[0].body, "Body text.");
    }
}

void testWrap() {
    auto rows = wrapText("The quick brown fox jumps over the lazy dog", 16);
    CHECK_EQ(std::to_string(rows.size()), "3");
    for (const auto& r : rows) CHECK(r.size() <= 16);
    CHECK_EQ(rows[0], "The quick brown");

    rows = wrapText("para one\n\n\n\npara two\nline break", 40);
    CHECK_EQ(std::to_string(rows.size()), "4");  // one blank between paragraphs, "\n" forces a break
    CHECK_EQ(rows[1], "");

    rows = wrapText("http://example.com/a/very/long/url/that/exceeds/the/grid/width/by/quite/a/lot", 40);
    CHECK(rows.size() >= 2);
    for (const auto& r : rows) CHECK(r.size() <= 40);
    CHECK(wrapText("", 40).empty());
    CHECK(wrapText("\n\n", 40).empty());
    for (const auto& r : wrapText("exactly forty characters long, right? x", 40)) CHECK(r.size() <= 40);
}

std::string longBody(int paragraphs) {
    std::string body;
    for (int i = 0; i < paragraphs; ++i) {
        body += "Paragraph " + std::to_string(i) +
                " has a fair amount of words in it so that it wraps across several rows of the grid nicely.\n\n";
    }
    return body;
}

void testPagination() {
    // Short article: one page, title + meta + body all present.
    ArticleLayout l = layoutArticle("Short headline", "Sun 20 Sep 08:41 - BBC", "Just one paragraph.", 3);
    CHECK_EQ(std::to_string(l.pages.size()), "1");
    CHECK(!l.truncated);
    CHECK_EQ(l.pages[0][0], "Short headline");
    CHECK_EQ(l.pages[0][1], "Sun 20 Sep 08:41 - BBC");

    // Long article: several pages, every row fits, no page overflows the content area.
    l = layoutArticle("A long article", "meta", longBody(30), 10);
    CHECK(l.pages.size() >= 3);
    CHECK(!l.truncated);
    for (const auto& page : l.pages) {
        CHECK(static_cast<int>(page.size()) <= kContentRows);
        CHECK(!page.back().empty());
        for (const auto& row : page) CHECK(static_cast<int>(row.size()) <= kCols);
    }
    CHECK_EQ(l.pages[1][0], "A long article (cont.)");

    // Page cap: truncated with a notice on the last page, still within the grid.
    l = layoutArticle("A long article", "meta", longBody(30), 2);
    CHECK_EQ(std::to_string(l.pages.size()), "2");
    CHECK(l.truncated);
    CHECK_EQ(l.pages.back().back(), "[story continues at source]");
    CHECK(static_cast<int>(l.pages.back().size()) <= kContentRows);

    // Very long title: capped at 3 rows.
    l = layoutArticle(std::string(400, 'x') + " tail", "", "body", 1);
    CHECK(l.pages[0].size() <= 6);

    // Numbered pages: chain links, headers, footer.
    l = layoutArticle("A long article", "meta", longBody(30), 3);
    auto pages = makeArticlePages(l, "WORLD", "wire.example", 210);
    CHECK_EQ(std::to_string(pages.size()), std::to_string(l.pages.size()));
    CHECK_EQ(std::to_string(pages[0].number), "210");
    CHECK_EQ(std::to_string(pages[0].prevPage), "0");
    CHECK_EQ(std::to_string(pages[0].nextPage), "211");
    CHECK_EQ(std::to_string(pages[1].prevPage), "210");
    CHECK_EQ(std::to_string(pages.back().nextPage), "0");
    for (const auto& p : pages) {
        CHECK_EQ(std::to_string(p.articleFirstPage), "210");
        for (const auto& line : p.lines) CHECK_EQ(std::to_string(line.size()), "40");
    }
    CHECK_EQ(pages[0].service, "wire.example");
    CHECK(pages[0].lines[kInfoRow].find("1/3") != std::string::npos);
    CHECK(pages[0].lines[kInfoRow].find("MORE 211") != std::string::npos);
    CHECK(pages.back().lines[kInfoRow].find("MORE") == std::string::npos);
    // Colors: headline yellow, dateline cyan, body white.
    CHECK(pages[0].fg[kContentFirstRow][0] == Color::Yellow);
    CHECK(pages[0].fg[kContentFirstRow + 1][0] == Color::Cyan);   // "A long article" is one row, then the dateline
    CHECK(pages[0].fg[kContentFirstRow + 3][0] == Color::White);
}

PageStore navStore() {
    PageStore store;
    auto add = [&](int number, int articleFirst) {
        TeletextPage p;
        p.number = number;
        p.articleFirstPage = articleFirst;
        store.add(p);
    };
    add(100, 0);
    add(110, 0);
    add(111, 111);
    add(112, 112);
    add(113, 112);  // continuation of 112
    add(120, 0);
    add(121, 121);
    store.finalize();
    return store;
}

void testNavigation() {
    const PageStore store = navStore();
    Navigator nav;
    CHECK_EQ(std::to_string(nav.currentPage()), "100");

    // Up/Down = next/previous populated page, wrapping.
    nav.up(store);   CHECK_EQ(std::to_string(nav.currentPage()), "110");
    nav.up(store);   CHECK_EQ(std::to_string(nav.currentPage()), "111");
    nav.down(store); CHECK_EQ(std::to_string(nav.currentPage()), "110");
    nav.goTo(100);
    nav.down(store); CHECK_EQ(std::to_string(nav.currentPage()), "121");  // wraps backwards
    nav.up(store);   CHECK_EQ(std::to_string(nav.currentPage()), "100");  // wraps forwards

    // Up/Down from a page that does not exist still lands on a real one.
    nav.goTo(150);
    nav.up(store);   CHECK_EQ(std::to_string(nav.currentPage()), "100");
    nav.goTo(150);
    nav.down(store); CHECK_EQ(std::to_string(nav.currentPage()), "121");

    // Left/Right = articles; continuation pages are skipped.
    nav.goTo(100);
    nav.right(store); CHECK_EQ(std::to_string(nav.currentPage()), "111");
    nav.right(store); CHECK_EQ(std::to_string(nav.currentPage()), "112");
    nav.right(store); CHECK_EQ(std::to_string(nav.currentPage()), "121");  // not 113
    nav.right(store); CHECK_EQ(std::to_string(nav.currentPage()), "111");  // wraps
    nav.goTo(113);
    nav.left(store);  CHECK_EQ(std::to_string(nav.currentPage()), "111");  // relative to article 112, not page 113
    nav.left(store);  CHECK_EQ(std::to_string(nav.currentPage()), "121");  // wraps
    nav.goTo(113);
    nav.right(store); CHECK_EQ(std::to_string(nav.currentPage()), "121");

    // Digit entry.
    nav.goTo(100);
    nav.digit(1, 0.0);
    CHECK(nav.entering());
    CHECK_EQ(nav.targetLabel(), "1--");
    nav.digit(2, 0.5);
    CHECK_EQ(nav.targetLabel(), "12-");
    CHECK_EQ(std::to_string(nav.currentPage()), "100");  // not jumped yet
    nav.digit(1, 1.0);
    CHECK(!nav.entering());
    CHECK_EQ(std::to_string(nav.currentPage()), "121");
    CHECK_EQ(nav.targetLabel(), "121");

    // A stalled entry is abandoned after the timeout, keeping the current page.
    nav.digit(2, 10.0);
    nav.update(10.0 + Navigator::kEntryTimeoutSeconds - 0.1);
    CHECK(nav.entering());
    nav.update(10.0 + Navigator::kEntryTimeoutSeconds + 0.1);
    CHECK(!nav.entering());
    CHECK_EQ(std::to_string(nav.currentPage()), "121");

    // Each digit restarts the timeout; arrows and cancel abandon an entry.
    nav.digit(1, 20.0);
    nav.digit(1, 22.5);
    nav.update(24.0);
    CHECK(nav.entering());
    nav.up(store);
    CHECK(!nav.entering());
    nav.digit(5, 30.0);
    CHECK(nav.cancelEntry());
    CHECK(!nav.cancelEntry());

    // Typing a page that isn't there is allowed (the view shows "not found").
    nav.digit(9, 40.0); nav.digit(9, 40.0); nav.digit(9, 40.0);
    CHECK_EQ(std::to_string(nav.currentPage()), "999");
    CHECK(store.find(999) == nullptr);
    CHECK(makeNotFoundPage(999).lines[10].find("PAGE NOT FOUND") != std::string::npos);

    // Empty store: nothing to navigate to, nothing crashes.
    PageStore empty;
    empty.finalize();
    nav.goTo(100);
    nav.up(empty); nav.down(empty); nav.left(empty); nav.right(empty);
    CHECK_EQ(std::to_string(nav.currentPage()), "100");
}

bool pageContains(const TeletextPage& page, const std::string& text) {
    for (const auto& line : page.lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void testConfig() {
    NewsConfig c;
    std::vector<std::string> warnings;
    const std::string text =
        "# comment\n"
        "refresh_minutes=1\n"
        "block_size=10\n"
        "max_article_pages=2\n"
        "source = 120 | tech | https://b.example/feed\n"
        "source = 110 | W\xC3\xB6" "rld news with a very long name | http://a.example/rss\n"
        "source = 115 | OVERLAP | https://c.example/x\n"
        "source = 50 | LOW | https://d.example/x\n"
        "source = 130 | BADURL | ftp://e.example/x\n"
        "source = 140 | MISSING FIELD\n"
        "nonsense line\n"
        "unknown=1\n";
    parseNewsConfig(text, c, &warnings);
    CHECK_EQ(std::to_string(c.sources.size()), "2");
    CHECK_EQ(std::to_string(c.refreshMinutes), std::to_string(kMinRefreshMinutes));  // clamped up
    CHECK_EQ(std::to_string(c.maxArticlePages), "2");
    if (c.sources.size() == 2) {
        CHECK_EQ(std::to_string(c.sources[0].startPage), "110");  // sorted by page
        CHECK_EQ(c.sources[0].category, "WOERLD NEWS WITH");        // ASCII, upper-case, cut to 16
        CHECK_EQ(c.sources[1].category, "TECH");
        CHECK_EQ(c.sources[1].url, "https://b.example/feed");
    }
    CHECK(warnings.size() >= 6);
    CHECK_EQ(c.sources[0].provider, "a.example");   // defaulted from the URL host
    CHECK_EQ(c.sources[0].logo, "WOERLD NEWS WITH");  // defaulted from the category  // refresh clamp, overlap, low page, bad url, missing field, nonsense, unknown

    // Explicit provider/logo, defaults, and weather_page.
    NewsConfig d;
    parseNewsConfig("weather_page=150\n"
                    "source=110 | WORLD | https://www.tagesschau.de/xml/rss2/ | tagesschau.de | tagesschau\n"
                    "source=120 | TECH | https://feeds.example.org:8080/a/b?x=1\n",
                    d, nullptr);
    CHECK_EQ(std::to_string(d.weatherPage), "150");
    CHECK_EQ(std::to_string(d.sources.size()), "2");
    if (d.sources.size() == 2) {
        CHECK_EQ(d.sources[0].provider, "tagesschau.de");
        CHECK_EQ(d.sources[0].logo, "TAGESSCHAU");     // upper-cased
        CHECK_EQ(d.sources[1].provider, "example.org");  // scheme, "feeds." prefix, port and path dropped
        CHECK_EQ(d.sources[1].logo, "TECH");
    }
    NewsConfig badWeather;
    std::vector<std::string> weatherWarnings;
    parseNewsConfig("weather_page=42\n", badWeather, &weatherWarnings);
    CHECK_EQ(std::to_string(badWeather.weatherPage), "0");
    CHECK(weatherWarnings.size() == 1);

    NewsConfig empty;
    parseNewsConfig("", empty, nullptr);
    CHECK(empty.sources.empty());
    NewsConfig none;
    CHECK(!loadNewsConfig("/nonexistent/path/news.cfg", none, nullptr));
}

FeedItem makeItem(int n, std::time_t published, const std::string& body) {
    FeedItem item;
    item.title = "Story number " + std::to_string(n);
    item.published = published;
    item.body = body;
    return item;
}

void testPageStore() {
    NewsConfig config;
    config.blockSize = 10;
    config.maxArticlePages = 3;
    config.sources = {mkSource(110, "WORLD", "http://x"), mkSource(120, "TECH", "http://y"),
                      mkSource(130, "EMPTY", "http://z"), mkSource(140, "DOWN", "http://w")};

    std::vector<SourceData> data(4);
    // WORLD: 30 short teasers, given oldest-first to prove sorting is by date.
    data[0].status = SourceStatus::Ok;
    data[0].fetchedAt = 1'800'000'000;
    for (int i = 0; i < 30; ++i) {
        data[0].items.push_back(makeItem(i, 1'700'000'000 + i * 3600, "A short teaser."));
    }
    // TECH: long articles, each wants the 3-page maximum.
    data[1].status = SourceStatus::Cached;
    data[1].fetchedAt = 1'800'000'000;
    for (int i = 0; i < 6; ++i) {
        data[1].items.push_back(makeItem(i, 1'700'000'000 - i * 3600, longBody(40)));
    }
    data[2].status = SourceStatus::Pending;
    data[3].status = SourceStatus::Failed;

    const auto store = buildPageStore(config, data);
    CHECK(store->find(100) != nullptr);

    // Blocks never spill past their budget, and pages stay inside their block.
    for (const auto& [number, page] : store->pages()) {
        const bool inBlock = number == 100 || (number >= 110 && number <= 149);
        CHECK(inBlock);
        if (page.isArticlePage()) {
            CHECK(page.articleFirstPage / 10 == number / 10);
        }
        for (const auto& line : page.lines) CHECK_EQ(std::to_string(line.size()), "40");
    }

    // WORLD: page 110 lists headlines, 111.. are articles, newest first, oldest evicted.
    const TeletextPage* headlines = store->find(110);
    CHECK(headlines != nullptr);
    if (headlines) {
        CHECK(headlines->lines[6].find("Story number 29") != std::string::npos);  // newest at the top of the list
        CHECK(!pageContains(*headlines, "Story number 0 "));
        CHECK_EQ(std::to_string(headlines->parentPage), "100");
    }
    CHECK(store->find(111) != nullptr && store->find(119) != nullptr);
    CHECK(store->find(111)->lines[kContentFirstRow].find("Story number 29") != std::string::npos);
    CHECK_EQ(std::to_string(store->find(111)->parentPage), "110");
    bool foundOldest = false;
    for (const auto& [number, page] : store->pages()) {
        if (number < 120 && page.lines[kContentFirstRow].find("Story number 0 ") != std::string::npos) foundOldest = true;
    }
    CHECK(!foundOldest);

    // TECH: long articles fill the block with multi-page chains; the last
    // article is cut to fit; nothing lands beyond page 129.
    CHECK(store->find(120) != nullptr);
    CHECK(store->find(130 - 1) != nullptr);  // block fully used
    int continuationPages = 0;
    for (int n = 121; n <= 129; ++n) {
        const TeletextPage* p = store->find(n);
        CHECK(p != nullptr);
        if (p && p->articleFirstPage != n) ++continuationPages;
    }
    CHECK(continuationPages > 0);
    CHECK(store->find(129)->nextPage == 0);  // chain ends inside the block

    // Empty/failed categories still get a first page explaining the state.
    CHECK(store->find(130) != nullptr && pageContains(*store->find(130), "fetched"));
    CHECK(store->find(140) != nullptr && pageContains(*store->find(140), "No news is available"));
    CHECK(store->find(131) == nullptr);

    // Index: lists all four categories with their pages, whatever their state.
    const TeletextPage* index = store->find(100);
    CHECK(index->lines[7].find("WORLD") == 2 && index->lines[7].find("110") == 37);
    CHECK(index->lines[10].find("DOWN") == 2 && index->lines[10].find("140") == 37);
    CHECK(index->fg[7][37] == Color::Blue && index->bg[7][37] == Color::White);  // blue page numbers on the white list
    CHECK(index->parentPage == 0);

    // Article navigation only visits article starts, never headline/message pages.
    Navigator nav;
    nav.goTo(100);
    for (int i = 0; i < 40; ++i) {
        nav.right(*store);
        const TeletextPage* p = store->find(nav.currentPage());
        CHECK(p != nullptr && p->articleFirstPage == p->number);
    }

    // Back: article -> headlines -> index -> (leave).
    nav.goTo(112);
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "110");
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "100");
    CHECK(!nav.back(*store));
    nav.goTo(777);  // nonexistent page: back goes to the index
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "100");
    nav.digit(4, 0.0);
    CHECK(nav.back(*store));  // abandons the entry first...
    CHECK(!nav.entering());
    CHECK_EQ(std::to_string(nav.currentPage()), "100");  // ...without moving

    // No sources at all: index still renders and says so.
    NewsConfig none;
    const auto bare = buildPageStore(none, {});
    CHECK(bare->find(100) != nullptr);
    CHECK(pageContains(*bare->find(100), "No news sources"));

    // A single tiny block (2 pages): 1 headline page + 1 article page still works.
    NewsConfig tiny;
    tiny.blockSize = 2;
    tiny.sources = {mkSource(110, "TINY", "http://t", "", "", 111)};
    std::vector<SourceData> tinyData(1);
    tinyData[0].status = SourceStatus::Ok;
    for (int i = 0; i < 5; ++i) tinyData[0].items.push_back(makeItem(i, 1'700'000'000 - i, longBody(30)));
    const auto tinyStore = buildPageStore(tiny, tinyData);
    CHECK(tinyStore->find(110) != nullptr && tinyStore->find(111) != nullptr && tinyStore->find(112) == nullptr);
}

// Hundreds mode (the tagesschau section): ranges, reserved pages, overviews.
void testHundreds() {
    // Config: ranges, hundreds mode, service name/logo, and rejections.
    NewsConfig c;
    std::vector<std::string> warnings;
    parseNewsConfig("overview_pages=hundreds\n"
                    "service_name=tagesschau.de\n"
                    "service_logo=tagesschau\n"
                    "source=110-149 | A | http://a/x\n"
                    "source=150-199 | B | http://b/x\n"
                    "source=510-699 | AUSLAND | http://c/x\n"
                    "source=200-260 | RESERVED START | http://d/x\n"      // a hundred page as the start
                    "source=140-160 | OVERLAP | http://e/x\n"             // overlaps A and B
                    "source=700-700 | TOO SMALL | http://f/x\n"           // needs at least 2 pages
                    "source=800-1200 | TOO BIG | http://g/x\n"            // beyond page 999
                    "source=910-x | GARBAGE | http://h/x\n"
                    "source=990 | DEFAULT SIZE | http://i/x\n",           // block_size (10) is capped at 999
                    c, &warnings);
    CHECK(c.hundredOverviews);
    CHECK_EQ(c.serviceName, "tagesschau.de");
    CHECK_EQ(c.serviceLogo, "TAGESSCHAU");
    CHECK_EQ(std::to_string(c.sources.size()), "4");
    if (c.sources.size() == 4) {
        CHECK_EQ(std::to_string(c.sources[0].endPage), "149");
        CHECK_EQ(std::to_string(c.sources[2].endPage), "699");
        CHECK_EQ(std::to_string(c.sources[3].startPage), "990");
        CHECK_EQ(std::to_string(c.sources[3].endPage), "999");
    }
    CHECK(warnings.size() >= 5);

    // Builder: A 110-149, B 150-199, AUSLAND 510-699 (spans the reserved 600), plus one section in 800s.
    NewsConfig config;
    config.hundredOverviews = true;
    config.serviceName = "tagesschau.de";
    config.serviceLogo = "TAGESSCHAU";
    config.sources = {mkSource(110, "MELD", "http://a", "tagesschau.de", "MELDUNGEN", 149),
                      mkSource(150, "START", "http://b", "tagesschau.de", "STARTSEITE", 199),
                      mkSource(510, "AUSLAND", "http://c", "tagesschau.de", "AUSLAND", 699),
                      mkSource(810, "WISSEN", "http://d", "tagesschau.de", "WISSEN", 849)};
    std::vector<SourceData> data(4);
    for (auto& d : data) {
        d.status = SourceStatus::Ok;
        d.fetchedAt = 1'800'000'000;
    }
    for (int i = 0; i < 20; ++i) data[0].items.push_back(makeItem(i, 1'700'000'000 + i * 100, "Short."));
    // START shares two headlines with MELD (must appear once in the overview) and has one newer story.
    data[1].items.push_back(makeItem(100, 1'700'009'000, "Newest, only in START."));
    data[1].items.push_back(makeItem(19, 1'700'000'000 + 19 * 100, "Short."));
    data[1].items.push_back(makeItem(18, 1'700'000'000 + 18 * 100, "Short."));
    // AUSLAND: 70 one-page stories -> more than fit below 600, so some land on 601..
    for (int i = 0; i < 90; ++i) data[2].items.push_back(makeItem(1000 + i, 1'600'000'000 + i * 100, "Short."));
    // AUSLAND also has a long story that will want three pages.
    data[2].items.push_back(makeItem(2000, 1'500'000'000, longBody(40)));
    // WISSEN: empty (failed): still gets a section page and the 800 overview.
    data[3].status = SourceStatus::Failed;

    const auto store = buildPageStore(config, data);

    // Hundred pages exist exactly where sources have pages: 100, 500, 600, 800.
    for (int h : {100, 500, 600, 800}) CHECK(store->find(h) != nullptr);
    for (int h : {200, 300, 400, 700, 900}) CHECK(store->find(h) == nullptr);

    // 100: banner text is the service logo; top articles: newest first, duplicates once, with page numbers.
    const TeletextPage* top = store->find(100);
    CHECK_EQ(top->service, "tagesschau.de");
    CHECK_EQ(std::to_string(top->parentPage), "0");
    CHECK(pageContains(*top, "MELD / START"));
    CHECK(pageContains(*top, "MELD 110  START 150"));
    CHECK(top->lines[6].find("Story number 100") != std::string::npos);  // newest first (only in START)
    int shortCount = 0;
    for (const auto& line : top->lines) {
        if (line.find("Short.") != std::string::npos) ++shortCount;
    }
    CHECK(shortCount == 0);  // bodies are not shown, only headlines
    int storyLines = 0;
    for (int r = 6; r < 18; ++r) {
        if (top->fg[static_cast<size_t>(r)][38] == Color::Blue && top->lines[static_cast<size_t>(r)][38] != ' ') ++storyLines;
    }
    CHECK(storyLines == 6);  // a small overview: six stories

    // Every headline on the overview links to a real article page whose title matches.
    for (int r = 6; r < 18; ++r) {
        const std::string& line = top->lines[static_cast<size_t>(r)];
        if (top->fg[static_cast<size_t>(r)][38] != Color::Blue || line[38] == ' ') continue;
        const int page = std::atoi(line.substr(37, 3).c_str());
        const TeletextPage* article = store->find(page);
        CHECK(article != nullptr && article->articleFirstPage == page);
    }
    // ...and the two duplicate stories appear only once.
    int dupes = 0;
    for (const auto& line : top->lines) {
        if (line.find("Story number 19") != std::string::npos) ++dupes;
    }
    CHECK(dupes <= 1);

    // The reserved page 600 is never used by an article or section page, and
    // no article's pages straddle it.
    CHECK(store->find(600)->articleFirstPage == 0);
    CHECK(store->find(600)->category == "OVERVIEW");
    bool anyAfter600 = false;
    for (const auto& [number, page] : store->pages()) {
        if (page.isArticlePage()) {
            const int first = page.articleFirstPage;
            CHECK((first < 600) == (number < 600));   // an article's pages are all on one side of 600
            CHECK(number % 100 != 0);
            anyAfter600 |= number > 600;
        }
    }
    CHECK(anyAfter600);
    // 500 and 600 both list AUSLAND stories located in their own range.
    CHECK(pageContains(*store->find(500), "AUSLAND 510"));
    CHECK(pageContains(*store->find(600), "AUSLAND 510"));
    for (int h : {500, 600}) {
        int found = 0;
        for (int r = 6; r < 18; ++r) {
            const std::string& line = store->find(h)->lines[static_cast<size_t>(r)];
            if (line.size() == 40 && line[38] != ' ' && std::isdigit(static_cast<unsigned char>(line[38]))) {
                const int page = std::atoi(line.substr(37, 3).c_str());
                CHECK(page >= h && page < h + 100);
                ++found;
            }
        }
        CHECK(found > 0);
    }

    // Failed section: its section page and the 800 overview still exist and say so.
    CHECK(store->find(810) != nullptr && pageContains(*store->find(810), "No news is available"));
    CHECK(pageContains(*store->find(800), "No articles available yet"));
    CHECK(pageContains(*store->find(800), "WISSEN 810"));

    // No weather page configured: the blue button's label is blank on every page, its rectangle stays.
    for (const auto& [number, page] : store->pages()) {
        CHECK_EQ(page.lines[kFooterRow].substr(30), "          ");
        CHECK(page.bg[kFooterRow][35] == Color::Blue);
        CHECK_EQ(page.lines[kFooterRow].substr(20, 10), "   News   ");
    }

    // Back: article -> section page -> hundred page -> 100 -> leave.
    Navigator nav;
    int article = 0;
    for (const auto& [number, page] : store->pages()) {
        if (page.isArticlePage() && number > 500 && number < 600) {
            article = number;
            break;
        }
    }
    nav.goTo(article);
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "510");
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "500");
    CHECK(nav.back(*store)); CHECK_EQ(std::to_string(nav.currentPage()), "100");
    CHECK(!nav.back(*store));

    // Article navigation never lands on a hundred or section page; up/down visits every page in order.
    nav.goTo(100);
    for (int i = 0; i < 30; ++i) {
        nav.right(*store);
        const TeletextPage* p = store->find(nav.currentPage());
        CHECK(p != nullptr && p->isArticlePage());
    }

    // With a weather page configured the label stays.
    config.weatherPage = 810;
    const auto withWeather = buildPageStore(config, data);
    CHECK(withWeather->find(100)->lines[kFooterRow].substr(30).find("Weather") != std::string::npos);

    // No sources in hundreds mode: page 100 still renders.
    NewsConfig empty;
    empty.hundredOverviews = true;
    CHECK(buildPageStore(empty, {})->find(100) != nullptr);
}

// The look of a page: fastext bar, header, banner/sixels.
void testVisuals() {
    // Fastext bar: four 10-column filled rectangles with centered labels.
    TeletextPage page;
    const Color fills[4] = {Color::Red, Color::Green, Color::Yellow, Color::Blue};
    for (int b = 0; b < 4; ++b) {
        for (int c = b * 10; c < b * 10 + 10; ++c) {
            CHECK(page.bg[kFooterRow][static_cast<size_t>(c)] == fills[b]);
        }
    }
    CHECK_EQ(page.lines[kFooterRow], "    -     " "    +     " "   News   " " Weather  ");
    CHECK(page.fg[kFooterRow][35] == Color::White);   // blue button: white text
    CHECK(page.fg[kFooterRow][5] == Color::Black);

    // Header: 0-2 current page (green), 4-6 target (white), 8.. provider (cyan), date and time.
    page.number = 111;
    page.service = "tagesschau.de";
    composeHeader(page, "1--", "20.09.", "23:42:07");
    CHECK_EQ(page.lines[kHeaderRow], "111 1-- tagesschau.de    20.09. 23:42:07");
    CHECK(page.fg[kHeaderRow][0] == Color::Green && page.fg[kHeaderRow][2] == Color::Green);
    CHECK(page.fg[kHeaderRow][4] == Color::White);
    CHECK(page.fg[kHeaderRow][8] == Color::Cyan && page.fg[kHeaderRow][20] == Color::Cyan);
    CHECK(page.fg[kHeaderRow][25] == Color::White && page.fg[kHeaderRow][39] == Color::White);
    CHECK(page.bg[kHeaderRow][0] == Color::Black);
    page.service = "a-very-long-provider-name.example";
    composeHeader(page, "111", "01.02.", "03:04:05");
    CHECK_EQ(std::to_string(page.lines[kHeaderRow].size()), "40");
    CHECK_EQ(page.lines[kHeaderRow].substr(25), "01.02. 03:04:05");  // long names never overwrite the clock

    // Banner: blue block, sixel letters in white, centered, only in its rows.
    TeletextPage banner;
    drawBanner(banner, "TAGESSCHAU");
    int sixels = 0;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const bool inBanner = row >= kBannerFirstRow && row < kBannerFirstRow + kBannerRows;
            const auto r = static_cast<size_t>(row);
            const auto c = static_cast<size_t>(col);
            if (banner.sixel[r][c] & kSixelFlag) {
                ++sixels;
                CHECK(inBanner);
                CHECK(banner.fg[r][c] == Color::White && banner.bg[r][c] == Color::Blue);
                CHECK((banner.sixel[r][c] & 0x3F) != 0);
            }
            if (inBanner) CHECK(banner.bg[r][c] == Color::Blue);
            if (row >= kBannerFirstRow + kBannerRows && row < kFooterRow) CHECK(banner.bg[r][c] != Color::Blue);
        }
    }
    CHECK(sixels > 20);
    // Centered: the outermost lit cells sit at (nearly) equal distances from the edges.
    int first = kCols, last = -1;
    for (int col = 0; col < kCols; ++col) {
        for (int row = kBannerFirstRow; row < kBannerFirstRow + kBannerRows; ++row) {
            if (banner.sixel[static_cast<size_t>(row)][static_cast<size_t>(col)] & kSixelFlag) {
                first = std::min(first, col);
                last = std::max(last, col);
            }
        }
    }
    CHECK(std::abs(first - (kCols - 1 - last)) <= 1);

    // Short logos are drawn wider than long ones; over-long text is cut, not overflowed.
    TeletextPage shortLogo, longLogo;
    drawBanner(shortLogo, "NPR");
    drawBanner(longLogo, std::string(40, 'W'));
    auto litColumns = [](const TeletextPage& p) {
        int n = 0;
        for (int col = 0; col < kCols; ++col) {
            for (int row = kBannerFirstRow; row < kBannerFirstRow + kBannerRows; ++row) {
                if (p.sixel[static_cast<size_t>(row)][static_cast<size_t>(col)] & kSixelFlag) {
                    ++n;
                    break;
                }
            }
        }
        return n;
    };
    CHECK(litColumns(shortLogo) > 0);
    CHECK(litColumns(longLogo) <= kCols);
    drawBanner(shortLogo, "");  // empty text: just the blue block
    drawBanner(shortLogo, "\x01\xC3\xA4?");  // unsupported characters: blanks, no crash

    // Provider index page from the builder: banner, white list block, blue numbers, service in header.
    NewsConfig config;
    config.sources = {mkSource(110, "DEUTSCH", "http://x", "tagesschau.de", "TAGESSCHAU")};
    std::vector<SourceData> data(1);
    data[0].status = SourceStatus::Ok;
    data[0].items = {makeItem(1, 1'700'000'000, "Text.")};
    const auto store = buildPageStore(config, data);
    const TeletextPage* index = store->find(110);
    CHECK(index != nullptr);
    if (index) {
        CHECK_EQ(index->service, "tagesschau.de");
        CHECK(index->bg[2][10] == Color::Blue);
        CHECK(index->bg[6][5] == Color::White && index->fg[6][5] == Color::Black);
        CHECK(index->fg[6][38] == Color::Blue);           // page number of the first headline
        CHECK(index->fg[kInfoRow][2] == Color::Cyan);
        CHECK_EQ(store->find(111)->service, "tagesschau.de");
        CHECK(store->find(100)->bg[2][10] == Color::Blue);   // page 100 has its own banner
        CHECK_EQ(store->find(100)->service, "PVM NEWS");
    }
    CHECK_EQ(makeNotFoundPage(500).service, "PVM NEWS");
}

// The service must boot with pages from the disk cache and no network at
// all (the source URLs below are unroutable and start() is never called).
void testCache() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pvm_teletext_selftest_cache";
    fs::remove_all(dir);
    fs::create_directories(dir);

    NewsConfig config;
    config.sources = {mkSource(110, "GOOD", "http://192.0.2.1/good.xml"),
                      mkSource(120, "FOREIGN", "http://192.0.2.1/foreign.xml"),
                      mkSource(130, "CORRUPT", "http://192.0.2.1/corrupt.xml"),
                      mkSource(140, "NONE", "http://192.0.2.1/none.xml")};

    const std::string rss =
        "<rss><channel><title>Cached Feed</title>"
        "<item><title>Cached headline</title><pubDate>Sun, 20 Sep 2026 10:00:00 GMT</pubDate>"
        "<description>Cached body text.</description></item></channel></rss>";
    auto write = [&](const std::string& url, const std::string& header, const std::string& body) {
        std::ofstream(dir / NewsService::cacheFileName(url), std::ios::binary) << header << '\n' << body;
    };
    write("http://192.0.2.1/good.xml", "PVMNEWS1 1789000000 http://192.0.2.1/good.xml", rss);
    write("http://192.0.2.1/foreign.xml", "PVMNEWS1 1789000000 http://someone.else/other.xml", rss);  // URL mismatch
    write("http://192.0.2.1/corrupt.xml", "PVMNEWS1 1789000000 http://192.0.2.1/corrupt.xml", "<rss><channel><item>");

    NewsService service(config, dir.string());
    const auto store = service.snapshot();
    CHECK(store->find(100) != nullptr);
    CHECK(store->find(111) != nullptr);
    if (store->find(111)) {
        CHECK(pageContains(*store->find(111), "Cached headline"));
        CHECK(pageContains(*store->find(111), "Cached body text."));
        CHECK(pageContains(*store->find(111), "Cached Feed"));  // dateline carries the feed title
    }
    CHECK(pageContains(*store->find(110), "Updated "));
    CHECK(!pageContains(*store->find(110), "failed"));        // cached, not failed
    CHECK(store->find(121) == nullptr);                       // foreign cache file ignored
    CHECK(pageContains(*store->find(120), "fetched"));        // ...category shows the waiting message
    CHECK(store->find(131) == nullptr);                       // corrupt cache file ignored
    CHECK(store->find(141) == nullptr);

    // A refresh that fails (nothing listens there) must keep the cached pages.
    // Not exercised here: it needs a network stack; covered by the manual
    // offline-boot check in the phase 5 verification.
    fs::remove_all(dir);
}

}  // namespace

int main() {
    testHtmlStrip();
    testAscii();
    testDates();
    testFeedParser();
    testWrap();
    testPagination();
    testNavigation();
    testConfig();
    testPageStore();
    testCache();
    testVisuals();
    testHundreds();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("teletext_selftest: all checks passed\n");
    return 0;
}
