#include "mvw_pages.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "html_strip.h"
#include "title_art.h"

namespace teletext {

namespace {

// Layout, mirroring news_pages.cpp's provider-index-page shape exactly (see
// its own comment): banner rows 1-4, a category/label row at 5, the list
// from row 6 (first page) or row 3 (continuation pages) through row 21, the
// info line at 22, the fastext bar at 23.
constexpr int kCategoryRow = kBannerFirstRow + kBannerRows;               // 5
constexpr int kFirstPageListRow = kCategoryRow + 1;                       // 6
constexpr int kOtherPageListRow = kContentFirstRow + 1;                   // 3
constexpr int kListLastRow = kInfoRow - 1;                                // 21
constexpr int kFirstPageListRows = kListLastRow - kFirstPageListRow + 1;  // 16
constexpr int kOtherPageListRows = kListLastRow - kOtherPageListRow + 1;  // 19

constexpr int kListIndent = 2;
constexpr int kSuffixWidth = 7;  // right-aligned "MM:SS"/"H:MM:SS" or a 3-digit page number

std::string shorten(const std::string& s, size_t maxLen) {
    return s.size() <= maxLen ? s : s.substr(0, maxLen - 3) + "...";
}

std::string upperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

void putListRow(TeletextPage& page, int row, const std::string& text) {
    page.setLine(row, text, Color::Black, Color::White);
}

// "  text .......... 123": text left, `number` right-aligned in the last 3
// columns, dotted between. Also registers the row as a page link.
std::string dottedPage(const std::string& text, int number) {
    std::string line = std::string(kListIndent, ' ') + text.substr(0, static_cast<size_t>(kCols - 4 - kListIndent));
    const int gap = kCols - 3 - static_cast<int>(line.size());
    line += gap >= 2 ? " " + std::string(static_cast<size_t>(gap - 1), '.')
                     : std::string(static_cast<size_t>(std::max(gap, 0)), ' ');
    char num[8];
    std::snprintf(num, sizeof(num), "%3d", number);
    return line + num;
}

void putPageLinkRow(TeletextPage& page, int row, const std::string& text, int gotoPage) {
    putListRow(page, row, dottedPage(text, gotoPage));
    page.paint(row, kCols - 3, 3, Color::Blue, Color::White);
    page.addPageLink(row, gotoPage);
}

std::string formatDuration(int seconds) {
    if (seconds <= 0) {
        return "";
    }
    const int h = seconds / 3600;
    const int m = (seconds % 3600) / 60;
    const int s = seconds % 60;
    char buf[16];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

std::string formatBadge(int season, int episode) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "S%02dE%02d", season, episode);
    return buf;
}

// "  S01E07 Folge 7 .......... 44:04" -- badge + title left, duration
// right-aligned, dotted between; long titles are truncated with "..."
// rather than wrapped to a second row (kept simple: cleaned-up episode
// titles are short, see shortEpisodeTitle()).
std::string itemRow(const std::string& badge, const std::string& text, const std::string& duration) {
    std::string left = std::string(kListIndent, ' ');
    if (!badge.empty()) {
        left += badge + " ";
    }
    const int maxTextLen = kCols - static_cast<int>(left.size()) - kSuffixWidth - 1;
    left += shorten(text, static_cast<size_t>(std::max(maxTextLen, 0)));
    const int gap = kCols - kSuffixWidth - static_cast<int>(left.size());
    left += gap >= 2 ? " " + std::string(static_cast<size_t>(gap - 1), '.')
                     : std::string(static_cast<size_t>(std::max(gap, 0)), ' ');
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "%*s", kSuffixWidth, duration.c_str());
    return left + suffix;
}

void putPlayLinkRow(TeletextPage& page, int row, const std::string& text, const std::string& url,
                    const std::string& title) {
    putListRow(page, row, text);
    page.paint(row, kCols - kSuffixWidth, kSuffixWidth, Color::Blue, Color::White);
    page.addPlayLink(row, url, title);
}

std::string statusText(const MvwShowData& d) {
    switch (d.status) {
        case MvwFetchStatus::Pending:
            return "Waiting for first update...";
        case MvwFetchStatus::Failed:
            return d.items.empty() ? "Source not responding - retrying"
                                   : "Updated " + formatLocalTime(d.fetchedAt, "%d %b %H:%M") + " (update failed)";
        case MvwFetchStatus::Ok:
            return "Updated " + formatLocalTime(d.fetchedAt, "%d %b %H:%M");
    }
    return "";
}

