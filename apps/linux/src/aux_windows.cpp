#include "aux_windows.hpp"

#include "kettlelogic/app/help_markdown.hpp"
#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;

namespace {

// A window that belongs to the app, with a header bar over its content.
GtkWidget* NewWindow(Host& host, const std::string& title, int width, int height, GtkWidget* content, GtkWidget* bottom = nullptr) {
    GtkWidget* window = adw_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_application(GTK_WINDOW(window), gtk_window_get_application(host.window));
    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
    if (bottom != nullptr) adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), bottom);
    adw_window_set_content(ADW_WINDOW(window), toolbar);
    return window;
}

// Tells the owner that its window is gone, once the window has finished closing.
template <typename Owner>
void ReportClosed(GtkWidget* window, Owner* owner, std::function<void(Owner*)> closed) {
    ui::On(window, "destroy", [owner, closed = std::move(closed)] { ui::Later([owner, closed] { closed(owner); }); });
}

void SetAttributes(GtkWidget* label, double scale, bool bold, bool strike) {
    PangoAttrList* list = pango_attr_list_new();
    pango_attr_list_insert(list, pango_attr_scale_new(scale));
    if (bold) pango_attr_list_insert(list, pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD));
    if (strike) pango_attr_list_insert(list, pango_attr_strikethrough_new(TRUE));
    gtk_label_set_attributes(GTK_LABEL(label), list);
    pango_attr_list_unref(list);
}

}  // namespace

// MARK: Tool window

ToolWindow::ToolWindow(Host& host, ToolKind kind, std::map<std::string, std::string> prefill,
                       std::function<void(ToolWindow*)> closed)
    : view_(kind, std::move(prefill), host.store.waterProfiles) {
    window_ = NewWindow(host, toolSpec(kind).title, 620, 760, view_.Root());
    ReportClosed<ToolWindow>(window_, this, std::move(closed));
    gtk_window_present(GTK_WINDOW(window_));
}

// MARK: Brew day

// A large checklist of the procedure, for reading at the kettle. Click a step to tick it off. The first step not yet
// ticked is the current one. The display stays awake while it is open, unless the switch at the bottom turns that off.
BrewDayWindow::BrewDayWindow(Host& host, BrewDayPlan plan, std::function<void(BrewDayWindow*)> closed)
    : host_(host), plan_(std::move(plan)) {
    done_ = plan_.loadProgress(host_.store.preferences);
    content_ = adw_bin_new();

    GtkWidget* keepOn = ui::Switch(host_.store.preferences.boolean(PrefKey::keepDisplayOn), [this](bool on) {
        host_.store.preferences.setBoolean(PrefKey::keepDisplayOn, on);
        displayLock_.Set(GTK_WINDOW(window_), on);
    });
    GtkWidget* footer = ui::Box(GTK_ORIENTATION_HORIZONTAL, 12);
    ui::Margins(footer, 24, 10, 24, 10);
    GtkWidget* keepLabel = ui::Text("Keep display on");
    gtk_widget_set_hexpand(keepLabel, TRUE);
    gtk_widget_set_valign(keepLabel, GTK_ALIGN_CENTER);
    ui::Append(footer, keepLabel);
    ui::Append(footer, keepOn);

    window_ = NewWindow(host, "Brew Day: " + plan_.recipeName, 720, 860, content_, footer);
    displayLock_.Set(GTK_WINDOW(window_), host_.store.preferences.boolean(PrefKey::keepDisplayOn));

    auto* tick = new std::pair<BrewDayWindow*, std::shared_ptr<bool>>(this, alive_);
    // The timer owns its data, so a tick that arrives after the window is gone still reads live memory, sees the
    // flag cleared, and removes the timer.
    timer_ = g_timeout_add_seconds_full(
        G_PRIORITY_DEFAULT, 1,
        [](gpointer data) -> gboolean {
            auto* pair = static_cast<std::pair<BrewDayWindow*, std::shared_ptr<bool>>*>(data);
            if (!*pair->second) return G_SOURCE_REMOVE;
            pair->first->Tick();
            return G_SOURCE_CONTINUE;
        },
        tick,
        [](gpointer data) { delete static_cast<std::pair<BrewDayWindow*, std::shared_ptr<bool>>*>(data); });
    g_source_set_name_by_id(timer_, "kl-brew-day-timer");

    ReportClosed<BrewDayWindow>(window_, this, std::move(closed));
    Rebuild();
    gtk_window_present(GTK_WINDOW(window_));
}

