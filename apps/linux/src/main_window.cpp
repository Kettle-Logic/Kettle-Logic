#include "main_window.hpp"

#include "kettlelogic/app/text.hpp"
#include "app_update_check.hpp"
#include "telemetry.hpp"

namespace kl {

using namespace kettlelogic::app;

namespace {

AppPaths MakePaths() {
    return AppPaths{platform::HomeFolder(), platform::DataFolder()};
}

const std::vector<platform::FileType>& RecipeTypes() {
    static const std::vector<platform::FileType> types = {{"BeerJSON recipes", {"*.json"}}};
    return types;
}

// Adds an action to the window's action map, and returns it.
GSimpleAction* AddAction(GtkWidget* window, const char* name, ui::Callback activate) {
    GSimpleAction* action = g_simple_action_new(name, nullptr);
    ui::OnAction(action, std::move(activate));
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(action));
    g_object_unref(action);
    return action;
}

void Accelerate(GtkApplication* application, const char* action, std::initializer_list<const char*> accels) {
    std::vector<const char*> list(accels);
    list.push_back(nullptr);
    gtk_application_set_accels_for_action(application, action, list.data());
}

const char* LibraryIcon(const LibraryPage& page) {
    switch (page.kind) {
        case LibraryPage::Kind::yours: return "avatar-default-symbolic";
        case LibraryPage::Kind::equipment: return "emblem-system-symbolic";
        case LibraryPage::Kind::ingredient:
            switch (page.ingredient) {
                case IngredientKind::fermentable: return "emblem-documents-symbolic";
                case IngredientKind::hop: return "user-bookmarks-symbolic";
                case IngredientKind::culture: return "applications-science-symbolic";
                case IngredientKind::water: return "weather-showers-symbolic";
                case IngredientKind::miscellaneous: return "view-more-symbolic";
            }
    }
    return "folder-symbolic";
}

// What the app adds to the system theme, installed once.
void InstallStyle() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, ".kl-current { background-color: alpha(@accent_bg_color, 0.18); }", -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

}  // namespace

// MARK: Window

MainWindow::MainWindow(AdwApplication* application)
    : application_(application), store_(MakePaths()), host_{store_, nullptr, {}, {}, {}, {}, {}} {
    InstallStyle();
    window_ = adw_application_window_new(GTK_APPLICATION(application));
    host_.window = GTK_WINDOW(window_);
    gtk_window_set_title(GTK_WINDOW(window_), "Kettle Logic");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1320, 860);
    gtk_widget_set_size_request(window_, 360, 400);

    host_.refresh = [this] { ScheduleRefresh(); };
    host_.openTool = [this](ToolKind kind, std::map<std::string, std::string> prefill) {
        OpenToolWindow(kind, std::move(prefill));
    };
    host_.openBrewDay = [this](const BrewDayPlan& plan) { OpenBrewDayWindow(plan); };
    host_.openHelp = [this] { OpenHelpWindow(); };
    host_.openFile = [this] { OpenFile(); };
    store_.changed = [this] { ScheduleRefresh(); };

    BuildActions();
    BuildLayout();

    autosaveSource_ = g_timeout_add(
        500,
        [](gpointer data) -> gboolean {
            static_cast<MainWindow*>(data)->store_.tick();
            return G_SOURCE_CONTINUE;
        },
        this);

    // Whatever is waiting for its autosave is saved before the window goes. A field being edited is committed first,
    // by taking the focus away from it.
    g_signal_connect_data(
        window_, "close-request",
        G_CALLBACK(+[](GtkWindow* window, gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            gtk_window_set_focus(window, nullptr);
            self->store_.tick(DocumentStore::Clock::now() + std::chrono::hours(1));
            // The other windows belong to this one, so closing it ends the app.
            g_application_quit(G_APPLICATION(self->application_));
            return FALSE;
        }),
        this, nullptr, static_cast<GConnectFlags>(0));

    store_.loadRecipeFolder();
    Refresh();
}

MainWindow::~MainWindow() {
    telemetry::Stop();
    *alive_ = false;
    if (refreshSource_ != 0) g_source_remove(refreshSource_);
    if (autosaveSource_ != 0) g_source_remove(autosaveSource_);
}

void MainWindow::Present() {
    gtk_window_present(GTK_WINDOW(window_));
    if (startupDialogsShown_) return;
    ui::Later([this, alive = alive_] {
        if (!*alive || startupDialogsShown_) return;
        startupDialogsShown_ = true;
        if (!store_.preferences.boolean(PrefKey::interfaceChosen)) {
            // The first-launch dialog asks about telemetry too, and starts it when it is answered.
            dialogs::ShowInterfaceChoice(host_);
        } else if (!store_.preferences.boolean(PrefKey::telemetryNoticeShown)) {
            // An upgrade from a version that never asked: tell the user once, and start when they have read it.
            dialogs::ShowTelemetryNotice(host_);
        } else {
            telemetry::Start(store_.preferences);
        }
        Refresh();
        if (store_.preferences.boolean(PrefKey::checkForUpdates)) {
            CheckForUpdates(false);
        }
    });
}

