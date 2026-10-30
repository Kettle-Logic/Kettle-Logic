#include "pdf_writer.hpp"

#include <algorithm>
#include <cstdio>

namespace kettlelogic::io::pdf {

namespace {

// Helvetica's advance widths for the printable ASCII characters, from space (32) to tilde (126), in thousandths of an em.
constexpr int kAdvance[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,  // space to /
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556,                                // 0 to 9
    278, 278, 584, 584, 584, 556, 1015,                                              // : to @
    667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611,
    722, 667, 944, 667, 667, 611,                                                    // A to Z
    278, 278, 278, 469, 556, 333,                                                    // [ to `
    556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278,
    556, 500, 722, 500, 500, 500,                                                    // a to z
    334, 260, 334, 584,                                                              // { to ~
};

// The width given to characters outside ASCII, which is about the average of the face's letters.
constexpr int kOtherAdvance = 556;

// Bold text is wider than regular text. Its measured width is this many times the regular width, a little above the
// true figure, so that wrapped lines fit.
constexpr double kBoldAllowance = 1.1;

// The number of bytes in the UTF-8 sequence that starts with `lead`. A byte that cannot start a sequence counts as one.
std::size_t sequenceLength(unsigned char lead) {
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

// The UTF-8 characters of `text`, one string each.
std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t length = std::min(sequenceLength(static_cast<unsigned char>(text[i])), text.size() - i);
        out.push_back(text.substr(i, length));
        i += length;
    }
    return out;
}

// One code point as the byte WinAnsiEncoding gives it, or '?' when the encoding has none.
char winAnsi(unsigned long cp) {
    if (cp < 0x80) return static_cast<char>(cp);
    if (cp >= 0xA0 && cp <= 0xFF) return static_cast<char>(cp);
    switch (cp) {
        case 0x2013: return static_cast<char>(0x96);  // en dash
        case 0x2014: return static_cast<char>(0x97);  // em dash
        case 0x2018: return static_cast<char>(0x91);
        case 0x2019: return static_cast<char>(0x92);
        case 0x201C: return static_cast<char>(0x93);
        case 0x201D: return static_cast<char>(0x94);
        case 0x2026: return static_cast<char>(0x85);
        default: return '?';
    }
}

// The text with a space for each control character, except newlines, and with carriage returns dropped.
std::string printable(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\r') continue;
        if (c == '\n' || static_cast<unsigned char>(c) >= 0x20) {
            out.push_back(c);
        } else {
            out.push_back(' ');
        }
    }
    return out;
}

// Breaks one paragraph (no newlines) into lines at its spaces, appending them to `lines`.
void wrapParagraph(const std::string& paragraph, double width, double size, bool bold, std::vector<std::string>& lines) {
    std::string line;
    std::size_t start = 0;
    while (start <= paragraph.size()) {
        std::size_t space = paragraph.find(' ', start);
        if (space == std::string::npos) space = paragraph.size();
        const std::string word = paragraph.substr(start, space - start);
        start = space + 1;

        const std::string joined = line.empty() ? word : line + " " + word;
        if (textWidth(joined, size, bold) <= width) {
            line = joined;
            continue;
        }
        if (!line.empty()) {
            lines.push_back(line);
            line.clear();
        }
        // A word wider than the line is broken between its characters.
        std::string chunk;
        for (const std::string& character : characters(word)) {
            if (!chunk.empty() && textWidth(chunk + character, size, bold) > width) {
                lines.push_back(chunk);
                chunk.clear();
            }
            chunk += character;
        }
        line = chunk;
    }
    lines.push_back(line);
}

}  // namespace

std::string num(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
    return buffer;
}

std::string toWinAnsi(const std::string& utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned long cp = c;
        std::size_t length = 1;
        if (c >= 0xF0) { cp = c & 0x07; length = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; length = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; length = 2; }
        else if (c >= 0x80) { out.push_back('?'); ++i; continue; }
        if (i + length > utf8.size()) {
            out.push_back('?');
            break;
        }
        bool ok = true;
        for (std::size_t k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(utf8[i + k]);
            if ((next & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        out.push_back(ok ? winAnsi(cp) : '?');
        i += ok ? length : 1;
    }
    return out;
}

std::string literal(const std::string& winAnsiText) {
    std::string out = "(";
    for (const char c : winAnsiText) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out + ")";
}

double textWidth(const std::string& utf8, double size, bool bold) {
    double units = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            units += (c >= 32 && c <= 126) ? kAdvance[c - 32] : kAdvance[0];
            i += 1;
        } else {
            units += kOtherAdvance;
            i += sequenceLength(c);
        }
    }
    return units / 1000 * size * (bold ? kBoldAllowance : 1);
}

std::vector<std::string> wrap(const std::string& utf8, double width, double size, bool bold) {
    // Trailing line breaks would print as blank lines at the end of the text, so they are dropped.
    std::string text = printable(utf8);
    while (!text.empty() && text.back() == '\n') text.pop_back();

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        wrapParagraph(text.substr(start, end - start), width, size, bold, lines);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

void Page::text(double x, double top, double size, bool bold, const std::string& utf8, double gray) {
    const double baseline = kPageHeight - top - size * 0.8;
    ops_ += num(gray) + " g BT /F" + (bold ? "2 " : "1 ") + num(size) + " Tf " + num(x) + " " + num(baseline) +
            " Td " + literal(toWinAnsi(utf8)) + " Tj ET 0 g\n";
}

void Page::box(double x, double top, double width, double height) {
    ops_ += "0.55 G 0.6 w " + num(x) + " " + num(kPageHeight - top - height) + " " + num(width) + " " +
            num(height) + " re S\n";
}

void Page::fill(double x, double top, double width, double height, double gray) {
    ops_ += num(gray) + " g " + num(x) + " " + num(kPageHeight - top - height) + " " + num(width) + " " +
            num(height) + " re f 0 g\n";
}

void Page::rule(double top, double left, double right) {
    ops_ += "0.6 G 0.5 w " + num(left) + " " + num(kPageHeight - top) + " m " + num(right) + " " +
            num(kPageHeight - top) + " l S\n";
}

std::string assemble(const std::vector<std::string>& pages, const std::string& title) {
    // Objects: 1 catalog, 2 pages, 3 Helvetica, 4 Helvetica-Bold, then a page and its content for each page, then the
    // document info. Page i is object 5 + 2i, and its content is the object after it.
    const std::size_t info = 5 + 2 * pages.size();
    std::string kids;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        kids += (i > 0 ? " " : "") + std::to_string(5 + 2 * i) + " 0 R";
    }
    std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    };
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const std::size_t content = 6 + 2 * i;
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(kPageWidth) + " " + num(kPageHeight) +
                          "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + std::to_string(content) +
                          " 0 R >>");
        objects.push_back("<< /Length " + std::to_string(pages[i].size()) + " >>\nstream\n" + pages[i] + "endstream");
    }
    objects.push_back("<< /Title " + literal(toWinAnsi(title)) + " /Producer (Kettle Logic) >>");

    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char entry[32];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info " + std::to_string(info) +
           " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

}  // namespace kettlelogic::io::pdf
