#include "markdown_view.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace kl {

namespace {

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::size_t Indent(const std::string& line) {
    std::size_t n = 0;
    for (const char c : line) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// A list marker at the start of a line: "- ", "* ", or "1. ". Returns the length of the marker, or 0.
std::size_t ListMarker(const std::string& trimmedLine, bool* ordered) {
    if (trimmedLine.size() > 1 && (trimmedLine[0] == '-' || trimmedLine[0] == '*' || trimmedLine[0] == '+') && trimmedLine[1] == ' ') {
        *ordered = false;
        return 2;
    }
    std::size_t i = 0;
    while (i < trimmedLine.size() && std::isdigit(static_cast<unsigned char>(trimmedLine[i]))) ++i;
    if (i > 0 && i + 1 < trimmedLine.size() && trimmedLine[i] == '.' && trimmedLine[i + 1] == ' ') {
        *ordered = true;
        return i + 2;
    }
    return 0;
}

std::vector<std::string> SplitRow(const std::string& line) {
    std::vector<std::string> cells;
    std::string current;
    std::string body = Trim(line);
    if (!body.empty() && body.front() == '|') body.erase(0, 1);
    if (!body.empty() && body.back() == '|') body.pop_back();
    for (const char c : body) {
        if (c == '|') {
            cells.push_back(Trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    cells.push_back(Trim(current));
    return cells;
}

bool IsSeparatorRow(const std::string& line) {
    const std::string body = Trim(line);
    if (body.empty() || body.find('-') == std::string::npos) return false;
    return body.find_first_not_of("|-: ") == std::string::npos;
}

// A table cell's inline text as Pango markup.
std::string ToMarkup(const std::string& text) {
    std::string out;
    const auto escape = [](const std::string& raw) {
        char* escaped = g_markup_escape_text(raw.c_str(), static_cast<gssize>(raw.size()));
        std::string result = escaped;
        g_free(escaped);
        return result;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '`') {
            const auto close = text.find('`', i + 1);
            if (close != std::string::npos) {
                out += "<tt>" + escape(text.substr(i + 1, close - i - 1)) + "</tt>";
                i = close;
                continue;
            }
        } else if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto close = text.find("**", i + 2);
            if (close != std::string::npos) {
                out += "<b>" + escape(text.substr(i + 2, close - i - 2)) + "</b>";
                i = close + 1;
                continue;
            }
        } else if (c == '*') {
            const auto close = text.find('*', i + 1);
            if (close != std::string::npos && close > i + 1) {
                out += "<i>" + escape(text.substr(i + 1, close - i - 1)) + "</i>";
                i = close;
                continue;
            }
        } else if (c == '[') {
            const auto close = text.find("](", i);
            const auto end = close == std::string::npos ? close : text.find(')', close);
            if (end != std::string::npos) {
                out += escape(text.substr(i + 1, close - i - 1));
                i = end;
                continue;
            }
        }
        out += escape(std::string(1, c));
    }
    return out;
}

struct Writer {
    GtkTextBuffer* buffer = nullptr;
    GtkTextView* view = nullptr;
    std::vector<GtkTextTag*> links;
    std::vector<std::pair<GtkTextChildAnchor*, GtkWidget*>> tables;

    GtkTextTag* tag(const char* name) { return gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name); }

    // Appends text with the named tags applied to it.
    void put(const std::string& text, std::initializer_list<const char*> tags, GtkTextTag* extra = nullptr) {
        if (text.empty()) return;
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        const int start = gtk_text_iter_get_offset(&end);
        gtk_text_buffer_insert(buffer, &end, text.c_str(), -1);
        GtkTextIter from, to;
        gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
        gtk_text_buffer_get_end_iter(buffer, &to);
        for (const char* name : tags) {
            if (GtkTextTag* t = tag(name)) gtk_text_buffer_apply_tag(buffer, t, &from, &to);
        }
        if (extra != nullptr) gtk_text_buffer_apply_tag(buffer, extra, &from, &to);
    }

    // Appends text that may hold `code`, **bold**, *italic*, and [links](target).
    void inlineText(const std::string& text, std::vector<const char*> base) {
        std::string run;
        const auto flush = [&] {
            if (run.empty()) return;
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            const int start = gtk_text_iter_get_offset(&end);
            gtk_text_buffer_insert(buffer, &end, run.c_str(), -1);
            GtkTextIter from, to;
            gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
            gtk_text_buffer_get_end_iter(buffer, &to);
            for (const char* name : base) {
                if (GtkTextTag* t = tag(name)) gtk_text_buffer_apply_tag(buffer, t, &from, &to);
            }
            run.clear();
        };
        const auto with = [&](const std::string& inner, const char* extra) {
            flush();
            auto tags = base;
            tags.push_back(extra);
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            const int start = gtk_text_iter_get_offset(&end);
            gtk_text_buffer_insert(buffer, &end, inner.c_str(), -1);
            GtkTextIter from, to;
            gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
            gtk_text_buffer_get_end_iter(buffer, &to);
            for (const char* name : tags) {
                if (GtkTextTag* t = tag(name)) gtk_text_buffer_apply_tag(buffer, t, &from, &to);
            }
        };
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\\' && i + 1 < text.size()) {
                run.push_back(text[++i]);
            } else if (c == '`') {
                const auto close = text.find('`', i + 1);
                if (close == std::string::npos) {
                    run.push_back(c);
                } else {
                    with(text.substr(i + 1, close - i - 1), "code");
                    i = close;
                }
            } else if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
                const auto close = text.find("**", i + 2);
                if (close == std::string::npos) {
                    run.push_back(c);
                } else {
                    with(text.substr(i + 2, close - i - 2), "bold");
                    i = close + 1;
                }
            } else if (c == '*' || (c == '_' && (i == 0 || text[i - 1] == ' '))) {
                const auto close = text.find(c, i + 1);
                if (close == std::string::npos || close == i + 1) {
                    run.push_back(c);
                } else {
                    with(text.substr(i + 1, close - i - 1), "italic");
                    i = close;
                }
            } else if (c == '[') {
                const auto close = text.find("](", i);
                const auto end = close == std::string::npos ? close : text.find(')', close);
                if (end == std::string::npos) {
                    run.push_back(c);
                } else {
                    flush();
                    GtkTextTag* link = gtk_text_buffer_create_tag(buffer, nullptr, "underline", PANGO_UNDERLINE_SINGLE, nullptr);
                    GdkRGBA color;
                    if (!gtk_style_context_lookup_color(gtk_widget_get_style_context(GTK_WIDGET(view)), "accent_color", &color)) {
                        gdk_rgba_parse(&color, "#3584e4");
                    }
                    g_object_set(link, "foreground-rgba", &color, nullptr);
                    g_object_set_data_full(G_OBJECT(link), "href", g_strdup(text.substr(close + 2, end - close - 2).c_str()), g_free);
                    links.push_back(link);
                    GtkTextIter at;
                    gtk_text_buffer_get_end_iter(buffer, &at);
                    const int start = gtk_text_iter_get_offset(&at);
                    gtk_text_buffer_insert(buffer, &at, text.substr(i + 1, close - i - 1).c_str(), -1);
                    GtkTextIter from, to;
                    gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
                    gtk_text_buffer_get_end_iter(buffer, &to);
                    for (const char* name : base) {
                        if (GtkTextTag* t = tag(name)) gtk_text_buffer_apply_tag(buffer, t, &from, &to);
                    }
                    gtk_text_buffer_apply_tag(buffer, link, &from, &to);
                    i = end;
                }
            } else {
                run.push_back(c);
            }
        }
        flush();
    }
};

