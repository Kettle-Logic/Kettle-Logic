#include "pch.h"

#include "Dialogs.h"
#include "Views.h"

#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;
using platform::H;
using platform::U;
using winrt::box_value;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

namespace {

bool ContainsIgnoringCase(const std::string& text, const std::string& search) {
    if (search.empty()) return true;
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    return lower(text).find(lower(search)) != std::string::npos;
}

// An entry's name, with its group under it when the list is not already grouped, and its key figures on the right.
xaml::UIElement DirectoryRow(const std::string& name, const std::string& group, const std::string& detail,
                             std::function<void()> open, ctl::MenuFlyout const& menu) {
    ctl::Grid grid;
    grid.ColumnSpacing(16);
    grid.ColumnDefinitions().Append(ctl::ColumnDefinition());
    ctl::ColumnDefinition right;
    right.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
    grid.ColumnDefinitions().Append(right);

    ctl::StackPanel left;
    left.Spacing(0);
    left.Children().Append(ui::Text(name));
    if (!group.empty()) left.Children().Append(ui::Text(group, ui::Kind::caption));
    grid.Children().Append(left);

    ctl::TextBlock figures = ui::Text(detail, ui::Kind::secondary);
    figures.TextAlignment(xaml::TextAlignment::Right);
    figures.MaxWidth(380);
    figures.VerticalAlignment(xaml::VerticalAlignment::Center);
    ctl::Grid::SetColumn(figures, 1);
    grid.Children().Append(figures);

    ctl::Button button;
    button.Content(grid);
    button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    button.Background(ui::ColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
    button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    button.Padding(xaml::Thickness{0, 4, 0, 4});
    button.Click([open = std::move(open)](auto const&, auto const&) { open(); });
    if (menu) button.ContextFlyout(menu);
    return button;
}

ctl::MenuFlyoutItem FlyoutEntry(const std::string& text, std::function<void()> action) {
    ctl::MenuFlyoutItem item;
    item.Text(H(text));
    item.Click([action = std::move(action)](auto const&, auto const&) { action(); });
    return item;
}

// The page of the ingredient directory: the user's own ingredients, or the built-in ingredients of one kind. A
// built-in page is split into sections by catalog group. Built-in entries are copied, never changed: opening one
// starts an editor from its values, so a copy can be saved as a custom ingredient. Custom ingredients can be edited
// and deleted here.
class DirectoryView final : public View {
public:
    DirectoryView(Host& host, const LibraryPage& page) : host_(host), page_(page) {
        ctl::RowDefinition bar;
        bar.Height(xaml::GridLength{1, xaml::GridUnitType::Auto});
        ctl::RowDefinition body;
        body.Height(xaml::GridLength{1, xaml::GridUnitType::Star});
        root_.RowDefinitions().Append(bar);
        root_.RowDefinitions().Append(body);

        ctl::Grid header;
        header.Margin(xaml::Thickness{20, 12, 20, 0});
        header.ColumnSpacing(12);
        header.ColumnDefinitions().Append(ctl::ColumnDefinition());
        ctl::ColumnDefinition auto1;
        auto1.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        header.ColumnDefinitions().Append(auto1);
        ctl::StackPanel titles;
        titles.Children().Append(ui::Text(page.title(), ui::Kind::title));
        header.Children().Append(titles);

        ctl::StackPanel tools;
        tools.Orientation(ctl::Orientation::Horizontal);
        tools.Spacing(8);
        search_.PlaceholderText(L"Search ingredients");
        search_.Width(240);
        search_.TextChanged([this](auto const& sender, auto const&) {
            search_text_ = U(sender.template as<ctl::TextBox>().Text());
            RebuildList();
        });
        tools.Children().Append(search_);
        ctl::DropDownButton add;
        add.Content(ui::HStack({ctl::SymbolIcon(ctl::Symbol::Add), ui::Text("New Custom Ingredient")}, 6));
        ui::SetTip(add, "Make your own ingredient");
        ctl::MenuFlyout kinds;
        for (const IngredientKind kind : allIngredientKinds()) {
            kinds.Items().Append(FlyoutEntry(ingredientKindTitle(kind), [this, kind] { NewCustom(kind); }));
        }
        add.Flyout(kinds);
        tools.Children().Append(add);
        ctl::Grid::SetColumn(tools, 1);
        header.Children().Append(tools);
        ctl::Grid::SetRow(header, 0);
        root_.Children().Append(header);

        ctl::Grid::SetRow(list_, 1);
        list_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
        list_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
        root_.Children().Append(list_);
        RebuildList();
    }

    xaml::UIElement Root() override { return root_; }
    void Refresh() override { RebuildList(); }

private:
    Units units() const { return host_.units(); }

    bool Matches(const std::string& name, const std::string& group) const {
        return ContainsIgnoringCase(name, search_text_) || ContainsIgnoringCase(group, search_text_);
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
                ctl::MenuFlyout menu;
                menu.Items().Append(FlyoutEntry("Edit", [this, item] { EditCustom(item); }));
                menu.Items().Append(FlyoutEntry("Duplicate", [this, id = item.id] {
                    host_.store.attempt([&] { host_.store.customIngredients.duplicate(id); });
                }));
                menu.Items().Append(FlyoutEntry("Delete", [this, id = item.id] {
                    host_.store.attempt([&] { host_.store.customIngredients.remove(id); });
                }));
                form.Wide(DirectoryRow(item.draft.name, ingredientKindTitle(item.draft.kind), item.draft.summary(),
                                       [this, item] { EditCustom(item); }, menu));
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
                    ctl::MenuFlyout menu;
                    menu.Items().Append(FlyoutEntry("Save a Copy…", [this, entry] { OpenCopy(entry); }));
                    form.Wide(DirectoryRow(entry.name, "", entry.draft.summary(), [this, entry] { OpenCopy(entry); }, menu));
                }
            }
        }