// A check the user asked for always says what it found. The automatic one at launch speaks only to offer an update.
void MainWindow::CheckForUpdates(bool asked) {
    update_check::CheckInBackground([this, alive = alive_, asked](updates::CheckResult result) {
        if (!*alive) return;
        namespace analytics = kettlelogic::app::analytics;
        const char* outcome = result.status == updates::CheckResult::Status::available   ? "available"
                              : result.status == updates::CheckResult::Status::upToDate ? "up_to_date"
                                                                                         : "failed";
        telemetry::Record(analytics::Event::updateCheck, {analytics::Param::word("result", outcome)});
        switch (result.status) {
        case updates::CheckResult::Status::available:
            update_check::Offer(window_, *result.release);
            break;
        case updates::CheckResult::Status::upToDate:
            if (asked) ui::ShowMessage(window_, "Kettle Logic Is Up to Date", "This is the newest version.");
            break;
        case updates::CheckResult::Status::failed:
            if (asked) ui::ShowMessage(window_, "Could Not Check for Updates", result.message);
            break;
        }
    });
}

void MainWindow::OpenFiles(const std::vector<std::filesystem::path>& files) {
    for (const auto& file : files) {
        try {
            store_.open(file);
        } catch (const std::exception& e) {
            store_.errorMessage = e.what();
        }
    }
    ScheduleRefresh();
}

// MARK: Actions

void MainWindow::BuildActions() {
    GtkApplication* app = GTK_APPLICATION(application_);

    AddAction(window_, "new-recipe", [this] { store_.newRecipe(); });
    AddAction(window_, "new-style", [this] { dialogs::ShowWizard(host_); });
    AddAction(window_, "open", [this] { OpenFile(); });
    saveAction_ = AddAction(window_, "save", [this] { Save(); });
    saveAsAction_ = AddAction(window_, "save-as", [this] { SaveAs(); });
    undoAction_ = AddAction(window_, "undo", [this] { store_.undo(); });
    redoAction_ = AddAction(window_, "redo", [this] { store_.redo(); });
    AddAction(window_, "settings", [this] { dialogs::ShowSettings(host_); });
    AddAction(window_, "help", [this] { OpenHelpWindow(); });
    AddAction(window_, "about", [this] { ShowAbout(); });
    AddAction(window_, "check-updates", [this] { CheckForUpdates(true); });
    AddAction(window_, "quit", [this] { gtk_window_close(GTK_WINDOW(window_)); });

    // Simple mode is a switch in the menu.
    simpleModeAction_ = g_simple_action_new_stateful("simple-mode", nullptr,
                                                     g_variant_new_boolean(store_.preferences.boolean(PrefKey::simpleMode)));
    g_signal_connect_data(
        simpleModeAction_, "activate",
        G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            const bool on = !g_variant_get_boolean(g_action_get_state(G_ACTION(action)));
            g_simple_action_set_state(action, g_variant_new_boolean(on));
            self->store_.preferences.setBoolean(PrefKey::simpleMode, on);
            self->ScheduleRefresh();
        }),
        this, nullptr, static_cast<GConnectFlags>(0));
    g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(simpleModeAction_));
    g_object_unref(simpleModeAction_);

    // The tools take the name of the tool as their target.
    GSimpleAction* tool = g_simple_action_new("tool", G_VARIANT_TYPE_STRING);
    g_signal_connect_data(
        tool, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer data) {
            const char* name = g_variant_get_string(parameter, nullptr);
            if (const auto kind = toolKindFromName(name)) static_cast<MainWindow*>(data)->OpenToolWindow(*kind, {});
        }),
        this, nullptr, static_cast<GConnectFlags>(0));
    g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(tool));
    g_object_unref(tool);

    // Ctrl+1 to Ctrl+9 go to a tab of the open recipe, or to a group of tabs when the tabs are grouped.
    for (int number = 1; number <= 9; ++number) {
        const std::string name = "tab-" + std::to_string(number);
        AddAction(window_, name.c_str(), [this, number] {
            if (store_.selection() && onTabShortcut_) onTabShortcut_(number);
        });
        const std::string accel = "<Control>" + std::to_string(number);
        Accelerate(app, ("win." + name).c_str(), {accel.c_str()});
    }

    Accelerate(app, "win.new-recipe", {"<Control>n"});
    Accelerate(app, "win.new-style", {"<Control><Shift>n"});
    Accelerate(app, "win.open", {"<Control>o"});
    Accelerate(app, "win.save", {"<Control>s"});
    Accelerate(app, "win.save-as", {"<Control><Shift>s"});
    Accelerate(app, "win.undo", {"<Control>z"});
    Accelerate(app, "win.redo", {"<Control>y", "<Control><Shift>z"});
    Accelerate(app, "win.simple-mode", {"<Control><Alt>m"});
    Accelerate(app, "win.settings", {"<Control>comma"});
    Accelerate(app, "win.help", {"F1"});
    Accelerate(app, "win.quit", {"<Control>q"});
}

