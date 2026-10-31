#include "pch.h"

#include "Dialogs.h"
#include "RecipeContext.h"
#include "Views.h"

#include "kettlelogic/app/glossary.hpp"
#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;
namespace styles = kettlelogic::styles;
using platform::H;
using platform::U;
using winrt::box_value;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

std::string NumberText(double value, int places, double factor) {
    return number(value * factor, 0, places);
}

std::optional<double> TypedNumber(const std::string& text, double factor) {
    const auto parsed = parseNumber(text);
    if (!parsed) {
        return std::nullopt;
    }
    return *parsed / factor;
}

namespace {

// MARK: Tabs

enum class Tab {
    recipe, equipment, boilOff, fermentation, procedure, starter, waterBuilder, mash, journal, results, plan, style,
    fermentables, hops, cultures, water, miscellaneous, notes,
};

// The groups the tabs fall into when the setting is on. Each group is a top-level choice, with its own tabs under it,
// so a recipe shows four choices instead of up to sixteen.
enum class Group { recipe, ingredients, process, results };

std::string TabTitle(Tab tab, bool simple) {
    switch (tab) {
        case Tab::recipe: return "Recipe";
        case Tab::equipment: return "Equipment";
        case Tab::boilOff: return "Boil-Off";
        case Tab::fermentation: return "Fermentation";
        case Tab::procedure: return "Procedure";
        case Tab::starter: return "Yeast Starter";
        case Tab::waterBuilder: return "Water Builder";
        case Tab::mash: return "Mash";
        case Tab::journal: return "Brew Journal";
        case Tab::results: return "Results";
        case Tab::plan: return "Plan";
        case Tab::style: return "Style";
        case Tab::fermentables: return "Fermentables";
        case Tab::hops: return "Hops";
        case Tab::cultures: return simple ? "Yeast" : "Yeast & Cultures";
        case Tab::water: return "Water";
        case Tab::miscellaneous: return "Miscellaneous";
        case Tab::notes: return "Notes";
    }
    return "";
}

std::string GroupTitle(Group group) {
    switch (group) {
        case Group::recipe: return "Recipe";
        case Group::ingredients: return "Ingredients";
        case Group::process: return "Process";
        case Group::results: return "Results";
    }
    return "";
}

const std::vector<Group>& AllGroups() {
    static const std::vector<Group> groups = {Group::recipe, Group::ingredients, Group::process, Group::results};
    return groups;
}

// The group's tabs, in the order they show under it. A tab the recipe does not have is left out by the caller.
std::vector<Tab> GroupTabs(Group group) {
    switch (group) {
        case Group::recipe: return {Tab::recipe};
        case Group::ingredients: return {Tab::fermentables, Tab::hops, Tab::cultures, Tab::water, Tab::miscellaneous};
        case Group::process:
            return {Tab::equipment, Tab::mash, Tab::boilOff, Tab::fermentation, Tab::waterBuilder, Tab::starter,
                    Tab::procedure, Tab::journal};
        case Group::results: return {Tab::results, Tab::style, Tab::plan, Tab::notes};
    }
    return {};
}

Group GroupOf(Tab tab) {
    for (const Group group : AllGroups()) {
        for (const Tab member : GroupTabs(group)) {
            if (member == tab) return group;
        }
    }
    return Group::recipe;
}

bool Contains(const std::vector<Tab>& tabs, Tab tab) {
    return std::find(tabs.begin(), tabs.end(), tab) != tabs.end();
}

// MARK: The recipe's screen

class RecipeView final : public TabbedView {
public:
    RecipeView(Host& host, const ItemKey& key)
        : ctx_{host, key, host.store.document(key), {}} {
        ctx_.equipmentName = host.store.preferences.defaultEquipmentName();
        BuildSkeleton();
        Rebuild();
    }

    xaml::UIElement Root() override { return root_; }

    void Refresh() override {
        if (ctx_.document->recipe(ctx_.key.id) == nullptr) {
            return;
        }
        // The screen is drawn again when the recipe changed, when a setting that shapes it changed, or when the screen
        // asked for it. Anything else, such as a file being autosaved, leaves it alone, so typing is never interrupted.
        const std::string settings = Signature();
        if (!ctx_.dirty && ctx_.document->revision() == drawnRevision_ && settings == drawnSettings_) {
            return;
        }
        Rebuild();
    }

    void SelectTab(int number) override {
        if (groupedTabs()) {
            const auto groups = VisibleGroups();
            if (number >= 1 && number <= static_cast<int>(groups.size())) {
                SelectGroup(groups[static_cast<std::size_t>(number - 1)]);
            }
        } else {
            const auto tabs = VisibleTabs();
            if (number >= 1 && number <= static_cast<int>(tabs.size())) {
                Select(tabs[static_cast<std::size_t>(number - 1)]);
            }
        }
    }

private:
    Host& host() { return ctx_.host; }
    DocumentStore& store() { return ctx_.host.store; }
    bool simple() const { return ctx_.simple(); }
    bool groupedTabs() const { return ctx_.host.store.preferences.boolean(PrefKey::groupedTabs); }
    const model::Recipe& recipe() const { return ctx_.recipe(); }
    const calc::Results& results() const { return ctx_.results(); }
    Units units() const { return ctx_.units(); }
    bool isDrink() const { return calc::isFermentedDrink(recipe().type); }

    // MARK: Layout

    void BuildSkeleton() {
        for (int i = 0; i < 4; ++i) {
            ctl::RowDefinition row;
            row.Height(i == 3 ? xaml::GridLength{1, xaml::GridUnitType::Star}
                              : xaml::GridLength{1, xaml::GridUnitType::Auto});
            root_.RowDefinitions().Append(row);
        }
        ctl::Grid::SetRow(groupBar_, 0);
        ctl::Grid::SetRow(tabBar_, 1);
        ctl::Grid::SetRow(strip_, 2);
        ctl::Grid::SetRow(content_, 3);
        root_.Children().Append(groupBar_);
        root_.Children().Append(tabBar_);
        root_.Children().Append(strip_);
        root_.Children().Append(content_);
        // A ContentControl aligns its content to the left unless told to stretch it. The results strip spans the window.
        for (const ctl::ContentControl& control : {groupBar_, tabBar_, strip_, content_}) {
            control.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
        }
        // A thin line under each bar, as the macOS screen has between the tabs, the results strip, and the form.
        for (const ctl::ContentControl& bar : {groupBar_, tabBar_, strip_}) {
            bar.BorderThickness(xaml::Thickness{0, 0, 0, 1});
            bar.BorderBrush(ui::ThemeBrush(L"DividerStrokeColorDefaultBrush"));
        }
        content_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
    }