BrewDayWindow::~BrewDayWindow() {
    *alive_ = false;
    displayLock_.Set(nullptr, false);
}

void BrewDayWindow::Tick() {
    const auto now = std::chrono::steady_clock::now();
    bool redraw = false;
    for (auto& [index, countdown] : countdowns_) {
        if (!countdown.end || countdown.finished) continue;
        const double left = std::chrono::duration<double>(*countdown.end - now).count();
        if (left <= 0) {
            countdown.finished = true;
            redraw = true;
        } else if (const auto clock = clocks_.find(index); clock != clocks_.end()) {
            ui::SetText(clock->second, clockText(left));
        }
    }
    if (redraw) Rebuild();
}

void BrewDayWindow::Rebuild() {
    clocks_.clear();
    int current = -1;
    for (std::size_t i = 0; i < plan_.steps.size(); ++i) {
        if (done_.count(static_cast<int>(i)) == 0) {
            current = static_cast<int>(i);
            break;
        }
    }

    GtkWidget* steps = ui::Box(GTK_ORIENTATION_VERTICAL, 14);
    ui::Margins(steps, 24, 20, 24, 20);

    GtkWidget* header = ui::Box(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget* title = ui::Text(plan_.recipeName, ui::Kind::title);
    SetAttributes(title, 1.5, true, false);
    ui::Append(header, title);
    GtkWidget* progress = ui::Text("Boil " + std::to_string(plan_.boilMinutes) + " minutes \xC2\xB7 " + std::to_string(done_.size()) +
                                       " of " + std::to_string(plan_.steps.size()) + " done",
                                   ui::Kind::secondary);
    SetAttributes(progress, 1.2, false, false);
    ui::Append(header, progress);
    GtkWidget* startOver = ui::Button("Start Over", [this] {
        done_.clear();
        plan_.saveProgress(host_.store.preferences, done_);
        ui::Later([alive = alive_, this] { if (*alive) Rebuild(); });
    });
    ui::Enable(startOver, !done_.empty());
    ui::SetTip(startOver, "Untick every step. Ticked steps are remembered if you close this window.");
    ui::Append(header, startOver);
    ui::Append(steps, header);

    for (std::size_t i = 0; i < plan_.steps.size(); ++i) {
        const int index = static_cast<int>(i);
        const calc::ProcedureStep& step = plan_.steps[i];
        const bool isDone = done_.count(index) != 0;
        const bool isCurrent = index == current;

        GtkWidget* column = ui::Box(GTK_ORIENTATION_VERTICAL, 8);

        GtkWidget* card = ui::Box(GTK_ORIENTATION_HORIZONTAL, 16);
        ui::Margins(card, 18, 18, 18, 18);
        GtkWidget* tick = gtk_image_new_from_icon_name(isDone ? "object-select-symbolic" : "radio-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(tick), 28);
        gtk_widget_set_valign(tick, GTK_ALIGN_START);
        if (isDone) ui::Style(tick, "success"); else ui::Style(tick, "dim-label");
        ui::Append(card, tick);

        GtkWidget* texts = ui::Box(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_hexpand(texts, TRUE);
        GtkWidget* heading = ui::Text(step.minute ? step.phase + " \xC2\xB7 " + std::to_string(*step.minute) + " min" : step.phase,
                                      ui::Kind::strong);
        ui::Style(heading, "dim-label");
        ui::Append(texts, heading);
        GtkWidget* text = ui::Text(step.text);
        gtk_widget_set_hexpand(text, TRUE);
        SetAttributes(text, isCurrent ? 1.9 : 1.5, isCurrent, isDone);
        if (step.warning) ui::Style(text, "error");
        ui::Append(texts, text);
        ui::Append(card, texts);

        GtkWidget* button = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(button), card);
        ui::Style(button, "card");
        if (isCurrent) ui::Style(button, "kl-current");
        gtk_widget_set_opacity(button, isDone ? 0.5 : 1);
        ui::SetTip(button, isDone ? "Click to untick" : "Click to tick off");
        ui::On(button, "clicked", [this, index] {
            if (done_.count(index) != 0) done_.erase(index);
            else done_.insert(index);
            plan_.saveProgress(host_.store.preferences, done_);
            ui::Later([alive = alive_, this] { if (*alive) Rebuild(); });
        });
        ui::Append(column, button);

        // A step that needs a timer has one beneath it, outside the tick button so the two are not nested.
        if (step.timerMinutes) {
            const int minutes = static_cast<int>(*step.timerMinutes);
            Countdown& countdown = countdowns_[index];
            GtkWidget* timer = ui::Box(GTK_ORIENTATION_HORIZONTAL, 12);
            ui::Margins(timer, 18, 0, 0, 0);

            const auto rebuild = [this] { ui::Later([alive = alive_, this] { if (*alive) Rebuild(); }); };
            const auto start = [this, index, minutes, rebuild] {
                Countdown& cd = countdowns_[index];
                cd.end = std::chrono::steady_clock::now() + std::chrono::seconds(minutes * 60);
                cd.secondsLeft.reset();
                cd.finished = false;
                rebuild();
            };
            const auto big = [](GtkWidget* b) {
                gtk_widget_set_size_request(b, -1, 40);
                return b;
            };
            if (countdown.end) {
                if (countdown.finished) {
                    GtkWidget* up = ui::Text("\xF0\x9F\x94\x94 Time's up", ui::Kind::strong);
                    SetAttributes(up, 1.4, true, false);
                    ui::Style(up, "warning");
                    gtk_widget_set_valign(up, GTK_ALIGN_CENTER);
                    ui::Append(timer, up);
                } else {
                    const double left = std::chrono::duration<double>(*countdown.end - std::chrono::steady_clock::now()).count();
                    GtkWidget* clock = ui::Text(clockText(left), ui::Kind::strong);
                    SetAttributes(clock, 1.7, true, false);
                    gtk_widget_set_valign(clock, GTK_ALIGN_CENTER);
                    clocks_[index] = clock;
                    ui::Append(timer, clock);
                    ui::Append(timer, big(ui::Button("Pause", [this, index, rebuild] {
                        Countdown& cd = countdowns_[index];
                        if (!cd.end) return;
                        cd.secondsLeft = std::max(0.0, std::chrono::duration<double>(*cd.end - std::chrono::steady_clock::now()).count());
                        cd.end.reset();
                        rebuild();
                    })));
                }
                ui::Append(timer, big(ui::Button("Restart", start)));
            } else if (countdown.secondsLeft) {
                GtkWidget* clock = ui::Text(clockText(*countdown.secondsLeft), ui::Kind::secondary);
                SetAttributes(clock, 1.7, false, false);
                gtk_widget_set_valign(clock, GTK_ALIGN_CENTER);
                ui::Append(timer, clock);
                ui::Append(timer, big(ui::Button("Resume", [this, index, rebuild] {
                    Countdown& cd = countdowns_[index];
                    if (!cd.secondsLeft) return;
                    cd.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(*cd.secondsLeft * 1000));
                    cd.secondsLeft.reset();
                    rebuild();
                })));
                ui::Append(timer, big(ui::Button("Restart", start)));
            } else {
                ui::Append(timer, big(ui::IconLabelButton("alarm-symbolic", "Start " + std::to_string(minutes) + "-minute timer", "", start)));
            }
            ui::Append(column, timer);
        }
        ui::Append(steps, column);
    }

    GtkWidget* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 900);
    adw_clamp_set_child(ADW_CLAMP(clamp), steps);
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // Redrawing keeps the place the reader has scrolled to.
    GtkWidget* old = adw_bin_get_child(ADW_BIN(content_));
    const double scrolled = old != nullptr
        ? gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(old))) : 0;
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), clamp);
    adw_bin_set_child(ADW_BIN(content_), scroller);
    if (scrolled > 0) {
        ui::Later([scroller, scrolled] {
            gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller)), scrolled);
        });
    }
}