GMenuModel* MainWindow::BuildMainMenu() {
    GMenu* menu = g_menu_new();

    GMenu* files = g_menu_new();
    g_menu_append(files, "New Recipe", "win.new-recipe");
    g_menu_append(files, "New Recipe from a Style…", "win.new-style");
    g_menu_append(files, "Open…", "win.open");
    g_menu_append(files, "Save As…", "win.save-as");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(files));
    g_object_unref(files);

    GMenu* tools = g_menu_new();
    const auto addTools = [&](GMenu* into, const std::vector<ToolKind>& kinds) {
        for (const ToolKind kind : kinds) {
            GMenuItem* item = g_menu_item_new(toolSpec(kind).title.c_str(), nullptr);
            g_menu_item_set_action_and_target_value(item, "win.tool", g_variant_new_string(toolKindName(kind).c_str()));
            g_menu_append_item(into, item);
            g_object_unref(item);
        }
    };
    GMenu* calculatorsMenu = g_menu_new();
    addTools(calculatorsMenu, calculators());
    GMenu* convertersMenu = g_menu_new();
    addTools(convertersMenu, converters());
    g_menu_append_submenu(tools, "Tools", G_MENU_MODEL(calculatorsMenu));
    g_menu_append_submenu(tools, "Unit Tools", G_MENU_MODEL(convertersMenu));
    g_object_unref(calculatorsMenu);
    g_object_unref(convertersMenu);
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(tools));
    g_object_unref(tools);

    GMenu* settings = g_menu_new();
    g_menu_append(settings, "Simple Mode", "win.simple-mode");
    g_menu_append(settings, "Settings", "win.settings");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(settings));
    g_object_unref(settings);

    GMenu* about = g_menu_new();
    g_menu_append(about, "Kettle Logic Help", "win.help");
    g_menu_append(about, "Check for Updates…", "win.check-updates");
    g_menu_append(about, "About Kettle Logic", "win.about");
    g_menu_append(about, "Quit", "win.quit");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(about));
    g_object_unref(about);
    return G_MENU_MODEL(menu);
}

// MARK: Layout

GtkWidget* MainWindow::BuildSidebar() {
    GtkWidget* header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("Recipes", ""));
    // One button adds a recipe or a folder, so the title keeps its room.
    GtkWidget* add = ui::MenuButton("list-add-symbolic", "", "Add a recipe or a folder", [this] {
        std::vector<ui::MenuLine> entries;
        entries.push_back(ui::MenuLine::Item("Blank Recipe", [this] { store_.newRecipe(); }));
        entries.push_back(ui::MenuLine::Item("Recipe from a Style…", [this] { dialogs::ShowWizard(host_); }));
        entries.push_back(ui::MenuLine::Separator());
        entries.push_back(ui::MenuLine::Item("New Folder…", [this] {
            const auto doc = store_.document();
            if (!doc) return;
            dialogs::ShowNameDialog(host_, "New Folder", "Create", "Folder name", "",
                                    [this, id = doc->id()](const std::string& name) { store_.createFolder(name, id); });
        }, store_.document() != nullptr));
        return entries;
    });
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), add);

    sidebarList_ = gtk_list_box_new();
    ui::Style(sidebarList_, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebarList_), GTK_SELECTION_SINGLE);
    g_signal_connect_data(
        sidebarList_, "row-activated",
        G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
            static_cast<MainWindow*>(data)->OnRowActivated(gtk_list_box_row_get_index(row));
        }),
        this, nullptr, static_cast<GConnectFlags>(0));
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), sidebarList_);

    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroller);
    return GTK_WIDGET(adw_navigation_page_new(toolbar, "Recipes"));
}

