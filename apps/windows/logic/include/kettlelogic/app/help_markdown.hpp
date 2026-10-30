#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kettlelogic::app {

// Turns the Markdown of the bundled docs into a web page for the Help window. It handles the subset those files
// use: headings, paragraphs, nested lists, code blocks, tables, block quotes, rules, and inline code, bold, italic,
// and links. The colors follow light or dark mode. Nothing in a document can become markup.
namespace HelpMarkdown {
std::string html(const std::string& markdown, const std::string& title);
}

// One document of the bundled docs.
struct HelpPage {
    std::string id;                  // the path under the Help folder, such as "01-getting-started.md"
    std::filesystem::path file;
    std::string title;
};

// The user guide: the Markdown pages in the app's `Help` folder.
std::vector<HelpPage> helpPages(const std::filesystem::path& root);

}  // namespace kettlelogic::app
