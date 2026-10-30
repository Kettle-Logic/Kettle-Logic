// Opens the app's windows and drives its screens, so a crash or a GTK warning shows up. It checks that nothing
// crashes or logs a warning; the engine and the app logic have their own tests. It needs a display.
//
// Set KETTLELOGIC_SMOKE_SHOTS to a folder to save a PNG of each screen it visits.

#include "app_update_check.hpp"
#include "main_window.hpp"

#include "kettlelogic/app/recipe_builder.hpp"
#include "kettlelogic/app/templates.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace kettlelogic::app;

namespace {

std::vector<std::string> gProblems;
std::string gShots;
int gChecked = 0;

void LogHandler(const gchar* domain, GLogLevelFlags level, const gchar* message, gpointer) {
    const std::string text = message != nullptr ? message : "";
    // macOS reports skipped frames when a window is not on screen. It says nothing about the app.
    if (text.find("skipped frame") != std::string::npos) return;
    std::string line = std::string(domain != nullptr ? domain : "") + ": " + text;
    if (level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_ERROR)) {
        gProblems.push_back(line);
        std::fprintf(stderr, "PROBLEM %s\n", line.c_str());
    }
}

// Lets the app run: timers fire, rebuilds happen, and widgets settle.
void Spin(int milliseconds) {
    GTimer* timer = g_timer_new();
    while (g_timer_elapsed(timer, nullptr) * 1000 < milliseconds) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        g_usleep(2000);
    }
    g_timer_destroy(timer);
}

void Collect(GtkWidget* root, const std::function<bool(GtkWidget*)>& match, std::vector<GtkWidget*>& out) {
    if (match(root)) out.push_back(root);
    for (GtkWidget* child = gtk_widget_get_first_child(root); child != nullptr; child = gtk_widget_get_next_sibling(child)) {
        Collect(child, match, out);
    }
}

std::vector<GtkWidget*> Find(GtkWidget* root, const std::function<bool(GtkWidget*)>& match) {
    std::vector<GtkWidget*> out;
    Collect(root, match, out);
    return out;
}

template <typename T>
std::vector<GtkWidget*> FindType(GtkWidget* root, GType type) {
    return Find(root, [type](GtkWidget* w) { return G_TYPE_CHECK_INSTANCE_TYPE(w, type); });
}

std::string LabelOf(GtkWidget* button) {
    if (const char* label = GTK_IS_BUTTON(button) ? gtk_button_get_label(GTK_BUTTON(button)) : nullptr) return label;
    // A button with an icon and a label holds the label somewhere inside it.
    const auto labels = Find(button, [](GtkWidget* w) { return GTK_IS_LABEL(w); });
    return labels.empty() ? "" : gtk_label_get_text(GTK_LABEL(labels.front()));
}

GtkWidget* ButtonNamed(GtkWidget* root, const std::string& label) {
    for (GtkWidget* button : FindType<void>(root, GTK_TYPE_BUTTON)) {
        if (LabelOf(button) == label && gtk_widget_get_mapped(button)) return button;
    }
    return nullptr;
}

bool Click(GtkWidget* root, const std::string& label) {
    GtkWidget* button = ButtonNamed(root, label);
    if (button == nullptr) return false;
    g_signal_emit_by_name(button, "clicked");
    return true;
}

// Saves what a window shows, when asked to.
void Shot(GtkWindow* window, const std::string& name) {
    ++gChecked;
    if (gShots.empty()) return;
    GtkWidget* widget = GTK_WIDGET(window);
    const int width = gtk_widget_get_width(widget);
    const int height = gtk_widget_get_height(widget);
    if (width <= 0 || height <= 0) return;
    GdkPaintable* paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot* snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, width, height);
    GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
    GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
    if (node != nullptr && renderer != nullptr) {
        graphene_rect_t bounds;
        graphene_rect_init(&bounds, 0, 0, static_cast<float>(width), static_cast<float>(height));
        if (GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &bounds)) {
            const std::string path = gShots + "/" + name + ".png";
            gdk_texture_save_to_png(texture, path.c_str());
            g_object_unref(texture);
        }
    }
    if (node != nullptr) gsk_render_node_unref(node);
    g_object_unref(paintable);
}