GtkWidget* MainWindow::BuildContent() {
    GtkWidget* header = adw_header_bar_new();
    windowTitle_ = adw_window_title_new("Kettle Logic", "");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), windowTitle_);

    undoButton_ = ui::IconButton("edit-undo-symbolic", "Undo", [this] { store_.undo(); });
    redoButton_ = ui::IconButton("edit-redo-symbolic", "Redo", [this] { store_.redo(); });
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), undoButton_);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), redoButton_);

    // The end of the bar, from the right: the main menu, then the recipe's actions.
    GtkWidget* menu = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu), "open-menu-symbolic");
    GMenuModel* model = BuildMainMenu();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu), model);
    g_object_unref(model);
    ui::SetTip(menu, "Main menu");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu);

    GtkWidget* remove = ui::IconButton("user-trash-symbolic", "Delete this recipe. It moves to Recently Deleted.", [this] {
        if (store_.selection()) store_.requestDelete(*store_.selection());
    });
    GtkWidget* duplicate = ui::IconButton("edit-copy-symbolic", "Make a copy of this recipe, after it in the list", [this] {
        if (store_.selection()) store_.duplicateRecipe(*store_.selection());
    });
    GtkWidget* exportMenu = ui::MenuButton("document-send-symbolic", "", "Export this recipe as a file", [this] {
        std::vector<ui::MenuLine> formats;
        for (const auto& [title, format] : std::vector<std::pair<std::string, std::string>>{
                 {"BeerXML…", "beerxml"}, {"Text…", "text"}, {"PDF…", "pdf"}, {"BeerJSON…", "beerjson"}}) {
            formats.push_back(ui::MenuLine::Item(title, [this, format = format] { ExportRecipe(format); }));
        }
        return formats;
    });
    GtkWidget* save = ui::IconButton("document-save-symbolic", "Save this document. Ctrl+S also saves.", [this] { Save(); });
    statusText_ = ui::Text("", ui::Kind::secondary);
    gtk_widget_set_valign(statusText_, GTK_ALIGN_CENTER);
    gtk_label_set_wrap(GTK_LABEL(statusText_), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(statusText_), PANGO_ELLIPSIZE_END);

    for (GtkWidget* widget : {remove, duplicate, exportMenu, save, statusText_}) {
        adw_header_bar_pack_end(ADW_HEADER_BAR(header), widget);
        recipeButtons_.push_back(widget);
    }

    detail_ = adw_bin_new();
    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), detail_);
    contentPage_ = GTK_WIDGET(adw_navigation_page_new(toolbar, "Kettle Logic"));
    return contentPage_;
}

void MainWindow::BuildLayout() {
    split_ = adw_navigation_split_view_new();
    adw_navigation_split_view_set_min_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split_), 240);
    adw_navigation_split_view_set_max_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split_), 340);
    adw_navigation_split_view_set_sidebar_width_fraction(ADW_NAVIGATION_SPLIT_VIEW(split_), 0.24);
    adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split_), ADW_NAVIGATION_PAGE(BuildSidebar()));
    adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split_), ADW_NAVIGATION_PAGE(BuildContent()));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), split_);

    // A narrow window shows the recipe list or the recipe, one at a time.
    AdwBreakpoint* breakpoint = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 720sp"));
    GValue collapsed = G_VALUE_INIT;
    g_value_init(&collapsed, G_TYPE_BOOLEAN);
    g_value_set_boolean(&collapsed, TRUE);
    adw_breakpoint_add_setter(breakpoint, G_OBJECT(split_), "collapsed", &collapsed);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window_), breakpoint);
}

// MARK: Refresh

void MainWindow::ScheduleRefresh() {
    if (refreshSource_ != 0) {
        return;
    }
    refreshSource_ = g_timeout_add(
        120,
        [](gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            self->refreshSource_ = 0;
            self->Refresh();
            return G_SOURCE_REMOVE;
        },
        this);
}

void MainWindow::Refresh() {
    UpdateChrome();
    RebuildSidebar();
    UpdateDetail();
    if (startupDialogsShown_) {
        PresentPending();
    }
}

void MainWindow::UpdateChrome() {
    const auto doc = store_.document();
    const bool hasDocument = doc != nullptr;
    g_simple_action_set_enabled(saveAction_, hasDocument);
    g_simple_action_set_enabled(saveAsAction_, hasDocument);

    const bool canUndo = hasDocument && doc->canUndo();
    const bool canRedo = hasDocument && doc->canRedo();
    g_simple_action_set_enabled(undoAction_, canUndo);
    g_simple_action_set_enabled(redoAction_, canRedo);
    ui::Enable(undoButton_, canUndo);
    ui::Enable(redoButton_, canRedo);
    ui::SetTip(undoButton_, canUndo && !doc->undoLabel().empty() ? "Undo " + doc->undoLabel() : "Undo");
    ui::SetTip(redoButton_, canRedo && !doc->redoLabel().empty() ? "Redo " + doc->redoLabel() : "Redo");
    g_simple_action_set_state(simpleModeAction_, g_variant_new_boolean(store_.preferences.boolean(PrefKey::simpleMode)));

    const model::Recipe* open = store_.recipe(store_.selection());
    const bool showsRecipe = hasDocument && open != nullptr;
    gtk_window_set_title(GTK_WINDOW(window_), open != nullptr ? (open->name + " \xE2\x80\x94 Kettle Logic").c_str() : "Kettle Logic");

    std::string title = "Kettle Logic";
    if (open != nullptr) {
        title = open->name;
    } else if (store_.library()) {
        title = store_.library()->title();
    }
    adw_window_title_set_title(ADW_WINDOW_TITLE(windowTitle_), title.c_str());
    adw_navigation_page_set_title(ADW_NAVIGATION_PAGE(contentPage_), title.c_str());
    for (GtkWidget* widget : recipeButtons_) gtk_widget_set_visible(widget, showsRecipe);
    if (showsRecipe) ui::SetText(statusText_, store_.saveStatus(*doc, store_.selection()->id));
}

