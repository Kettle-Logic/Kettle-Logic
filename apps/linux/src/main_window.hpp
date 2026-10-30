#pragma once

#include "aux_windows.hpp"
#include "dialogs.hpp"
#include "host.hpp"
#include "views.hpp"

#include <set>

namespace kl {

// The main window: the sidebar with the recipes and the ingredient directory, the header bar's actions, and the detail
// area with the open screen. It owns the app's state and every other window.
class MainWindow {
public:
    explicit MainWindow(AdwApplication* application);
    ~MainWindow();

    void Present();
    // Checks for a newer version, and offers it. `asked` is true for a check from the menu, which always says the result.
    void CheckForUpdates(bool asked);
    // The app's state and window, for the smoke test.
    kettlelogic::app::DocumentStore& store() { return store_; }
    GtkWindow* window() const { return GTK_WINDOW(window_); }
    // Opens recipe files from the command line or the file manager.
    void OpenFiles(const std::vector<std::filesystem::path>& files);

private:
    void BuildActions();
    void BuildLayout();
    GtkWidget* BuildSidebar();
    GtkWidget* BuildContent();
    GMenuModel* BuildMainMenu();

    // Draws the window again, after the state changed. Changes coalesce.
    void ScheduleRefresh();
    void Refresh();
    void UpdateChrome();
    void RebuildSidebar();
    void UpdateDetail();
    void PresentPending();

    void OpenFile();
    void Save();
    void SaveAs();
    void ExportRecipe(const std::string& format);

    void OpenToolWindow(kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill);
    void OpenBrewDayWindow(const kettlelogic::app::BrewDayPlan& plan);
    void OpenHelpWindow();
    void ShowAbout();

    // One line of the sidebar.
    struct SideRow {
        enum class Kind { document, folder, recipe, heading, library } kind = Kind::heading;
        std::string text;
        const char* icon = nullptr;
        int indent = 0;
        bool italic = false;
        bool expanded = false;
        bool trash = false;                            // the Recently Deleted folder
        kettlelogic::app::ItemKey key;                 // the recipe or folder, or the document for a heading
        std::size_t page = 0;                          // the directory page of a library line
        bool hasFolderMenu = false;
        std::string signature() const;
    };
    std::vector<SideRow> SideRows() const;
    GtkWidget* MakeSideWidget(const SideRow& row);
    std::vector<ui::MenuLine> RecipeMenu(const kettlelogic::app::ItemKey& key);
    void MakeDropTarget(GtkWidget* target, const std::string& documentId, std::optional<kettlelogic::app::EntityId> folder);
    void OnRowActivated(int index);

    AdwApplication* application_;
    kettlelogic::app::DocumentStore store_;
    Host host_;
    GtkWidget* window_;

    // The window's parts.
    GtkWidget* split_ = nullptr;
    GtkWidget* sidebarList_ = nullptr;
    GtkWidget* detail_ = nullptr;
    GtkWidget* contentPage_ = nullptr;
    GtkWidget* windowTitle_ = nullptr;
    GtkWidget* statusText_ = nullptr;
    GtkWidget* undoButton_ = nullptr;
    GtkWidget* redoButton_ = nullptr;
    std::vector<GtkWidget*> recipeButtons_;   // save, export, duplicate, delete: shown while a recipe is open
    GSimpleAction* saveAction_ = nullptr;
    GSimpleAction* saveAsAction_ = nullptr;
    GSimpleAction* undoAction_ = nullptr;
    GSimpleAction* redoAction_ = nullptr;
    GSimpleAction* simpleModeAction_ = nullptr;

    std::unique_ptr<View> view_;
    std::function<void(int)> onTabShortcut_;
    std::string viewKey_;
    std::vector<SideRow> sideRows_;
    std::string sideSignature_;
    std::set<kettlelogic::app::ItemKey> closedFolders_;
    bool rebuildingSidebar_ = false;
    bool startupDialogsShown_ = false;
    guint refreshSource_ = 0;
    guint autosaveSource_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::vector<std::unique_ptr<ToolWindow>> toolWindows_;
    std::vector<std::unique_ptr<BrewDayWindow>> brewDayWindows_;
    std::unique_ptr<HelpWindow> helpWindow_;
};

}  // namespace kl