// Fills the shared blank-white-block background for a list before its real
// rows get drawn over the top, matching news_pages.cpp's own filler pass.
void fillListBlock(TeletextPage& page, int firstRow, int rows) {
    for (int r = 0; r < rows; ++r) {
        putListRow(page, firstRow + r, "");
    }
}

// One show's page: a title-art index sub-page (0) followed by as many
// episode-list sub-pages (1, 2, ...) as the item count and `maxSubPages`
// allow -- all sharing the single page number `pageNumber`, so a show never
// needs more than the one number it was given (see TeletextPage::subPage).
// Numbered episodes (season/episode parsed from the title) come first,
// sorted by season then episode; everything else (extras, interviews,
// trailers) follows, newest first. Every row is a play link -- a show page
// is a leaf, unlike NEWS's headline lists, which link to further pages.
void buildShowPages(int pageNumber, int maxSubPages, const std::string& serviceName, const std::string& label,
                    const std::string& topic, const MvwShowData& data, int parentPage, PageStore& store) {
    if (data.items.empty()) {
        TeletextPage page;
        page.number = pageNumber;
        page.category = "SHOW";
        page.service = serviceName;
        page.parentPage = parentPage;
        drawBanner(page, label);
        page.putText(kCategoryRow, 1, shorten(topic, kCols - 2), Color::Yellow);
        if (data.status == MvwFetchStatus::Pending) {
            page.setLine(8, "  Episodes for this show are being", Color::White);
            page.setLine(9, "  fetched. Please wait a moment,", Color::White);
            page.setLine(10, "  then try this page again.", Color::White);
        } else {
            page.setLine(8, "  No episodes are available for this", Color::White);
            page.setLine(9, "  show right now. Check the topic name", Color::White);
            page.setLine(10, "  in the config file, or try again later.", Color::White);
        }
        page.setLine(kInfoRow, statusText(data), Color::Cyan);
        store.add(std::move(page));
        return;
    }

    std::vector<MvwItem> numbered;
    std::vector<MvwItem> other;
    for (const MvwItem& item : data.items) {
        (item.season > 0 || item.episode > 0 ? numbered : other).push_back(item);
    }
    std::stable_sort(numbered.begin(), numbered.end(), [](const MvwItem& a, const MvwItem& b) {
        return a.season != b.season ? a.season < b.season : a.episode < b.episode;
    });
    std::stable_sort(other.begin(), other.end(), [](const MvwItem& a, const MvwItem& b) {
        return a.timestamp > b.timestamp;
    });
    std::vector<MvwItem> ordered = std::move(numbered);
    ordered.insert(ordered.end(), other.begin(), other.end());

    std::vector<std::vector<std::string>> rowsPerSub(1);
    std::vector<std::vector<const MvwItem*>> itemsPerSub(1);
    for (const MvwItem& item : ordered) {
        const int capacity = rowsPerSub.size() == 1 ? kFirstPageListRows : kOtherPageListRows;
        if (static_cast<int>(rowsPerSub.back().size()) >= capacity) {
            if (static_cast<int>(rowsPerSub.size()) >= maxSubPages) {
                break;  // out of sub-page budget: the rest are simply not shown
            }
            rowsPerSub.emplace_back();
            itemsPerSub.emplace_back();
        }
        const std::string badge = (item.season > 0 || item.episode > 0) ? formatBadge(item.season, item.episode) : "";
        rowsPerSub.back().push_back(itemRow(badge, toTeletextAscii(shortEpisodeTitle(item.title, topic)),
                                            formatDuration(item.durationSeconds)));
        itemsPerSub.back().push_back(&item);
    }

    const int subPageCount = static_cast<int>(rowsPerSub.size());
    for (int i = 0; i < subPageCount; ++i) {
        TeletextPage page;
        page.number = pageNumber;
        page.subPage = i;
        page.category = "SHOW";
        page.service = serviceName;
        page.parentPage = parentPage;
        if (i == 0) {
            drawBanner(page, label);
            page.putText(kCategoryRow, 1, shorten(toTeletextAscii(topic), kCols - 2), Color::Yellow);
        } else {
            page.setLine(kContentFirstRow,
                        shorten(toTeletextAscii(topic), kCols - 10) + " " + std::to_string(i + 1) + "/" +
                            std::to_string(subPageCount),
                        Color::Yellow);
        }

        const int listRow = i == 0 ? kFirstPageListRow : kOtherPageListRow;
        const int listRows = i == 0 ? kFirstPageListRows : kOtherPageListRows;
        fillListBlock(page, listRow, listRows);
        const auto& rows = rowsPerSub[static_cast<size_t>(i)];
        const auto& items = itemsPerSub[static_cast<size_t>(i)];
        for (size_t r = 0; r < rows.size(); ++r) {
            const std::string playTitle =
                toTeletextAscii(topic) + " - " + toTeletextAscii(shortEpisodeTitle(items[r]->title, topic));
            putPlayLinkRow(page, listRow + static_cast<int>(r), rows[r], items[r]->videoUrl, playTitle);
        }

        // No page-number-consuming "MORE nnn" hint here (there is no
        // further page number to name) -- Left/Right already turns to the
        // next sub-page on its own; "i+1/N" just orients the reader.
        std::string info = i == 0 ? statusText(data) : "";
        if (subPageCount > 1) {
            info += (info.empty() ? " " : "   ") + std::to_string(i + 1) + "/" + std::to_string(subPageCount);
        }
        page.setLine(kInfoRow, info, Color::Cyan);
        store.add(std::move(page));
    }
}