// MARK: Sidebar

std::string MainWindow::SideRow::signature() const {
    return std::to_string(static_cast<int>(kind)) + "|" + text + "|" + std::to_string(indent) + "|" + (italic ? "i" : "") +
           (expanded ? "e" : "") + (trash ? "t" : "") + "|" + key.dragText() + "|" + std::to_string(page);
}

std::vector<MainWindow::SideRow> MainWindow::SideRows() const {
    std::vector<SideRow> rows;
    const auto recipeRow = [&](const RecipeDocument& doc, const model::Recipe& recipe, int indent) {
        SideRow row;
        row.kind = SideRow::Kind::recipe;
        row.key = ItemKey{doc.id(), recipe.id};
        const bool unsaved = doc.isUnsaved(recipe.id);
        row.text = unsaved ? recipe.name + "*" : recipe.name;
        row.italic = unsaved;
        row.indent = indent;
        row.icon = "text-x-generic-symbolic";
        rows.push_back(row);
    };

    for (const auto& doc : store_.documents()) {
        SideRow header;
        header.kind = SideRow::Kind::document;
        header.text = doc->displayName();
        header.key = ItemKey{doc->id(), 0};
        rows.push_back(header);

        // Recently Deleted is listed last, after the folders the user made.
        const auto folderRow = [&](const model::Folder& folder) {
            SideRow row;
            row.kind = SideRow::Kind::folder;
            row.key = ItemKey{doc->id(), folder.id};
            row.text = folder.name;
            row.trash = folder.trash;
            row.expanded = closedFolders_.count(row.key) == 0;
            row.icon = folder.trash ? "user-trash-symbolic" : row.expanded ? "folder-open-symbolic" : "folder-symbolic";
            rows.push_back(row);
            if (row.expanded) {
                for (const model::Recipe& recipe : doc->recipes()) {
                    if (recipe.folder == folder.id) recipeRow(*doc, recipe, 1);
                }
            }
        };
        for (const model::Folder& folder : doc->folders()) {
            if (!folder.trash) folderRow(folder);
        }
        for (const model::Folder& folder : doc->folders()) {
            if (folder.trash) folderRow(folder);
        }
        for (const model::Recipe& recipe : doc->recipes()) {
            if (!recipe.folder) recipeRow(*doc, recipe, 0);
        }
    }

    SideRow heading;
    heading.kind = SideRow::Kind::heading;
    heading.text = "Ingredient Directory";
    rows.push_back(heading);
    const auto& pages = LibraryPage::all();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        SideRow row;
        row.kind = SideRow::Kind::library;
        row.text = pages[i].title();
        row.icon = LibraryIcon(pages[i]);
        row.page = i;
        rows.push_back(row);
    }
    return rows;
}

std::vector<ui::MenuLine> MainWindow::RecipeMenu(const ItemKey& key) {
    std::vector<ui::MenuLine> menu;
    const auto doc = store_.document(key);
    const model::Recipe* recipe = store_.recipe(key);
    if (!doc || recipe == nullptr) return menu;

    // A recipe in Recently Deleted comes back to the top level, or to any folder the user picks.
    const bool deleted = doc->isDeleted(key.id);
    menu.push_back(ui::MenuLine::Item("Rename Recipe…", [this, key, name = recipe->name] {
        dialogs::ShowNameDialog(host_, "Rename Recipe", "Rename", "Recipe name", name,
                                [this, key](const std::string& renamed) { store_.renameRecipe(key, renamed); });
    }));
    menu.push_back(ui::MenuLine::Separator());
    if (deleted) {
        menu.push_back(ui::MenuLine::Item("Restore", [this, key] { store_.moveRecipe(key, std::nullopt); }));
    }
    std::vector<ui::MenuLine> move;
    move.push_back(ui::MenuLine::Item("Top Level", [this, key] { store_.moveRecipe(key, std::nullopt); }, recipe->folder.has_value()));
    for (const model::Folder& folder : doc->folders()) {
        if (folder.trash) continue;
        move.push_back(ui::MenuLine::Item(folder.name, [this, key, id = folder.id] { store_.moveRecipe(key, id); }, recipe->folder != folder.id));
    }
    menu.push_back(ui::MenuLine::Sub("Move to Folder", std::move(move)));
    menu.push_back(ui::MenuLine::Separator());
    if (!deleted) {
        menu.push_back(ui::MenuLine::Item("Duplicate Recipe", [this, key] { store_.duplicateRecipe(key); }));
    }
    // Deleting moves a recipe to Recently Deleted. Deleting one there removes it for good.
    menu.push_back(ui::MenuLine::Item(deleted ? "Delete Permanently" : "Delete Recipe", [this, key] { store_.requestDelete(key); }));
    return menu;
}

