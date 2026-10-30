#include "ingredient_picker.hpp"

#include "kettlelogic/app/ingredient_search.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/catalog/ingredient_query.hpp"

#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace kl::dialogs {
namespace app = kettlelogic::app;
namespace catalog = kettlelogic::catalog;

namespace {

// Everything the picker shows and does. The panel holds it, so it lives as long as the dialog does. A callback reaches it
// through a weak pointer, because the dialog may close first.
struct Picker {
    std::vector<app::MenuEntry> entries;
    std::vector<catalog::IngredientEntry> searchEntries;
    std::vector<std::string> summaries;
    catalog::IngredientFacets facets;
    catalog::IngredientMetrics metrics;
    catalog::IngredientQuery query;
    std::function<void(const app::MenuEntry&)> pick;
    std::weak_ptr<ui::Dialog> dialog;
    std::weak_ptr<Picker> self;

    GListStore* store = nullptr;  // owned by the selection
    GtkSingleSelection* selection = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* search = nullptr;
    GtkWidget* count = nullptr;
    GtkWidget* empty = nullptr;
    GtkWidget* chips = nullptr;
};

using Weak = std::weak_ptr<Picker>;

std::string Title(const catalog::IngredientMetric& metric) {
    return metric.unit.empty() ? metric.label : metric.label + " (" + metric.unit + ")";
}

// The entry at a position of the list, or nothing when the position is out of range.
std::optional<std::size_t> IndexAt(GListModel* model, guint position) {
    if (model == nullptr || position >= g_list_model_get_n_items(model)) return std::nullopt;
    gpointer item = g_list_model_get_item(model, position);
    if (item == nullptr) return std::nullopt;
    const std::size_t index = std::strtoul(gtk_string_object_get_string(GTK_STRING_OBJECT(item)), nullptr, 10);
    g_object_unref(item);
    return index;
}

void RebuildChips(const std::weak_ptr<Picker>& weak, Picker& p);

// Draws the list again for the query. The engine finds and orders the entries; the store holds their positions.
void Refresh(Picker& p) {
    RebuildChips(p.self, p);
    const std::vector<std::size_t> found = catalog::queryIngredients(p.searchEntries, p.query);
    std::vector<gpointer> items;
    items.reserve(found.size());
    for (const std::size_t index : found) {
        items.push_back(gtk_string_object_new(std::to_string(index).c_str()));
    }
    g_list_store_splice(p.store, 0, g_list_model_get_n_items(G_LIST_MODEL(p.store)), items.data(),
                        static_cast<guint>(items.size()));
    for (gpointer item : items) g_object_unref(item);  // the store holds its own references now

    const std::size_t total = p.entries.size();
    ui::SetText(p.count, found.size() == total ? std::to_string(total) + " ingredients"
                                               : std::to_string(found.size()) + " of " + std::to_string(total));
    gtk_widget_set_visible(p.empty, found.empty());
    gtk_widget_set_visible(p.list, !found.empty());
    if (!found.empty()) gtk_list_view_scroll_to(GTK_LIST_VIEW(p.list), 0, GTK_LIST_SCROLL_NONE, nullptr);
}

// Runs a change to the picker, then draws it again once the event that made the change has finished.
void Update(const Weak& weak, const std::function<void(Picker&)>& change) {
    if (auto p = weak.lock()) change(*p);
    ui::Later([weak] {
        if (auto p = weak.lock()) Refresh(*p);
    });
}

void ToggleValue(std::vector<std::string>& values, const std::string& value) {
    const auto at = std::find(values.begin(), values.end(), value);
    if (at != values.end()) {
        values.erase(at);
    } else {
        values.push_back(value);
    }
}

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool IsFiltering(const catalog::IngredientQuery& q) {
    return !q.groups.empty() || !q.types.empty() || !q.forms.empty() || q.source != catalog::IngredientSource::any ||
           q.minPrimary || q.maxPrimary;
}

// A menu line with a check mark when it is on.
std::string Checked(bool on, const std::string& label) {
    return (on ? "\xE2\x9C\x93 " : "   ") + label;
}

// Chooses the entry at `index` and closes the picker.
void Choose(Picker& p, std::size_t index) {
    const app::MenuEntry chosen = p.entries[index];
    p.pick(chosen);
    if (auto d = p.dialog.lock()) d->Close();
}

void Move(Picker& p, int step) {
    const guint count = g_list_model_get_n_items(G_LIST_MODEL(p.selection));
    if (count == 0) return;
    guint at = gtk_single_selection_get_selected(p.selection);
    if (at == GTK_INVALID_LIST_POSITION) {
        at = 0;
    } else if (step > 0) {
        at = std::min(at + 1, count - 1);
    } else {
        at = at > 0 ? at - 1 : 0;
    }
    gtk_single_selection_set_selected(p.selection, at);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p.list), at, GTK_LIST_SCROLL_NONE, nullptr);
}