    // MARK: Which tabs show

    // The tabs that apply to this recipe, in the order they show.
    std::vector<Tab> VisibleTabs() const {
        return simple() ? SimpleTabs() : AllTabs();
    }

    // Simple mode: ingredients, results, style, the procedure, and the journal. Water and miscellaneous show only when
    // the recipe already has some, so no data is ever hidden.
    std::vector<Tab> SimpleTabs() const {
        std::vector<Tab> tabs = {Tab::recipe, Tab::fermentables, Tab::hops, Tab::cultures};
        if (recipe().waters && !recipe().waters->empty()) tabs.push_back(Tab::water);
        if (recipe().miscellaneous && !recipe().miscellaneous->empty()) tabs.push_back(Tab::miscellaneous);
        tabs.push_back(Tab::results);
        if (!isDrink()) tabs.push_back(Tab::style);
        tabs.push_back(Tab::procedure);
        tabs.push_back(Tab::journal);
        tabs.push_back(Tab::equipment);
        tabs.push_back(Tab::notes);
        return tabs;
    }

    std::vector<Tab> AllTabs() const {
        std::vector<Tab> tabs = {Tab::recipe, Tab::equipment};
        tabs.push_back(isDrink() ? Tab::fermentation : Tab::boilOff);
        tabs.push_back(Tab::procedure);
        tabs.push_back(Tab::starter);
        tabs.push_back(Tab::waterBuilder);
        tabs.push_back(Tab::mash);
        tabs.push_back(Tab::journal);
        tabs.push_back(Tab::results);
        if (results().drink) tabs.push_back(Tab::plan);
        if (!isDrink()) tabs.push_back(Tab::style);
        for (const Tab tab : {Tab::fermentables, Tab::hops, Tab::cultures, Tab::water, Tab::miscellaneous, Tab::notes}) {
            tabs.push_back(tab);
        }
        return tabs;
    }

    // The selected tab, or the recipe tab when the selected one no longer applies (after a change of type).
    Tab ActiveTab() const {
        return Contains(VisibleTabs(), selected_) ? selected_ : Tab::recipe;
    }

    std::vector<Tab> TabsIn(Group group) const {
        std::vector<Tab> out;
        const auto visible = VisibleTabs();
        for (const Tab tab : GroupTabs(group)) {
            if (Contains(visible, tab)) out.push_back(tab);
        }
        return out;
    }

    std::vector<Group> VisibleGroups() const {
        std::vector<Group> out;
        for (const Group group : AllGroups()) {
            if (!TabsIn(group).empty()) out.push_back(group);
        }
        return out;
    }

    void Select(Tab tab) {
        if (selected_ == tab) return;
        selected_ = tab;
        lastTab_[GroupOf(tab)] = tab;
        ctx_.redraw();
    }

    void SelectGroup(Group group) {
        const auto available = TabsIn(group);
        if (available.empty()) return;
        Tab target = available.front();
        const auto last = lastTab_.find(group);
        if (last != lastTab_.end() && Contains(available, last->second)) target = last->second;
        Select(target);
    }

    // MARK: Building

    // The settings that change how the recipe is shown.
    std::string Signature() const {
        const Preferences& p = ctx_.host.store.preferences;
        return p.string(PrefKey::units) + "|" + p.string(PrefKey::simpleMode) + "|" + p.string(PrefKey::groupedTabs) + "|" +
               std::to_string(ctx_.host.store.waterProfiles.profiles().size()) + "|" +
               std::to_string(ctx_.host.store.equipment.custom().size());
    }

    void Rebuild() {
        ctx_.dirty = false;
        drawnRevision_ = ctx_.document->revision();
        drawnSettings_ = Signature();
        building_ = true;
        focus_.Capture(host().xamlRoot(), content_.Content().try_as<xaml::UIElement>());

        const Tab active = ActiveTab();
        BuildBars(active);

        strip_.Content(BuildStrip());
        strip_.Visibility(active == Tab::results ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);

        ui::Form form;
        BuildSection(active, form);
        const auto screen = form.Build();
        content_.Content(screen);
        screen.UpdateLayout();
        focus_.Restore(screen);
        building_ = false;
    }