namespace {

struct DropData {
    MainWindow* window;
    std::function<void(const ItemKey&)> move;
    std::string document;
};

}  // namespace

// A recipe is dragged by its key, and dropped onto a folder or a document's name.
void MainWindow::MakeDropTarget(GtkWidget* target, const std::string& documentId, std::optional<EntityId> folder) {
    GtkDropTarget* drop = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_MOVE);
    auto* data = new DropData{this, [this, folder](const ItemKey& key) { store_.moveRecipe(key, folder); }, documentId};
    g_signal_connect_data(
        drop, "drop",
        G_CALLBACK(+[](GtkDropTarget*, const GValue* value, double, double, gpointer userData) -> gboolean {
            auto* data = static_cast<DropData*>(userData);
            if (!G_VALUE_HOLDS_STRING(value)) return FALSE;
            // Recipes stay in their own document.
            const auto key = ItemKey::fromDragText(g_value_get_string(value));
            if (!key || key->document != data->document) return FALSE;
            // Moving rebuilds the list the drop came from, so it waits until the drop has finished.
            ui::Later([data = *data, key = *key] { data.move(key); });
            return TRUE;
        }),
        data, [](gpointer data, GClosure*) { delete static_cast<DropData*>(data); }, static_cast<GConnectFlags>(0));
    gtk_widget_add_controller(target, GTK_EVENT_CONTROLLER(drop));
}

GtkWidget* MainWindow::MakeSideWidget(const SideRow& row) {
    GtkWidget* line = ui::Box(GTK_ORIENTATION_HORIZONTAL, 8);
    ui::Margins(line, 4 + row.indent * 18, 2, 4, 2);

    if (row.kind == SideRow::Kind::document) {
        GtkWidget* title = ui::Text(row.text, ui::Kind::caption);
        ui::Style(title, "heading");
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_label_set_wrap(GTK_LABEL(title), FALSE);
        ui::Margins(line, 4, 10, 4, 2);
        ui::Append(line, title);
        // Dropping a recipe on the document's name moves it to the top level of that document.
        MakeDropTarget(line, row.key.document, std::nullopt);
        return line;
    }
    if (row.kind == SideRow::Kind::heading) {
        GtkWidget* title = ui::Text(row.text, ui::Kind::caption);
        ui::Style(title, "heading");
        ui::Margins(line, 4, 14, 4, 2);
        ui::Append(line, title);
        return line;
    }

    if (row.icon != nullptr) ui::Append(line, gtk_image_new_from_icon_name(row.icon));
    GtkWidget* label = ui::Text(row.text);
    gtk_label_set_wrap(GTK_LABEL(label), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);
    if (row.italic) {
        PangoAttrList* attributes = pango_attr_list_new();
        pango_attr_list_insert(attributes, pango_attr_style_new(PANGO_STYLE_ITALIC));
        gtk_label_set_attributes(GTK_LABEL(label), attributes);
        pango_attr_list_unref(attributes);
    }
    ui::Append(line, label);

    if (row.kind == SideRow::Kind::recipe) {
        const ItemKey key = row.key;
        ui::ContextMenu(line, [this, key] { return RecipeMenu(key); });
        GtkDragSource* source = gtk_drag_source_new();
        gtk_drag_source_set_actions(source, GDK_ACTION_MOVE);
        g_signal_connect_data(
            source, "prepare",
            G_CALLBACK(+[](GtkDragSource*, double, double, gpointer data) -> GdkContentProvider* {
                return gdk_content_provider_new_typed(G_TYPE_STRING, static_cast<std::string*>(data)->c_str());
            }),
            new std::string(key.dragText()), [](gpointer data, GClosure*) { delete static_cast<std::string*>(data); },
            static_cast<GConnectFlags>(0));
        gtk_widget_add_controller(line, GTK_EVENT_CONTROLLER(source));
    } else if (row.kind == SideRow::Kind::folder) {
        const ItemKey key = row.key;
        const std::string name = row.text;
        // Recently Deleted is kept by the app, so it has no menu to rename or delete it.
        if (!row.trash) {
            ui::ContextMenu(line, [this, key, name] {
                return std::vector<ui::MenuLine>{
                    ui::MenuLine::Item("Rename Folder…", [this, key, name] {
                        dialogs::ShowNameDialog(host_, "Rename Folder", "Rename", "Folder name", name,
                                                [this, key](const std::string& renamed) { store_.renameFolder(key, renamed); });
                    }),
                    ui::MenuLine::Item("Delete Folder", [this, key] { store_.deleteFolder(key); })};
            });
        }
        MakeDropTarget(line, key.document, key.id);
        GtkWidget* arrow = gtk_image_new_from_icon_name(row.expanded ? "pan-down-symbolic" : "pan-end-symbolic");
        ui::Append(line, arrow);
    }
    return line;
}