TeletextPage buildMvwIndex(const MvwConfig& config, const std::vector<MvwShowData>& favoritesData) {
    TeletextPage page;
    page.number = kIndexPage;
    page.category = "INDEX";
    page.service = config.serviceName;
    drawBanner(page, config.serviceLogo);
    page.putText(kCategoryRow, 1, "FAVORITES", Color::Yellow);

    const int listRows = std::max<int>(4, static_cast<int>(config.favorites.size()) + 3);
    fillListBlock(page, kFirstPageListRow, listRows);

    int row = kFirstPageListRow;
    for (const MvwFavorite& fav : config.favorites) {
        putPageLinkRow(page, row++, fav.label, fav.page);
    }
    if (config.favorites.empty()) {
        putListRow(page, row++, "  No favorites configured.");
    }
    ++row;  // blank separator before the A-Z link
    putPageLinkRow(page, row, "ALL SHOWS (A-Z)", config.azStartPage);

    time_t latest = 0;
    int waiting = 0;
    int withData = 0;
    for (const MvwShowData& d : favoritesData) {
        latest = std::max(latest, d.fetchedAt);
        waiting += d.status == MvwFetchStatus::Pending ? 1 : 0;
        withData += d.items.empty() ? 0 : 1;
    }
    std::string status;
    if (config.favorites.empty()) {
        status = "";
    } else if (withData == 0 && waiting > 0) {
        status = "Waiting for first update...";
    } else if (withData == 0) {
        status = "Offline - no shows available yet";
    } else {
        status = "Updated " + formatLocalTime(latest, "%d %b %H:%M");
    }
    page.setLine(kInfoRow - 2, "UP/DOWN select  LEFT/RIGHT page", Color::Yellow);
    page.setLine(kInfoRow, status, Color::Cyan);
    return page;
}

struct AzShow {
    std::string topic;
    std::vector<MvwItem> items;
};

// Groups by MediathekViewWeb `topic` (a std::map sorts them alphabetically
// for free); caps the show count so the generated section fits its page
// budget, keeping the alphabetically-first shows on a long list.
std::vector<AzShow> groupByTopic(const std::vector<MvwItem>& items, int maxShows) {
    std::map<std::string, std::vector<MvwItem>> byTopic;
    for (const MvwItem& item : items) {
        byTopic[item.topic].push_back(item);
    }
    std::vector<AzShow> shows;
    shows.reserve(byTopic.size());
    for (auto& [topic, its] : byTopic) {
        shows.push_back({topic, std::move(its)});
    }
    if (static_cast<int>(shows.size()) > maxShows) {
        shows.resize(static_cast<size_t>(maxShows));
    }
    return shows;
}

