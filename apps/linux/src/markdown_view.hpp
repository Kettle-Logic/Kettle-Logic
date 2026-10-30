#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>

namespace kl {

// Shows the Markdown of the user guide in a text view. It handles the subset those files use: headings, paragraphs,
// nested lists, code blocks, tables, block quotes, rules, and inline code, bold, italic, and links. Nothing in a
// document can become anything but text.
class MarkdownView {
public:
    // `openLink` is called with the target of a link the reader clicks.
    explicit MarkdownView(std::function<void(const std::string&)> openLink);
    GtkWidget* Root() const { return scroller_; }
    void Show(const std::string& markdown);

private:
    GtkWidget* scroller_;
    GtkWidget* view_;
    std::function<void(const std::string&)> openLink_;
};

}  // namespace kl
