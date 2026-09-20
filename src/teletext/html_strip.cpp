#include "html_strip.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace teletext {

namespace {

// ---- UTF-8 -----------------------------------------------------------------

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decodes one code point starting at s[i], advancing i. Malformed bytes
// decode as U+FFFD and consume one byte, so the scan always makes progress.
std::uint32_t nextCodePoint(const std::string& s, size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    int extra = 0;
    std::uint32_t cp = 0;
    if (b0 < 0x80) {
        ++i;
        return b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = b0 & 0x07;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + static_cast<size_t>(extra) >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (int k = 1; k <= extra; ++k) {
        const auto b = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    i += static_cast<size_t>(extra) + 1;
    return cp;
}

// ---- Character references --------------------------------------------------

const std::unordered_map<std::string, std::uint32_t>& namedEntities() {
    static const std::unordered_map<std::string, std::uint32_t> table = {
        {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},     {"apos", '\''},
        {"nbsp", 0xA0},    {"shy", 0xAD},     {"copy", 0xA9},    {"reg", 0xAE},     {"trade", 0x2122},
        {"euro", 0x20AC},  {"pound", 0xA3},   {"yen", 0xA5},     {"cent", 0xA2},    {"deg", 0xB0},
        {"plusmn", 0xB1},  {"times", 0xD7},   {"divide", 0xF7},  {"frac12", 0xBD},  {"frac14", 0xBC},
        {"frac34", 0xBE},  {"middot", 0xB7},  {"bull", 0x2022},  {"hellip", 0x2026},{"ndash", 0x2013},
        {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C},
        {"rdquo", 0x201D}, {"bdquo", 0x201E}, {"laquo", 0xAB},   {"raquo", 0xBB},   {"prime", 0x2032},
        {"Prime", 0x2033}, {"auml", 0xE4},    {"ouml", 0xF6},    {"uuml", 0xFC},    {"Auml", 0xC4},
        {"Ouml", 0xD6},    {"Uuml", 0xDC},    {"szlig", 0xDF},   {"eacute", 0xE9},  {"egrave", 0xE8},
        {"ecirc", 0xEA},   {"aacute", 0xE1},  {"agrave", 0xE0},  {"acirc", 0xE2},   {"iacute", 0xED},
        {"oacute", 0xF3},  {"uacute", 0xFA},  {"ntilde", 0xF1},  {"ccedil", 0xE7},  {"aring", 0xE5},
        {"oslash", 0xF8},  {"aelig", 0xE6},   {"Eacute", 0xC9},  {"sect", 0xA7},    {"para", 0xB6},
    };
    return table;
}

// `body` is the text between '&' and ';'. Returns false if it isn't a
// recognized reference (caller then keeps the '&' literally).
bool decodeReference(const std::string& body, std::uint32_t& cp) {
    if (body.empty()) {
        return false;
    }
    if (body[0] == '#') {
        if (body.size() < 2) {
            return false;
        }
        const bool hex = body[1] == 'x' || body[1] == 'X';
        const std::string digits = body.substr(hex ? 2 : 1);
        if (digits.empty() || digits.size() > 8) {
            return false;
        }
        std::uint32_t value = 0;
        for (char c : digits) {
            const int d = hex ? (std::isxdigit(static_cast<unsigned char>(c)) ? (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10)) : -1)
                              : (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : -1);
            if (d < 0) {
                return false;
            }
            value = value * (hex ? 16u : 10u) + static_cast<std::uint32_t>(d);
        }
        if (value == 0 || value > 0x10FFFF) {
            return false;
        }
        cp = value;
        return true;
    }
    auto it = namedEntities().find(body);
    if (it == namedEntities().end()) {
        return false;
    }
    cp = it->second;
    return true;
}

// ---- Tags ------------------------------------------------------------------

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isBlockTag(const std::string& name) {
    static const char* const kBlocks[] = {"p",  "div",  "h1",     "h2",         "h3",      "h4",     "h5",
                                          "h6", "ul",   "ol",     "blockquote", "table",   "tr",     "section",
                                          "article", "figure", "figcaption", "pre", "hr", "header", "footer", "aside"};
    for (const char* b : kBlocks) {
        if (name == b) {
            return true;
        }
    }
    return false;
}

// Scanner state, split out so stripHtml() stays readable.
struct Stripper {
    const std::string& in;
    std::string out;
    size_t i = 0;

    explicit Stripper(const std::string& html) : in(html) {}

    void paragraphBreak() {
        // Only ever emit breaks *between* content: trailing/leading ones are
        // trimmed and repeats collapsed in finish().
        out += "\n\n";
    }
    void lineBreak() { out += '\n'; }

    void text(const std::string& s) { out += s; }

    // Handles a '<' at in[i]. Returns false if it is not a tag at all (a
    // literal '<' in text, e.g. "a < b"), leaving i untouched.
    bool tag() {
        if (in.compare(i, 4, "<!--") == 0) {
            const size_t end = in.find("-->", i + 4);
            i = end == std::string::npos ? in.size() : end + 3;
            return true;
        }
        size_t j = i + 1;
        const bool closing = j < in.size() && in[j] == '/';
        if (closing) {
            ++j;
        }
        if (j >= in.size() || !(std::isalpha(static_cast<unsigned char>(in[j])) || (!closing && (in[j] == '!' || in[j] == '?')))) {
            return false;
        }
        if (in[j] == '!' || in[j] == '?') {  // <!DOCTYPE ...>, <?xml ...?>
            const size_t end = in.find('>', j);
            i = end == std::string::npos ? in.size() : end + 1;
            return true;
        }
        size_t nameEnd = j;
        while (nameEnd < in.size() && (std::isalnum(static_cast<unsigned char>(in[nameEnd])) || in[nameEnd] == ':')) {
            ++nameEnd;
        }
        const std::string name = lowerAscii(in.substr(j, nameEnd - j));

        // Find the closing '>' while respecting quoted attribute values
        // (which may legitimately contain '>').
        size_t k = nameEnd;
        char quote = 0;
        for (; k < in.size(); ++k) {
            const char c = in[k];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
        }
        const bool selfClosing = k > nameEnd && k <= in.size() && k > 0 && in[k - 1] == '/';
        i = k < in.size() ? k + 1 : in.size();

        if (!closing && !selfClosing && (name == "script" || name == "style")) {
            const std::string closeTag = "</" + name;
            size_t end = i;
            while (end < in.size()) {
                end = in.find("</", end);
                if (end == std::string::npos) {
                    break;
                }
                if (lowerAscii(in.substr(end, closeTag.size())) == closeTag) {
                    break;
                }
                end += 2;
            }
            if (end == std::string::npos) {
                i = in.size();
            } else {
                const size_t gt = in.find('>', end);
                i = gt == std::string::npos ? in.size() : gt + 1;
            }
            return true;
        }

        if (name == "br") {
            lineBreak();
        } else if (name == "li") {
            if (!closing) {
                lineBreak();
                text("* ");
            }
        } else if (isBlockTag(name)) {
            paragraphBreak();
        }
        return true;
    }

    // Handles '&' at in[i]: decodes a reference, or keeps the '&' literally.
    void reference() {
        const size_t semi = in.find(';', i + 1);
        if (semi != std::string::npos && semi - i <= 12) {
            std::uint32_t cp = 0;
            if (decodeReference(in.substr(i + 1, semi - i - 1), cp)) {
                std::string utf8;
                appendUtf8(utf8, cp);
                out += utf8;
                i = semi + 1;
                return;
            }
        }
        out += '&';
        ++i;
    }

    void run() {
        while (i < in.size()) {
            const char c = in[i];
            if (c == '<') {
                if (!tag()) {
                    out += '<';
                    ++i;
                }
            } else if (c == '&') {
                reference();
            } else {
                out += c;
                ++i;
            }
        }
    }
};

// Turns raw scanner output into normalized text: whitespace runs (other
// than the '\n' break markers) collapse to one space, spaces around
// newlines vanish, three or more newlines become a blank line, ends trim.
std::string normalizeWhitespace(const std::string& raw) {
    // Pass 1: NBSP and other Unicode spaces -> plain space, so they collapse.
    std::string spaced;
    spaced.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        const size_t start = i;
        const std::uint32_t cp = nextCodePoint(raw, i);
        if (cp == 0xA0 || cp == 0x2009 || cp == 0x200A || cp == 0x202F || cp == 0x3000 || cp == 0x2007 ||
            cp == 0x2002 || cp == 0x2003) {
            spaced += ' ';
        } else {
            spaced.append(raw, start, i - start);
        }
    }

    std::string out;
    out.reserve(spaced.size());
    int newlines = 0;
    bool pendingSpace = false;
    for (char c : spaced) {
        if (c == '\n') {
            ++newlines;
            pendingSpace = false;
        } else if (c == ' ' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {  // other controls act as spaces
            pendingSpace = true;
        } else {
            if (!out.empty()) {
                if (newlines > 0) {
                    out.append(static_cast<size_t>(newlines >= 2 ? 2 : 1), '\n');
                } else if (pendingSpace) {
                    out += ' ';
                }
            }
            newlines = 0;
            pendingSpace = false;
            out += c;
        }
    }
    return out;
}

bool looksLikeMarkup(const std::string& s) {
    return s.find("</") != std::string::npos || s.find("<p>") != std::string::npos ||
           s.find("<br") != std::string::npos;
}

}  // namespace

std::string stripHtml(const std::string& html) {
    Stripper s(html);
    s.run();
    std::string text = normalizeWhitespace(s.out);
    // Some feeds double-escape their markup (&amp;lt;p&amp;gt;), which one
    // pass of entity decoding turns back into literal tags.
    if (looksLikeMarkup(text)) {
        Stripper again(text);
        again.run();
        text = normalizeWhitespace(again.out);
    }
    return text;
}

std::string collapseWhitespace(const std::string& text) {
    std::string out;
    bool pendingSpace = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            pendingSpace = true;
        } else {
            if (pendingSpace && !out.empty()) {
                out += ' ';
            }
            pendingSpace = false;
            out += c;
        }
    }
    return out;
}