    void BuildBars(Tab active) {
        const auto makeBar = [this](const std::vector<std::pair<std::string, int>>& items, int selectedValue,
                                    std::function<void(int)> chosen) {
            ctl::SelectorBar bar;
            for (const auto& [title, value] : items) {
                ctl::SelectorBarItem item;
                item.Text(H(title));
                item.Tag(box_value(value));
                bar.Items().Append(item);
                if (value == selectedValue) bar.SelectedItem(item);
            }
            bar.SelectionChanged([this, chosen = std::move(chosen)](ctl::SelectorBar const& sender, auto const&) {
                if (building_) return;
                const auto item = sender.SelectedItem();
                if (item) chosen(winrt::unbox_value<int>(item.Tag()));
            });
            ctl::ScrollViewer scroller;
            scroller.HorizontalScrollBarVisibility(ctl::ScrollBarVisibility::Auto);
            scroller.VerticalScrollBarVisibility(ctl::ScrollBarVisibility::Disabled);
            scroller.HorizontalScrollMode(ctl::ScrollMode::Auto);
            scroller.Padding(xaml::Thickness{12, 4, 12, 0});
            scroller.Content(bar);
            return scroller;
        };

        if (groupedTabs()) {
            std::vector<std::pair<std::string, int>> groups;
            for (const Group group : VisibleGroups()) groups.push_back({GroupTitle(group), static_cast<int>(group)});
            groupBar_.Content(makeBar(groups, static_cast<int>(GroupOf(active)),
                                      [this](int value) { SelectGroup(static_cast<Group>(value)); }));
            groupBar_.Visibility(xaml::Visibility::Visible);
            const auto tabs = TabsIn(GroupOf(active));
            std::vector<std::pair<std::string, int>> items;
            for (const Tab tab : tabs) items.push_back({TabTitle(tab, simple()), static_cast<int>(tab)});
            tabBar_.Content(makeBar(items, static_cast<int>(active), [this](int value) { Select(static_cast<Tab>(value)); }));
            tabBar_.Visibility(tabs.size() > 1 ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
        } else {
            groupBar_.Visibility(xaml::Visibility::Collapsed);
            std::vector<std::pair<std::string, int>> items;
            for (const Tab tab : VisibleTabs()) items.push_back({TabTitle(tab, simple()), static_cast<int>(tab)});
            tabBar_.Content(makeBar(items, static_cast<int>(active), [this](int value) { Select(static_cast<Tab>(value)); }));
            tabBar_.Visibility(xaml::Visibility::Visible);
        }
    }

    // The recipe's headline numbers, pinned above the tabs so they stay in view while ingredients change. With a style
    // chosen, each number is marked by whether it falls inside the style's range. Clicking the strip opens Results.
    xaml::UIElement BuildStrip() {
        const auto comparison = StyleComparisonOrNone();
        const auto fitOf = [&](calc::Metric metric) -> std::optional<calc::Fit> {
            if (!comparison) return std::nullopt;
            for (const calc::Check& check : comparison->checks) {
                if (check.metric == metric) return check.fit;
            }
            return std::nullopt;
        };

        ctl::Grid grid;
        const auto tile = [&](const std::string& title, std::optional<Term> term, std::optional<double> value, int places,
                              const std::string& suffix, std::optional<calc::Metric> metric, bool swatch) {
            ctl::StackPanel panel;
            panel.Spacing(2);
            panel.HorizontalAlignment(xaml::HorizontalAlignment::Center);
            ctl::TextBlock caption = ui::Text(title, ui::Kind::caption);
            caption.HorizontalAlignment(xaml::HorizontalAlignment::Center);
            panel.Children().Append(caption);

            ctl::StackPanel line;
            line.Orientation(ctl::Orientation::Horizontal);
            line.Spacing(4);
            line.HorizontalAlignment(xaml::HorizontalAlignment::Center);
            if (swatch && value) {
                const Rgb rgb = srmColor(*value);
                ctl::Grid dot;
                dot.Width(12);
                dot.Height(12);
                dot.VerticalAlignment(xaml::VerticalAlignment::Center);
                winrt::Microsoft::UI::Xaml::Shapes::Ellipse ellipse;
                ellipse.Width(12);
                ellipse.Height(12);
                ellipse.Fill(ui::Solid(rgb.r, rgb.g, rgb.b));
                ellipse.Stroke(ui::ThemeBrush(L"ControlStrongStrokeColorDefaultBrush"));
                ellipse.StrokeThickness(0.5);
                dot.Children().Append(ellipse);
                line.Children().Append(dot);
            }
            ctl::TextBlock shown = ui::Text(value ? number(*value, places, places) + suffix : "\xE2\x80\x94", ui::Kind::strong);
            shown.FontSize(18);
            line.Children().Append(shown);
            if (metric) {
                if (const auto fit = fitOf(*metric)) {
                    ctl::FontIcon mark;
                    mark.FontSize(14);
                    switch (*fit) {
                        case calc::Fit::Within:
                            mark.Glyph(L"\xE73E");
                            mark.Foreground(ui::ThemeBrush(L"SystemFillColorSuccessBrush"));
                            line.Children().Append(mark);
                            break;
                        case calc::Fit::Below:
                            mark.Glyph(L"\xE74B");
                            mark.Foreground(ui::ThemeBrush(L"SystemFillColorCautionBrush"));
                            line.Children().Append(mark);
                            break;
                        case calc::Fit::Above:
                            mark.Glyph(L"\xE74A");
                            mark.Foreground(ui::ThemeBrush(L"SystemFillColorCautionBrush"));
                            line.Children().Append(mark);
                            break;
                        case calc::Fit::Unknown: break;
                    }
                }
            }
            panel.Children().Append(line);
            ui::SetTip(panel, term ? explanation(*term) : std::string());
            const int column = static_cast<int>(grid.ColumnDefinitions().Size());
            ctl::ColumnDefinition definition;
            definition.Width(xaml::GridLength{1, xaml::GridUnitType::Star});
            grid.ColumnDefinitions().Append(definition);
            ctl::Grid::SetColumn(panel, column);
            grid.Children().Append(panel);
        };

        const calc::Results& r = results();
        tile("OG", Term::originalGravity, r.originalGravity, 3, "", calc::Metric::OriginalGravity, false);
        tile("FG", Term::finalGravity, r.finalGravity, 3, "", calc::Metric::FinalGravity, false);
        tile("ABV", Term::abv, r.abv, 1, "%", calc::Metric::Abv, false);
        if (!isDrink()) {
            tile("IBU", Term::ibu, r.ibu, 0, "", calc::Metric::Ibu, false);
            tile("Color", Term::srm, r.srm, 1, " SRM", calc::Metric::Srm, true);
            if (!simple()) {
                tile("Post-boil", std::nullopt,
                     r.postBoilLiters ? std::optional<double>(units().displayVolume(*r.postBoilLiters)) : std::nullopt, 1,
                     " " + units().volumeSymbol(), std::nullopt, false);
            }
        }
        if (r.cost) {
            ctl::StackPanel panel;
            panel.Spacing(2);
            panel.HorizontalAlignment(xaml::HorizontalAlignment::Center);
            ctl::TextBlock caption = ui::Text("Cost", ui::Kind::caption);
            caption.HorizontalAlignment(xaml::HorizontalAlignment::Center);
            panel.Children().Append(caption);
            ctl::TextBlock shown = ui::Text(money(r.cost->total, r.cost->currency), ui::Kind::strong);
            shown.FontSize(18);
            panel.Children().Append(shown);
            ui::SetTip(panel, r.cost->unpriced == 0
                                  ? "What the ingredients cost"
                                  : "What the priced ingredients cost. " + std::to_string(r.cost->unpriced) +
                                        " without a price are not counted.");
            ctl::ColumnDefinition definition;
            definition.Width(xaml::GridLength{1, xaml::GridUnitType::Star});
            ctl::Grid::SetColumn(panel, static_cast<int>(grid.ColumnDefinitions().Size()));
            grid.ColumnDefinitions().Append(definition);
            grid.Children().Append(panel);
        }

        ctl::Button button;
        button.Content(grid);
        button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
        button.Padding(xaml::Thickness{12, 8, 12, 8});
        button.CornerRadius(xaml::CornerRadius{0, 0, 0, 0});
        button.Background(ui::ThemeBrush(L"LayerFillColorDefaultBrush"));
        button.BorderThickness(xaml::Thickness{0, 0, 0, 1});
        button.BorderBrush(ui::ThemeBrush(L"DividerStrokeColorDefaultBrush"));
        ui::SetTip(button, "Show the full results");
        button.Click([this](auto const&, auto const&) { Select(Tab::results); });
        return button;
    }

    std::optional<StyleComparison> StyleComparisonOrNone() const {
        try {
            return ctx_.document->styleComparison(ctx_.key.id);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    // MARK: Sections

    void BuildSection(Tab tab, ui::Form& form) {
        switch (tab) {
            case Tab::recipe:
                RecipeSection(form);
                OverviewSection(form);
                break;
            case Tab::equipment: EquipmentSection(form); break;
            case Tab::boilOff: BoilOffSection(form); break;
            case Tab::fermentation: FermentationSection(form); break;
            case Tab::procedure: BuildProcedureTab(ctx_, form); break;
            case Tab::starter: BuildStarterTab(ctx_, form); break;
            case Tab::waterBuilder: BuildWaterTab(ctx_, form); break;
            case Tab::mash: BuildMashTab(ctx_, form); break;
            case Tab::journal:
                BuildJournalTab(ctx_, form);
                JournalCalculators(form);
                break;
            case Tab::results: ResultsSection(form); break;
            case Tab::plan: PlanSection(form); break;
            case Tab::style: StyleSection(form); break;
            case Tab::fermentables: FermentablesSection(form); break;
            case Tab::hops:
                HopsSection(form);
                HopCalculators(form);
                break;
            case Tab::cultures:
                CulturesSection(form);
                YeastCalculators(form);
                break;
            case Tab::water: WaterSection(form); break;
            case Tab::miscellaneous: MiscellaneousSection(form); break;
            case Tab::notes: NotesSection(form); break;
        }
    }

    void RecipeSection(ui::Form& form) {
        form.Section("Recipe", simple() ? "Simple mode is on. Mash, water chemistry, yeast starter, and boil-off use typical "
                                          "values for your brewing system. Turn it off in the View menu or Settings for full control."
                                        : "");
        auto& c = ctx_;
        const EntityId id = c.id();
        form.Labeled("Name", ui::Field(recipe().name, [&c, id](std::string text) {
                         c.attempt([&] { c.document->rename(id, text); });
                     }, 260, false));
        form.Labeled("Type", ui::Picker(ui::Choices(recipeTypes()), model::recipeTypeName(recipe().type),
                                        [&c, id](std::string type) { c.attempt([&] { c.document->setType(id, type); }); }, 180));
        form.Labeled("Currency", ui::Picker(ui::Choices(options(currencyCodes(), recipe().currency)), recipe().currency,
                                            [&c, id](std::string code) { c.attempt([&] { c.document->setRecipeCurrency(id, code); }); }, 120),
                     "The currency of the ingredient prices");
        const QuantityText batch = describe(recipe().batchSize);
        form.Labeled("Batch size", ui::HStack({ui::Field(batch.value, [&c, id, unit = batch.unit](std::string text) {
                                                   c.attempt([&] { c.document->setBatchSize(id, text, unit); });
                                               }, 100), ui::Text(batch.unit)}, 8));
    }

    // The recipe at a glance: its style and the bill of ingredients with their amounts.
    void OverviewSection(ui::Form& form) {
        form.Section("Overview", "");
        form.Labeled("Style", ui::Text(StyleTitleOf(recipe().bjcpStyle)));
        bool any = false;
        const auto row = [&](const std::string& name, const std::string& amount) {
            form.Labeled(name, ui::Text(amount));
            any = true;
        };
        for (const model::Fermentable& item : recipe().fermentables) row(item.name, describe(item.amount).display());
        for (const model::Hop& item : recipe().hops.value_or(std::vector<model::Hop>{})) row(item.name, describe(item.amount).display());
        for (const model::Culture& item : recipe().cultures.value_or(std::vector<model::Culture>{})) {
            row(item.name, describe(item.amount).display());
        }
        for (const model::WaterAddition& item : recipe().waters.value_or(std::vector<model::WaterAddition>{})) {
            row(item.water.name, item.amount ? describe(*item.amount).display() : "");
        }
        for (const model::Miscellaneous& item : recipe().miscellaneous.value_or(std::vector<model::Miscellaneous>{})) {
            row(item.name, describe(item.amount).display());
        }
        if (!any) form.Note("No ingredients yet.");
    }

    void EquipmentSection(ui::Form& form) {
        form.Section("Equipment", "Applying a system sets the batch size, brewhouse efficiency, and boil-off in one undo step. "
                                  "The figures are starting points, so measure your own system.");
        auto& c = ctx_;
        std::vector<ui::Choice> choices;
        for (const EquipmentProfile& profile : store().equipment.all()) choices.push_back({profile.name, profile.name});
        const EquipmentProfile profile = c.equipment();
        form.Labeled("Brewing system", ui::Picker(choices, profile.name, [this](std::string name) {
                         ctx_.equipmentName = name;
                         ctx_.redraw();
                     }, 300));
        form.Note(profile.note, ui::Kind::caption);
        form.Labeled(ui::HelpLabel("Brewhouse efficiency", explanation(Term::brewhouseEfficiency), simple()),
                     ui::Text(describe(recipe().efficiency.brewhouse).display()));
        form.Wide(ui::Button("Apply to Recipe", [this] { ApplyEquipment(); }));
    }

    void ApplyEquipment() {
        const EquipmentProfile profile = ctx_.equipment();
        const Units u = units();
        ctx_.attempt([&] {
            ctx_.document->applyEquipment(ctx_.id(), u.volumeTextFromLiters(profile.batchLiters), u.volumeUnit(),
                                          profile.efficiencyPercent, profile.evaporationPercent, profile.trubLossLiters);
        });
    }

    void BoilOffSection(ui::Form& form) {
        form.Section("Boil-off");
        auto& c = ctx_;
        const Units u = units();
        const QuantityText evaporation = describe(recipe().boilOff.evaporationRate);
        const QuantityText trub = describe(recipe().boilOff.trubLoss);
        const std::string trubShown = u.volumeTextFromLiters(trub.value);
        const auto commit = [this](std::string evaporationText, std::string trubText) {
            const Units now = units();
            const QuantityText current = describe(recipe().boilOff.trubLoss);
            // An untouched field keeps the recipe's own figure, so rounding the display never changes it.
            const bool untouched = trubText == now.volumeTextFromLiters(current.value);
            ctx_.attempt([&] {
                ctx_.document->setBoilOff(ctx_.id(), evaporationText, untouched ? current.value : now.litersTextFromVolume(trubText));
            });
        };
        auto evaporationField = ui::Field(evaporation.value, [this, commit](std::string text) {
            commit(text, units().volumeTextFromLiters(describe(recipe().boilOff.trubLoss).value));
        });
        form.Labeled(ui::HelpLabel("Evaporation (% per hour)", explanation(Term::evaporation), simple()), evaporationField);
        auto trubField = ui::Field(trubShown, [this, commit](std::string text) {
            commit(describe(recipe().boilOff.evaporationRate).value, text);
        });
        form.Labeled(ui::HelpLabel("Trub and chiller loss (" + u.volumeSymbol() + ")", explanation(Term::trubLoss), simple()),
                     trubField);
        (void)c;
    }

    void FermentationSection(ui::Form& form) {
        form.Section("Fermentation", "The share of sugar that yeast turns into alcohol. Used to plan the sugar a drink needs.");
        auto& c = ctx_;
        const EntityId id = c.id();
        form.Labeled(ui::HelpLabel("Fermentation efficiency (%)", explanation(Term::fermentationEfficiency), simple()),
                     ui::Field(describe(recipe().fermentationEfficiency).value, [&c, id](std::string text) {
                         c.attempt([&] { c.document->setFermentationEfficiency(id, text); });
                     }));
    }

    // A calculated value, or a dash when the engine could not compute it.
    void ResultRow(ui::Form& form, const std::string& title, std::optional<double> value, int places,
                   std::optional<Term> term = std::nullopt) {
        const std::string shown = value ? number(*value, places, places) : "\xE2\x80\x94";
        if (term) {
            form.Labeled(ui::HelpLabel(title, explanation(*term), simple()), ui::Text(shown));
        } else {
            form.Labeled(title, ui::Text(shown));
        }
    }

    void ResultsSection(ui::Form& form) {
        form.Section("Results");
        const calc::Results& r = results();
        ResultRow(form, "Original gravity", r.originalGravity, 3, Term::originalGravity);
        ResultRow(form, "Final gravity", r.finalGravity, 3, Term::finalGravity);
        ResultRow(form, "Alcohol (% by volume)", r.abv, 2, Term::abv);
        if (!isDrink()) {
            ResultRow(form, "Bitterness (IBU)", r.ibu, 1, Term::ibu);
            ResultRow(form, "Color (SRM)", r.srm, 1, Term::srm);
        }
        if (!isDrink() && !simple()) {
            ResultRow(form, "Post-boil volume (" + units().volumeSymbol() + ")",
                      r.postBoilLiters ? std::optional<double>(units().displayVolume(*r.postBoilLiters)) : std::nullopt, 2);
        }
        if (!simple() && r.sulfiteSo2Ppm) {
            ResultRow(form, "Sulfite SO2 added (mg/L)", r.sulfiteSo2Ppm, 1);
        }
        if (r.cost) {
            CostSection(form, *r.cost);
        }
    }

    // What the priced ingredients cost. Only a recipe with at least one price has this.
    void CostSection(ui::Form& form, const calc::RecipeCost& cost) {
        form.Section("Cost", "Prices are the ones you entered, for the amounts in this recipe. They are not looked up.");
        form.Labeled("Ingredients", ui::Text(money(cost.total, cost.currency)));
        const auto perVolume = units().metric() ? cost.perLiter : cost.perGallon;
        if (perVolume) {
            form.Labeled(std::string("Per ") + (units().metric() ? "liter" : "gallon"), ui::Text(money(*perVolume, cost.currency)));
        }
        if (cost.unpriced > 0) {
            const std::size_t n = cost.unpriced;
            form.Note(std::to_string(n) + (n == 1 ? " ingredient has" : " ingredients have") + " no price, so " +
                      (n == 1 ? "it is" : "they are") + " not counted. Water is never priced.", ui::Kind::caption);
        }
    }

    void PlanSection(ui::Form& form) {
        form.Section("Plan");
        if (const auto& plan = results().drink) {
            ResultRow(form, "Sugar to reach target gravity (g)", plan->gravityGrams, 1);
            ResultRow(form, "Sugar to reach target alcohol (g)", plan->alcoholGrams, 1);
            for (const std::string& note : plan->notes) form.Note(note);
        }
    }

    // A scale with the style's range shaded and a marker at the recipe's value. The scale stretches to include the
    // value, so a value outside the range still shows.
    static xaml::UIElement RangeBar(double value, double low, double high) {
        double lower = std::min(low, value);
        double upper = std::max(high, value);
        const double span = upper - lower;
        if (span == 0) {
            lower -= 0.5;
            upper += 0.5;
        } else {
            lower -= span * 0.1;
            upper += span * 0.1;
        }
        const double total = upper - lower;
        const auto star = [](double weight) {
            return xaml::GridLength{std::max(weight, 0.0001), xaml::GridUnitType::Star};
        };

        ctl::Grid track;
        track.Height(12);
        // The shaded range.
        ctl::Grid ranges;
        for (const double weight : {low - lower, high - low, upper - high}) {
            ctl::ColumnDefinition column;
            column.Width(star(weight / total));
            ranges.ColumnDefinitions().Append(column);
        }
        ctl::Border back;
        back.Height(6);
        back.CornerRadius(xaml::CornerRadius{3, 3, 3, 3});
        back.Background(ui::ThemeBrush(L"ControlStrongFillColorDisabledBrush"));
        back.VerticalAlignment(xaml::VerticalAlignment::Center);
        track.Children().Append(back);
        ctl::Border shade;
        shade.Height(6);
        shade.CornerRadius(xaml::CornerRadius{3, 3, 3, 3});
        shade.Background(ui::ThemeBrush(L"AccentFillColorDefaultBrush"));
        shade.Opacity(0.6);
        shade.VerticalAlignment(xaml::VerticalAlignment::Center);
        ctl::Grid::SetColumn(shade, 1);
        ranges.Children().Append(shade);
        track.Children().Append(ranges);

        // The marker, between two stretches that put it at the value.
        ctl::Grid marker;
        const double at = (value - lower) / total;
        ctl::ColumnDefinition before;
        before.Width(star(at));
        ctl::ColumnDefinition dotColumn;
        dotColumn.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        ctl::ColumnDefinition after;
        after.Width(star(1 - at));
        marker.ColumnDefinitions().Append(before);
        marker.ColumnDefinitions().Append(dotColumn);
        marker.ColumnDefinitions().Append(after);
        winrt::Microsoft::UI::Xaml::Shapes::Ellipse dot;
        dot.Width(10);
        dot.Height(10);
        dot.Fill(ui::ThemeBrush(L"TextFillColorPrimaryBrush"));
        dot.Margin(xaml::Thickness{-5, 0, -5, 0});
        ctl::Grid::SetColumn(dot, 1);
        marker.Children().Append(dot);
        track.Children().Append(marker);
        return track;
    }

    // The name of a style as the picker shows it, or "None" for no style.
    std::string StyleTitleOf(const std::optional<std::string>& id) const {
        if (!id) return "None";
        for (const StyleChoice& choice : ctx_.document->styleChoices()) {
            if (choice.id == *id) return choice.title;
        }
        return "None";
    }

    void StyleSection(ui::Form& form) {
        form.Section("Style", "Compares the results with the style's range. The marker shows where the recipe falls in "
                              "that range.");
        auto& c = ctx_;
        const EntityId id = c.id();
        std::vector<ui::Choice> choices = {{"", "None"}};
        for (const StyleChoice& style : c.document->styleChoices()) {
            choices.push_back({style.id, style.custom ? style.title + " (yours)" : style.title});
        }
        form.Labeled("Style", ui::Picker(choices, recipe().bjcpStyle.value_or(""), [&c, id](std::string value) {
                         c.attempt([&] {
                             c.document->setStyle(id, value.empty() ? std::nullopt : std::optional<std::string>(value));
                         });
                     }, 280));
        if (const auto comparison = StyleComparisonOrNone()) {
            for (const calc::Check& check : comparison->checks) {
                StyleCheckRow(form, check, styles::isUserStyleId(comparison->id));
            }
        }

        form.SectionWithAction("Your styles", "New Style\xE2\x80\xA6", [this] { OpenStyleEditor(std::nullopt); },
                               "Styles you define are saved in this recipe's file, so they travel with it.");
        for (const styles::UserStyle& style : c.document->userStyles()) {
            ctl::HyperlinkButton link = ui::LinkButton(style.name, [this, style] { OpenStyleEditor(style); });
            ctl::Button removeButton = ui::IconButton(ctl::Symbol::Remove, "Remove " + style.name, [this, styleId = style.id] {
                ctx_.attempt([&] { ctx_.document->removeUserStyle(styleId); });
            });
            form.Wide(ui::HStack({link, removeButton}, 12));
        }
    }

    // The editor for a new style, or for one of the user's styles. A new style is chosen for this recipe when it is saved.
    void OpenStyleEditor(std::optional<styles::UserStyle> existing) {
        dialogs::StyleRequest request;
        request.title = existing ? "Edit Style" : "New Style";
        request.seed = existing.value_or(styles::UserStyle{});
        const EntityId id = ctx_.id();
        request.save = [this, id, isNew = !existing](const styles::UserStyle& saved) {
            ctx_.document->grouped(isNew ? "New Style" : "Edit Style", [&] {
                if (isNew) {
                    const std::string created = ctx_.document->addUserStyle(saved);
                    ctx_.document->setStyle(id, created);
                } else {
                    ctx_.document->updateUserStyle(saved);
                }
            });
            ctx_.attempt([] {});  // schedules the autosave and draws the screens again
        };
        if (existing) {
            request.remove = [this, styleId = existing->id] {
                ctx_.attempt([&] { ctx_.document->removeUserStyle(styleId); });
            };
        }
        dialogs::ShowStyleEditor(host(), std::move(request));
    }

    // One calculated value, its range, and where the value falls in that range. The range is the BJCP guideline's for a
    // guideline style, and the user's own otherwise.
    void StyleCheckRow(ui::Form& form, const calc::Check& check, bool ownStyle) {
        std::string title;
        int places = 1;
        switch (check.metric) {
            case calc::Metric::OriginalGravity: title = "Original gravity"; places = 3; break;
            case calc::Metric::FinalGravity: title = "Final gravity"; places = 3; break;
            case calc::Metric::Abv: title = "Alcohol (% by volume)"; break;
            case calc::Metric::Ibu: title = "Bitterness (IBU)"; break;
            case calc::Metric::Srm: title = "Color (SRM)"; break;
        }
        const int rangePlaces = check.metric == calc::Metric::Ibu ? 0 : places;
        std::string fitText;
        switch (check.fit) {
            case calc::Fit::Below: fitText = "below the range"; break;
            case calc::Fit::Above: fitText = "above the range"; break;
            case calc::Fit::Within: fitText = "within the range"; break;
            case calc::Fit::Unknown: fitText = "no value yet"; break;
        }
        ctl::StackPanel panel;
        panel.Spacing(6);
        ctl::Grid line;
        line.ColumnDefinitions().Append(ctl::ColumnDefinition());
        ctl::ColumnDefinition right;
        right.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        line.ColumnDefinitions().Append(right);
        line.Children().Append(ui::Text(title));
        ctl::TextBlock shown = ui::Text(check.value ? number(*check.value, places, places) : "\xE2\x80\x94");
        ctl::Grid::SetColumn(shown, 1);
        line.Children().Append(shown);
        panel.Children().Append(line);
        if (check.value) {
            panel.Children().Append(RangeBar(*check.value, check.range.low, check.range.high));
        }
        panel.Children().Append(ui::Text(std::string(ownStyle ? "Your range " : "BJCP range ") + number(check.range.low, rangePlaces, rangePlaces) + "\xE2\x80\x93" +
                                             number(check.range.high, rangePlaces, rangePlaces) + " \xC2\xB7 " + fitText,
                                         ui::Kind::caption));
        form.Wide(panel);
    }

    // MARK: Ingredient lists

    // An ingredient's name, which opens its editor, a line of detail, and the means to duplicate or remove it.
    void IngredientRow(ui::Form& form, const std::string& name, const std::string& detail, std::function<void()> edit,
                       std::function<void()> duplicate, std::function<void()> remove) {
        ctl::Grid grid;
        grid.ColumnSpacing(12);
        grid.ColumnDefinitions().Append(ctl::ColumnDefinition());
        ctl::ColumnDefinition detailColumn;
        detailColumn.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(detailColumn);
        ctl::ColumnDefinition removeColumn;
        removeColumn.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(removeColumn);

        ctl::HyperlinkButton link = ui::LinkButton(name, edit);
        link.VerticalAlignment(xaml::VerticalAlignment::Center);
        ui::SetTip(link, "Edit " + name);
        grid.Children().Append(link);
        ctl::TextBlock detailText = ui::Text(detail, ui::Kind::secondary);
        detailText.VerticalAlignment(xaml::VerticalAlignment::Center);
        detailText.TextAlignment(xaml::TextAlignment::Right);
        detailText.MaxWidth(420);
        ctl::Grid::SetColumn(detailText, 1);
        grid.Children().Append(detailText);
        ctl::Button removeButton = ui::IconButton(ctl::Symbol::Remove, "Remove " + name, remove);
        ctl::Grid::SetColumn(removeButton, 2);
        grid.Children().Append(removeButton);

        ctl::MenuFlyout menu;
        const auto entry = [](const std::string& text, std::function<void()> action) {
            ctl::MenuFlyoutItem item;
            item.Text(H(text));
            item.Click([action = std::move(action)](auto const&, auto const&) { action(); });
            return item;
        };
        menu.Items().Append(entry("Edit", edit));
        menu.Items().Append(entry("Duplicate", duplicate));
        menu.Items().Append(entry("Remove", remove));
        grid.ContextFlyout(menu);
        form.Wide(grid);
    }

    void OpenEditor(IngredientDraft draft, std::optional<EntityId> item) {
        dialogs::IngredientRequest request;
        const std::string kindTitle = ingredientKindTitle(draft.kind);
        request.title = (item ? "Edit " : "Add ") + kindTitle;
        request.saveTitle = item ? "Save" : "Add";
        request.seed = draft;
        if (!item) {
            request.menu = menuEntries(draft.kind, store().customIngredients.ingredients(), units().system);
        }
        request.currency = recipe().currency;
        const std::string label = request.title;
        request.save = [this, label, item](const IngredientDraft& saved) {
            // The ingredient and its price are one edit, so one undo takes back both.
            ctx_.document->grouped(label, [&] { saved.commit(*ctx_.document, ctx_.id(), item); });
            store().attempt([] {});  // schedules the autosave and draws the screens again
        };
        dialogs::ShowIngredientEditor(host(), std::move(request));
    }

    void Add(IngredientKind kind) {
        OpenEditor(IngredientDraft(kind, units().system), std::nullopt);
    }

    void FermentablesSection(ui::Form& form) {
        form.SectionWithAction("Fermentables", "Add", [this] { Add(IngredientKind::fermentable); });
        if (recipe().fermentables.empty()) form.Note("No fermentables yet.");
        const auto& c = ctx_;
        for (const model::Fermentable& item : recipe().fermentables) {
            const EntityId itemId = item.id;
            IngredientRow(form, item.name,
                          fermentableDetail(item) + priceDetail(recipe(), results(), item.price, item.uuid),
                          [this, item] { OpenEditor(IngredientDraft::from(item, units().system), item.id); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->duplicateIngredient(IngredientKind::fermentable, ctx_.id(), itemId); }); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->removeFermentable(ctx_.id(), itemId); }); });
        }
        (void)c;
    }