// MARK: Rows

void SetupRow(GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget* name = ui::Text("", ui::Kind::body);
    GtkWidget* summary = ui::Text("", ui::Kind::caption);
    gtk_label_set_xalign(GTK_LABEL(summary), 0);
    gtk_label_set_wrap(GTK_LABEL(summary), TRUE);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    GtkWidget* group = ui::Text("", ui::Kind::secondary);
    gtk_label_set_xalign(GTK_LABEL(group), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(group), 22);
    gtk_label_set_ellipsize(GTK_LABEL(group), PANGO_ELLIPSIZE_END);
    ui::Append(text, name);
    ui::Append(text, summary);
    ui::Append(row, text);
    ui::Append(row, group);
    ui::Margins(row, 8, 6, 8, 6);
    gtk_list_item_set_child(item, row);
}

void BindRow(GtkSignalListItemFactory*, GtkListItem* item, gpointer data) {
    auto* p = static_cast<Picker*>(data);
    gpointer object = gtk_list_item_get_item(item);
    if (object == nullptr) return;
    const std::size_t index = std::strtoul(gtk_string_object_get_string(GTK_STRING_OBJECT(object)), nullptr, 10);
    const app::MenuEntry& entry = p->entries[index];
    // The row's widgets are the name and summary in a box, with the group beside that box.
    GtkWidget* row = gtk_list_item_get_child(item);
    GtkWidget* text = gtk_widget_get_first_child(row);
    GtkWidget* name = gtk_widget_get_first_child(text);
    GtkWidget* summary = gtk_widget_get_next_sibling(name);
    GtkWidget* group = gtk_widget_get_next_sibling(text);
    ui::SetText(name, entry.name);
    ui::SetText(summary, p->summaries[index]);
    ui::SetText(group, entry.id.rfind("custom-", 0) == 0 ? "Yours" : entry.group);
}

void OnActivate(GtkListView* view, guint position, gpointer data) {
    auto* p = static_cast<Picker*>(data);
    if (auto index = IndexAt(G_LIST_MODEL(gtk_list_view_get_model(view)), position)) Choose(*p, *index);
}

// Enter in the search field picks the highlighted entry.
void OnSearchActivate(GtkSearchEntry*, gpointer data) {
    auto* p = static_cast<Picker*>(data);
    if (auto index = IndexAt(G_LIST_MODEL(p->selection), gtk_single_selection_get_selected(p->selection))) {
        Choose(*p, *index);
    }
}

// Up and Down move the highlight while the search field keeps the keyboard.
gboolean OnSearchKey(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer data) {
    auto* p = static_cast<Picker*>(data);
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up) {
        Move(*p, keyval == GDK_KEY_Down ? 1 : -1);
        return TRUE;
    }
    return FALSE;
}

// MARK: Controls

catalog::IngredientSort SortNamed(const std::string& value) {
    if (value == "name") return catalog::IngredientSort::name;
    if (value == "group") return catalog::IngredientSort::group;
    if (value == "primary") return catalog::IngredientSort::primary;
    if (value == "secondary") return catalog::IngredientSort::secondary;
    return catalog::IngredientSort::catalog;
}

// Rebuilds the chips that show each active filter, each removed by a click.
void RebuildChips(const Weak& weak, Picker& p) {
    while (GtkWidget* child = gtk_widget_get_first_child(p.chips)) gtk_box_remove(GTK_BOX(p.chips), child);
    const catalog::IngredientQuery& q = p.query;
    const auto chip = [&](const std::string& label, std::function<void(Picker&)> remove) {
        ui::Append(p.chips, ui::Button(label + "  \xC3\x97", [weak, remove] { Update(weak, remove); }));
    };
    for (const std::string& group : q.groups) {
        chip(group, [group](Picker& s) { ToggleValue(s.query.groups, group); });
    }
    for (const std::string& type : q.types) {
        chip(type, [type](Picker& s) { ToggleValue(s.query.types, type); });
    }
    for (const std::string& form : q.forms) {
        chip(form, [form](Picker& s) { ToggleValue(s.query.forms, form); });
    }
    if (q.source != catalog::IngredientSource::any) {
        chip(q.source == catalog::IngredientSource::custom ? "Yours" : "Built in",
             [](Picker& s) { s.query.source = catalog::IngredientSource::any; });
    }
    if (q.minPrimary || q.maxPrimary) {
        chip(Title(*p.metrics.primary), [](Picker& s) {
            s.query.minPrimary.reset();
            s.query.maxPrimary.reset();
        });
    }
    if (IsFiltering(q)) {
        ui::Append(p.chips, ui::LinkButton("Clear filters", [weak] {
            Update(weak, [](Picker& s) {
                s.query.groups.clear();
                s.query.types.clear();
                s.query.forms.clear();
                s.query.source = catalog::IngredientSource::any;
                s.query.minPrimary.reset();
                s.query.maxPrimary.reset();
            });
        }));
    }
}