std::string toTeletextAscii(const std::string& utf8) {
    struct Mapping {
        std::uint32_t cp;
        const char* ascii;
    };
    static const Mapping kMap[] = {
        // Windows-1252 bytes 0x80-0x9F as they surface when a "latin1" feed
        // was really cp1252 (C1 controls in Unicode terms).
        {0x80, "EUR"}, {0x85, "..."}, {0x91, "'"}, {0x92, "'"}, {0x93, "\""}, {0x94, "\""}, {0x96, "-"}, {0x97, "-"},
        // Latin-1 supplement
        {0xA1, "!"}, {0xA2, "c"}, {0xA3, "GBP"}, {0xA5, "JPY"}, {0xA7, "S"}, {0xA9, "(c)"}, {0xAB, "\""},
        {0xAE, "(R)"}, {0xB0, ""}, {0xB1, "+/-"}, {0xB7, "*"}, {0xBB, "\""}, {0xBC, "1/4"}, {0xBD, "1/2"},
        {0xBE, "3/4"}, {0xBF, "?"}, {0xC0, "A"}, {0xC1, "A"}, {0xC2, "A"}, {0xC3, "A"}, {0xC4, "Ae"},
        {0xC5, "A"}, {0xC6, "AE"}, {0xC7, "C"}, {0xC8, "E"}, {0xC9, "E"}, {0xCA, "E"}, {0xCB, "E"},
        {0xCC, "I"}, {0xCD, "I"}, {0xCE, "I"}, {0xCF, "I"}, {0xD0, "D"}, {0xD1, "N"}, {0xD2, "O"},
        {0xD3, "O"}, {0xD4, "O"}, {0xD5, "O"}, {0xD6, "Oe"}, {0xD7, "x"}, {0xD8, "O"}, {0xD9, "U"},
        {0xDA, "U"}, {0xDB, "U"}, {0xDC, "Ue"}, {0xDD, "Y"}, {0xDE, "Th"}, {0xDF, "ss"}, {0xE0, "a"},
        {0xE1, "a"}, {0xE2, "a"}, {0xE3, "a"}, {0xE4, "ae"}, {0xE5, "a"}, {0xE6, "ae"}, {0xE7, "c"},
        {0xE8, "e"}, {0xE9, "e"}, {0xEA, "e"}, {0xEB, "e"}, {0xEC, "i"}, {0xED, "i"}, {0xEE, "i"},
        {0xEF, "i"}, {0xF0, "d"}, {0xF1, "n"}, {0xF2, "o"}, {0xF3, "o"}, {0xF4, "o"}, {0xF5, "o"},
        {0xF6, "oe"}, {0xF7, "/"}, {0xF8, "o"}, {0xF9, "u"}, {0xFA, "u"}, {0xFB, "u"}, {0xFC, "ue"},
        {0xFD, "y"}, {0xFE, "th"}, {0xFF, "y"},
        // Latin Extended-A, the common ones (Polish, Czech, Turkish, ...)
        {0x0100, "A"}, {0x0101, "a"}, {0x0104, "A"}, {0x0105, "a"}, {0x0106, "C"}, {0x0107, "c"},
        {0x010C, "C"}, {0x010D, "c"}, {0x010E, "D"}, {0x010F, "d"}, {0x0118, "E"}, {0x0119, "e"},
        {0x011A, "E"}, {0x011B, "e"}, {0x011E, "G"}, {0x011F, "g"}, {0x0130, "I"}, {0x0131, "i"},
        {0x0141, "L"}, {0x0142, "l"}, {0x0143, "N"}, {0x0144, "n"}, {0x0147, "N"}, {0x0148, "n"},
        {0x0152, "OE"}, {0x0153, "oe"}, {0x0158, "R"}, {0x0159, "r"}, {0x015A, "S"}, {0x015B, "s"},
        {0x015E, "S"}, {0x015F, "s"}, {0x0160, "S"}, {0x0161, "s"}, {0x0164, "T"}, {0x0165, "t"},
        {0x016E, "U"}, {0x016F, "u"}, {0x0178, "Y"}, {0x0179, "Z"}, {0x017A, "z"}, {0x017B, "Z"},
        {0x017C, "z"}, {0x017D, "Z"}, {0x017E, "z"},
        // Punctuation and symbols
        {0x2010, "-"}, {0x2011, "-"}, {0x2012, "-"}, {0x2013, "-"}, {0x2014, "-"}, {0x2015, "-"},
        {0x2212, "-"}, {0x2018, "'"}, {0x2019, "'"}, {0x201A, ","}, {0x201B, "'"}, {0x2032, "'"},
        {0x201C, "\""}, {0x201D, "\""}, {0x201E, "\""}, {0x201F, "\""}, {0x2033, "\""}, {0x2022, "*"},
        {0x2023, "*"}, {0x25CF, "*"}, {0x2026, "..."}, {0x2039, "<"}, {0x203A, ">"}, {0x20AC, "EUR"},
        {0x2122, "TM"}, {0x2190, "<-"}, {0x2192, "->"}, {0x2264, "<="}, {0x2265, ">="}, {0x2248, "~"},
    };
    static const std::unordered_map<std::uint32_t, const char*> table = [] {
        std::unordered_map<std::uint32_t, const char*> m;
        for (const Mapping& e : kMap) {
            m[e.cp] = e.ascii;
        }
        return m;
    }();

    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const std::uint32_t cp = nextCodePoint(utf8, i);
        if (cp >= 0x20 && cp < 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp == '\n') {
            out += '\n';  // paragraph structure survives; the paginator handles it
        } else if (cp < 0x20 || cp == 0x7F || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A)) {
            out += ' ';
        } else if (auto it = table.find(cp); it != table.end()) {
            out += it->second;
        } else if ((cp >= 0x200B && cp <= 0x200F) || cp == 0xAD || cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F) ||
                   (cp >= 0x2028 && cp <= 0x202E) || cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF)) {
            // Zero-width/formatting characters, variation selectors, emoji
            // and dingbats: nothing sensible to show.
        } else {
            out += '?';
        }
    }
    return out;
}

}  // namespace teletext
