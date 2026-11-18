// PocketDoomBuilder — Android port (Dear ImGui)
// helpdoc.h — The reference manual, formatted for ImGui.
//
// The desktop app opens Help/*.chm through the Win32 help viewer. The pages that
// go into it are plain XHTML (Help/*.html in this repository), so the port ships
// them as APK assets and formats them here: a small HTML parser keeps the parts
// the manual actually uses - headings, paragraphs, lists, tables, preformatted
// command syntax, bold/italic, links - and turns them into a flat list of lines
// that ImGui draws. Cross-page links become in-app navigation.

#pragma once

#include <string>
#include <vector>

namespace pdb {

enum class HelpStyle {
    Title,    // <h1> in the page's title bar
    Heading2, // <h2>
    Heading3, // <h3>
    Body,     // normal paragraph text
    Bullet,   // <li>
    TableHead,
    TableRow,
    Code,     // <pre>
    Link,
    Rule,
};

// A styled run of text. Bold/italic are merged into `style` where the manual
// relies on them for emphasis only.
struct HelpSpan {
    std::string text;
    HelpStyle style = HelpStyle::Body;
    bool bold = false;
    bool italic = false;
    bool codeFont = false;
    // If non-empty, tapping this run opens another page: "file.html" or
    // "file.html#anchor", or an external "http(s)://" URL.
    std::string link;
};

struct HelpLine {
    std::vector<HelpSpan> spans;
    float indent = 0.0f;   // pixels, for list nesting and table cells
    bool blank = false;
};

struct HelpPage {
    std::string file;      // "e_linedefs.html"
    std::string title;     // text of the first <h1>, or the document title
    std::vector<HelpLine> lines;
    std::vector<std::string> links; // pages this one links to (for the tools)
};

// Format one manual page. `html` is the raw document; `file` is used to resolve
// relative links. `basePath` is the asset directory the manual lives in
// (default "help/").
HelpPage parseHelpPage(const std::string& html, const std::string& file);

// Strip HTML to plain text (used for titles, hints and the plain-text export).
std::string helpHtmlToText(const std::string& html);

// Escape a string for inclusion in HTML (the reverse, for the export).
std::string helpEscapeHtml(const std::string& text);

} // namespace pdb