        if (!any) {
            form.Section();
            form.Note("No results" + (search_text_.empty() ? std::string() : " for \xE2\x80\x9C" + search_text_ + "\xE2\x80\x9D") + ".");
        }
        list_.Content(form.Build());
    }

    using MenuEntry_ = kettlelogic::app::MenuEntry;

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
    std::string search_text_;
    ctl::Grid root_;
    ctl::TextBox search_;
    ctl::ContentControl list_;
};

// The brewing systems in the ingredient directory. The user's own systems can be made, changed, duplicated, and
// deleted. A built-in system is copied to make a change to it. Each one shows its starting figures and the note on
// where they come from.
class EquipmentDirectoryView final : public View {
public:
    explicit EquipmentDirectoryView(Host& host) : host_(host) {
        ctl::RowDefinition bar;
        bar.Height(xaml::GridLength{1, xaml::GridUnitType::Auto});
        ctl::RowDefinition body;
        body.Height(xaml::GridLength{1, xaml::GridUnitType::Star});
        root_.RowDefinitions().Append(bar);
        root_.RowDefinitions().Append(body);

        ctl::Grid header;
        header.Margin(xaml::Thickness{20, 12, 20, 0});
        header.ColumnSpacing(12);
        header.ColumnDefinitions().Append(ctl::ColumnDefinition());
        ctl::ColumnDefinition auto1;
        auto1.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        header.ColumnDefinitions().Append(auto1);
        header.Children().Append(ui::Text(LibraryPage{LibraryPage::Kind::equipment}.title(), ui::Kind::title));

        ctl::StackPanel tools;
        tools.Orientation(ctl::Orientation::Horizontal);
        tools.Spacing(8);
        search_.PlaceholderText(L"Search brewing systems");
        search_.Width(240);
        search_.TextChanged([this](auto const& sender, auto const&) {
            searchText_ = U(sender.template as<ctl::TextBox>().Text());
            RebuildList();
        });
        tools.Children().Append(search_);
        ctl::Button add = ui::Button("New Brewing System", [this] {
            EquipmentProfile profile = Equipment::builtIn()[0];
            profile.name = "";
            profile.note = "";
            dialogs::ShowEquipmentEditor(host_, "New Brewing System", profile, std::nullopt);
        });
        ui::SetTip(add, "Make your own brewing system");
        tools.Children().Append(add);
        ctl::Grid::SetColumn(tools, 1);
        header.Children().Append(tools);
        root_.Children().Append(header);

        ctl::Grid::SetRow(list_, 1);
        root_.Children().Append(list_);
        RebuildList();
    }