std::vector<GtkWindow*> OtherWindows(GtkWindow* main) {
    std::vector<GtkWindow*> out;
    GListModel* list = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(list); ++i) {
        GtkWindow* window = GTK_WINDOW(g_list_model_get_item(list, i));
        if (window != main) out.push_back(window);
        g_object_unref(window);
    }
    return out;
}

void CloseOthers(GtkWindow* main) {
    for (GtkWindow* window : OtherWindows(main)) gtk_window_destroy(window);
    Spin(100);
}

// Closes the dialog that is showing by pressing the named button.
void CloseDialog(GtkWindow* main, const std::string& button) {
    if (!Click(GTK_WIDGET(main), button)) {
        gProblems.push_back("no " + button + " button to close the dialog");
    }
    Spin(250);
}

std::vector<std::string> TabNames(GtkWindow* window) {
    std::vector<std::string> names;
    for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
        if (gtk_widget_has_css_class(button, "kl-tab") && gtk_widget_get_mapped(button)) names.push_back(LabelOf(button));
    }
    return names;
}

// Changes a few of the controls on the screen, one at a time, as a user would.
void Poke(GtkWindow* window, int limit) {
    GtkWidget* root = GTK_WIDGET(window);
    const auto entries = [&] {
        return Find(root, [](GtkWidget* w) {
            return GTK_IS_ENTRY(w) && gtk_widget_get_mapped(w) && gtk_widget_get_sensitive(w) && !GTK_IS_SEARCH_ENTRY(w);
        });
    };
    for (int k = 0; k < limit; ++k) {
        const auto found = entries();
        if (k >= static_cast<int>(found.size())) break;
        gtk_editable_set_text(GTK_EDITABLE(found[static_cast<std::size_t>(k)]), "7");
        g_signal_emit_by_name(found[static_cast<std::size_t>(k)], "activate");
        Spin(170);
    }
    const auto switches = [&] { return FindType<void>(root, GTK_TYPE_SWITCH); };
    for (int k = 0; k < limit; ++k) {
        const auto found = switches();
        if (k >= static_cast<int>(found.size())) break;
        GtkSwitch* toggle = GTK_SWITCH(found[static_cast<std::size_t>(k)]);
        gtk_switch_set_active(toggle, !gtk_switch_get_active(toggle));
        Spin(170);
    }
    const auto drops = [&] { return Find(root, [](GtkWidget* w) { return GTK_IS_DROP_DOWN(w) && gtk_widget_get_mapped(w); }); };
    for (int k = 0; k < limit; ++k) {
        const auto found = drops();
        if (k >= static_cast<int>(found.size())) break;
        GtkDropDown* drop = GTK_DROP_DOWN(found[static_cast<std::size_t>(k)]);
        const guint count = g_list_model_get_n_items(gtk_drop_down_get_model(drop));
        if (count < 2) continue;
        gtk_drop_down_set_selected(drop, (gtk_drop_down_get_selected(drop) + 1) % count);
        Spin(170);
    }
}

// Writes the style editor's fields: the name, then the low and high end of each range, in the order the editor
// shows them. The editor is the last thing in the window, so its entries are the last mapped ones.
void FillStyleEditor(GtkWindow* window, const std::string& name, const std::vector<std::string>& ends) {
    const auto entries = Find(GTK_WIDGET(window), [](GtkWidget* w) {
        return GTK_IS_ENTRY(w) && gtk_widget_get_mapped(w) && !GTK_IS_SEARCH_ENTRY(w);
    });
    const std::size_t count = ends.size() + 1;
    if (entries.size() < count) {
        gProblems.push_back("the style editor has only " + std::to_string(entries.size()) + " fields");
        return;
    }
    const std::size_t first = entries.size() - count;
    gtk_editable_set_text(GTK_EDITABLE(entries[first]), name.c_str());
    for (std::size_t i = 0; i < ends.size(); ++i) {
        gtk_editable_set_text(GTK_EDITABLE(entries[first + 1 + i]), ends[i].c_str());
    }
    Spin(200);
}

