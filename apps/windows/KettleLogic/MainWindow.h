#pragma once

#include "Host.h"
#include "Views.h"

#include "kettlelogic/app/help_markdown.hpp"

namespace kl {

class ToolWindow;
class BrewDayWindow;
class HelpWindow;

// The main window: the menu bar, the navigation pane with the recipes and the ingredient directory, and the detail
// area with the open screen. It owns the app's state and every other window.
class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    void Activate();

private:
    void BuildMenu();
    void BuildLayout();
    void BuildToolbar();
    void AddAccelerators();

    // Draws the window again, after the state changed. Changes coalesce.
    void ScheduleRefresh();
    void Refresh();
    // Checks for a newer version and offers it. `asked` is true for a check from the menu, which always says the result.
    void CheckForUpdates(bool asked);
    void UpdateChrome();
    void RebuildSidebar();
    void UpdateDetail();
    void PresentPending();

    void OnSelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
    void OpenFile();
    void Save();
    void SaveAs();
    // Writes the selected recipe in a format to a file the user picks, or to a temporary file for the share sheet.
    void ExportRecipe(const std::string& format, bool share);

    winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem RecipeItem(const kettlelogic::app::RecipeDocument& doc,
                                                                    const kettlelogic::model::Recipe& recipe);
    void MakeDropTarget(winrt::Microsoft::UI::Xaml::UIElement const& target, const std::string& documentId,
                        std::optional<kettlelogic::app::EntityId> folder);

    void OpenToolWindow(kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill);
    void OpenBrewDayWindow(const kettlelogic::app::BrewDayPlan& plan);
    void OpenHelpWindow();

    winrt::Microsoft::UI::Xaml::Window window_;
    kettlelogic::app::DocumentStore store_;
    Host host_;

    // The window's parts.
    winrt::Microsoft::UI::Xaml::Controls::Grid root_;
    winrt::Microsoft::UI::Xaml::Controls::MenuBar menuBar_;
    winrt::Microsoft::UI::Xaml::Controls::NavigationView nav_;
    winrt::Microsoft::UI::Xaml::Controls::CommandBar toolbar_;
    winrt::Microsoft::UI::Xaml::Controls::ContentControl detail_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;
    winrt::Microsoft::UI::Xaml::Controls::Button newFolderButton_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton undoButton_;
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton redoButton_;
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton saveButton_;
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton shareButton_;
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton duplicateButton_;
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton deleteButton_;

    winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem saveItem_;
    winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem saveAsItem_;
    winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem undoItem_;
    winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem redoItem_;
    winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem simpleModeItem_;

    std::unique_ptr<View> view_;
    std::function<void(int)> onTabShortcut_;
    std::string viewKey_;
    std::set<kettlelogic::app::ItemKey> closedFolders_;
    bool rebuildingSidebar_ = false;
    bool refreshPending_ = false;
    bool startupDialogsShown_ = false;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer refreshTimer_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer autosaveTimer_{nullptr};

    std::vector<std::unique_ptr<ToolWindow>> toolWindows_;
    std::vector<std::unique_ptr<BrewDayWindow>> brewDayWindows_;
    std::unique_ptr<HelpWindow> helpWindow_;
};

// A window of its own for a calculator or a converter.
class ToolWindow {
public:
    ToolWindow(Host& host, kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill,
               std::function<void(ToolWindow*)> closed);
    void Activate() { window_.Activate(); }

private:
    winrt::Microsoft::UI::Xaml::Window window_;
    ToolView view_;
};

// The brew-day checklist for one generated procedure.
class BrewDayWindow {
public:
    BrewDayWindow(Host& host, kettlelogic::app::BrewDayPlan plan, std::function<void(BrewDayWindow*)> closed);
    ~BrewDayWindow();
    void Activate() { window_.Activate(); }

private:
    void Rebuild();
    void Tick();

    Host& host_;
    kettlelogic::app::BrewDayPlan plan_;
    winrt::Microsoft::UI::Xaml::Window window_;
    winrt::Microsoft::UI::Xaml::Controls::ContentControl content_;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    platform::DisplayLock displayLock_;
    std::set<int> done_;

    // The countdown of a step: when it ends while it runs, or the seconds left while it is paused.
    struct Countdown {
        std::optional<std::chrono::steady_clock::time_point> end;
        std::optional<double> secondsLeft;
        bool finished = false;
    };
    std::map<int, Countdown> countdowns_;
    std::map<int, winrt::Microsoft::UI::Xaml::Controls::TextBlock> clocks_;
};

// The user guide, from the Help folder beside the app.
class HelpWindow {
public:
    HelpWindow(Host& host, std::function<void(HelpWindow*)> closed);
    ~HelpWindow();
    void Activate() { window_.Activate(); }
    // Shows the pages in the app's color theme.
    void ApplyTheme();

private:
    winrt::fire_and_forget Start();
    void Show(const std::string& id);

    Host& host_;
    winrt::Microsoft::UI::Xaml::Window window_;
    std::vector<kettlelogic::app::HelpPage> pages_;
    winrt::Microsoft::UI::Xaml::Controls::ListView list_;
    winrt::Microsoft::UI::Xaml::Controls::WebView2 web_;
    std::string shown_;
    bool ready_ = false;
    std::string pending_;
    // Cleared when the window goes, so a coroutine that is still waiting does not touch it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace kl