// The filter menu, built each time it opens so its check marks match the query.
std::vector<ui::MenuLine> FilterMenu(const Weak& weak) {
    std::vector<ui::MenuLine> lines;
    auto p = weak.lock();
    if (!p) return lines;
    const auto facet = [&](const char* title, const std::vector<std::string>& values,
                           std::vector<std::string> catalog::IngredientQuery::*field) {
        if (values.size() < 2) return;
        std::vector<ui::MenuLine> items;
        for (const std::string& value : values) {
            const bool on = Contains(p->query.*field, value);
            items.push_back(ui::MenuLine::Item(Checked(on, value), [weak, value, field] {
                Update(weak, [value, field](Picker& s) { ToggleValue(s.query.*field, value); });
            }));
        }
        lines.push_back(ui::MenuLine::Sub(title, std::move(items)));
    };
    facet("Group", p->facets.groups, &catalog::IngredientQuery::groups);
    facet("Type", p->facets.types, &catalog::IngredientQuery::types);
    facet("Form", p->facets.forms, &catalog::IngredientQuery::forms);

    const bool mixed = std::any_of(p->entries.begin(), p->entries.end(), [](const app::MenuEntry& e) {
                           return e.id.rfind("custom-", 0) == 0;
                       }) &&
                       std::any_of(p->entries.begin(), p->entries.end(), [](const app::MenuEntry& e) {
                           return e.id.rfind("custom-", 0) != 0;
                       });
    if (mixed) {
        const catalog::IngredientSource current = p->query.source;
        const auto source = [weak, current](catalog::IngredientSource value, const char* label) {
            return ui::MenuLine::Item(Checked(current == value, label), [weak, value] {
                Update(weak, [value](Picker& s) { s.query.source = value; });
            });
        };
        lines.push_back(ui::MenuLine::Sub("Source", {source(catalog::IngredientSource::any, "All"),
                                                     source(catalog::IngredientSource::builtIn, "Built in"),
                                                     source(catalog::IngredientSource::custom, "Yours")}));
    }
    if (lines.empty()) lines.push_back(ui::MenuLine::Item("No filters for these ingredients", nullptr, false));
    lines.push_back(ui::MenuLine::Separator());
    lines.push_back(ui::MenuLine::Item("Clear Filters", [weak] {
        Update(weak, [](Picker& s) {
            s.query.groups.clear();
            s.query.types.clear();
            s.query.forms.clear();
            s.query.source = catalog::IngredientSource::any;
            s.query.minPrimary.reset();
            s.query.maxPrimary.reset();
        });
    }, IsFiltering(p->query)));
    return lines;
}

// A bound typed into a range field. An empty or unreadable field is no bound.
std::optional<double> Bound(const std::string& text) {
    return app::parseNumber(text);
}

}  // namespace

