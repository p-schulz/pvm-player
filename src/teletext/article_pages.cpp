#include "article_pages.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace teletext {

namespace {

constexpr int kMaxTitleRows = 3;
const char* const kTruncationNotice = "[story continues at source]";

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> words;
    std::istringstream in(s);
    std::string w;
    while (in >> w) {
        words.push_back(w);
    }
    return words;
}

std::vector<std::string> wrapParagraph(const std::string& paragraph, int width) {
    std::vector<std::string> rows;
    std::string line;
    const auto flush = [&] {
        if (!line.empty()) {
            rows.push_back(line);
            line.clear();
        }
    };
    for (std::string word : splitWords(paragraph)) {
        // A word wider than the grid (a URL) is chopped into full rows,
        // with the remainder left in `line` to continue from.
        while (static_cast<int>(word.size()) > width) {
            flush();
            rows.push_back(word.substr(0, static_cast<size_t>(width)));
            word.erase(0, static_cast<size_t>(width));
        }
        if (word.empty()) {
            continue;
        }
        if (line.empty()) {
            line = word;
        } else if (static_cast<int>(line.size() + 1 + word.size()) <= width) {
            line += ' ';
            line += word;
        } else {
            flush();
            line = word;
        }
    }
    flush();
    return rows;
}

std::string shorten(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) {
        return s;
    }
    return s.substr(0, maxLen - 3) + "...";
}

}  // namespace

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> rows;
    std::istringstream in(text);
    std::string segment;
    while (std::getline(in, segment)) {
        const std::vector<std::string> wrapped = wrapParagraph(segment, width);
        if (wrapped.empty()) {
            // An empty segment is a paragraph break; never stack two blanks.
            if (!rows.empty() && !rows.back().empty()) {
                rows.emplace_back();
            }
        } else {
            rows.insert(rows.end(), wrapped.begin(), wrapped.end());
        }
    }
    while (!rows.empty() && rows.back().empty()) {
        rows.pop_back();
    }
    return rows;
}

ArticleLayout layoutArticle(const std::string& title, const std::string& meta, const std::string& body,
                            int maxPages) {
    ArticleLayout layout;
    maxPages = std::max(1, maxPages);

    std::vector<std::string> titleRows = wrapText(title, kCols);
    if (static_cast<int>(titleRows.size()) > kMaxTitleRows) {
        titleRows.resize(kMaxTitleRows);
        titleRows.back() = shorten(titleRows.back() + "...", kCols);
    }

    std::vector<std::string> firstHead = titleRows;
    std::vector<Color> firstHeadColors(titleRows.size(), Color::Yellow);
    if (!meta.empty()) {
        firstHead.push_back(meta);
        firstHeadColors.push_back(Color::Cyan);
    }
    firstHead.emplace_back();  // blank row between heading and text
    firstHeadColors.push_back(Color::White);

    const std::vector<std::string> contHead = {shorten(title, kCols - 8) + " (cont.)", ""};
    const std::vector<Color> contHeadColors = {Color::Yellow, Color::White};

    const std::vector<std::string> bodyRows = wrapText(body, kCols);
    size_t next = 0;

    for (int pageIndex = 0; pageIndex < maxPages; ++pageIndex) {
        std::vector<std::string> rows = pageIndex == 0 ? firstHead : contHead;
        std::vector<Color> colors = pageIndex == 0 ? firstHeadColors : contHeadColors;
        const int capacity = kContentRows - static_cast<int>(rows.size());
        const bool lastAllowed = pageIndex == maxPages - 1;

        // A paragraph break landing at the top of a page is just wasted space.
        while (next < bodyRows.size() && bodyRows[next].empty()) {
            ++next;
        }

        int take = std::min<int>(capacity, static_cast<int>(bodyRows.size() - next));
        const bool willTruncate = lastAllowed && next + static_cast<size_t>(take) < bodyRows.size();
        if (willTruncate) {
            take = std::max(0, capacity - 2);  // room for a blank row and the notice
        }
        rows.insert(rows.end(), bodyRows.begin() + static_cast<long>(next),
                    bodyRows.begin() + static_cast<long>(next) + take);
        colors.resize(rows.size(), Color::White);
        next += static_cast<size_t>(take);

        if (willTruncate) {
            while (!rows.empty() && rows.back().empty()) {
                rows.pop_back();
            }
            colors.resize(rows.size(), Color::White);
            rows.emplace_back();
            rows.emplace_back(kTruncationNotice);
            colors.push_back(Color::White);
            colors.push_back(Color::Cyan);
            layout.truncated = true;
        } else if (take > 0 && rows.back().empty() && next < bodyRows.size()) {
            // Don't leave a paragraph break dangling at the bottom of a page.
            rows.pop_back();
            colors.pop_back();
        }
        layout.pages.push_back(std::move(rows));
        layout.colors.push_back(std::move(colors));
        if (next >= bodyRows.size()) {
            break;
        }
    }
    return layout;
}

std::vector<TeletextPage> makeArticlePages(const ArticleLayout& layout, const std::string& category,
                                           const std::string& service, int firstPage) {
    std::vector<TeletextPage> pages;
    const int count = static_cast<int>(layout.pages.size());
    for (int i = 0; i < count; ++i) {
        TeletextPage page;
        page.number = firstPage + i;
        page.category = category;
        page.service = service;
        page.articleFirstPage = firstPage;
        page.prevPage = i > 0 ? firstPage + i - 1 : 0;
        page.nextPage = i + 1 < count ? firstPage + i + 1 : 0;

        const auto& rows = layout.pages[static_cast<size_t>(i)];
        const auto& colors = layout.colors[static_cast<size_t>(i)];
        for (size_t r = 0; r < rows.size() && static_cast<int>(r) < kContentRows; ++r) {
            page.setLine(kContentFirstRow + static_cast<int>(r), rows[r], colors[r]);
        }

        char info[kCols + 1];
        char more[16] = "";
        if (page.nextPage) {
            std::snprintf(more, sizeof(more), "MORE %d", page.nextPage);
        }
        std::snprintf(info, sizeof(info), " %d/%d   %s", i + 1, count, more);
        page.setLine(kInfoRow, info, Color::Cyan);
        pages.push_back(std::move(page));
    }
    return pages;
}

}  // namespace teletext