    xaml::UIElement Root() override { return root_; }
    void Refresh() override { RebuildList(); }

private:
    bool Matches(const EquipmentProfile& profile) const { return ContainsIgnoringCase(profile.name, searchText_); }

    // A brewing system's name, with its starting figures and its note under it.
    xaml::UIElement Row(const EquipmentProfile& profile, std::function<void()> open, ctl::MenuFlyout const& menu) {
        ctl::StackPanel panel;
        panel.Spacing(2);
        panel.Children().Append(ui::Text(profile.name));
        panel.Children().Append(ui::Text(summary(profile, host_.store.preferences.units()), ui::Kind::caption));
        if (!profile.note.empty()) panel.Children().Append(ui::Text(profile.note, ui::Kind::caption));
        ctl::Button button;
        button.Content(panel);
        button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(xaml::HorizontalAlignment::Left);
        button.Background(ui::ColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
        button.Padding(xaml::Thickness{0, 4, 0, 4});
        button.Click([open = std::move(open)](auto const&, auto const&) { open(); });
        if (menu) button.ContextFlyout(menu);
        return button;
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
                ctl::MenuFlyout menu;
                menu.Items().Append(FlyoutEntry("Edit", edit));
                menu.Items().Append(FlyoutEntry("Duplicate", [this, name = profile.name] {
                    host_.store.attempt([&] { host_.store.equipment.duplicate(name); });
                }));
                menu.Items().Append(FlyoutEntry("Delete", [this, name = profile.name] {
                    host_.store.attempt([&] { host_.store.equipment.remove(name); });
                }));
                form.Wide(Row(profile, edit, menu));
            }
        }
        if (!builtIn.empty()) {
            form.Section("Built-in");
            for (const EquipmentProfile& profile : builtIn) {
                const auto copy = [this, profile] {
                    dialogs::ShowEquipmentEditor(host_, "Save as Brewing System", CopyOf(profile), std::nullopt);
                };
                ctl::MenuFlyout menu;
                menu.Items().Append(FlyoutEntry("Save a Copy…", copy));
                form.Wide(Row(profile, copy, menu));
            }
        }
        if (custom.empty() && builtIn.empty()) {
            form.Section();
            form.Note("No results" + (searchText_.empty() ? std::string() : " for \xE2\x80\x9C" + searchText_ + "\xE2\x80\x9D") + ".");
        }
        list_.Content(form.Build());
    }

    Host& host_;
    std::string searchText_;
    ctl::Grid root_;
    ctl::TextBox search_;
    ctl::ContentControl list_;
};

// What shows when no recipe is open.
class WelcomeView final : public View {
public:
    explicit WelcomeView(Host& host) {
        ctl::StackPanel stack;
        stack.Spacing(10);
        stack.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        stack.VerticalAlignment(xaml::VerticalAlignment::Center);
        ctl::FontIcon icon;
        icon.Glyph(L"\xE8A5");
        icon.FontSize(48);
        ui::UseStyle(icon, L"KLSecondaryGlyph");
        stack.Children().Append(icon);
        stack.Children().Append(ui::Text("No Recipe Selected", ui::Kind::subtitle));
        stack.Children().Append(ui::Text("Open a recipe, or start a new one.", ui::Kind::secondary));
        stack.Children().Append(ui::Spacer(6));
        ctl::Button style = ui::Button("New Recipe from a Style…", [&host] { dialogs::ShowWizard(host); }, true);
        style.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        stack.Children().Append(style);
        ctl::Button blank = ui::Button("Blank Recipe", [&host] { host.store.newRecipe(); });
        blank.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        stack.Children().Append(blank);
        ctl::Button open = ui::Button("Open…", [&host] { if (host.openFile) host.openFile(); });
        open.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        stack.Children().Append(open);
        root_.Children().Append(stack);
    }

    xaml::UIElement Root() override { return root_; }

private:
    ctl::Grid root_;
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