// The generated "ALL SHOWS (A-Z)" index and every show's own pages that fit
// after it, within [config.azStartPage, kMaxPage].
void buildAzSection(const MvwConfig& config, const MvwShowData& azData, PageStore& store) {
    if (azData.items.empty()) {
        TeletextPage page;
        page.number = config.azStartPage;
        page.category = "INDEX";
        page.service = config.serviceName;
        page.parentPage = kIndexPage;
        drawBanner(page, "ALL SHOWS");
        page.putText(kCategoryRow, 1, "A-Z", Color::Yellow);
        if (azData.status == MvwFetchStatus::Pending) {
            page.setLine(8, "  The list of available shows is being", Color::White);
            page.setLine(9, "  fetched. Please wait a moment, then", Color::White);
            page.setLine(10, "  try this page again.", Color::White);
        } else {
            page.setLine(8, "  Could not reach the " + shorten(config.serviceName, kCols - 15), Color::White);
            page.setLine(9, "  catalog right now. Retrying...", Color::White);
        }
        page.setLine(kInfoRow, statusText(azData), Color::Cyan);
        store.add(std::move(page));
        return;
    }

    const std::vector<AzShow> shows = groupByTopic(azData.items, config.azMaxShows);

    int indexPages = 1;
    {
        int remaining = static_cast<int>(shows.size()) - kFirstPageListRows;
        while (remaining > 0) {
            ++indexPages;
            remaining -= kOtherPageListRows;
        }
    }

    // Each show gets exactly one page number right after the index pages,
    // in the same alphabetical order -- its episodes live on that number's
    // own sub-pages (see buildShowPages()), not on further distinct
    // numbers, so a run of hundreds of shows still fits easily. Shows past
    // kMaxPage simply aren't included in this run (see the plan's "Risks":
    // this is a snapshot of what's available now, not a stable catalog,
    // and re-generates on every refresh) -- azMaxShows keeps this from
    // being reached in practice.
    struct Placed {
        const AzShow* show;
        int page;
    };
    std::vector<Placed> placed;
    int cursor = config.azStartPage + indexPages;
    for (const AzShow& show : shows) {
        if (cursor > kMaxPage) {
            break;
        }
        placed.push_back({&show, cursor});
        ++cursor;
    }

    for (int i = 0; i < indexPages; ++i) {
        TeletextPage page;
        page.number = config.azStartPage + i;
        page.category = "INDEX";
        page.service = config.serviceName;
        page.parentPage = kIndexPage;
        if (i == 0) {
            drawBanner(page, "ALL SHOWS");
            page.putText(kCategoryRow, 1, "A-Z", Color::Yellow);
        } else {
            page.setLine(kContentFirstRow, "ALL SHOWS (A-Z) " + std::to_string(i + 1) + "/" + std::to_string(indexPages),
                        Color::Yellow);
        }
        page.prevPage = i > 0 ? page.number - 1 : 0;
        page.nextPage = i + 1 < indexPages ? page.number + 1 : 0;

        const int listRow = i == 0 ? kFirstPageListRow : kOtherPageListRow;
        const int listRows = i == 0 ? kFirstPageListRows : kOtherPageListRows;
        fillListBlock(page, listRow, listRows);

        const int startIdx = i == 0 ? 0 : kFirstPageListRows + (i - 1) * kOtherPageListRows;
        for (int r = 0; r < listRows; ++r) {
            const size_t idx = static_cast<size_t>(startIdx + r);
            if (idx >= placed.size()) {
                break;
            }
            putPageLinkRow(page, listRow + r, toTeletextAscii(placed[idx].show->topic), placed[idx].page);
        }

        std::string info = i == 0 ? statusText(azData) : "";
        if (page.nextPage) {
            info += (info.empty() ? " " : "   ") + std::string("MORE ") + std::to_string(page.nextPage);
        }
        page.setLine(kInfoRow, info, Color::Cyan);
        store.add(std::move(page));
    }

    for (const Placed& p : placed) {
        MvwShowData data;
        data.status = MvwFetchStatus::Ok;
        data.items = p.show->items;
        data.fetchedAt = azData.fetchedAt;
        const std::string label = upperAscii(collapseWhitespace(toTeletextAscii(p.show->topic)));
        buildShowPages(p.page, config.maxEpisodePages, config.serviceName, label, p.show->topic, data,
                       config.azStartPage, store);
    }
}

}  // namespace

std::shared_ptr<const PageStore> buildMvwPageStore(const MvwConfig& config,
                                                    const std::vector<MvwShowData>& favoritesData,
                                                    const MvwShowData& azData) {
    auto store = std::make_shared<PageStore>();
    store->add(buildMvwIndex(config, favoritesData));
    for (size_t i = 0; i < config.favorites.size(); ++i) {
        const MvwShowData empty;
        const MvwShowData& data = i < favoritesData.size() ? favoritesData[i] : empty;
        buildShowPages(config.favorites[i].page, config.maxEpisodePages, config.serviceName, config.favorites[i].label,
                       config.favorites[i].topic, data, kIndexPage, *store);
    }
    buildAzSection(config, azData, *store);
    store->finalize();
    return store;
}

}  // namespace teletext
