// PocketDoomBuilder — Android port (Dear ImGui)
// helpdoc.cpp — HTML → formatted manual lines.
//
// This is not a general HTML renderer. It handles exactly the markup used by
// the bundled manual (see Help/*.html): headings, paragraphs, <br>, lists,
// tables, <pre> blocks, inline emphasis and links. Anything else is dropped,
// which is what the desktop help viewer does too - the pages carry a lot of
// boilerplate (<object>, <script>, a stylesheet link) that must not leak into
// the text.

#include "helpdoc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace pdb {

namespace {

// ---------------------------------------------------------------------------
// Character references
// ---------------------------------------------------------------------------

const std::map<std::string, std::string>& entityTable() {
    static const std::map<std::string, std::string> table = {
        {"amp", "&"},      {"lt", "<"},        {"gt", ">"},       {"quot", "\""},
        {"apos", "'"},     {"nbsp", " "},      {"copy", "\xC2\xA9"},
        {"reg", "\xC2\xAE"}, {"eacute", "\xC3\xA9"}, {"ndash", "-"},
        {"mdash", "-"},    {"hellip", "..."},  {"bull", "*"},     {"rsquo", "'"},
        {"lsquo", "'"},    {"rdquo", "\""},    {"ldquo", "\""},   {"deg", "deg"},
        {"times", "x"},    {"uuml", "\xC3\xBC"}, {"ouml", "\xC3\xB6"},
        {"auml", "\xC3\xA4"}, {"szlig", "\xC3\x9F"},
    };
    return table;
}

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += '?';
    }
}

// Replace &amp; &nbsp; &#123; &#x1F; etc.
std::string decodeEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out += in[i];
            continue;
        }
        const size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += in[i];
            continue;
        }
        const std::string name = in.substr(i + 1, semi - i - 1);
        if (!name.empty() && name[0] == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            const std::string digits = name.substr(hex ? 2 : 1);
            const unsigned long cp = strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
            appendUtf8(out, cp);
            i = semi;
            continue;
        }
        auto it = entityTable().find(name);
        if (it != entityTable().end()) {
            out += it->second;
        } else {
            out += '&';
            out += name;
            out += ';';
        }
        i = semi;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

struct Token {
    bool isTag = false;
    bool closing = false;
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;          // for text tokens (entities decoded)
    bool preformatted = false; // came from inside <pre>
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

// Collapse runs of whitespace to one space, as HTML does outside <pre>.
std::string collapseSpace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool pending = false;
    for (char c : s) {
        if ((unsigned char)c <= ' ') {
            pending = true; // a leading space is the gap after an inline element
        } else {
            if (pending) out += ' ';
            pending = false;
            out += c;
        }
    }
    if (pending) out += ' ';
    return out;
}

void parseAttributes(const std::string& raw, Token& token) {
    size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size() && (unsigned char)raw[i] <= ' ') ++i;
        size_t start = i;
        while (i < raw.size() && raw[i] != '=' && (unsigned char)raw[i] > ' ') ++i;
        std::string name = lower(raw.substr(start, i - start));
        while (i < raw.size() && (unsigned char)raw[i] <= ' ') ++i;
        std::string value;
        if (i < raw.size() && raw[i] == '=') {
            ++i;
            while (i < raw.size() && (unsigned char)raw[i] <= ' ') ++i;
            if (i < raw.size() && (raw[i] == '"' || raw[i] == '\'')) {
                const char quote = raw[i++];
                start = i;
                while (i < raw.size() && raw[i] != quote) ++i;
                value = raw.substr(start, i - start);
                ++i;
            } else {
                start = i;
                while (i < raw.size() && (unsigned char)raw[i] > ' ') ++i;
                value = raw.substr(start, i - start);
            }
        }
        if (!name.empty()) token.attrs[name] = decodeEntities(value);
    }
}