void ShowIngredientPicker(GtkWidget* parent, app::IngredientKind kind, std::vector<app::MenuEntry> entries,
                          std::function<void(const app::MenuEntry&)> pick) {
    auto p = std::make_shared<Picker>();
    p->self = p;
    p->entries = std::move(entries);
    p->pick = std::move(pick);
    const std::string kindName = app::searchKindName(kind);
    p->metrics = catalog::ingredientMetrics(kindName);
    for (const app::MenuEntry& entry : p->entries) {
        p->searchEntries.push_back(app::searchEntry(entry));
        p->summaries.push_back(entry.draft.summary());
    }
    p->facets = catalog::ingredientFacets(p->searchEntries);
    const Weak weak = p;

    // The panel: search, sort and filter controls, the range, the active filters, and the list.
    GtkWidget* panel = ui::Box(GTK_ORIENTATION_VERTICAL, 8);
    ui::Margins(panel, 16, 12, 16, 16);

    p->search = gtk_search_entry_new();
    gtk_widget_set_name(p->search, "ingredient-picker-search");  // how the smoke test finds it
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->search), "Search name or group");
    ui::Append(panel, p->search);
    ui::On(p->search, "changed", [weak, search = p->search] {
        Update(weak, [search](Picker& s) { s.query.text = gtk_editable_get_text(GTK_EDITABLE(search)); });
    });
    g_signal_connect(p->search, "activate", G_CALLBACK(OnSearchActivate), p.get());
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(OnSearchKey), p.get());
    gtk_widget_add_controller(p->search, keys);

    // Sort, direction, and filter in one row, with the count at its right.
    std::vector<ui::Choice> sorts = {{"catalog", "Catalog order"}, {"name", "Name"}, {"group", "Group"}};
    if (p->metrics.primary) sorts.push_back({"primary", Title(*p->metrics.primary)});
    if (p->metrics.secondary) sorts.push_back({"secondary", Title(*p->metrics.secondary)});
    GtkWidget* sort = ui::Picker(sorts, "catalog", [weak](std::string value) {
        Update(weak, [value](Picker& s) { s.query.sort = SortNamed(value); });
    });
    GtkWidget* direction = ui::Picker({{"asc", "Ascending"}, {"desc", "Descending"}}, "asc", [weak](std::string value) {
        Update(weak, [value](Picker& s) { s.query.descending = value == "desc"; });
    });
    p->count = ui::Text("", ui::Kind::caption);
    gtk_widget_set_hexpand(p->count, TRUE);
    gtk_label_set_xalign(GTK_LABEL(p->count), 1);
    GtkWidget* controls = ui::HStack({sort, direction, ui::MenuButton(nullptr, "Filter", "Show only some ingredients",
                                                                      [weak] { return FilterMenu(weak); }),
                                      p->count});
    ui::Append(panel, controls);

    // The range of the kind's main figure, such as the color of a malt, with the figure's range in the fields' hints.
    if (p->metrics.primary) {
        const std::string title = Title(*p->metrics.primary);
        const auto hint = [](std::optional<double> value) {
            char text[32];
            std::snprintf(text, sizeof text, "%g", value.value_or(0));
            return std::string(text);
        };
        GtkWidget* low = ui::LiveField("", [weak](std::string text) {
            Update(weak, [text](Picker& s) { s.query.minPrimary = Bound(text); });
        }, 90);
        GtkWidget* high = ui::LiveField("", [weak](std::string text) {
            Update(weak, [text](Picker& s) { s.query.maxPrimary = Bound(text); });
        }, 90);
        ui::Placeholder(low, p->facets.primaryMin ? "from " + hint(p->facets.primaryMin) : "from");
        ui::Placeholder(high, p->facets.primaryMax ? "to " + hint(p->facets.primaryMax) : "to");
        ui::Append(panel, ui::HStack({ui::Text(title, ui::Kind::secondary), low, ui::Text("to", ui::Kind::secondary), high}));
    }

    p->chips = ui::Box(GTK_ORIENTATION_HORIZONTAL, 6);
    ui::Append(panel, p->chips);

    // The list. The engine's answer is positions in the store; GtkListView draws only the rows on screen.
    p->store = g_list_store_new(GTK_TYPE_STRING_OBJECT);
    p->selection = gtk_single_selection_new(G_LIST_MODEL(p->store));  // takes the store's reference
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(SetupRow), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(BindRow), p.get());
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);  // takes the selection and the factory
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(p->list), TRUE);
    g_signal_connect(p->list, "activate", G_CALLBACK(OnActivate), p.get());
    p->empty = ui::Text("No ingredients match. Try other words, or clear a filter.", ui::Kind::secondary);
    gtk_label_set_wrap(GTK_LABEL(p->empty), TRUE);
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), p->list);
    gtk_widget_set_vexpand(scroller, TRUE);
    // The list grows with its rows, up to a height that leaves room for the controls above it.
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 380);
    ui::Append(panel, scroller);
    ui::Append(panel, p->empty);

    // Keep the picker alive with its panel. Its dialog is held weakly, so the dialog closes without a cycle.
    g_object_set_data_full(G_OBJECT(panel), "kl-picker", new std::shared_ptr<Picker>(p), [](gpointer data) {
        delete static_cast<std::shared_ptr<Picker>*>(data);
    });

    ui::DialogOptions options;
    options.title = "Common Ingredients";
    options.content = panel;
    options.closeText = "Cancel";
    options.width = 520;
    p->dialog = ui::ShowDialog(parent, options);
    Refresh(*p);
    gtk_widget_grab_focus(p->search);
}

}  // namespace kl::dialogs