// MARK: Help

// The user guide: the documents in a list, and the selected one beside it. A link to another document opens it in the
// same view, and a web link opens in the browser.
HelpWindow::HelpWindow(Host& host, std::function<void(HelpWindow*)> closed) {
    pages_ = helpPages(platform::HelpFolder());

    GtkWidget* content = nullptr;
    if (pages_.empty()) {
        content = adw_status_page_new();
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(content), "help-browser-symbolic");
        adw_status_page_set_title(ADW_STATUS_PAGE(content), "Help Is Missing");
        adw_status_page_set_description(ADW_STATUS_PAGE(content), "The documentation was not found in this copy of the app.");
    } else {
        markdown_ = std::make_unique<MarkdownView>([this](const std::string& target) {
            if (target.find("://") != std::string::npos) {
                platform::OpenUrl(GTK_WINDOW(window_), target);
                return;
            }
            for (std::size_t i = 0; i < pages_.size(); ++i) {
                if (pages_[i].id == target) {
                    gtk_list_box_select_row(GTK_LIST_BOX(list_), gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_), static_cast<int>(i)));
                    break;
                }
            }
        });
        list_ = gtk_list_box_new();
        ui::Style(list_, "navigation-sidebar");
        for (const HelpPage& page : pages_) {
            GtkWidget* label = ui::Text(page.title);
            ui::Margins(label, 6, 8, 6, 8);
            gtk_list_box_append(GTK_LIST_BOX(list_), label);
        }
        g_signal_connect_data(
            list_, "row-selected",
            G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
                if (row == nullptr) return;
                auto* self = static_cast<HelpWindow*>(data);
                const int index = gtk_list_box_row_get_index(row);
                if (index >= 0 && index < static_cast<int>(self->pages_.size())) {
                    self->Show(self->pages_[static_cast<std::size_t>(index)].id);
                }
            }),
            this, nullptr, static_cast<GConnectFlags>(0));
        GtkWidget* side = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side), list_);
        gtk_widget_set_size_request(side, 230, -1);

        content = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_set_start_child(GTK_PANED(content), side);
        gtk_paned_set_end_child(GTK_PANED(content), markdown_->Root());
        gtk_paned_set_shrink_start_child(GTK_PANED(content), FALSE);
        gtk_paned_set_resize_start_child(GTK_PANED(content), FALSE);
        gtk_paned_set_position(GTK_PANED(content), 230);
    }
    window_ = NewWindow(host, "Kettle Logic Help", 1000, 720, content);
    ReportClosed<HelpWindow>(window_, this, std::move(closed));
    if (!pages_.empty()) {
        gtk_list_box_select_row(GTK_LIST_BOX(list_), gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_), 0));
    }
    gtk_window_present(GTK_WINDOW(window_));
}

void HelpWindow::Show(const std::string& id) {
    if (shown_ == id) return;
    for (const HelpPage& page : pages_) {
        if (page.id != id) continue;
        shown_ = id;
        std::string markdown;
        try {
            markdown = platform::ReadTextFile(page.file);
        } catch (const std::exception&) {
        }
        markdown_->Show(markdown);
        return;
    }
}

}  // namespace kl
