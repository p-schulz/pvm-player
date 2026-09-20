#include "page.h"

#include <algorithm>
#include <cstdio>

namespace teletext {

namespace {

std::string fitToGrid(const std::string& text) {
    std::string out = text.substr(0, static_cast<size_t>(kCols));
    out.resize(static_cast<size_t>(kCols), ' ');
    return out;
}

}  // namespace

TeletextPage::TeletextPage() {
    for (int row = 0; row < kRows; ++row) {
        lines[static_cast<size_t>(row)] = std::string(static_cast<size_t>(kCols), ' ');
        fg[static_cast<size_t>(row)].fill(Color::White);
        bg[static_cast<size_t>(row)].fill(Color::Black);
        sixel[static_cast<size_t>(row)].fill(0);
    }

    // Fastext bar: four equal filled rectangles with centered labels.
    static const struct {
        const char* label;
        Color fill;
        Color text;
    } kButtons[4] = {{"-", Color::Red, Color::Black},
                     {"+", Color::Green, Color::Black},
                     {"News", Color::Yellow, Color::Black},
                     {"Weather", Color::Blue, Color::White}};
    const int width = kCols / 4;
    for (int i = 0; i < 4; ++i) {
        const std::string label = kButtons[i].label;
        const int pad = (width - static_cast<int>(label.size())) / 2;
        paint(kFooterRow, i * width, width, kButtons[i].text, kButtons[i].fill);
        putText(kFooterRow, i * width + pad, label, kButtons[i].text);
    }
}

void TeletextPage::setLine(int row, const std::string& text, Color foreground, Color background) {
    if (row < 0 || row >= kRows) {
        return;
    }
    const auto r = static_cast<size_t>(row);
    lines[r] = fitToGrid(text);
    fg[r].fill(foreground);
    bg[r].fill(background);
    sixel[r].fill(0);
}

void TeletextPage::putText(int row, int col, const std::string& text, Color foreground) {
    if (row < 0 || row >= kRows) {
        return;
    }
    const auto r = static_cast<size_t>(row);
    for (size_t i = 0; i < text.size(); ++i) {
        const int c = col + static_cast<int>(i);
        if (c < 0 || c >= kCols) {
            continue;
        }
        lines[r][static_cast<size_t>(c)] = text[i];
        fg[r][static_cast<size_t>(c)] = foreground;
        sixel[r][static_cast<size_t>(c)] = 0;
    }
}

void TeletextPage::paint(int row, int col, int length, Color foreground, Color background) {
    if (row < 0 || row >= kRows) {
        return;
    }
    const auto r = static_cast<size_t>(row);
    for (int c = std::max(col, 0); c < std::min(col + length, kCols); ++c) {
        fg[r][static_cast<size_t>(c)] = foreground;
        bg[r][static_cast<size_t>(c)] = background;
    }
}

void TeletextPage::fillRows(int firstRow, int count, Color background) {
    for (int row = firstRow; row < firstRow + count; ++row) {
        setLine(row, "", Color::White, background);
    }
}

void TeletextPage::setSixel(int row, int col, std::uint8_t mask, Color foreground, Color background) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) {
        return;
    }
    const auto r = static_cast<size_t>(row);
    const auto c = static_cast<size_t>(col);
    lines[r][c] = ' ';
    fg[r][c] = foreground;
    bg[r][c] = background;
    sixel[r][c] = static_cast<std::uint8_t>(kSixelFlag | (mask & 0x3F));
}

void PageStore::add(TeletextPage page) {
    const int number = page.number;
    pages_[number] = std::move(page);
}

void PageStore::finalize() {
    articleStarts_.clear();
    for (const auto& [number, page] : pages_) {
        if (page.isArticlePage() && page.articleFirstPage == number) {
            articleStarts_.push_back(number);
        }
    }
}

const TeletextPage* PageStore::find(int number) const {
    auto it = pages_.find(number);
    return it == pages_.end() ? nullptr : &it->second;
}

int PageStore::nextPage(int from) const {
    if (pages_.empty()) {
        return from;
    }
    auto it = pages_.upper_bound(from);
    return it == pages_.end() ? pages_.begin()->first : it->first;
}

int PageStore::prevPage(int from) const {
    if (pages_.empty()) {
        return from;
    }
    auto it = pages_.lower_bound(from);  // first page >= from
    return it == pages_.begin() ? pages_.rbegin()->first : std::prev(it)->first;
}

int PageStore::nextArticle(int from) const {
    if (articleStarts_.empty()) {
        return from;
    }
    auto it = std::upper_bound(articleStarts_.begin(), articleStarts_.end(), from);
    return it == articleStarts_.end() ? articleStarts_.front() : *it;
}

int PageStore::prevArticle(int from) const {
    if (articleStarts_.empty()) {
        return from;
    }
    // On a continuation page, "previous article" is relative to the article
    // it belongs to, not to the continuation page's own number.
    int reference = from;
    if (const TeletextPage* page = find(from); page && page->isArticlePage()) {
        reference = page->articleFirstPage;
    }
    auto it = std::lower_bound(articleStarts_.begin(), articleStarts_.end(), reference);
    return it == articleStarts_.begin() ? articleStarts_.back() : *std::prev(it);
}

void composeHeader(TeletextPage& page, const std::string& targetLabel, const std::string& date,
                   const std::string& time) {
    char current[8];
    std::snprintf(current, sizeof(current), "%03d", page.number);
    page.setLine(kHeaderRow, "", Color::White, Color::Black);
    page.putText(kHeaderRow, kHeaderCurrentCol, current, Color::Green);
    page.putText(kHeaderRow, kHeaderTargetCol, targetLabel.substr(0, 3), Color::White);
    page.putText(kHeaderRow, kHeaderServiceCol, page.service.substr(0, static_cast<size_t>(kHeaderServiceWidth)),
                 Color::Cyan);
    page.putText(kHeaderRow, kHeaderDateCol, date, Color::White);
    page.putText(kHeaderRow, kHeaderDateCol + 7, time, Color::White);
}

TeletextPage makeNotFoundPage(int number, const std::string& service) {
    TeletextPage page;
    page.number = number;
    page.category = "";
    page.service = service;
    page.parentPage = kIndexPage;
    page.setLine(10, "         PAGE NOT FOUND", Color::Yellow);
    page.setLine(12, "   No page with this number exists.");
    page.setLine(14, "   Enter another page number, or press", Color::Cyan);
    page.setLine(15, "   the yellow key for the news index.", Color::Cyan);
    return page;
}

}  // namespace teletext