// Your styles: a new style is made in the editor, chosen for the recipe, edited, and deleted.
void YourStyles(GtkWindow* window) {
    for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
        if (gtk_widget_has_css_class(button, "kl-tab") && LabelOf(button) == "Style") gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    Spin(300);
    if (!Click(GTK_WIDGET(window), "New Style\xE2\x80\xA6")) {
        gProblems.push_back("no New Style button on the Style tab");
        return;
    }
    Spin(350);
    Shot(window, "style-editor-new");
    FillStyleEditor(window, "House Pale", {"1.040", "1.050", "1.008", "1.012", "20", "30", "4", "7", "4", "5.5"});
    if (!Click(GTK_WIDGET(window), "Save")) gProblems.push_back("no Save button on the style editor");
    Spin(350);
    if (ButtonNamed(GTK_WIDGET(window), "House Pale") == nullptr) {
        gProblems.push_back("the new style is not listed under Your styles");
        return;
    }
    Shot(window, "style-chosen");

    // Edit it, then delete it.
    Click(GTK_WIDGET(window), "House Pale");
    Spin(350);
    Shot(window, "style-editor-edit");
    if (!Click(GTK_WIDGET(window), "Delete")) gProblems.push_back("no Delete button on the style editor");
    Spin(350);
    if (ButtonNamed(GTK_WIDGET(window), "House Pale") != nullptr) gProblems.push_back("the deleted style is still listed");
}

// Goes through every tab of the open recipe.
void WalkTabs(GtkWindow* window, const std::string& prefix, int pokes) {
    const std::vector<std::string> names = TabNames(window);
    if (names.size() < 3) gProblems.push_back(prefix + ": the recipe has only " + std::to_string(names.size()) + " tabs");
    for (const std::string& name : names) {
        GtkWidget* target = nullptr;
        for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
            if (LabelOf(button) == name) target = button;
        }
        if (target == nullptr) {
            gProblems.push_back(prefix + ": tab " + name + " went missing");
            continue;
        }
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(target), TRUE);
        Spin(300);
        Shot(window, prefix + "-" + name);
        Poke(window, pokes);
    }
}

