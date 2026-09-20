#pragma once

#include <string>

namespace teletext {

// Feed bodies are HTML fragments (RSS <description>, <content:encoded>, Atom
// <content>). This is deliberately not an HTML parser: it removes tags,
// drops <script>/<style>/comments, turns block-level tags into paragraph
// breaks, and decodes character references -- enough for teletext text.
//
// Returns UTF-8 text where paragraphs are separated by a blank line ("\n\n"),
// list items by a single "\n" (prefixed "* "), and runs of whitespace are
// collapsed to single spaces. Leading/trailing whitespace is trimmed.
std::string stripHtml(const std::string& html);

// Replaces everything outside printable ASCII with a close ASCII rendering
// (umlauts -> "ae"/"oe"/"ue", curly quotes -> straight, dashes -> '-',
// '...' for the ellipsis, ...), drops what has none (emoji, zero-width
// characters) and turns other controls into spaces. The 40x24 grid and the
// teletext fonts are ASCII-only, and real teletext did the same.
std::string toTeletextAscii(const std::string& utf8);

// Collapses all whitespace (including newlines) to single spaces and trims.
std::string collapseWhitespace(const std::string& text);

}  // namespace teletext