void MakeTags(GtkTextBuffer* buffer, GtkWidget* view) {
    GdkRGBA text;
    gtk_widget_get_color(view, &text);
    GdkRGBA faint = text;
    faint.alpha = 0.12f;
    gtk_text_buffer_create_tag(buffer, "h1", "scale", 1.7, "weight", PANGO_WEIGHT_BOLD, "pixels-above-lines", 4,
                               "pixels-below-lines", 10, nullptr);
    gtk_text_buffer_create_tag(buffer, "h2", "scale", 1.3, "weight", PANGO_WEIGHT_BOLD, "pixels-above-lines", 18,
                               "pixels-below-lines", 6, nullptr);
    gtk_text_buffer_create_tag(buffer, "h3", "scale", 1.1, "weight", PANGO_WEIGHT_BOLD, "pixels-above-lines", 12,
                               "pixels-below-lines", 4, nullptr);
    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, nullptr);
    gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, nullptr);
    gtk_text_buffer_create_tag(buffer, "code", "family", "monospace", "background-rgba", &faint, nullptr);
    gtk_text_buffer_create_tag(buffer, "block", "family", "monospace", "paragraph-background-rgba", &faint, "left-margin", 42,
                               "right-margin", 28, "pixels-below-lines", 0, nullptr);
    gtk_text_buffer_create_tag(buffer, "para", "pixels-below-lines", 10, "wrap-mode", GTK_WRAP_WORD_CHAR, nullptr);
    gtk_text_buffer_create_tag(buffer, "quote", "left-margin", 50, "style", PANGO_STYLE_ITALIC, "pixels-below-lines", 10,
                               "wrap-mode", GTK_WRAP_WORD_CHAR, nullptr);
    gtk_text_buffer_create_tag(buffer, "table", "family", "monospace", "wrap-mode", GTK_WRAP_NONE, "left-margin", 8, nullptr);
    gtk_text_buffer_create_tag(buffer, "rule", "foreground-rgba", &faint, "pixels-below-lines", 10, nullptr);
    for (int level = 0; level < 6; ++level) {
        const std::string name = "list" + std::to_string(level);
        gtk_text_buffer_create_tag(buffer, name.c_str(), "left-margin", 52 + level * 22, "indent", -20, "pixels-below-lines", 4,
                                   "wrap-mode", GTK_WRAP_WORD_CHAR, nullptr);
    }
}

