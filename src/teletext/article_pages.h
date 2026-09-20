#pragma once

#include <string>
#include <vector>

#include "page.h"

namespace teletext {

// Greedy word wrap to `width` columns. "\n\n" in `text` yields one blank row,
// a single "\n" a forced line break; words longer than `width` (URLs) are
// hard-split. Input must already be ASCII (see toTeletextAscii()).
std::vector<std::string> wrapText(const std::string& text, int width);

// An article laid out as rows of text per page, before page numbers exist.
// Splitting layout from numbering lets the aggregator measure how many pages
// an article needs (for the per-category page budget) before deciding where
// it goes.
struct ArticleLayout {
    // pages[i] holds the content rows for page i, starting at kContentFirstRow.
    std::vector<std::vector<std::string>> pages;
    // Text color of each row in `pages` (same shape): headline yellow,
    // dateline and notices cyan, body white.
    std::vector<std::vector<Color>> colors;
    bool truncated = false;  // body was cut at maxPages
};

// Lays out an article: the first page carries the wrapped title, `meta` (a
// dateline), a blank row and the body; continuation pages repeat the title,
// shortened to one row, then the body. Bodies that do not fit in `maxPages`
// are cut and end with a "continues at source" row.
ArticleLayout layoutArticle(const std::string& title, const std::string& meta, const std::string& body,
                            int maxPages);

// Turns a layout into numbered pages firstPage, firstPage+1, ...: colored
// text, the info row ("1/3   MORE 113") and prev/next/articleFirstPage
// links. `service` is the provider name the header shows.
std::vector<TeletextPage> makeArticlePages(const ArticleLayout& layout, const std::string& category,
                                           const std::string& service, int firstPage);

}  // namespace teletext