void Run(kl::MainWindow& app) {
    GtkWindow* window = app.window();
    DocumentStore& store = app.store();
    Spin(500);
    Shot(window, "00-first-launch");
    if (!Click(GTK_WIDGET(window), "Continue")) gProblems.push_back("the first-launch choice did not appear");
    Spin(300);
    Shot(window, "01-welcome");

    // A recipe from the first style, in full mode.
    store.preferences.setBoolean(PrefKey::simpleMode, false);
    const Units units{store.preferences.units()};
    const EquipmentProfile system = store.equipment.profile(store.preferences.defaultEquipmentName());
    store.newRecipe(RecipeBuildRequest{RecipeTemplate::all()[0], units.volumeTextFromLiters(system.batchLiters), system, units.system});
    Spin(500);
    Shot(window, "02-recipe");
    WalkTabs(window, "full", 3);

    // The same in simple mode, then with grouped tabs.
    g_action_group_activate_action(G_ACTION_GROUP(window), "simple-mode", nullptr);
    Spin(400);
    WalkTabs(window, "simple", 1);
    store.preferences.setBoolean(PrefKey::groupedTabs, true);
    store.preferences.setBoolean(PrefKey::simpleMode, false);
    app.store().changed();
    Spin(400);
    for (int n = 1; n <= 4; ++n) {
        g_action_group_activate_action(G_ACTION_GROUP(window), ("tab-" + std::to_string(n)).c_str(), nullptr);
        Spin(300);
        Shot(window, "grouped-" + std::to_string(n));
    }
    store.preferences.setBoolean(PrefKey::groupedTabs, false);
    app.store().changed();
    Spin(300);

    // The ingredient editors.
    for (const char* tab : {"Fermentables", "Hops", "Yeast & Cultures", "Water", "Miscellaneous"}) {
        for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
            if (LabelOf(button) == tab) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
        }
        Spin(300);
        if (!Click(GTK_WIDGET(window), "Add")) {
            gProblems.push_back(std::string("no Add button on the ") + tab + " tab");
            continue;
        }
        Spin(350);
        Shot(window, std::string("editor-") + tab);
        // The common-ingredients menu fills in the form.
        CloseDialog(window, "Cancel");
    }

    // The ingredient picker, from a long list: search for a malt, then pick the match with Enter.
    for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
        if (LabelOf(button) == "Fermentables") gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    Spin(300);
    Click(GTK_WIDGET(window), "Add");
    Spin(350);
    if (!Click(GTK_WIDGET(window), "Choose\xE2\x80\xA6")) {
        gProblems.push_back("no Choose button on the fermentable editor");
    }
    Spin(350);
    const auto searches = Find(GTK_WIDGET(window), [](GtkWidget* w) {
        return GTK_IS_SEARCH_ENTRY(w) && std::string(gtk_widget_get_name(w)) == "ingredient-picker-search";
    });
    if (searches.empty()) {
        gProblems.push_back("the ingredient picker did not open");
    } else {
        Shot(window, "picker-open");
        gtk_editable_set_text(GTK_EDITABLE(searches.front()), "crystal 60");
        Spin(300);
        Shot(window, "picker-search");
        g_signal_emit_by_name(searches.front(), "activate");
        Spin(350);
        bool named = false;
        for (GtkWidget* entry : FindType<void>(GTK_WIDGET(window), GTK_TYPE_ENTRY)) {
            if (std::string(gtk_editable_get_text(GTK_EDITABLE(entry))) == "Crystal 60") named = true;
        }
        if (!named) gProblems.push_back("picking the first search match did not fill in Crystal 60");
        ++gChecked;
    }

    // A color range narrows the list, and its count says how many are left.
    Click(GTK_WIDGET(window), "Choose\xE2\x80\xA6");
    Spin(350);
    const auto from = Find(GTK_WIDGET(window), [](GtkWidget* w) {
        return GTK_IS_ENTRY(w) && std::string(gtk_entry_get_placeholder_text(GTK_ENTRY(w)) ?: "").rfind("from", 0) == 0;
    });
    if (from.empty()) {
        gProblems.push_back("the picker has no color range field");
    } else {
        gtk_editable_set_text(GTK_EDITABLE(from.front()), "20");
        Spin(300);
        Shot(window, "picker-range");
        bool counted = false;
        for (GtkWidget* label : FindType<void>(GTK_WIDGET(window), GTK_TYPE_LABEL)) {
            if (std::string(gtk_label_get_text(GTK_LABEL(label))).find(" of ") != std::string::npos) counted = true;
        }
        if (!counted) gProblems.push_back("the color range did not update the count");
        ++gChecked;
    }
    // The filter menu, which is built each time it opens, with the filter that is on checked.
    for (GtkWidget* menu : FindType<void>(GTK_WIDGET(window), GTK_TYPE_MENU_BUTTON)) {
        if (LabelOf(menu) == "Filter" && gtk_widget_get_mapped(menu)) {
            gtk_menu_button_popup(GTK_MENU_BUTTON(menu));
            Spin(300);
            Shot(window, "picker-filter");
            gtk_menu_button_popdown(GTK_MENU_BUTTON(menu));
            Spin(200);
            break;
        }
    }
    if (const auto open = Find(GTK_WIDGET(window), [](GtkWidget* w) {
            return GTK_IS_SEARCH_ENTRY(w) && std::string(gtk_widget_get_name(w)) == "ingredient-picker-search";
        });
        !open.empty()) {
        g_signal_emit_by_name(open.front(), "activate");
        Spin(300);
    }
    CloseDialog(window, "Cancel");

    // The procedure, and the brew-day window it opens.
    for (GtkWidget* button : FindType<void>(GTK_WIDGET(window), GTK_TYPE_TOGGLE_BUTTON)) {
        if (LabelOf(button) == "Procedure") gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    Spin(300);
    Click(GTK_WIDGET(window), "Generate Procedure");
    Spin(400);
    Shot(window, "procedure");
    if (!Click(GTK_WIDGET(window), "Open Brew-Day View")) gProblems.push_back("no brew-day button");
    Spin(500);
    const auto brew = OtherWindows(window);
    if (brew.empty()) {
        gProblems.push_back("the brew-day window did not open");
    } else {
        Shot(brew.front(), "brew-day");
        for (GtkWidget* button : FindType<void>(GTK_WIDGET(brew.front()), GTK_TYPE_BUTTON)) {
            if (gtk_widget_has_css_class(button, "card")) {
                g_signal_emit_by_name(button, "clicked");
                break;
            }
        }
        Spin(300);
        for (GtkWidget* button : FindType<void>(GTK_WIDGET(brew.front()), GTK_TYPE_BUTTON)) {
            if (LabelOf(button).find("-minute timer") != std::string::npos) {
                g_signal_emit_by_name(button, "clicked");
                break;
            }
        }
        Spin(1300);
        Shot(brew.front(), "brew-day-timer");
    }
    CloseOthers(window);

    // Every calculator and converter.
    auto openTool = [&](ToolKind kind) {
        g_action_group_activate_action(G_ACTION_GROUP(window), "tool", g_variant_new_string(toolKindName(kind).c_str()));
        Spin(250);
    };
    int index = 0;
    for (const ToolKind kind : calculators()) {
        openTool(kind);
        const auto opened = OtherWindows(window);
        if (opened.empty()) {
            gProblems.push_back("tool " + toolKindName(kind) + " did not open");
            continue;
        }
        if (index++ < 3) Shot(opened.back(), "tool-" + toolKindName(kind));
        Poke(opened.back(), 2);
        CloseOthers(window);
    }
    for (const ToolKind kind : converters()) {
        openTool(kind);
        if (OtherWindows(window).empty()) gProblems.push_back("converter " + toolKindName(kind) + " did not open");
        CloseOthers(window);
    }

    // The help window, page by page.
    g_action_group_activate_action(G_ACTION_GROUP(window), "help", nullptr);
    Spin(500);
    const auto help = OtherWindows(window);
    if (help.empty()) {
        gProblems.push_back("the help window did not open");
    } else {
        Shot(help.front(), "help");
        auto rows = FindType<void>(GTK_WIDGET(help.front()), GTK_TYPE_LIST_BOX_ROW);
        if (rows.size() < 5) gProblems.push_back("the help window lists only " + std::to_string(rows.size()) + " pages");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            gtk_list_box_select_row(GTK_LIST_BOX(gtk_widget_get_parent(rows[i])), GTK_LIST_BOX_ROW(rows[i]));
            Spin(150);
            if (i == 8) Shot(help.front(), "help-shortcuts");
        }
    }
    CloseOthers(window);

    YourStyles(window);

  // The dialogs.
    g_action_group_activate_action(G_ACTION_GROUP(window), "settings", nullptr);
    Spin(350);
    Shot(window, "settings");

    // A newer version is offered, and Later closes the offer.
    kl::update_check::Offer(GTK_WIDGET(window), kettlelogic::app::updates::Release{
        *kettlelogic::app::updates::Version::parse("0.2.0"), "https://example.com/release", ""});
    Spin(300);
    Shot(window, "update-offer");
    CloseDialog(window, "Later");
    Poke(window, 3);
    CloseDialog(window, "Done");
    g_action_group_activate_action(G_ACTION_GROUP(window), "new-style", nullptr);
    Spin(350);
    Shot(window, "wizard-1");
    Click(GTK_WIDGET(window), "Next");
    Spin(350);
    Shot(window, "wizard-2");
    Click(GTK_WIDGET(window), "Next");
    Spin(500);
    Shot(window, "wizard-3");
    Click(GTK_WIDGET(window), "Back");
    Spin(300);
    CloseDialog(window, "Cancel");

    // The ingredient directory, from the sidebar.
    const std::size_t pages = LibraryPage::all().size();
    for (std::size_t p = 0; p < pages; ++p) {
        auto rows = FindType<void>(GTK_WIDGET(window), GTK_TYPE_LIST_BOX_ROW);
        std::vector<GtkWidget*> library;
        for (GtkWidget* row : rows) {
            GtkWidget* line = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
            if (line != nullptr && gtk_widget_get_parent(row) != nullptr && GTK_IS_LIST_BOX(gtk_widget_get_parent(row)) &&
                gtk_widget_has_css_class(gtk_widget_get_parent(row), "navigation-sidebar")) {
                library.push_back(row);
            }
        }
        // The library pages are the last rows of the list.
        if (library.size() < pages) {
            gProblems.push_back("the sidebar is missing the directory pages");
            break;
        }
        GtkWidget* row = library[library.size() - pages + p];
        g_signal_emit_by_name(row, "activate");
        Spin(450);
        Shot(window, "directory-" + std::to_string(p));
    }
    for (GtkWidget* search : FindType<void>(GTK_WIDGET(window), GTK_TYPE_SEARCH_ENTRY)) {
        gtk_editable_set_text(GTK_EDITABLE(search), "pale");
        Spin(300);
        Shot(window, "directory-search");
        gtk_editable_set_text(GTK_EDITABLE(search), "");
    }

    // Editing history, copies, and a deletion.
    store.undo();
    Spin(300);
    store.redo();
    Spin(300);
    if (store.selection()) store.duplicateRecipe(*store.selection());
    Spin(300);
    Shot(window, "after-duplicate");
    if (store.selection()) store.deleteRecipe(*store.selection());
    Spin(300);
    Shot(window, "after-delete");
}

}  // namespace

