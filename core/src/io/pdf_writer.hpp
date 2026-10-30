#pragma once

#include <string>
#include <vector>

// The PDF writer the engine's printed documents share: US Letter pages with text set in Helvetica and Helvetica-Bold,
// so no fonts come from the system. Each page's operators are built with Page, and assemble() writes the file. This
// header is internal to the engine; the documents built on it are in brew_sheet_pdf.hpp and recipe_pdf.hpp.
namespace kettlelogic::io::pdf {

constexpr double kPageWidth = 612;
constexpr double kPageHeight = 792;

// A number as PDF operators take it: plain decimal, always with a period.
std::string num(double value);

// UTF-8 text as WinAnsi bytes, with the characters the encoding lacks shown as '?'.
std::string toWinAnsi(const std::string& utf8);

// A PDF string literal, with the characters that would end it escaped.
std::string literal(const std::string& winAnsiText);

// The width in points of `utf8` at `size`, in Helvetica or, when `bold`, Helvetica-Bold. The bold width is an estimate
// a little above the true one, so wrapped text stays inside its column.
double textWidth(const std::string& utf8, double size, bool bold);

// Breaks `utf8` into lines no wider than `width` points. Lines break at spaces and at newlines; a word wider than a
// line is broken between characters.
std::vector<std::string> wrap(const std::string& utf8, double width, double size, bool bold);

// The operators of one page. Positions are measured from the top-left corner, as a reader lays out a form.
class Page {
public:
    // Text whose top is at `top`, in Helvetica or Helvetica-Bold (font 1 or 2), at `gray` (0 is black).
    void text(double x, double top, double size, bool bold, const std::string& utf8, double gray = 0);

    // A box outline, with its top-left corner at (x, top).
    void box(double x, double top, double width, double height);

    // A filled rectangle, with its top-left corner at (x, top), at `gray`.
    void fill(double x, double top, double width, double height, double gray);

    // A thin rule at `top`, from `left` to `right`. The default is the page's text margins.
    void rule(double top, double left = 40, double right = 572);

    const std::string& ops() const { return ops_; }

private:
    std::string ops_;
};

// The PDF file for pages whose operators are `pages`, in order, with `title` (UTF-8) in the document info.
std::string assemble(const std::vector<std::string>& pages, const std::string& title);

}  // namespace kettlelogic::io::pdf
