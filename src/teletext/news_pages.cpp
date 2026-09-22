#include "news_pages.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "article_pages.h"
#include "html_strip.h"
#include "title_art.h"

namespace teletext {

namespace {

// Layout of a provider's index page (its first headline page):
//   row 0      header (drawn live)
//   rows 1-4   title-art banner
//   row 5      category label
//   rows 6-21  headlines, black on white, page numbers in blue
//   row 22     info line, row 23 fastext bar
// Continuation headline pages have no banner and start their list higher.
constexpr int kCategoryRow = kBannerFirstRow + kBannerRows;                 // 5
constexpr int kFirstPageListRow = kCategoryRow + 1;                         // 6
constexpr int kOtherPageListRow = kContentFirstRow + 1;                     // 3
constexpr int kListLastRow = kInfoRow - 1;                                  // 21
constexpr int kFirstPageListRows = kListLastRow - kFirstPageListRow + 1;    // 16
constexpr int kOtherPageListRows = kListLastRow - kOtherPageListRow + 1;    // 19

// Headline rows: text wraps to this width; the page number sits in the last
// three columns, joined to the text by a dotted leader. Two rows per
// headline keeps a 10-page block to about eight headlines on one page;
// longer headlines are cut with "...".
constexpr int kListIndent = 2;
constexpr int kHeadlineTextWidth = 33;  // kListIndent + this leaves the last columns for the leader and number
constexpr size_t kMaxHeadlineRows = 2;

std::string shorten(const std::string& s, size_t maxLen) {
    return s.size() <= maxLen ? s : s.substr(0, maxLen - 3) + "...";
}

// "text ........ 123": text on the left, `number` right-aligned, dotted between.
std::string dotted(const std::string& text, int number) {
    std::string line = std::string(kListIndent, ' ') + text.substr(0, static_cast<size_t>(kCols - 4 - kListIndent));
    const int gap = kCols - 3 - static_cast<int>(line.size());
    line += gap >= 2 ? " " + std::string(static_cast<size_t>(gap - 1), '.') : std::string(static_cast<size_t>(std::max(gap, 0)), ' ');
    char num[8];
    std::snprintf(num, sizeof(num), "%3d", number);
    return line + num;
}

// A row of a black-on-white list whose last three columns are a blue page
// number when `linkPage` is set (also registered as a selectable RowLink,
// so Up/Down can land on it and Enter jump there).
void putListRow(TeletextPage& page, int row, const std::string& text, int linkPage = 0) {
    page.setLine(row, text, Color::Black, Color::White);
    if (linkPage > 0) {
        page.paint(row, kCols - 3, 3, Color::Blue, Color::White);
        page.addPageLink(row, linkPage);
    }
}

struct Headline {
    std::string title;
    int page = 0;
};

// One row of a headline list; `linkPage` (nonzero on the last row of each
// headline) is the page it links to, drawn in blue.
struct ListLine {
    std::string text;
    int linkPage = 0;
};

std::vector<ListLine> headlineRows(const Headline& h) {
    std::vector<std::string> rows = wrapText(h.title, kHeadlineTextWidth);
    if (rows.empty()) {
        rows.push_back("(untitled)");
    }
    if (rows.size() > kMaxHeadlineRows) {
        rows.resize(kMaxHeadlineRows);
        rows.back() = shorten(rows.back() + "...", kHeadlineTextWidth);
    }
    std::vector<ListLine> lines;
    for (size_t i = 0; i < rows.size(); ++i) {
        const bool last = i + 1 == rows.size();
        lines.push_back({last ? dotted(rows[i], h.page) : std::string(kListIndent, ' ') + rows[i],
                         last ? h.page : 0});
    }
    return lines;
}

// Splits headlines across pages without ever splitting one headline; the
// first page has less room (banner). Always returns at least one page.
std::vector<std::vector<ListLine>> layoutHeadlines(const std::vector<Headline>& headlines) {
    std::vector<std::vector<ListLine>> pages(1);
    for (const Headline& h : headlines) {
        const std::vector<ListLine> entry = headlineRows(h);
        const int capacity = pages.size() == 1 ? kFirstPageListRows : kOtherPageListRows;
        if (static_cast<int>(pages.back().size() + entry.size()) > capacity) {
            pages.emplace_back();
        }
        pages.back().insert(pages.back().end(), entry.begin(), entry.end());
    }
    return pages;
}

std::string statusText(const SourceData& d) {
    switch (d.status) {
        case SourceStatus::Pending:
            return "Waiting for first update...";
        case SourceStatus::Failed:
            return "Source not responding - retrying";
        case SourceStatus::Stale:
            return "Updated " + formatLocalTime(d.fetchedAt, "%d %b %H:%M") + " (update failed)";
        case SourceStatus::Ok:
        case SourceStatus::Cached:
            return "Updated " + formatLocalTime(d.fetchedAt, "%d %b %H:%M");
    }
    return "";
}

// The shared top of a provider's index page: header service name, banner,
// category label.
void beginProviderPage(TeletextPage& page, const NewsSource& src) {
    page.number = src.startPage;
    page.category = src.category;
    page.service = src.provider;
    page.parentPage = kIndexPage;  // buildCategory() overrides this in hundreds mode
    drawBanner(page, src.logo);
    page.putText(kCategoryRow, 1, src.category, Color::Yellow);
}

// In hundreds mode the pages 100, 200, ... 900 belong to the overview pages.
bool isReserved(const NewsConfig& config, int page) {
    return config.hundredOverviews && page % 100 == 0 && page >= 100 && page <= 900;
}

// The first page >= `page` that is not reserved.
int skipReserved(const NewsConfig& config, int page) {
    return isReserved(config, page) ? page + 1 : page;
}

struct PlacedArticle {
    const FeedItem* item = nullptr;
    ArticleLayout layout;
    int firstPage = 0;
};

std::string datelineFor(const FeedItem& item, const SourceData& data) {
    std::string source = toTeletextAscii(data.feedTitle);
    std::string dateline = item.published ? formatLocalTime(item.published, "%a %d %b %H:%M") : "";
    if (!source.empty()) {
        const size_t room = dateline.empty() ? kCols : kCols - dateline.size() - 3;
        source = shorten(source, room);
        dateline = dateline.empty() ? source : dateline + " - " + source;
    }
    return dateline;
}

// Places articles newest-first into [start + headlinePages, end], each getting
// 1..maxArticlePages pages; the last one is cut to whatever pages remain.
std::vector<PlacedArticle> placeArticles(const std::vector<const FeedItem*>& items, const SourceData& data,
                                         const NewsConfig& config, int start, int end, int headlinePages) {
    std::vector<PlacedArticle> placed;
    int cursor = skipReserved(config, start + headlinePages);
    for (const FeedItem* item : items) {
        std::string body = toTeletextAscii(item->body);
        if (body.empty()) {
            body = "No article text is available from this source.";
        }
        PlacedArticle p;
        p.item = item;
        // An article's pages are consecutive numbers, so one that would run
        // over a reserved overview page starts after it instead.
        for (bool settled = false; !settled;) {
            const int remaining = end - cursor + 1;
            if (remaining <= 0) {
                return placed;  // block full: everything older than this is evicted
            }
            p.layout = layoutArticle(toTeletextAscii(item->title), datelineFor(*item, data), body,
                                     std::min(config.maxArticlePages, remaining));
            const int last = cursor + static_cast<int>(p.layout.pages.size()) - 1;
            settled = true;
            for (int page = cursor; page <= last && settled; ++page) {
                if (isReserved(config, page)) {
                    cursor = page + 1;
                    settled = false;
                }
            }
        }
        p.firstPage = cursor;
        cursor += static_cast<int>(p.layout.pages.size());
        placed.push_back(std::move(p));
    }
    return placed;
}

std::vector<Headline> headlinesOf(const std::vector<PlacedArticle>& placed) {
    std::vector<Headline> headlines;
    for (const PlacedArticle& p : placed) {
        headlines.push_back({toTeletextAscii(p.item->title), p.firstPage});
    }
    return headlines;
}

// An article that got pages, for the hundred-page overviews.
struct ArticleRef {
    std::string title;
    std::time_t published = 0;
    int page = 0;
};

// Fills one category block with pages and adds them to `store`. Returns the
// articles that were given pages, newest first.
std::vector<ArticleRef> buildCategory(const NewsSource& src, const SourceData& data, const NewsConfig& config,
                                      PageStore& store) {
    const int start = src.startPage;
    const int end = src.endPage;
    std::vector<ArticleRef> refs;

    if (data.items.empty()) {
        TeletextPage page;
        beginProviderPage(page, src);
        if (data.status == SourceStatus::Pending) {
            page.setLine(8, "  News for this category is being", Color::White);
            page.setLine(9, "  fetched. Please wait a moment,", Color::White);
            page.setLine(10, "  then try this page again.", Color::White);
        } else {
            page.setLine(8, "  No news is available for this", Color::White);
            page.setLine(9, "  category right now. The source", Color::White);
            page.setLine(10, "  did not respond; retrying.", Color::White);
        }
        page.setLine(kInfoRow, statusText(data), Color::Cyan);
        store.add(std::move(page));
        return refs;
    }

    // Newest first; undated items (published == 0) sort after dated ones and
    // otherwise keep feed order. Drop repeated headlines.
    std::vector<const FeedItem*> items;
    std::set<std::string> seenTitles;
    for (const FeedItem& item : data.items) {
        if (seenTitles.insert(item.title).second) {
            items.push_back(&item);
        }
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const FeedItem* a, const FeedItem* b) { return a->published > b->published; });

