#pragma once

#include "host.hpp"
#include "markdown_view.hpp"
#include "views.hpp"

#include "kettlelogic/app/help_markdown.hpp"

#include <set>

namespace kl {

// A window of its own for a calculator or a converter.
class ToolWindow {
public:
    ToolWindow(Host& host, kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill,
               std::function<void(ToolWindow*)> closed);
    void Activate() { gtk_window_present(GTK_WINDOW(window_)); }

private:
    GtkWidget* window_;
    ToolView view_;
};

// The brew-day checklist for one generated procedure.
class BrewDayWindow {
public:
    BrewDayWindow(Host& host, kettlelogic::app::BrewDayPlan plan, std::function<void(BrewDayWindow*)> closed);
    ~BrewDayWindow();
    void Activate() { gtk_window_present(GTK_WINDOW(window_)); }

private:
    void Rebuild();
    void Tick();

    Host& host_;
    kettlelogic::app::BrewDayPlan plan_;
    GtkWidget* window_;
    GtkWidget* content_;
    guint timer_ = 0;
    platform::DisplayLock displayLock_;
    std::set<int> done_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    // The countdown of a step: when it ends while it runs, or the seconds left while it is paused.
    struct Countdown {
        std::optional<std::chrono::steady_clock::time_point> end;
        std::optional<double> secondsLeft;
        bool finished = false;
    };
    std::map<int, Countdown> countdowns_;
    std::map<int, GtkWidget*> clocks_;
};

// The user guide, from the Help folder beside the app.
class HelpWindow {
public:
    HelpWindow(Host& host, std::function<void(HelpWindow*)> closed);
    void Activate() { gtk_window_present(GTK_WINDOW(window_)); }

private:
    void Show(const std::string& id);

    GtkWidget* window_;
    std::vector<kettlelogic::app::HelpPage> pages_;
    std::unique_ptr<MarkdownView> markdown_;
    GtkWidget* list_ = nullptr;
    std::string shown_;
};

}  // namespace kl