std::vector<Token> tokenize(const std::string& html) {
    std::vector<Token> tokens;
    size_t i = 0;
    bool inPre = false;

    // Skip the document prologue: anything before <body> is boilerplate except
    // <title>, which can double as the page title.
    while (i < html.size()) {
        if (html[i] != '<') {
            const size_t next = html.find('<', i);
            Token t;
            t.text = decodeEntities(html.substr(i, next == std::string::npos ? std::string::npos
                                                                           : next - i));
            if (t.text.find_first_not_of(" \t\r\n") != std::string::npos) {
                if (!inPre) t.text = collapseSpace(t.text);
                t.preformatted = inPre;
                tokens.push_back(t);
            }
            if (next == std::string::npos) break;
            i = next;
            continue;
        }

        // Comment.
        if (html.compare(i, 4, "<!--") == 0) {
            const size_t end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        // Doctype / processing instruction: dropped.
        if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
            const size_t end = html.find('>', i);
            i = end == std::string::npos ? html.size() : end + 1;
            continue;
        }

        const size_t end = html.find('>', i);
        if (end == std::string::npos) break;
        std::string raw = html.substr(i + 1, end - i - 1);
        i = end + 1;

        Token t;
        t.isTag = true;
        if (!raw.empty() && raw[0] == '/') {
            t.closing = true;
            raw.erase(0, 1);
        }
        // Self-closing marker.
        while (!raw.empty() && ((unsigned char)raw.back() <= ' ' || raw.back() == '/')) {
            raw.pop_back();
        }
        size_t nameEnd = 0;
        while (nameEnd < raw.size() && (unsigned char)raw[nameEnd] > ' ' && raw[nameEnd] != '/') {
            ++nameEnd;
        }
        t.name = lower(raw.substr(0, nameEnd));
        parseAttributes(raw.substr(nameEnd), t);

        // <script>/<style> content is not text: skip to the closing tag.
        if (!t.closing && (t.name == "script" || t.name == "style")) {
            const std::string close = "</" + t.name;
            const size_t closeAt = lower(html).find(close, i);
            i = closeAt == std::string::npos ? html.size() : html.find('>', closeAt) + 1;
            continue;
        }
        if (t.name == "pre") {
            inPre = !t.closing;
        }
        t.preformatted = inPre;
        tokens.push_back(t);
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// Lineariser
// ---------------------------------------------------------------------------

// Where text currently goes: a paragraph in progress, a table cell, or a <pre>
// block. The manual nests these at most two levels deep.
struct Builder {
    HelpPage page;
    std::vector<HelpSpan> pending;   // spans of the line being assembled
    int nestDepth = 0;               // <ul>/<ol> nesting for indentation
    bool inTableCell = false;
    float cellIndent = 0.0f;
    bool preformatted = false;
    float listIndent = 0.0f;
    bool bold = false;
    bool italic = false;
    bool codeFont = false;
    std::string link;

    void flushLine() {
        // Trailing/leading whitespace in a line is not meaningful.
        while (!pending.empty()) {
            HelpSpan& last = pending.back();
            size_t b = last.text.size();
            while (b > 0 && (unsigned char)last.text[b - 1] <= ' ') --b;
            last.text.erase(b);
            if (last.text.empty()) {
                pending.pop_back();
            } else {
                break;
            }
        }
        if (!pending.empty()) {
            HelpLine line;
            line.spans = pending;
            line.indent = inTableCell ? cellIndent : listIndent;
            page.lines.push_back(std::move(line));
        }
        pending.clear();
    }

    void flushBlank() {
        flushLine();
        if (page.lines.empty()) return;
        if (page.lines.back().blank) return;
        HelpLine blank;
        blank.blank = true;
        page.lines.push_back(std::move(blank));
    }

    // Word-wrap cannot happen here (ImGui wraps), but a <br> does force a line.
    void addText(const std::string& text, HelpStyle style) {
        if (text.empty()) return;
        // Leading space after another span is meaningful (word gap); a pure
        // whitespace run is not a span on its own.
        if (text.find_first_not_of(' ') == std::string::npos && pending.empty()) return;
        HelpSpan span;
        span.text = text;
        span.style = style;
        span.bold = bold;
        span.italic = italic;
        span.codeFont = codeFont;
        span.link = link;
        // Merge with the previous span when formatting is identical, so the UI
        // can draw whole runs with one call.
        if (!pending.empty()) {
            HelpSpan& prev = pending.back();
            if (prev.style == span.style && prev.bold == span.bold &&
                prev.italic == span.italic && prev.codeFont == span.codeFont &&
                prev.link == span.link) {
                prev.text += span.text;
                return;
            }
        }
        pending.push_back(std::move(span));
    }
};

std::string resolveLink(const std::string& href, const std::string& file) {
    const std::string h = trim(href);
    if (h.empty()) return "";
    if (h.compare(0, 7, "http://") == 0 || h.compare(0, 8, "https://") == 0 ||
        h.compare(0, 7, "mailto:") == 0) {
        return h;
    }
    if (h[0] == '#') return file + h;           // in-page anchor
    std::string page = h;
    // The manual links with backslashes in a few places (Windows authoring).
    std::replace(page.begin(), page.end(), '\\', '/');
    if (page.compare(0, 6, "gzdb/") == 0) return ""; // not shipped (see the index)
    return page;
}

void flattenTables(std::vector<Token>& tokens) {
    // <table> rows are turned into one line per row: "cell  |  cell". This is
    // enough for the manual's data tables (the largest is the shortcut list in
    // the pages we skip) and keeps the renderer simple.
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const Token& t) {
                                    if (!t.isTag) return false;
                                    return t.name == "colgroup" || t.name == "col" ||
                                           t.name == "tbody" || t.name == "thead" ||
                                           t.name == "tfoot";
                                }),
                 tokens.end());
}