// The target of the link at a point of the view, if there is one.
const char* LinkAt(GtkWidget* view, double x, double y) {
    int bx, by;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_WIDGET, static_cast<int>(x), static_cast<int>(y), &bx, &by);
    GtkTextIter iter;
    if (!gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &iter, bx, by)) return nullptr;
    const char* href = nullptr;
    GSList* tags = gtk_text_iter_get_tags(&iter);
    for (GSList* t = tags; t != nullptr; t = t->next) {
        if (const char* found = static_cast<const char*>(g_object_get_data(G_OBJECT(t->data), "href"))) href = found;
    }
    g_slist_free(tags);
    return href;
}

struct Hooks {
    MarkdownView* self;
};

}  // namespace

MarkdownView::MarkdownView(std::function<void(const std::string&)> openLink) : openLink_(std::move(openLink)) {
    view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view_), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view_), 28);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view_), 28);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view_), 20);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view_), 28);
    scroller_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller_), view_);
    gtk_widget_set_hexpand(scroller_, TRUE);
    gtk_widget_set_vexpand(scroller_, TRUE);

    // A click on a link follows it, and the pointer is a hand over one.
    auto* hooks = new Hooks{this};
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect_data(
        click, "released",
        G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer data) {
            auto* hooks = static_cast<Hooks*>(data);
            GtkWidget* view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
            // A drag that selected text is not a click on a link.
            if (gtk_text_buffer_get_has_selection(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)))) return;
            if (const char* href = LinkAt(view, x, y)) {
                if (hooks->self->openLink_) hooks->self->openLink_(href);
            }
        }),
        hooks, [](gpointer data, GClosure*) { delete static_cast<Hooks*>(data); }, static_cast<GConnectFlags>(0));
    gtk_widget_add_controller(view_, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect_data(
        motion, "motion",
        G_CALLBACK(+[](GtkEventControllerMotion* controller, double x, double y, gpointer) {
            GtkWidget* view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
            gtk_widget_set_cursor_from_name(view, LinkAt(view, x, y) != nullptr ? "pointer" : "text");
        }),
        nullptr, nullptr, static_cast<GConnectFlags>(0));
    gtk_widget_add_controller(view_, motion);
}