    // Headline pages come first in the block, so how many articles fit
    // depends on how many headline pages exist. Try a few sizes; for each,
    // drop the oldest articles until their headlines fit on that many pages,
    // and keep the size that leaves the most articles (fewest headline pages
    // on ties).
    std::vector<PlacedArticle> placed;
    std::vector<std::vector<ListLine>> rowsPerPage;
    // Big blocks (tagesschau: 40-90 pages) need several headline pages.
    const int maxHeadlinePages = std::min(8, end - start);
    for (int pages = 1; pages <= maxHeadlinePages; ++pages) {
        std::vector<PlacedArticle> candidate = placeArticles(items, data, config, start, end, pages);
        std::vector<std::vector<ListLine>> rows = layoutHeadlines(headlinesOf(candidate));
        while (static_cast<int>(rows.size()) > pages && !candidate.empty()) {
            candidate.pop_back();
            rows = layoutHeadlines(headlinesOf(candidate));
        }
        if (pages == 1 || candidate.size() > placed.size()) {
            placed = std::move(candidate);
            rowsPerPage = std::move(rows);
        }
    }

    for (const PlacedArticle& p : placed) {
        for (TeletextPage& page : makeArticlePages(p.layout, src.category, src.provider, p.firstPage)) {
            page.parentPage = start;
            store.add(std::move(page));
        }
        refs.push_back({toTeletextAscii(p.item->title), p.item->published, p.firstPage});
    }