HelpPage buildPage(const std::vector<Token>& tokens, const std::string& file) {
    Builder b;
    b.page.file = file;

    HelpStyle currentStyle = HelpStyle::Body;
    bool inTitleTag = false;
    int tableDepth = 0;
    int cellIndex = 0;
    std::string lastCellText;

    for (const Token& t : tokens) {
        if (!t.isTag) {
            if (inTitleTag) {
                b.page.title += (b.page.title.empty() ? "" : " ") + trim(t.text);
                continue;
            }
            std::string text = t.text;
            if (!b.preformatted) {
                text = collapseSpace(text);
                // Trim leading space at the start of a line to avoid indents.
                if (b.pending.empty() && text.size() && text[0] == ' ') text.erase(0, 1);
            }
            if (text.empty()) continue;
            b.addText(text, currentStyle);
            continue;
        }

        const std::string& n = t.name;

        if (n == "title") {
            inTitleTag = !t.closing;
            if (t.closing) b.page.title = trim(b.page.title);
            continue;
        }
        if (n == "h1") {
            if (t.closing) {
                b.flushLine();
                b.flushBlank();
                currentStyle = HelpStyle::Body;
            } else {
                b.flushBlank();
                currentStyle = HelpStyle::Title;
            }
            continue;
        }
        if (n == "h2" || n == "h3") {
            if (t.closing) {
                b.flushLine();
                b.flushBlank();
                currentStyle = HelpStyle::Body;
            } else {
                b.flushBlank();
                currentStyle = (n == "h2") ? HelpStyle::Heading2 : HelpStyle::Heading3;
            }
            continue;
        }
        if (n == "p" || n == "div" || n == "body" || n == "blockquote") {
            if (t.closing) {
                b.flushLine();
                b.flushBlank();
            } else {
                b.flushLine();
            }
            continue;
        }
        if (n == "br") {
            b.flushLine();
            continue;
        }
        if (n == "hr") {
            b.flushBlank();
            HelpLine rule;
            rule.indent = 0.0f;
            HelpSpan span;
            span.style = HelpStyle::Rule;
            rule.spans.push_back(span);
            b.page.lines.push_back(rule);
            b.flushBlank();
            continue;
        }
        if (n == "ul" || n == "ol") {
            b.flushLine();
            if (!t.closing) {
                ++b.nestDepth;
                b.listIndent = 14.0f * (float)b.nestDepth;
            } else {
                --b.nestDepth;
                b.listIndent = b.nestDepth > 0 ? 14.0f * (float)b.nestDepth : 0.0f;
                b.flushBlank();
            }
            continue;
        }
        if (n == "li") {
            if (t.closing) {
                b.flushLine();
            } else {
                b.flushLine();
                b.addText("* ", HelpStyle::Bullet);
            }
            continue;
        }
        if (n == "b" || n == "strong") {
            b.bold = !t.closing;
            continue;
        }
        if (n == "i" || n == "em") {
            b.italic = !t.closing;
            continue;
        }
        if (n == "code" || n == "tt" || n == "kbd") {
            b.codeFont = !t.closing;
            continue;
        }
        if (n == "pre") {
            if (t.closing) {
                b.flushLine();
                b.flushBlank();
                b.preformatted = false;
                currentStyle = HelpStyle::Body;
                b.codeFont = false;
            } else {
                b.flushBlank();
                b.preformatted = true;
                currentStyle = HelpStyle::Code;
                b.codeFont = true;
            }
            continue;
        }
        if (n == "a") {
            if (!t.closing) {
                auto it = t.attrs.find("href");
                if (it != t.attrs.end()) {
                    const std::string target = resolveLink(it->second, file);
                    if (!target.empty()) {
                        b.link = target;
                        if (b.page.links.size() < 64) b.page.links.push_back(target);
                    } else {
                        b.link.clear();
                    }
                } else {
                    b.link.clear();
                }
            } else {
                b.link.clear();
            }
            continue;
        }
        if (n == "table") {
            b.flushLine();
            if (t.closing) {
                b.flushBlank();
                tableDepth = std::max(0, tableDepth - 1);
            } else {
                ++tableDepth;
                b.flushBlank();
            }
            continue;
        }
        if (n == "tr") {
            if (t.closing) {
                b.flushLine();
                cellIndex = 0;
            } else {
                b.flushLine();
                currentStyle = HelpStyle::TableRow;
                cellIndex = 0;
            }
            continue;
        }
        if (n == "th" || n == "td") {
            if (t.closing) {
                // Cells of one row stay on one line: the manual's control
                // tables read as "key - what it does" that way.
                b.inTableCell = false;
                b.cellIndent = 0.0f;
                ++cellIndex;
            } else {
                if (cellIndex > 0) b.addText("  -  ", HelpStyle::TableRow);
                b.cellIndent = 0.0f;
                b.inTableCell = false;
                currentStyle = (n == "th") ? HelpStyle::TableHead : HelpStyle::TableRow;
                if (n == "th") b.bold = true;
            }
            if (t.closing && n == "th") b.bold = false;
            continue;
        }
        if (n == "img") {
            auto it = t.attrs.find("alt");
            if (it != t.attrs.end() && !it->second.empty()) {
                b.addText("[" + it->second + "]", HelpStyle::Body);
            }
            continue;
        }
        // Everything else (span, font, object, iframe, ...) carries no text.
    }

    b.flushLine();
    while (!b.page.lines.empty() && b.page.lines.back().blank) b.page.lines.pop_back();

    for (const HelpLine& line : b.page.lines) {
        for (const HelpSpan& span : line.spans) {
            if (span.style == HelpStyle::Title && !trim(span.text).empty()) {
                b.page.title = trim(span.text);
                return b.page;
            }
        }
        if (!line.blank) break; // only the very first line can be the heading
    }
    if (b.page.title.empty()) b.page.title = file;
    return b.page;
}

} // namespace

HelpPage parseHelpPage(const std::string& html, const std::string& file) {
    std::vector<Token> tokens = tokenize(html);
    flattenTables(tokens);
    return buildPage(tokens, file);
}

std::string helpHtmlToText(const std::string& html) {
    const std::vector<Token> tokens = tokenize(html);
    std::string out;
    for (const Token& t : tokens) {
        if (t.isTag) {
            if (t.name == "br" || t.name == "p" || t.name == "li" || t.name == "h1" ||
                t.name == "h2" || t.name == "h3" || t.name == "tr") {
                if (!out.empty() && out.back() != '\n') out += '\n';
            }
            continue;
        }
        out += t.text;
    }
    // Tidy: collapse the blank runs the block tags leave behind.
    std::string tidy;
    int newlines = 0;
    for (char c : out) {
        if (c == '\n') {
            if (++newlines > 2) continue;
        } else if ((unsigned char)c > ' ') {
            newlines = 0;
        }
        tidy += c;
    }
    return trim(tidy);
}

std::string helpEscapeHtml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace pdb