int main(int argc, char** argv) {
    // The app keeps its files under the home folder, so the test uses one of its own.
    const std::filesystem::path home = std::filesystem::temp_directory_path() / ("kettlelogic-smoke-" + std::to_string(getpid()));
    std::filesystem::create_directories(home / "data");
    setenv("HOME", home.c_str(), 1);
    setenv("XDG_DATA_HOME", (home / "data").c_str(), 1);
    if (const char* shots = std::getenv("KETTLELOGIC_SMOKE_SHOTS")) {
        gShots = shots;
        std::filesystem::create_directories(gShots);
    }
    g_log_set_default_handler(LogHandler, nullptr);

    AdwApplication* application = adw_application_new("org.kettlelogic.KettleLogic.Smoke", G_APPLICATION_NON_UNIQUE);
    static std::unique_ptr<kl::MainWindow> app;
    g_signal_connect(application, "activate", G_CALLBACK(+[](GApplication* a, gpointer) {
                         app = std::make_unique<kl::MainWindow>(ADW_APPLICATION(a));
                         app->Present();
                         // The script runs from the main loop, which it also drives while it waits.
                         g_idle_add(+[](gpointer) -> gboolean {
                             Run(*app);
                             g_application_quit(g_application_get_default());
                             return G_SOURCE_REMOVE;
                         }, nullptr);
                     }), nullptr);
    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    app.reset();
    std::filesystem::remove_all(home);

    std::printf("%d screens visited, %zu problems\n", gChecked, gProblems.size());
    for (const std::string& problem : gProblems) std::printf("  %s\n", problem.c_str());
    return status != 0 || !gProblems.empty() ? 1 : 0;
}