void MarkdownView::Show(const std::string& markdown) {
    GtkTextBuffer* buffer = gtk_text_buffer_new(nullptr);
    MakeTags(buffer, view_);
    Writer w;
    w.buffer = buffer;
    w.view = GTK_TEXT_VIEW(view_);

    std::vector<std::string> lines;
    {
        std::string current;
        for (const char c : markdown) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        lines.push_back(current);
    }

    std::string paragraph;
    std::vector<std::size_t> listIndents;     // the indents of the open list levels
    std::vector<int> listCounters;
    const auto endParagraph = [&] {
        if (paragraph.empty()) return;
        w.inlineText(paragraph, {"para"});
        w.put("\n", {"para"});
        paragraph.clear();
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& raw = lines[i];
        const std::string line = Trim(raw);

        if (StartsWith(line, "```")) {
            endParagraph();
            listIndents.clear();
            std::string block;
            for (++i; i < lines.size() && !StartsWith(Trim(lines[i]), "```"); ++i) block += lines[i] + "\n";
            w.put(block.empty() ? "\n" : block, {"block"});
            w.put("\n", {"para"});
            continue;
        }
        if (line.empty()) {
            endParagraph();
            continue;
        }
        if (line[0] == '#') {
            endParagraph();
            listIndents.clear();
            std::size_t level = 0;
            while (level < line.size() && line[level] == '#') ++level;
            const std::string title = Trim(line.substr(level));
            const char* tag = level == 1 ? "h1" : level == 2 ? "h2" : "h3";
            w.inlineText(title, {tag});
            w.put("\n", {tag});
            continue;
        }
        if (line.size() >= 3 && line.find_first_not_of("-*_ ") == std::string::npos && line.find_first_not_of(' ') != std::string::npos &&
            (line[0] == '-' || line[0] == '*' || line[0] == '_') && std::count(line.begin(), line.end(), line[0]) >= 3) {
            endParagraph();
            listIndents.clear();
            w.put("\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n", {"rule"});
            continue;
        }
        if (line[0] == '|') {
            endParagraph();
            listIndents.clear();
            std::vector<std::vector<std::string>> rows;
            std::size_t headerRows = 0;
            for (; i < lines.size() && Trim(lines[i]).rfind('|', 0) == 0; ++i) {
                if (IsSeparatorRow(lines[i])) {
                    headerRows = rows.size();
                    continue;
                }
                rows.push_back(SplitRow(lines[i]));
            }
            --i;
            // The table is a grid of labels that wrap, so a wide table fits the window.
            GtkWidget* grid = gtk_grid_new();
            gtk_grid_set_column_spacing(GTK_GRID(grid), 28);
            gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
            gtk_widget_set_margin_start(grid, 8);
            gtk_widget_set_margin_bottom(grid, 12);
            for (std::size_t r = 0; r < rows.size(); ++r) {
                for (std::size_t c = 0; c < rows[r].size(); ++c) {
                    std::string markup = ToMarkup(rows[r][c]);
                    if (r < headerRows) markup = "<b>" + markup + "</b>";
                    GtkWidget* cell = gtk_label_new(nullptr);
                    gtk_label_set_markup(GTK_LABEL(cell), markup.c_str());
                    gtk_label_set_xalign(GTK_LABEL(cell), 0);
                    // The last column takes what is left, and wraps. The others keep to one line.
                    if (c + 1 == rows[r].size() && c > 0) {
                        gtk_label_set_wrap(GTK_LABEL(cell), TRUE);
                        gtk_label_set_wrap_mode(GTK_LABEL(cell), PANGO_WRAP_WORD_CHAR);
                        gtk_label_set_width_chars(GTK_LABEL(cell), 44);
                        gtk_label_set_max_width_chars(GTK_LABEL(cell), 60);
                    }
                    gtk_widget_set_valign(cell, GTK_ALIGN_START);
                    gtk_grid_attach(GTK_GRID(grid), cell, static_cast<int>(c), static_cast<int>(r), 1, 1);
                }
            }
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            GtkTextChildAnchor* anchor = gtk_text_buffer_create_child_anchor(buffer, &end);
            w.tables.emplace_back(anchor, grid);
            w.put("\n", {"para"});
            continue;
        }
        if (line[0] == '>') {
            endParagraph();
            listIndents.clear();
            w.inlineText(Trim(line.substr(1)), {"quote"});
            w.put("\n", {"quote"});
            continue;
        }
        bool ordered = false;
        if (const std::size_t marker = ListMarker(line, &ordered); marker > 0) {
            endParagraph();
            const std::size_t indent = Indent(raw);
            while (!listIndents.empty() && listIndents.back() > indent) {
                listIndents.pop_back();
                listCounters.pop_back();
            }
            if (listIndents.empty() || listIndents.back() < indent) {
                listIndents.push_back(indent);
                listCounters.push_back(0);
            }
            const std::size_t level = std::min<std::size_t>(listIndents.size() - 1, 5);
            const std::string tag = "list" + std::to_string(level);
            const std::string bullet = ordered ? std::to_string(++listCounters.back()) + ".\t" : "\xE2\x80\xA2\t";
            w.put(bullet, {tag.c_str()});
            w.inlineText(Trim(line.substr(marker)), {tag.c_str()});
            w.put("\n", {tag.c_str()});
            continue;
        }
        // A continuation of a list item, indented under it, joins that item.
        if (!listIndents.empty() && Indent(raw) > listIndents.back()) {
            const std::string tag = "list" + std::to_string(std::min<std::size_t>(listIndents.size() - 1, 5));
            w.put("\t", {tag.c_str()});
            w.inlineText(line, {tag.c_str()});
            w.put("\n", {tag.c_str()});
            continue;
        }
        listIndents.clear();
        listCounters.clear();
        paragraph += (paragraph.empty() ? "" : " ") + line;
    }
    endParagraph();

    gtk_text_view_set_buffer(GTK_TEXT_VIEW(view_), buffer);
    for (const auto& [anchor, widget] : w.tables) gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(view_), widget, anchor);
    g_object_unref(buffer);
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
    gtk_adjustment_set_value(adjustment, 0);
}

}  // namespace kl
