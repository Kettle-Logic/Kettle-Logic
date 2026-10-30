#include "kettlelogic/app/help_markdown.hpp"

#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace kettlelogic::app {

namespace {

std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || (static_cast<unsigned char>(c) & 0x80) != 0;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

// `*text*` or `_text_` as emphasis, where the marker is not inside a word and the text does not start or end with a
// space. `marker` is '*' or '_'.
std::string emphasize(const std::string& text, char marker) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const bool opens =
            text[i] == marker && (i == 0 || !(isWordChar(text[i - 1]) || text[i - 1] == marker || text[i - 1] == '*')) &&
            i + 1 < text.size() && !isSpace(text[i + 1]) && text[i + 1] != marker;
        if (opens) {
            // The nearest closing marker that is not after a space and not before a word character.
            std::size_t close = std::string::npos;
            for (std::size_t j = i + 2; j < text.size(); ++j) {
                if (text[j] == marker && !isSpace(text[j - 1]) &&
                    (j + 1 >= text.size() || !(isWordChar(text[j + 1]) || text[j + 1] == marker || text[j + 1] == '*'))) {
                    close = j;
                    break;
                }
            }
            if (close != std::string::npos) {
                out += "<em>" + text.substr(i + 1, close - i - 1) + "</em>";
                i = close + 1;
                continue;
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

std::string emphasis(const std::string& text) {
    static const std::regex link(R"(\[([^\]]+)\]\(([^)\s]+)\))");
    static const std::regex bold(R"(\*\*(.+?)\*\*)");
    std::string result = std::regex_replace(text, link, "<a href=\"$2\">$1</a>");
    result = std::regex_replace(result, bold, "<strong>$1</strong>");
    result = emphasize(result, '*');
    result = emphasize(result, '_');
    return result;
}

// Escapes the text first, so that nothing in a document can become markup. The backtick pieces are code and get no
// further formatting.
std::string inlineText(const std::string& text) {
    const std::string escaped = escape(text);
    std::string out;
    std::size_t start = 0;
    bool code = false;
    for (;;) {
        const std::size_t tick = escaped.find('`', start);
        const std::string piece = escaped.substr(start, tick == std::string::npos ? std::string::npos : tick - start);
        out += code ? "<code>" + piece + "</code>" : emphasis(piece);
        if (tick == std::string::npos) {
            break;
        }
        start = tick + 1;
        code = !code;
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    lines.push_back(current);
    return lines;
}

int headingLevel(const std::string& line) {
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') {
        ++hashes;
    }
    if (hashes >= 1 && hashes <= 6 && hashes < line.size() && line[hashes] == ' ') {
        return static_cast<int>(hashes);
    }
    return 0;
}

struct ListItem {
    int indent = 0;
    bool ordered = false;
    std::string text;
};

std::optional<ListItem> listItem(const std::string& line) {
    std::size_t indent = 0;
    while (indent < line.size() && line[indent] == ' ') {
        ++indent;
    }
    const std::string rest = line.substr(indent);
    if (rest.size() >= 2 && (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') && rest[1] == ' ') {
        return ListItem{static_cast<int>(indent), false, rest.substr(2)};
    }
    std::size_t digits = 0;
    while (digits < rest.size() && std::isdigit(static_cast<unsigned char>(rest[digits]))) {
        ++digits;
    }
    if (digits > 0 && rest.compare(digits, 2, ". ") == 0) {
        return ListItem{static_cast<int>(indent), true, rest.substr(digits + 2)};
    }
    return std::nullopt;
}

bool isTableRule(const std::string& line) {
    const std::string text = trimmed(line);
    return startsWith(text, "|") && text.find('-') != std::string::npos &&
           text.find_first_not_of("|-: ") == std::string::npos;
}

std::vector<std::string> cells(const std::string& row) {
    std::string text = trimmed(row);
    if (startsWith(text, "|")) text.erase(0, 1);
    if (!text.empty() && text.back() == '|') text.pop_back();
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == '|') {
            out.push_back(trimmed(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(trimmed(current));
    return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        out += (i == 0 ? "" : separator) + parts[i];
    }
    return out;
}

std::string blocks(const std::string& markdown) {
    const std::vector<std::string> lines = splitLines(markdown);
    std::vector<std::string> out;
    std::vector<std::string> paragraph;
    struct OpenList {
        int indent;
        bool ordered;
    };
    std::vector<OpenList> lists;
    std::size_t index = 0;

    const auto flushParagraph = [&] {
        if (!paragraph.empty()) {
            out.push_back("<p>" + inlineText(join(paragraph, " ")) + "</p>");
            paragraph.clear();
        }
    };
    // Closes the lists indented deeper than `indent`. Each closes its last item too.
    const auto closeLists = [&](int indent = -1) {
        while (!lists.empty() && lists.back().indent > indent) {
            out.push_back(std::string("</li></") + (lists.back().ordered ? "ol" : "ul") + ">");
            lists.pop_back();
        }
    };
    const auto addItem = [&](const ListItem& item) {
        closeLists(item.indent);
        if (!lists.empty() && lists.back().indent == item.indent && lists.back().ordered == item.ordered) {
            out.push_back("</li>");
        } else {
            // A list of the other kind at this depth ends, and a new one starts.
            closeLists(item.indent - 1);
            out.push_back(item.ordered ? "<ol>" : "<ul>");
            lists.push_back({item.indent, item.ordered});
        }
        out.push_back("<li>" + inlineText(item.text));
    };

    while (index < lines.size()) {
        const std::string& line = lines[index];
        const std::string text = trimmed(line);
        ++index;

        if (text.empty()) {
            flushParagraph();
        } else if (startsWith(text, "```")) {
            flushParagraph();
            closeLists();
            std::vector<std::string> code;
            while (index < lines.size() && !startsWith(trimmed(lines[index]), "```")) {
                code.push_back(lines[index++]);
            }
            ++index;  // the closing fence
            out.push_back("<pre><code>" + escape(join(code, "\n")) + "</code></pre>");
        } else if (const int level = headingLevel(text); level > 0) {
            flushParagraph();
            closeLists();
            out.push_back("<h" + std::to_string(level) + ">" + inlineText(text.substr(static_cast<std::size_t>(level) + 1)) +
                          "</h" + std::to_string(level) + ">");
        } else if (text == "---" || text == "***" || text == "___") {
            flushParagraph();
            closeLists();
            out.push_back("<hr>");
        } else if (startsWith(text, ">")) {
            flushParagraph();
            closeLists();
            std::vector<std::string> quote = {trimmed(text.substr(1))};
            while (index < lines.size() && startsWith(trimmed(lines[index]), ">")) {
                quote.push_back(trimmed(trimmed(lines[index]).substr(1)));
                ++index;
            }
            out.push_back("<blockquote><p>" + inlineText(join(quote, " ")) + "</p></blockquote>");
        } else if (startsWith(text, "|") && index < lines.size() && isTableRule(lines[index])) {
            flushParagraph();
            closeLists();
            const std::vector<std::string> header = cells(text);
            ++index;
            std::vector<std::vector<std::string>> rows;
            while (index < lines.size() && startsWith(trimmed(lines[index]), "|")) {
                rows.push_back(cells(lines[index]));
                ++index;
            }
            std::string head;
            for (const std::string& cell : header) head += "<th>" + inlineText(cell) + "</th>";
            std::string body;
            for (const auto& row : rows) {
                body += "<tr>";
                for (const std::string& cell : row) body += "<td>" + inlineText(cell) + "</td>";
                body += "</tr>";
            }
            out.push_back("<table><thead><tr>" + head + "</tr></thead><tbody>" + body + "</tbody></table>");
        } else if (const auto item = listItem(line)) {
            flushParagraph();
            addItem(*item);
        } else if (!lists.empty() && startsWith(line, " ") && !out.empty() && startsWith(out.back(), "<li>")) {
            // An indented line continues the list item before it.
            out.back() += " " + inlineText(text);
        } else {
            if (paragraph.empty()) {
                closeLists();
            }
            paragraph.push_back(text);
        }
    }
    flushParagraph();
    closeLists();
    return join(out, "\n");
}

const char* const kStyle = R"CSS(
:root { color-scheme: light dark; }
body { font: 14px/1.55 "Segoe UI Variable Text", "Segoe UI", sans-serif; color: CanvasText; background: Canvas;
       max-width: 720px; margin: 0 auto; padding: 24px 28px 48px; }
h1 { font-size: 24px; margin: 0 0 16px; }
h2 { font-size: 19px; margin: 28px 0 8px; }
h3 { font-size: 15px; margin: 20px 0 6px; }
p, ul, ol, pre, table, blockquote { margin: 10px 0; }
li { margin: 3px 0; }
code { font: 12px Consolas, "Cascadia Mono", monospace; padding: 1px 4px; border-radius: 4px;
       background: color-mix(in srgb, CanvasText 9%, transparent); }
pre { padding: 10px 12px; border-radius: 8px; overflow-x: auto;
      background: color-mix(in srgb, CanvasText 6%, transparent); }
pre code { padding: 0; background: none; }
table { display: block; overflow-x: auto; border-collapse: collapse; }
th, td { border: 1px solid color-mix(in srgb, CanvasText 20%, transparent);
         padding: 4px 8px; text-align: left; vertical-align: top; }
blockquote { margin-left: 0; padding-left: 12px; color: GrayText;
             border-left: 3px solid color-mix(in srgb, CanvasText 25%, transparent); }
hr { border: 0; border-top: 1px solid color-mix(in srgb, CanvasText 15%, transparent); }
a { color: LinkText; }
)CSS";

}  // namespace

std::string HelpMarkdown::html(const std::string& markdown, const std::string& title) {
    return "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>" + escape(title) + "</title>\n<style>" +
           kStyle + "</style>\n</head>\n<body>\n" + blocks(markdown) + "\n</body>\n</html>\n";
}

std::vector<HelpPage> helpPages(const std::filesystem::path& root) {
    std::vector<HelpPage> pages;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return pages;
    }
    for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".md") {
            continue;
        }
        std::ifstream in(it->path(), std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string title = it->path().stem().string();
        for (const std::string& line : splitLines(buffer.str())) {
            if (startsWith(line, "# ")) {
                title = line.substr(2);
                break;
            }
        }
        const std::string id = std::filesystem::relative(it->path(), root, error).generic_string();
        pages.push_back(HelpPage{id, it->path(), title});
    }
    std::sort(pages.begin(), pages.end(), [](const HelpPage& a, const HelpPage& b) { return a.id < b.id; });
    return pages;
}

}  // namespace kettlelogic::app