    const int shownPages = static_cast<int>(rowsPerPage.size());
    for (int i = 0; i < shownPages; ++i) {
        TeletextPage page;
        if (i == 0) {
            beginProviderPage(page, src);
        } else {
            page.number = start + i;
            page.category = src.category;
            page.service = src.provider;
            page.parentPage = kIndexPage;
            page.setLine(kContentFirstRow, src.category + " HEADLINES " + std::to_string(i + 1) + "/" +
                                               std::to_string(shownPages),
                         Color::Yellow);
        }
        page.number = start + i;
        // Where "back" leads: the hundred page in overview mode, else the index.
        page.parentPage = config.hundredOverviews ? (start / 100) * 100 : kIndexPage;
        page.prevPage = i > 0 ? page.number - 1 : 0;
        page.nextPage = i + 1 < shownPages ? page.number + 1 : 0;

        const int listRow = i == 0 ? kFirstPageListRow : kOtherPageListRow;
        const int listRows = i == 0 ? kFirstPageListRows : kOtherPageListRows;
        for (int r = 0; r < listRows; ++r) {
            putListRow(page, listRow + r, "");  // the white block, even where it is empty
        }
        const auto& rows = rowsPerPage[static_cast<size_t>(i)];
        for (size_t r = 0; r < rows.size(); ++r) {
            putListRow(page, listRow + static_cast<int>(r), rows[r].text, rows[r].linkPage);
        }
        std::string info = i == 0 ? statusText(data) : "";
        if (page.nextPage) {
            info += (info.empty() ? " " : "   ") + std::string("MORE ") + std::to_string(page.nextPage);
        }
        page.setLine(kInfoRow, info, Color::Cyan);
        store.add(std::move(page));
    }
    return refs;
}

TeletextPage buildIndex(const NewsConfig& config, const std::vector<SourceData>& data) {
    TeletextPage page;
    page.number = kIndexPage;
    page.category = "INDEX";
    page.service = config.serviceName;
    drawBanner(page, config.serviceLogo);

    const int listRows = std::max<int>(3, static_cast<int>(config.sources.size()) + 2);
    for (int r = 0; r < listRows; ++r) {
        putListRow(page, kFirstPageListRow + r, "", 0);
    }
    if (config.sources.empty()) {
        putListRow(page, kFirstPageListRow + 1, "  No news sources are configured.");
        putListRow(page, kFirstPageListRow + 3, "  Add 'source=' lines to news.cfg");
        putListRow(page, kFirstPageListRow + 4, "  next to the program.");
    }

    time_t latest = 0;
    int failing = 0;
    int waiting = 0;
    int withData = 0;
    for (size_t i = 0; i < config.sources.size(); ++i) {
        putListRow(page, kFirstPageListRow + 1 + static_cast<int>(i),
                   dotted(config.sources[i].category, config.sources[i].startPage),
                   config.sources[i].startPage);
        const SourceData empty;
        const SourceData& d = i < data.size() ? data[i] : empty;
        latest = std::max(latest, d.fetchedAt);
        failing += (d.status == SourceStatus::Failed || d.status == SourceStatus::Stale) ? 1 : 0;
        waiting += d.status == SourceStatus::Pending ? 1 : 0;
        withData += d.items.empty() ? 0 : 1;
    }

    std::string status;
    if (config.sources.empty()) {
        status = "";
    } else if (withData == 0 && waiting > 0) {
        status = "Waiting for first update...";
    } else if (withData == 0) {
        status = "Offline - no news available yet";
    } else {
        status = "Updated " + formatLocalTime(latest, "%d %b %H:%M");
        if (failing > 0) {
            status += " (" + std::to_string(failing) + " source" + (failing > 1 ? "s" : "") + " offline)";
        }
    }
    page.setLine(kInfoRow - 3, "Type a page number, or use:", Color::Yellow);
    page.setLine(kInfoRow - 2, "UP/DOWN select  LEFT/RIGHT page", Color::Yellow);
    page.setLine(kInfoRow, status, Color::Cyan);
    return page;
}

// Overview of one hundred-page range (hundreds mode): the newest articles
// whose first page lies in [hundred, hundred + 99], with their page numbers,
// and the sections (sources) that have pages there.
TeletextPage buildHundredPage(int hundred, const std::vector<size_t>& members, const NewsConfig& config,
                              const std::vector<SourceData>& data, const std::vector<ArticleRef>& allRefs) {
    constexpr size_t kTopArticles = 6;
    const NewsSource& first = config.sources[members.front()];

    TeletextPage page;
    page.number = hundred;
    page.category = "OVERVIEW";
    page.service = hundred == 100 ? config.serviceName : first.provider;
    page.parentPage = hundred == 100 ? 0 : kIndexPage;
    drawBanner(page, hundred == 100 ? config.serviceLogo : first.logo);

    // Section names for the label row: "MELDUNGEN / STARTSEITE".
    std::string label;
    for (size_t idx : members) {
        label += (label.empty() ? "" : " / ") + config.sources[idx].category;
    }
    page.putText(kCategoryRow, 1, shorten(label, kCols - 2), Color::Yellow);

    // Top articles located in this range, newest first (undated last, then by
    // page), each headline only once even if several feeds carry it.
    std::vector<ArticleRef> top;
    for (const ArticleRef& ref : allRefs) {
        if (ref.page >= hundred && ref.page < hundred + 100) {
            top.push_back(ref);
        }
    }
    if (top.empty()) {
        // A section that spans several hundreds (e.g. 510-699) may have used
        // up all its articles before reaching this one; show its newest
        // rather than an empty page.
        for (const ArticleRef& ref : allRefs) {
            for (size_t idx : members) {
                if (ref.page >= config.sources[idx].startPage && ref.page <= config.sources[idx].endPage) {
                    top.push_back(ref);
                    break;
                }
            }
        }
    }
    std::stable_sort(top.begin(), top.end(), [](const ArticleRef& a, const ArticleRef& b) {
        return a.published != b.published ? a.published > b.published : a.page < b.page;
    });
    std::set<std::string> seen;
    std::vector<ListLine> rows;
    size_t shown = 0;
    for (const ArticleRef& ref : top) {
        if (shown == kTopArticles) {
            break;
        }
        if (!seen.insert(ref.title).second) {
            continue;
        }
        const std::vector<ListLine> entry = headlineRows({ref.title, ref.page});
        rows.insert(rows.end(), entry.begin(), entry.end());
        ++shown;
    }

    const int listRows = static_cast<int>(kTopArticles * kMaxHeadlineRows);
    for (int r = 0; r < listRows; ++r) {
        putListRow(page, kFirstPageListRow + r, "");
    }
    if (rows.empty()) {
        putListRow(page, kFirstPageListRow + 2, std::string(kListIndent, ' ') + "No articles available yet.");
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        putListRow(page, kFirstPageListRow + static_cast<int>(r), rows[r].text, rows[r].linkPage);
    }

    // "Sections" below the list: "INLAND 210  INNENPOLITIK 260", wrapped.
    const int sectionsFirstRow = kFirstPageListRow + listRows + 1;
    std::string line;
    int row = sectionsFirstRow;
    for (size_t idx : members) {
        const std::string entry = config.sources[idx].category + " " + std::to_string(config.sources[idx].startPage);
        if (!line.empty() && line.size() + 2 + entry.size() > static_cast<size_t>(kCols)) {
            page.setLine(row++, line, Color::Yellow);
            line.clear();
        }
        line += (line.empty() ? "" : "  ") + entry;
    }
    if (!line.empty() && row < kInfoRow) {
        page.setLine(row, line, Color::Yellow);
    }

    time_t latest = 0;
    int withData = 0;
    int waiting = 0;
    for (size_t idx : members) {
        const SourceData empty;
        const SourceData& d = idx < data.size() ? data[idx] : empty;
        latest = std::max(latest, d.fetchedAt);
        withData += d.items.empty() ? 0 : 1;
        waiting += d.status == SourceStatus::Pending ? 1 : 0;
    }
    page.setLine(kInfoRow,
                 withData > 0 ? "Updated " + formatLocalTime(latest, "%d %b %H:%M")
                              : (waiting > 0 ? "Waiting for first update..." : "Offline - no news available yet"),
                 Color::Cyan);
    return page;
}

}  // namespace

std::shared_ptr<const PageStore> buildPageStore(const NewsConfig& config, const std::vector<SourceData>& data) {
    auto store = std::make_shared<PageStore>();
    std::vector<ArticleRef> allRefs;
    for (size_t i = 0; i < config.sources.size(); ++i) {
        const SourceData empty;
        const std::vector<ArticleRef> refs =
            buildCategory(config.sources[i], i < data.size() ? data[i] : empty, config, *store);
        allRefs.insert(allRefs.end(), refs.begin(), refs.end());
    }

    if (config.hundredOverviews) {
        for (int hundred = 100; hundred <= 900; hundred += 100) {
            std::vector<size_t> members;
            for (size_t i = 0; i < config.sources.size(); ++i) {
                if (config.sources[i].startPage <= hundred + 99 && config.sources[i].endPage >= hundred) {
                    members.push_back(i);
                }
            }
            if (!members.empty()) {
                store->add(buildHundredPage(hundred, members, config, data, allRefs));
            }
        }
        if (!store->find(kIndexPage)) {  // no sources at all: page 100 still exists
            store->add(buildIndex(config, data));
        }
    } else {
        store->add(buildIndex(config, data));
    }

    store->finalize();
    return store;
}

}  // namespace teletext