void MainWindow::RebuildSidebar() {
    std::vector<SideRow> rows = SideRows();
    std::string signature;
    for (const SideRow& row : rows) signature += row.signature() + "\n";

    rebuildingSidebar_ = true;
    if (signature != sideSignature_) {
        sideSignature_ = signature;
        sideRows_ = std::move(rows);
        gtk_list_box_remove_all(GTK_LIST_BOX(sidebarList_));
        for (const SideRow& row : sideRows_) {
            GtkWidget* line = MakeSideWidget(row);
            GtkWidget* item = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item), line);
            const bool choosable = row.kind == SideRow::Kind::recipe || row.kind == SideRow::Kind::library ||
                                   row.kind == SideRow::Kind::folder;
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(item), row.kind == SideRow::Kind::recipe || row.kind == SideRow::Kind::library);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(item), choosable);
            gtk_list_box_append(GTK_LIST_BOX(sidebarList_), item);
        }
    }

    // The open recipe or page is the selected row.
    GtkListBoxRow* wanted = nullptr;
    for (std::size_t i = 0; i < sideRows_.size(); ++i) {
        const SideRow& row = sideRows_[i];
        const bool match = (store_.selection() && row.kind == SideRow::Kind::recipe && row.key == *store_.selection()) ||
                           (!store_.selection() && store_.library() && row.kind == SideRow::Kind::library &&
                            LibraryPage::all()[row.page] == *store_.library());
        if (match) wanted = gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebarList_), static_cast<int>(i));
    }
    if (wanted != nullptr) {
        gtk_list_box_select_row(GTK_LIST_BOX(sidebarList_), wanted);
    } else {
        gtk_list_box_unselect_all(GTK_LIST_BOX(sidebarList_));
    }
    rebuildingSidebar_ = false;
}

void MainWindow::OnRowActivated(int index) {
    if (rebuildingSidebar_ || index < 0 || index >= static_cast<int>(sideRows_.size())) {
        return;
    }
    const SideRow row = sideRows_[static_cast<std::size_t>(index)];
    switch (row.kind) {
        case SideRow::Kind::recipe:
            store_.select(row.key);
            adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split_), TRUE);
            break;
        case SideRow::Kind::library:
            store_.showLibrary(LibraryPage::all()[row.page]);
            adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split_), TRUE);
            break;
        case SideRow::Kind::folder:
            // A folder only opens and closes.
            if (row.expanded) closedFolders_.insert(row.key);
            else closedFolders_.erase(row.key);
            ScheduleRefresh();
            break;
        case SideRow::Kind::document:
        case SideRow::Kind::heading:
            break;
    }
}

// MARK: Detail

void MainWindow::UpdateDetail() {
    std::string wanted;
    if (store_.library()) {
        wanted = "library:" + store_.library()->title();
    } else if (store_.selection() && store_.recipe(store_.selection())) {
        wanted = "recipe:" + store_.selection()->dragText();
    } else {
        wanted = "welcome";
    }

    if (wanted != viewKey_ || !view_) {
        viewKey_ = wanted;
        onTabShortcut_ = nullptr;
        std::unique_ptr<View> next;
        if (store_.library()) {
            next = store_.library()->kind == LibraryPage::Kind::equipment ? MakeEquipmentDirectoryView(host_)
                                                                           : MakeDirectoryView(host_, *store_.library());
        } else if (wanted == "welcome") {
            next = MakeWelcomeView(host_);
        } else {
            next = MakeRecipeView(host_, *store_.selection());
            if (auto* recipeView = dynamic_cast<TabbedView*>(next.get())) {
                onTabShortcut_ = [recipeView](int number) { recipeView->SelectTab(number); };
            }
        }
        // The old screen's widgets go first, then the screen itself.
        adw_bin_set_child(ADW_BIN(detail_), next->Root());
        view_ = std::move(next);
    } else {
        view_->Refresh();
    }
}

void MainWindow::PresentPending() {
    if (store_.errorMessage) {
        const std::string message = *store_.errorMessage;
        store_.errorMessage.reset();
        ui::ShowMessage(GTK_WIDGET(window_), "Kettle Logic", message);
    }
    if (store_.pendingDeletion) {
        const ItemKey key = *store_.pendingDeletion;
        const model::Recipe* recipe = store_.recipe(key);
        const std::string name = recipe ? recipe->name : "this recipe";
        const auto doc = store_.document(key);
        const bool forGood = doc && doc->isDeleted(key.id);
        // Showing it clears the request, so a refresh while the question is open does not ask again.
        store_.pendingDeletion.reset();
        ui::ShowConfirm(window_, "Delete \xE2\x80\x9C" + name + "\xE2\x80\x9D?",
                        forGood ? "It is removed for good." : "It moves to Recently Deleted, where you can restore it.",
                        forGood ? "Delete Permanently" : "Delete Recipe", [this, key] { store_.deleteRecipe(key); });
    }
}

// MARK: Files

