#include "dialogs.hpp"
#include "views.hpp"

#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;

namespace {

bool ContainsIgnoringCase(const std::string& text, const std::string& search) {
    if (search.empty()) return true;
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    return lower(text).find(lower(search)) != std::string::npos;
}

// A button with no frame that fills its row, so the whole row opens the entry.
GtkWidget* RowButton(GtkWidget* content, std::function<void()> open, ui::MenuBuilder menu) {
    GtkWidget* button = gtk_button_new();
    ui::Style(button, "flat");
    ui::Margins(content, 4, 6, 4, 6);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_hexpand(button, TRUE);
    ui::On(button, "clicked", std::move(open));
    if (menu) ui::ContextMenu(button, std::move(menu));
    return button;
}

// An entry's name, with its group under it when the list is not already grouped, and its key figures on the right.
GtkWidget* DirectoryRow(const std::string& name, const std::string& group, const std::string& detail,
                        std::function<void()> open, ui::MenuBuilder menu) {
    GtkWidget* row = ui::Box(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget* left = ui::Box(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(left, TRUE);
    ui::Append(left, ui::Text(name));
    if (!group.empty()) ui::Append(left, ui::Text(group, ui::Kind::caption));
    ui::Append(row, left);

    GtkWidget* figures = ui::Text(detail, ui::Kind::secondary);
    gtk_label_set_xalign(GTK_LABEL(figures), 1);
    gtk_label_set_justify(GTK_LABEL(figures), GTK_JUSTIFY_RIGHT);
    gtk_label_set_max_width_chars(GTK_LABEL(figures), 44);
    gtk_widget_set_halign(figures, GTK_ALIGN_END);
    gtk_widget_set_valign(figures, GTK_ALIGN_CENTER);
    ui::Append(row, figures);
    return RowButton(row, std::move(open), std::move(menu));
}

// The title, search box, and actions above a directory page.
struct DirectoryHeader {
    GtkWidget* root;
    GtkWidget* search;
};

DirectoryHeader MakeHeader(const std::string& title, const char* searchPlaceholder, GtkWidget* action) {
    GtkWidget* header = ui::Box(GTK_ORIENTATION_HORIZONTAL, 12);
    ui::Margins(header, 20, 14, 20, 6);
    GtkWidget* heading = ui::Text(title, ui::Kind::title);
    gtk_widget_set_hexpand(heading, TRUE);
    gtk_widget_set_valign(heading, GTK_ALIGN_CENTER);
    ui::Append(header, heading);
    GtkWidget* search = gtk_search_entry_new();
    gtk_widget_set_valign(search, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(search, 240, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), searchPlaceholder);
    ui::Append(header, search);
    ui::Append(header, action);
    return {header, search};
}

std::string SearchText(GtkWidget* search) {
    return gtk_editable_get_text(GTK_EDITABLE(search));
}

// The page of the ingredient directory: the user's own ingredients, or the built-in ingredients of one kind. A
// built-in page is split into sections by catalog group. Built-in entries are copied, never changed: opening one
// starts an editor from its values, so a copy can be saved as a custom ingredient. Custom ingredients can be edited
// and deleted here.
class DirectoryView final : public View {
public:
    DirectoryView(Host& host, const LibraryPage& page) : host_(host), page_(page) {
        GtkWidget* add = ui::MenuButton("list-add-symbolic", "New Custom Ingredient", "Make your own ingredient", [this] {
            std::vector<ui::MenuLine> kinds;
            for (const IngredientKind kind : allIngredientKinds()) {
                kinds.push_back(ui::MenuLine::Item(ingredientKindTitle(kind), [this, kind] { NewCustom(kind); }));
            }
            return kinds;
        });
        const DirectoryHeader header = MakeHeader(page.title(), "Search ingredients", add);
        search_ = header.search;
        ui::OnNotify(search_, "text", [this] {
            searchText_ = SearchText(search_);
            RebuildList();
        });
        root_ = ui::Box(GTK_ORIENTATION_VERTICAL, 0);
        ui::Append(root_, header.root);
        ui::Append(root_, list_.Root());
        RebuildList();
    }

    GtkWidget* Root() override { return root_; }
    void Refresh() override { RebuildList(); }

private:
    using MenuEntry_ = kettlelogic::app::MenuEntry;

    Units units() const { return host_.units(); }

    bool Matches(const std::string& name, const std::string& group) const {
        return ContainsIgnoringCase(name, searchText_) || ContainsIgnoringCase(group, searchText_);
    }

    void RebuildList() {
        ui::Form form;
        bool any = false;

        std::vector<CustomIngredient> custom;
        for (const CustomIngredient& item : host_.store.customIngredients.ingredients()) {
            if (page_.includes(item.draft.kind) && Matches(item.draft.name, ingredientKindTitle(item.draft.kind))) {
                custom.push_back(item);
            }
        }
        if (!custom.empty()) {
            any = true;
            form.Section("Your Ingredients");
            for (const CustomIngredient& item : custom) {
                form.Wide(DirectoryRow(item.draft.name, ingredientKindTitle(item.draft.kind), item.draft.summary(),
                                       [this, item] { EditCustom(item); }, [this, item] {
                                           return std::vector<ui::MenuLine>{
                                               ui::MenuLine::Item("Edit", [this, item] { EditCustom(item); }),
                                               ui::MenuLine::Item("Duplicate", [this, id = item.id] {
                                                   host_.store.attempt([&] { host_.store.customIngredients.duplicate(id); });
                                               }),
                                               ui::MenuLine::Item("Delete", [this, id = item.id] {
                                                   host_.store.attempt([&] { host_.store.customIngredients.remove(id); });
                                               })};
                                       }));
            }
        }

        for (const IngredientKind kind : page_.builtInKinds()) {
            std::vector<MenuEntry_> entries;
            for (const auto& entry : catalogEntries(kind, units().system)) {
                if (Matches(entry.name, entry.group)) entries.push_back(entry);
            }
            std::vector<std::string> groups;
            for (const auto& entry : entries) {
                if (std::find(groups.begin(), groups.end(), entry.group) == groups.end()) groups.push_back(entry.group);
            }
            for (const std::string& group : groups) {
                any = true;
                form.Section(group);
                for (const auto& entry : entries) {
                    if (entry.group != group) continue;
                    form.Wide(DirectoryRow(entry.name, "", entry.draft.summary(), [this, entry] { OpenCopy(entry); }, [this, entry] {
                        return std::vector<ui::MenuLine>{ui::MenuLine::Item("Save a Copy…", [this, entry] { OpenCopy(entry); })};
                    }));
                }
            }
        }

        if (!any) {
            form.Section();
            form.Note("No results" + (searchText_.empty() ? std::string() : " for \xE2\x80\x9C" + searchText_ + "\xE2\x80\x9D") + ".");
        }
        page_Set(form);
    }

    void page_Set(ui::Form& form) { list_.Set(form.Build()); }

    void Open(const std::string& title, const IngredientDraft& draft, std::vector<MenuEntry_> menu,
              std::optional<std::string> customId) {
        dialogs::IngredientRequest request;
        request.title = title;
        request.saveTitle = "Save";
        request.seed = draft;
        request.menu = std::move(menu);
        request.showsQuantities = false;
        request.save = [this, customId](const IngredientDraft& saved) {
            host_.store.customIngredients.save(saved, customId);
            host_.refresh();
        };
        dialogs::ShowIngredientEditor(host_, std::move(request));
    }

    void NewCustom(IngredientKind kind) {
        Open("New Custom " + ingredientKindTitle(kind), IngredientDraft(kind, units().system),
             menuEntries(kind, host_.store.customIngredients.ingredients(), units().system), std::nullopt);
    }

    void EditCustom(const CustomIngredient& item) {
        Open("Edit Custom Ingredient", item.draft, {}, item.id);
    }

    void OpenCopy(const MenuEntry_& entry) {
        Open("Save as Custom Ingredient", entry.draft, {}, std::nullopt);
    }

    Host& host_;
    LibraryPage page_;
    std::string searchText_;
    GtkWidget* root_ = nullptr;
    GtkWidget* search_ = nullptr;
    ui::Page list_;
};

// The brewing systems in the ingredient directory. The user's own systems can be made, changed, duplicated, and
// deleted. A built-in system is copied to make a change to it. Each one shows its starting figures and the note on
// where they come from.
class EquipmentDirectoryView final : public View {
public:
    explicit EquipmentDirectoryView(Host& host) : host_(host) {
        GtkWidget* add = ui::Button("New Brewing System", [this] {
            EquipmentProfile profile = Equipment::builtIn()[0];
            profile.name = "";
            profile.note = "";
            dialogs::ShowEquipmentEditor(host_, "New Brewing System", profile, std::nullopt);
        });
        ui::SetTip(add, "Make your own brewing system");
        const DirectoryHeader header = MakeHeader(LibraryPage{LibraryPage::Kind::equipment}.title(), "Search brewing systems", add);
        search_ = header.search;
        ui::OnNotify(search_, "text", [this] {
            searchText_ = SearchText(search_);
            RebuildList();
        });
        root_ = ui::Box(GTK_ORIENTATION_VERTICAL, 0);
        ui::Append(root_, header.root);
        ui::Append(root_, list_.Root());
        RebuildList();
    }

    GtkWidget* Root() override { return root_; }
    void Refresh() override { RebuildList(); }

private:
    bool Matches(const EquipmentProfile& profile) const { return ContainsIgnoringCase(profile.name, searchText_); }

    // A brewing system's name, with its starting figures and its note under it.
    GtkWidget* Row(const EquipmentProfile& profile, std::function<void()> open, ui::MenuBuilder menu) {
        GtkWidget* panel = ui::Box(GTK_ORIENTATION_VERTICAL, 2);
        ui::Append(panel, ui::Text(profile.name));
        ui::Append(panel, ui::Text(summary(profile, host_.store.preferences.units()), ui::Kind::caption));
        if (!profile.note.empty()) ui::Append(panel, ui::Text(profile.note, ui::Kind::caption));
        return RowButton(panel, std::move(open), std::move(menu));
    }

    // A built-in system's figures, under a name that says it is a copy.
    static EquipmentProfile CopyOf(const EquipmentProfile& profile) {
        EquipmentProfile copy = profile;
        copy.name = profile.name + " copy";
        return copy;
    }

    void RebuildList() {
        ui::Form form;
        std::vector<EquipmentProfile> custom;
        for (const EquipmentProfile& profile : host_.store.equipment.custom()) {
            if (Matches(profile)) custom.push_back(profile);
        }
        std::vector<EquipmentProfile> builtIn;
        for (const EquipmentProfile& profile : Equipment::builtIn()) {
            if (Matches(profile)) builtIn.push_back(profile);
        }

        if (!custom.empty()) {
            form.Section("Your Equipment");
            for (const EquipmentProfile& profile : custom) {
                const auto edit = [this, profile] {
                    dialogs::ShowEquipmentEditor(host_, "Edit Brewing System", profile, profile.name);
                };
                form.Wide(Row(profile, edit, [this, edit, name = profile.name] {
                    return std::vector<ui::MenuLine>{
                        ui::MenuLine::Item("Edit", edit),
                        ui::MenuLine::Item("Duplicate", [this, name] {
                            host_.store.attempt([&] { host_.store.equipment.duplicate(name); });
                        }),
                        ui::MenuLine::Item("Delete", [this, name] {
                            host_.store.attempt([&] { host_.store.equipment.remove(name); });
                        })};
                }));
            }
        }
        if (!builtIn.empty()) {
            form.Section("Built-in");
            for (const EquipmentProfile& profile : builtIn) {
                const auto copy = [this, profile] {
                    dialogs::ShowEquipmentEditor(host_, "Save as Brewing System", CopyOf(profile), std::nullopt);
                };
                form.Wide(Row(profile, copy, [copy] {
                    return std::vector<ui::MenuLine>{ui::MenuLine::Item("Save a Copy…", copy)};
                }));
            }
        }
        if (custom.empty() && builtIn.empty()) {
            form.Section();
            form.Note("No results" + (searchText_.empty() ? std::string() : " for \xE2\x80\x9C" + searchText_ + "\xE2\x80\x9D") + ".");
        }
        list_.Set(form.Build());
    }

    Host& host_;
    std::string searchText_;
    GtkWidget* root_ = nullptr;
    GtkWidget* search_ = nullptr;
    ui::Page list_;
};

// What shows when no recipe is open.
class WelcomeView final : public View {
public:
    explicit WelcomeView(Host& host) {
        GtkWidget* buttons = ui::Box(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_halign(buttons, GTK_ALIGN_CENTER);
        const auto add = [&](const char* label, std::function<void()> action, bool accent) {
            GtkWidget* button = ui::Button(label, std::move(action), accent);
            gtk_widget_set_halign(button, GTK_ALIGN_FILL);
            if (accent) ui::Style(button, "pill");
            gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(button))), 0.5f);
            ui::Append(buttons, button);
        };
        add("New Recipe from a Style…", [&host] { dialogs::ShowWizard(host); }, true);
        add("Blank Recipe", [&host] { host.store.newRecipe(); }, false);
        add("Open…", [&host] { if (host.openFile) host.openFile(); }, false);

        root_ = adw_status_page_new();
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(root_), "document-edit-symbolic");
        adw_status_page_set_title(ADW_STATUS_PAGE(root_), "No Recipe Selected");
        adw_status_page_set_description(ADW_STATUS_PAGE(root_), "Open a recipe, or start a new one.");
        adw_status_page_set_child(ADW_STATUS_PAGE(root_), buttons);
        gtk_widget_set_vexpand(root_, TRUE);
    }

    GtkWidget* Root() override { return root_; }

private:
    GtkWidget* root_;
};

}  // namespace

std::unique_ptr<View> MakeDirectoryView(Host& host, const LibraryPage& page) {
    return std::make_unique<DirectoryView>(host, page);
}

std::unique_ptr<View> MakeEquipmentDirectoryView(Host& host) {
    return std::make_unique<EquipmentDirectoryView>(host);
}

std::unique_ptr<View> MakeWelcomeView(Host& host) {
    return std::make_unique<WelcomeView>(host);
}

}  // namespace kl