    void HopsSection(ui::Form& form) {
        form.SectionWithAction("Hops", "Add", [this] { Add(IngredientKind::hop); });
        const auto hops = recipe().hops.value_or(std::vector<model::Hop>{});
        if (hops.empty()) form.Note("No hops yet.");
        for (const model::Hop& item : hops) {
            const EntityId itemId = item.id;
            IngredientRow(form, item.name, hopDetail(item) + priceDetail(recipe(), results(), item.price, item.uuid),
                          [this, item] { OpenEditor(IngredientDraft::from(item, units().system), item.id); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->duplicateIngredient(IngredientKind::hop, ctx_.id(), itemId); }); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->removeHop(ctx_.id(), itemId); }); });
        }
    }

    void CulturesSection(ui::Form& form) {
        form.SectionWithAction(simple() ? "Yeast" : "Yeast & Cultures", "Add", [this] { Add(IngredientKind::culture); });
        const auto cultures = recipe().cultures.value_or(std::vector<model::Culture>{});
        if (cultures.empty()) form.Note("No yeast or other cultures yet.");
        for (const model::Culture& item : cultures) {
            const EntityId itemId = item.id;
            IngredientRow(form, item.name, cultureDetail(item) + priceDetail(recipe(), results(), item.price, item.uuid),
                          [this, item] { OpenEditor(IngredientDraft::from(item, units().system), item.id); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->duplicateIngredient(IngredientKind::culture, ctx_.id(), itemId); }); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->removeCulture(ctx_.id(), itemId); }); });
        }
    }

    void WaterSection(ui::Form& form) {
        form.SectionWithAction("Water", "Add", [this] { Add(IngredientKind::water); });
        const auto waters = recipe().waters.value_or(std::vector<model::WaterAddition>{});
        if (waters.empty()) form.Note("No water yet.");
        for (const model::WaterAddition& item : waters) {
            const EntityId itemId = item.id;
            IngredientRow(form, item.water.name, waterDetail(item),
                          [this, item] { OpenEditor(IngredientDraft::from(item, units().system), item.id); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->duplicateIngredient(IngredientKind::water, ctx_.id(), itemId); }); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->removeWater(ctx_.id(), itemId); }); });
        }
    }

    void MiscellaneousSection(ui::Form& form) {
        form.SectionWithAction("Miscellaneous", "Add", [this] { Add(IngredientKind::miscellaneous); });
        const auto items = recipe().miscellaneous.value_or(std::vector<model::Miscellaneous>{});
        if (items.empty()) form.Note("No miscellaneous ingredients yet.");
        for (const model::Miscellaneous& item : items) {
            const EntityId itemId = item.id;
            IngredientRow(form, item.name, describe(item.amount).display() + priceDetail(recipe(), results(), item.price, item.uuid),
                          [this, item] { OpenEditor(IngredientDraft::from(item, units().system), item.id); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->duplicateIngredient(IngredientKind::miscellaneous, ctx_.id(), itemId); }); },
                          [this, itemId] { ctx_.attempt([&] { ctx_.document->removeMiscellaneous(ctx_.id(), itemId); }); });
        }
    }

    // MARK: Calculators

    void OpenTool(ToolKind kind, std::map<std::string, std::string> prefill) {
        dialogs::ShowToolDialog(host(), kind, std::move(prefill));
    }

    // A button that opens a calculator for the recipe, with its figures filled in.
    xaml::UIElement CalculatorButton(const std::string& title, ctl::Symbol symbol, std::function<void()> open) {
        ctl::Button button;
        button.Content(ui::HStack({ctl::SymbolIcon(symbol), ui::Text(title)}, 8));
        ui::SetTip(button, "Opens a calculator with this recipe's figures filled in");
        button.Click([open = std::move(open)](auto const&, auto const&) { open(); });
        return button;
    }

    // Checks the bitterness of a boil hop with the Tinseth calculator, filled in from the recipe.
    void HopCalculators(ui::Form& form) {
        std::vector<model::Hop> boilHops;
        for (const model::Hop& hop : recipe().hops.value_or(std::vector<model::Hop>{})) {
            if (hop.timing.use == "add_to_boil" && hop.timing.time && describe(*hop.timing.time).value != "0") {
                boilHops.push_back(hop);
            }
        }
        if (boilHops.empty()) return;
        form.Section("Calculators");
        ctl::DropDownButton menuButton;
        menuButton.Content(ui::HStack({ctl::SymbolIcon(ctl::Symbol::Calculator), ui::Text("Check a Hop's Bitterness…")}, 8));
        ui::SetTip(menuButton, "Opens the bitterness calculator with this hop's figures filled in");
        ctl::MenuFlyout menu;
        for (const model::Hop& hop : boilHops) {
            ctl::MenuFlyoutItem item;
            item.Text(H(hop.name + ", " + describe(*hop.timing.time).value + " min"));
            item.Click([this, hop](auto const&, auto const&) {
                OpenTool(ToolKind::hopBitterness, bitternessPrefill(recipe(), results(), hop));
            });
            menu.Items().Append(item);
        }
        menuButton.Flyout(menu);
        form.Wide(menuButton);
    }

    // The yeast starter calculator, which Simple Mode otherwise hides along with the Yeast Starter tab.
    void YeastCalculators(ui::Form& form) {
        form.Section("Calculators");
        form.Wide(CalculatorButton("Yeast Starter Calculator…", ctl::Symbol::Calculator, [this] {
            OpenTool(ToolKind::yeastStarter, starterPrefill(recipe(), results()));
        }));
    }

    // Corrections for the gravity readings a brewer takes on brew day.
    void JournalCalculators(ui::Form& form) {
        form.Section("Calculators");
        form.Wide(CalculatorButton("Correct a Hydrometer Reading for Temperature…", ctl::Symbol::Calculator,
                                   [this] { OpenTool(ToolKind::hydrometer, {}); }));
        form.Wide(CalculatorButton("Convert a Refractometer Reading…", ctl::Symbol::Calculator,
                                   [this] { OpenTool(ToolKind::refractometer, {}); }));
    }

    // MARK: Notes

    void NotesSection(ui::Form& form) {
        form.Section("Brewing Notes", "Your own notes for this recipe. They are saved when you click away.");
        auto& c = ctx_;
        form.Wide(ui::MultilineField(c.planning().notes, [&c](std::string text) {
            if (text == c.planning().notes) return;
            c.updatePlanning([&](RecipePlanning& planning) { planning.notes = text; });
        }, 180));
        if (!results().notes.empty()) {
            form.Section("Calculation Notes");
            for (const std::string& note : results().notes) form.Note(note);
        }
    }

    RecipeContext ctx_;
    Tab selected_ = Tab::recipe;
    std::map<Group, Tab> lastTab_;
    bool building_ = false;
    unsigned long long drawnRevision_ = 0;
    std::string drawnSettings_;

    ctl::Grid root_;
    ctl::ContentControl groupBar_;
    ctl::ContentControl tabBar_;
    ctl::ContentControl strip_;
    ctl::ContentControl content_;
    ui::FocusKeeper focus_;
};

}  // namespace

std::unique_ptr<View> MakeRecipeView(Host& host, const ItemKey& key) {
    return std::make_unique<RecipeView>(host, key);
}

}  // namespace kl