void MainWindow::OpenFile() {
    platform::OpenFile(GTK_WINDOW(window_), store_.recipeLibrary.folder(), RecipeTypes(),
                       "Open a BeerJSON recipe. Its sidecar file is opened with it.",
                       [this, alive = alive_](std::optional<std::filesystem::path> file) {
                           if (!*alive || !file) return;
                           OpenFiles({*file});
                       });
}

void MainWindow::Save() {
    if (store_.save() == DocumentStore::SaveResult::needsFile) {
        SaveAs();
    }
}

void MainWindow::SaveAs() {
    if (!store_.document()) {
        return;
    }
    std::filesystem::path start;
    try {
        start = store_.recipeLibrary.ensureFolder();
    } catch (const std::exception&) {
        start = store_.recipeLibrary.folder();
    }
    platform::SaveFile(GTK_WINDOW(window_), start, store_.suggestedFileName() + ".json", RecipeTypes(),
                       [this, alive = alive_](std::optional<std::filesystem::path> file) {
                           if (!*alive || !file) return;
                           std::filesystem::path target = *file;
                           if (target.extension().empty()) target += ".json";
                           store_.saveAs(target);
                       });
}

void MainWindow::ExportRecipe(const std::string& format) {
    const auto doc = store_.document();
    const model::Recipe* recipe = store_.recipe(store_.selection());
    if (!doc || recipe == nullptr) {
        return;
    }

    struct Format {
        const char* name;
        const char* title;
        const char* extension;
        const char* engine;
    };
    static const Format formats[] = {
        {"beerxml", "BeerXML", "xml", "beerxml"},
        {"text", "Text", "txt", "text"},
        {"pdf", "PDF", "pdf", ""},
        {"beerjson", "BeerJSON", "json", "beerjson"},
    };
    const Format* chosen = nullptr;
    for (const Format& f : formats) {
        if (format == f.name) chosen = &f;
    }
    if (chosen == nullptr) {
        return;
    }

    // The recipe's name, made safe for a file name.
    const std::string fileName = RecipeLibrary::safeFileName(recipe->name) + "." + chosen->extension;
    const EntityId recipeId = recipe->id;
    const std::string extension = chosen->extension;
    const std::string engine = chosen->engine;
    platform::SaveFile(GTK_WINDOW(window_), store_.recipeLibrary.folder(), fileName,
                       {{chosen->title, {"*." + extension}}},
                       [this, alive = alive_, doc, recipeId, extension, engine, format](std::optional<std::filesystem::path> picked) {
                           if (!*alive || !picked) return;
                           try {
                               std::filesystem::path target = *picked;
                               if (target.extension().empty()) target += "." + extension;
                               if (format == "pdf") {
                                   platform::WriteBinaryFile(target, doc->exportRecipePdf(recipeId));
                               } else {
                                   platform::WriteTextFile(target, doc->exportRecipe(recipeId, engine));
                               }
                               telemetry::Record(analytics::Event::exportRecipe, {analytics::Param::word("format", format)});
                           } catch (const std::exception& e) {
                               store_.errorMessage = e.what();
                               ScheduleRefresh();
                           }
                       });
}

// MARK: Other windows

void MainWindow::OpenToolWindow(ToolKind kind, std::map<std::string, std::string> prefill) {
    telemetry::Record(analytics::Event::featureUsed, {analytics::Param::word("feature", "tool_" + toolKindName(kind))});
    auto window = std::make_unique<ToolWindow>(host_, kind, std::move(prefill), [this, alive = alive_](ToolWindow* closed) {
        if (!*alive) return;
        toolWindows_.erase(std::remove_if(toolWindows_.begin(), toolWindows_.end(),
                                          [closed](const auto& w) { return w.get() == closed; }),
                           toolWindows_.end());
    });
    toolWindows_.push_back(std::move(window));
}

void MainWindow::OpenBrewDayWindow(const BrewDayPlan& plan) {
    auto window = std::make_unique<BrewDayWindow>(host_, plan, [this, alive = alive_](BrewDayWindow* closed) {
        if (!*alive) return;
        brewDayWindows_.erase(std::remove_if(brewDayWindows_.begin(), brewDayWindows_.end(),
                                             [closed](const auto& w) { return w.get() == closed; }),
                              brewDayWindows_.end());
    });
    brewDayWindows_.push_back(std::move(window));
}

void MainWindow::OpenHelpWindow() {
    if (helpWindow_) {
        helpWindow_->Activate();
        return;
    }
    helpWindow_ = std::make_unique<HelpWindow>(host_, [this, alive = alive_](HelpWindow*) {
        if (*alive) helpWindow_.reset();
    });
}

void MainWindow::ShowAbout() {
    AdwDialog* about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Kettle Logic");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), "0.1.0");
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about), "Design and manage homebrew recipes.");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "Kettle Logic");
    adw_dialog_present(about, window_);
}

}  // namespace kl
