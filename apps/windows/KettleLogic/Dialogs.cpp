#include "pch.h"

#include "Dialogs.h"

#include "Telemetry.h"
#include "Views.h"

#include "kettlelogic/app/glossary.hpp"
#include "kettlelogic/app/recipe_builder.hpp"
#include "kettlelogic/app/recipe_text.hpp"
#include "kettlelogic/app/text.hpp"

namespace kl::dialogs {

using namespace kettlelogic::app;
namespace styles = kettlelogic::styles;
using platform::H;
using platform::U;
using winrt::box_value;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

namespace {

ctl::StackPanel Row(std::initializer_list<xaml::UIElement> items) {
    return ui::HStack(items, 8);
}

// A form's panel inside a scroll viewer small enough for a dialog.
ctl::ScrollViewer InDialog(xaml::UIElement const& content, double maxHeight = 560) {
    ctl::ScrollViewer scroller;
    scroller.MaxHeight(maxHeight);
    scroller.VerticalScrollBarVisibility(ctl::ScrollBarVisibility::Auto);
    scroller.HorizontalScrollBarVisibility(ctl::ScrollBarVisibility::Disabled);
    scroller.Padding(xaml::Thickness{0, 0, 12, 0});
    scroller.Content(content);
    return scroller;
}

}  // namespace

// MARK: Ingredient editor

namespace {

// One editor for every kind of ingredient. The caller decides what saving does: a recipe's ingredient is added or
// changed in the engine, and a library entry is saved to the user's library. A refused value is shown in the editor,
// which stays open so the value can be fixed.
struct EditorState : std::enable_shared_from_this<EditorState> {
    Host* host = nullptr;
    IngredientRequest request;
    IngredientDraft draft;
    ctl::ContentDialog dialog{nullptr};
    ctl::ContentControl body;
    ctl::TextBlock errorText{nullptr};
    bool customTiming = false;
    bool showsMoreDetails = false;
    ui::FocusKeeper focus;

    bool simple() const { return host->simpleMode(); }
    bool quantities() const { return request.showsQuantities; }

    void UpdateSave() {
        if (dialog) dialog.IsPrimaryButtonEnabled(draft.isComplete(quantities()));
    }

    // A text field bound to one of the draft's values.
    ctl::TextBox Bound(std::string IngredientDraft::*member, double width = 100, bool right = true) {
        ctl::TextBox box;
        box.Text(H(draft.*member));
        if (width > 0) box.Width(width);
        if (right) box.TextAlignment(xaml::TextAlignment::Right);
        box.VerticalAlignment(xaml::VerticalAlignment::Center);
        auto self = weak_from_this();
        box.TextChanged([self, member](auto const& sender, auto const&) {
            if (auto state = self.lock()) {
                state->draft.*member = U(sender.template as<ctl::TextBox>().Text());
                state->UpdateSave();
            }
        });
        return box;
    }

    // A pick bound to one of the draft's values. `render` draws the editor again, for a pick that changes the fields.
    ctl::ComboBox Pick(std::string IngredientDraft::*member, const std::vector<ui::Choice>& choices, double width = 0,
                       bool render = false) {
        auto self = weak_from_this();
        return ui::Picker(choices, draft.*member, [self, member, render](std::string value) {
            if (auto state = self.lock()) {
                state->draft.*member = value;
                state->AfterChange(member == &IngredientDraft::unit || render);
            }
        }, width);
    }

    void AfterChange(bool redraw) {
        // A price per kg fits an amount in lb, but not one in liters, so a new amount unit keeps the price unit sensible.
        const std::vector<std::string> allowed = priceUnits(draft.unit);
        if (std::find(allowed.begin(), allowed.end(), draft.priceUnit) == allowed.end()) {
            draft.priceUnit = draft.unit;
            redraw = true;
        }
        UpdateSave();
        if (redraw) Render();
    }

    // The amount, with a stepper beside it in simple mode, and its unit.
    xaml::UIElement AmountRow(const std::vector<std::string>& units) {
        ctl::TextBox box = Bound(&IngredientDraft::amount, 100);
        auto self = weak_from_this();
        ctl::StackPanel field = Row({box});
        if (simple()) {
            const auto nudge = [self, box](int direction) {
                if (auto state = self.lock()) {
                    state->draft.amount = nudgedAmount(state->draft.amount, state->draft.unit, direction);
                    box.Text(H(state->draft.amount));
                }
            };
            ctl::StackPanel stepper;
            stepper.Spacing(0);
            ctl::Button up = ui::IconButton(ctl::Symbol::Up, "More", [nudge] { nudge(1); });
            ctl::Button down = ui::IconButton(ctl::Symbol::Download, "Less", [nudge] { nudge(-1); });
            up.Padding(xaml::Thickness{6, 0, 6, 0});
            down.Padding(xaml::Thickness{6, 0, 6, 0});
            up.MinHeight(16);
            down.MinHeight(16);
            stepper.Children().Append(up);
            stepper.Children().Append(down);
            field.Children().Append(stepper);
        }
        return Row({field, Pick(&IngredientDraft::unit, ui::Choices(options(units, draft.unit)), 90)});
    }

    // The technical fields. Simple mode folds them into a closed group, since the common-ingredient menu fills them in.
    void Advanced(ui::Form& form, const std::function<void(ui::Form&)>& fill) {
        if (!simple()) {
            fill(form);
            return;
        }
        ui::Form inner;
        inner.Section();
        fill(inner);
        ctl::Expander expander;
        expander.Header(ui::Text("More details"));
        expander.IsExpanded(showsMoreDetails);
        expander.Content(inner.BuildPanel());
        expander.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        expander.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
        auto self = weak_from_this();
        expander.Expanding([self](auto const&, auto const&) { if (auto s = self.lock()) s->showsMoreDetails = true; });
        expander.Collapsed([self](auto const&, auto const&) { if (auto s = self.lock()) s->showsMoreDetails = false; });
        form.Wide(expander);
    }

    void FermentableFields(ui::Form& form) {
        form.Labeled("Type", Pick(&IngredientDraft::type, ui::Choices(options(fermentableTypes(), draft.type)), 160));
        if (quantities()) form.Labeled("Amount", AmountRow(fermentableUnits()));
        Advanced(form, [this](ui::Form& f) {
            f.Labeled("Yield", Row({Bound(&IngredientDraft::yieldValue),
                                    Pick(&IngredientDraft::yieldUnit, ui::Choices(options({"sg", "plato", "brix"}, draft.yieldUnit)), 90)}));
            f.Labeled("Color", Row({Bound(&IngredientDraft::colorValue),
                                    Pick(&IngredientDraft::colorUnit, ui::Choices(options({"SRM", "EBC", "Lovi"}, draft.colorUnit)), 90)}));
        });
    }

    void HopFields(ui::Form& form) {
        form.Labeled(ui::HelpLabel("Alpha acid (%)", explanation(Term::alphaAcid), simple()), Bound(&IngredientDraft::alpha));
        Advanced(form, [this](ui::Form& f) {
            f.Labeled(ui::HelpLabel("Beta acid (%)", explanation(Term::betaAcid), simple()), Bound(&IngredientDraft::beta));
            std::vector<ui::Choice> withNone = {{"", "Not set"}};
            for (const std::string& value : options(hopForms(), draft.form)) withNone.push_back({value, value});
            f.Labeled("Form", Pick(&IngredientDraft::form, withNone, 140));
        });

        const bool presets = simple() && quantities();
        const HopTiming timing = customTiming ? HopTiming::custom : timingOf(draft);
        if (presets) {
            std::vector<ui::Choice> choices;
            for (const HopTiming option : allHopTimings()) choices.push_back({std::to_string(static_cast<int>(option)), hopTimingTitle(option)});
            auto self = weak_from_this();
            form.Labeled("When to add", ui::Picker(choices, std::to_string(static_cast<int>(timing)), [self](std::string value) {
                             if (auto state = self.lock()) {
                                 const HopTiming choice = static_cast<HopTiming>(std::stoi(value));
                                 state->customTiming = choice == HopTiming::custom;
                                 chooseHopTiming(state->draft, choice);
                                 state->Render();
                             }
                         }, 260));
            if (timing == HopTiming::dryHop) {
                form.Labeled("Days in the fermenter", Bound(&IngredientDraft::time));
            }
        }
        if (!presets || timing == HopTiming::custom) {
            std::vector<ui::Choice> uses = {{"", hopUseName("")}};
            for (const std::string& value : options(hopUses(), draft.use)) uses.push_back({value, hopUseName(value)});
            form.Labeled("Added to", Pick(&IngredientDraft::use, uses, 180));
        }
        if (quantities()) {
            form.Labeled("Amount", AmountRow(hopUnits()));
            if (!simple() || timing == HopTiming::custom) {
                form.Labeled("Time", Row({Bound(&IngredientDraft::time),
                                          Pick(&IngredientDraft::timeUnit, ui::Choices(options(hopTimeUnits(), draft.timeUnit)), 90)}));
            }
            form.Note(simple() && timing != HopTiming::custom
                          ? "Hops added early make the beer bitter. Hops added late, or after the boil, add aroma instead."
                          : "A boil hop is timed in minutes before the end of the boil, and counts toward bitterness. Other "
                            "additions are timed from the start of that step. Use a whole number, or leave the time empty.",
                      ui::Kind::caption);
        }
    }

    void CultureFields(ui::Form& form) {
        form.Labeled("Type", Pick(&IngredientDraft::type, ui::Choices(options(cultureTypes(), draft.type)), 160));
        Advanced(form, [this](ui::Form& f) {
            f.Labeled("Form", Pick(&IngredientDraft::form, ui::Choices(options(cultureForms(), draft.form)), 140));
            f.Labeled(ui::HelpLabel("Attenuation (%)", explanation(Term::attenuation), simple()), Bound(&IngredientDraft::attenuation));
        });
        if (quantities()) form.Labeled("Amount", AmountRow(cultureUnits()));
        form.Note("The first culture with an attenuation sets the final gravity. The figure is typical for the strain, so "
                  "check it against the packet. Leave it empty if you do not know it.", ui::Kind::caption);
    }

    void WaterFields(ui::Form& form) {
        form.Section("Minerals (mg/L)");
        form.Labeled("Calcium", Bound(&IngredientDraft::calcium));
        form.Labeled("Magnesium", Bound(&IngredientDraft::magnesium));
        form.Labeled("Sodium", Bound(&IngredientDraft::sodium));
        form.Labeled("Chloride", Bound(&IngredientDraft::chloride));
        form.Labeled("Sulfate", Bound(&IngredientDraft::sulfate));
        form.Labeled("Bicarbonate", Bound(&IngredientDraft::bicarbonate));
        form.Section();
        if (quantities()) form.Labeled("Volume (L)", Bound(&IngredientDraft::waterVolume));
        form.Note("City profiles are commonly published figures. A real water changes with the season and the source, so test your own.",
                  ui::Kind::caption);
    }

    void MiscellaneousFields(ui::Form& form) {
        form.Labeled("Type", Pick(&IngredientDraft::type, ui::Choices(options(miscellaneousTypes(), draft.type)), 160));
        if (quantities()) form.Labeled("Amount", AmountRow(miscellaneousUnits()));
    }

    void PriceFields(ui::Form& form) {
        form.Labeled("Price per" + (request.currency ? " (" + *request.currency + ")" : std::string()),
                     Row({Bound(&IngredientDraft::price),
                          Pick(&IngredientDraft::priceUnit, ui::Choices(options(priceUnits(draft.unit), draft.priceUnit)), 90)}));
        if (!draft.price.empty() || !simple()) {
            form.Note("Optional. With a price on its ingredients, the recipe shows what a batch costs.", ui::Kind::caption);
        }
    }

    // The editor's form, drawn from the draft. A pick that changes which fields show draws it again.
    void Render() {
        focus.Capture(dialog ? dialog.XamlRoot() : nullptr, body.Content().try_as<xaml::UIElement>());
        ui::Form form;
        form.Section();

        if (!request.menu.empty()) {
            ctl::DropDownButton menuButton;
            menuButton.Content(ui::Text("Common ingredients"));
            ctl::MenuFlyout menu;
            std::vector<std::string> groups;
            for (const MenuEntry& entry : request.menu) {
                if (std::find(groups.begin(), groups.end(), entry.group) == groups.end()) groups.push_back(entry.group);
            }
            auto self = weak_from_this();
            for (const std::string& group : groups) {
                ctl::MenuFlyoutSubItem sub;
                sub.Text(H(group));
                for (const MenuEntry& entry : request.menu) {
                    if (entry.group != group) continue;
                    ctl::MenuFlyoutItem item;
                    item.Text(H(entry.name));
                    item.Click([self, chosen = entry.draft](auto const&, auto const&) {
                        if (auto state = self.lock()) {
                            state->draft = state->draft.adopting(chosen);
                            state->UpdateSave();
                            state->Render();
                        }
                    });
                    sub.Items().Append(item);
                }
                menu.Items().Append(sub);
            }
            menuButton.Flyout(menu);
            form.Labeled("Common ingredients", menuButton);
        }

        form.Labeled("Name", Bound(&IngredientDraft::name, 260, false));
        switch (draft.kind) {
            case IngredientKind::fermentable: FermentableFields(form); break;
            case IngredientKind::hop: HopFields(form); break;
            case IngredientKind::culture: CultureFields(form); break;
            case IngredientKind::water: WaterFields(form); break;
            case IngredientKind::miscellaneous: MiscellaneousFields(form); break;
        }
        if (draft.kind != IngredientKind::water) PriceFields(form);

        errorText = ui::Text("", ui::Kind::error);
        errorText.IsTextSelectionEnabled(true);
        ctl::StackPanel stack;
        stack.Spacing(8);
        stack.Children().Append(form.BuildPanel());
        stack.Children().Append(errorText);
        const auto screen = InDialog(stack);
        body.Content(screen);
        screen.UpdateLayout();
        focus.Restore(screen);
    }
};

}  // namespace

void ShowIngredientEditor(Host& host, IngredientRequest request) {
    auto state = std::make_shared<EditorState>();
    state->host = &host;
    state->draft = request.seed;
    state->request = std::move(request);

    ui::DialogOptions options;
    options.title = state->request.title;
    options.content = state->body;
    options.primaryText = state->request.saveTitle;
    options.primaryEnabled = state->draft.isComplete(state->quantities());
    options.width = 560;
    options.primary = [state] {
        try {
            state->request.save(state->draft);
            return true;
        } catch (const std::exception& e) {
            state->errorText.Text(H(e.what()));
            return false;
        }
    };
    state->dialog = ui::ShowDialog(host.xamlRoot(), options, [state] {
        // The dialog and the editor hold each other, so the editor lets go when the dialog closes.
        state->dialog = nullptr;
    });
    state->Render();
}

// MARK: Equipment editor

void ShowEquipmentEditor(Host& host, std::string title, EquipmentProfile profile, std::optional<std::string> replacing) {
    struct State {
        EquipmentProfile draft;
        std::string boilMinutes;
        std::string original;
        ctl::TextBlock errorText{nullptr};
    };
    auto state = std::make_shared<State>();
    const Units units = host.units();
    const EquipmentProfile source = profile;
    // The volumes are typed in the unit system the user chose, so they start as text in that unit.
    state->draft = profile;
    state->draft.batchLiters = units.volumeTextFromLiters(profile.batchLiters);
    state->draft.trubLossLiters = units.volumeTextFromLiters(profile.trubLossLiters);
    state->boilMinutes = std::to_string(profile.boilMinutes);

    const auto bind = [state](std::string EquipmentProfile::*member, double width = 0, bool right = false) {
        ctl::TextBox box;
        box.Text(H(state->draft.*member));
        if (width > 0) box.Width(width);
        if (right) box.TextAlignment(xaml::TextAlignment::Right);
        box.TextChanged([state, member](auto const& sender, auto const&) {
            state->draft.*member = U(sender.template as<ctl::TextBox>().Text());
        });
        return box;
    };

    ui::Form form;
    form.Section({}, "The figures are starting points for a recipe, so measure your own system. A recipe applies them with its "
                     "batch size, efficiency, and boil-off in one undo step.");
    form.Labeled("Name", bind(&EquipmentProfile::name, 260));
    form.Labeled("Batch size (" + units.volumeSymbol() + ")", bind(&EquipmentProfile::batchLiters, 110, true));
    form.Labeled("Brewhouse efficiency (%)", bind(&EquipmentProfile::efficiencyPercent, 110, true));
    form.Labeled("Boil-off (% per hour)", bind(&EquipmentProfile::evaporationPercent, 110, true));
    form.Labeled("Trub and chiller loss (" + units.volumeSymbol() + ")", bind(&EquipmentProfile::trubLossLiters, 110, true));
    ctl::TextBox minutes;
    minutes.Text(H(state->boilMinutes));
    minutes.Width(110);
    minutes.TextAlignment(xaml::TextAlignment::Right);
    minutes.TextChanged([state](auto const& sender, auto const&) { state->boilMinutes = U(sender.template as<ctl::TextBox>().Text()); });
    form.Labeled("Boil length (minutes)", minutes);
    form.Labeled("Mash system", ui::Picker({{"all in one", "All in one"}, {"separate tun", "Separate tun"}}, state->draft.mashSystem,
                                           [state](std::string value) { state->draft.mashSystem = value; }, 160));
    form.Section("Note");
    ctl::TextBox note;
    note.Text(H(state->draft.note));
    note.PlaceholderText(L"Where the figures come from");
    note.AcceptsReturn(true);
    note.TextWrapping(xaml::TextWrapping::Wrap);
    note.MinHeight(64);
    note.TextChanged([state](auto const& sender, auto const&) { state->draft.note = U(sender.template as<ctl::TextBox>().Text()); });
    form.Wide(note);

    state->errorText = ui::Text("", ui::Kind::error);
    ctl::StackPanel stack;
    stack.Spacing(8);
    stack.Children().Append(form.BuildPanel());
    stack.Children().Append(state->errorText);

    // A plain decimal number, such as "19" or "9.5", within a range. The engine reads exact decimal text, so a sign, a
    // comma, or an exponent is refused.
    const auto plain = [](const std::string& text, double low, double high) {
        if (text.empty() || text.find_first_not_of("0123456789.") != std::string::npos ||
            std::count(text.begin(), text.end(), '.') > 1) {
            return false;
        }
        const auto value = parseNumber(text);
        return value && *value >= low && *value <= high;
    };
    const auto amount = [units](double liters) {
        return significant(units.displayVolume(liters)) + " " + units.volumeSymbol();
    };

    ui::DialogOptions options;
    options.title = title;
    options.content = InDialog(stack);
    options.primaryText = "Save";
    options.width = 520;
    options.primary = [&host, state, plain, amount, units, replacing, source] {
        EquipmentProfile saved = state->draft;
        saved.batchLiters = trimmed(saved.batchLiters);
        saved.efficiencyPercent = trimmed(saved.efficiencyPercent);
        saved.evaporationPercent = trimmed(saved.evaporationPercent);
        saved.trubLossLiters = trimmed(saved.trubLossLiters);
        saved.note = trimmed(saved.note);

        // The volumes are checked in the unit the user typed them in.
        const double factor = units.volumeFactor();
        struct Figure {
            std::string text;
            double low, high;
            std::string message;
        };
        const std::vector<Figure> figures = {
            {saved.batchLiters, 0.1 * factor, 1000 * factor, "The batch size must be from " + amount(0.1) + " to " + amount(1000) + "."},
            {saved.efficiencyPercent, 0.1, 100, "The brewhouse efficiency must be from 0.1 to 100 %."},
            {saved.evaporationPercent, 0, 99.9, "The boil-off must be from 0 to 99.9 % per hour."},
            {saved.trubLossLiters, 0, 100 * factor, "The trub and chiller loss must be from 0 to " + amount(100) + "."},
        };
        for (const Figure& figure : figures) {
            if (!plain(figure.text, figure.low, figure.high)) {
                state->errorText.Text(H(figure.message));
                return false;
            }
        }
        const std::string minutesText = trimmed(state->boilMinutes);
        const bool whole = !minutesText.empty() && minutesText.size() <= 4 && minutesText.find_first_not_of("0123456789") == std::string::npos;
        const int minutes = whole ? std::stoi(minutesText) : 0;
        if (!whole || minutes < 1 || minutes > 600) {
            state->errorText.Text(L"The boil length must be a whole number of minutes, from 1 to 600.");
            return false;
        }
        saved.boilMinutes = minutes;
        // A volume left as it was shown keeps its exact liters.
        const auto litersText = [&](const std::string& shown, const std::string& original) {
            return shown == units.volumeTextFromLiters(original) ? original : units.litersTextFromVolume(shown);
        };
        saved.batchLiters = litersText(saved.batchLiters, source.batchLiters);
        saved.trubLossLiters = litersText(saved.trubLossLiters, source.trubLossLiters);

        try {
            host.store.equipment.save(saved, replacing);
            host.refresh();
            return true;
        } catch (const std::exception& e) {
            state->errorText.Text(H(e.what()));
            return false;
        }
    };
    ui::ShowDialog(host.xamlRoot(), options);
}

// MARK: Names

// MARK: Style

void ShowStyleEditor(Host& host, StyleRequest request) {
    // The ranges, in the order the editor shows them, each with its title and the member of the style it sets.
    const std::vector<std::pair<std::string, styles::Range styles::Ranges::*>> figures = {
        {"Original gravity", &styles::Ranges::og},
        {"Final gravity", &styles::Ranges::fg},
        {"Bitterness (IBU)", &styles::Ranges::ibu},
        {"Color (SRM)", &styles::Ranges::srm},
        {"Alcohol (% by volume)", &styles::Ranges::abv},
    };

    // The fields are kept as the user typed them, so a refused save leaves the text where it was. The vectors are not
    // resized after the boxes bind to them, so the pointers stay valid.
    struct State {
        std::string name;
        std::vector<std::pair<std::string, std::string>> ends;  // the low and high text of each figure
        ctl::TextBlock errorText{nullptr};
    };
    auto state = std::make_shared<State>();
    state->name = request.seed.name;
    for (const auto& figure : figures) {
        const styles::Range& range = request.seed.ranges.*figure.second;
        state->ends.emplace_back(request.seed.id.empty() ? std::string() : significant(range.low),
                                 request.seed.id.empty() ? std::string() : significant(range.high));
    }

    const auto field = [](std::string* text, double width) {
        ctl::TextBox box;
        box.Text(H(*text));
        box.Width(width);
        box.TextAlignment(xaml::TextAlignment::Right);
        box.TextChanged([text](auto const& sender, auto const&) { *text = U(sender.template as<ctl::TextBox>().Text()); });
        return box;
    };

    ui::Form form;
    form.Section({}, "Your style is compared with a recipe the same way as a BJCP style. Enter each range as a plain number, "
                     "such as 1.040, with its low end first.");
    form.Labeled("Name", field(&state->name, 260));
    for (std::size_t i = 0; i < figures.size(); ++i) {
        form.Labeled(figures[i].first, ui::HStack({field(&state->ends[i].first, 90), ui::Text("\xE2\x80\x93", ui::Kind::secondary),
                                                   field(&state->ends[i].second, 90)}, 8));
    }

    state->errorText = ui::Text("", ui::Kind::error);
    ctl::StackPanel stack;
    stack.Spacing(8);
    stack.Children().Append(form.BuildPanel());
    stack.Children().Append(state->errorText);

    // A plain decimal number such as "1.040": digits with at most one point, and no sign or exponent.
    const auto plain = [](const std::string& text) {
        return !text.empty() && text.find_first_not_of("0123456789.") == std::string::npos &&
               std::count(text.begin(), text.end(), '.') <= 1 && parseNumber(text).has_value();
    };

    ui::DialogOptions options;
    options.title = request.title;
    options.content = InDialog(stack);
    options.primaryText = "Save";
    options.width = 520;
    options.primary = [state, request, figures, plain] {
        const std::string name = trimmed(state->name);
        if (name.empty()) {
            state->errorText.Text(L"A style needs a name.");
            return false;
        }
        styles::UserStyle saved = request.seed;
        saved.name = name;
        for (std::size_t i = 0; i < figures.size(); ++i) {
            const std::string low = trimmed(state->ends[i].first);
            const std::string high = trimmed(state->ends[i].second);
            if (!plain(low) || !plain(high)) {
                state->errorText.Text(H("The " + figures[i].first + " range needs two plain numbers, such as 1.040."));
                return false;
            }
            if (*parseNumber(low) > *parseNumber(high)) {
                state->errorText.Text(H("The low end of the " + figures[i].first + " range is above its high end."));
                return false;
            }
            saved.ranges.*figures[i].second = styles::Range{*parseNumber(low), *parseNumber(high)};
        }
        try {
            request.save(saved);
            return true;
        } catch (const std::exception& e) {
            state->errorText.Text(H(e.what()));
            return false;
        }
    };
    ctl::ContentDialog dialog = ui::ShowDialog(host.xamlRoot(), options);
    if (request.remove) {
        dialog.SecondaryButtonText(L"Delete");
        dialog.SecondaryButtonClick([remove = request.remove](auto const&, ctl::ContentDialogButtonClickEventArgs const& args) {
            args.Cancel(false);  // the dialog closes
            remove();
        });
    }
}

void ShowNameDialog(Host& host, std::string title, std::string actionTitle, std::string placeholder, std::string current,
                    std::function<void(const std::string&)> accept) {
    auto name = std::make_shared<std::string>(current);
    ctl::TextBox box;
    box.Text(H(current));
    box.PlaceholderText(H(placeholder));
    box.Width(300);
    ui::DialogOptions options;
    options.title = title;
    options.content = box;
    options.primaryText = actionTitle;
    options.primaryEnabled = !trimmed(current).empty();
    options.primary = [name, accept = std::move(accept)] {
        const std::string text = trimmed(*name);
        if (text.empty()) return false;
        accept(text);
        return true;
    };
    ctl::ContentDialog dialog = ui::ShowDialog(host.xamlRoot(), options);
    // The dialog holds the box, so the box refers back to the dialog weakly.
    box.TextChanged([name, weak = winrt::make_weak(dialog)](auto const& sender, auto const&) {
        *name = U(sender.template as<ctl::TextBox>().Text());
        if (auto shown = weak.get()) shown.IsPrimaryButtonEnabled(!trimmed(*name).empty());
    });
}

// MARK: Interface choice

namespace {

// The three telemetry levels, as the first-launch dialog, the notice, and Settings describe them. docs/privacy.md lists
// exactly what each one sends.
struct TelemetryLevelText {
    const char* value;
    const char* title;
    const char* detail;
};
constexpr TelemetryLevelText kTelemetryLevels[] = {
    {"off", "Off", "Nothing is sent."},
    {"limited", "Limited",
     "Crash and error reports, and that the app was opened, with the app and system version. Nothing about your recipes."},
    {"enhanced", "Enhanced",
     "Also which features are used, such as exporting or a tool. Still anonymous, and never your recipes."},
};

std::vector<ui::Choice> TelemetryChoices() {
    std::vector<ui::Choice> choices;
    for (const TelemetryLevelText& level : kTelemetryLevels) choices.push_back({level.value, level.title});
    return choices;
}

// Records a new telemetry level, and tells the reporter.
void SetTelemetryLevel(Host& host, const std::string& value) {
    host.store.preferences.setString(PrefKey::telemetryLevel, value);
    host.store.preferences.setBoolean(PrefKey::telemetryNoticeShown, true);
    telemetry::LevelChanged();
}

}  // namespace

void ShowTelemetryNotice(Host& host) {
    ctl::StackPanel stack;
    stack.Spacing(12);
    stack.Width(440);
    stack.Children().Append(ui::Text("Kettle Logic can send anonymous error reports and basic usage data, to help find and "
                                     "fix problems. Nothing about your recipes is ever sent, and nothing identifies you. It "
                                     "is set to Limited. Choose another level here, or change it any time in Settings under "
                                     "Privacy.", ui::Kind::body));
    stack.Children().Append(ui::Picker(TelemetryChoices(), host.store.preferences.string(PrefKey::telemetryLevel),
                                       [&host](std::string value) { SetTelemetryLevel(host, value); }, 160));
    for (const TelemetryLevelText& level : kTelemetryLevels) {
        stack.Children().Append(ui::Text(std::string(level.title) + ": " + level.detail, ui::Kind::caption));
    }

    ui::DialogOptions options;
    options.title = "Help Improve Kettle Logic";
    options.content = stack;
    options.closeText = "OK";
    ui::ShowDialog(host.xamlRoot(), options, [&host] {
        host.store.preferences.setBoolean(PrefKey::telemetryNoticeShown, true);
        telemetry::Start(host.store.preferences);
    });
}

void ShowInterfaceChoice(Host& host) {
    auto simple = std::make_shared<bool>(true);
    auto telemetryLevel = std::make_shared<std::string>(host.store.preferences.string(PrefKey::telemetryLevel));
    ctl::StackPanel stack;
    stack.Spacing(14);
    stack.Width(440);
    stack.Children().Append(ui::Text("How much would you like to see?", ui::Kind::secondary));

    ctl::RadioButtons choices;
    const auto option = [](const std::string& title, const std::string& detail) {
        ctl::StackPanel panel;
        panel.Spacing(2);
        panel.Children().Append(ui::Text(title, ui::Kind::strong));
        panel.Children().Append(ui::Text(detail, ui::Kind::caption));
        ctl::RadioButton radio;
        radio.Content(panel);
        return radio;
    };
    choices.Items().Append(option("Simple", "Recommended if you are new to brewing. Shows ingredients, results, style, and a "
                                             "brew-day procedure, and assumes typical values for the rest."));
    choices.Items().Append(option("Full", "Every setting: mash steps, water chemistry, yeast starters, boil-off, and detailed "
                                          "ingredient fields."));
    choices.SelectedIndex(0);
    choices.SelectionChanged([simple](auto const& sender, auto const&) {
        *simple = sender.template as<ctl::RadioButtons>().SelectedIndex() == 0;
    });
    stack.Children().Append(choices);
    stack.Children().Append(ui::Text("You can change this any time in Settings under Interface, or from View \xE2\x96\xB8 Simple Mode.",
                                     ui::Kind::caption));

    // Anonymous usage and error data. Limited is selected, as it is when the question is never asked.
    stack.Children().Append(ui::Text("Help improve Kettle Logic by sharing anonymous data?", ui::Kind::secondary));
    ctl::RadioButtons levels;
    int selectedLevel = 0;
    for (int i = 0; i < static_cast<int>(std::size(kTelemetryLevels)); ++i) {
        levels.Items().Append(option(kTelemetryLevels[i].title, kTelemetryLevels[i].detail));
        if (*telemetryLevel == kTelemetryLevels[i].value) selectedLevel = i;
    }
    levels.SelectedIndex(selectedLevel);
    levels.SelectionChanged([telemetryLevel](auto const& sender, auto const&) {
        const int index = sender.template as<ctl::RadioButtons>().SelectedIndex();
        if (index >= 0) *telemetryLevel = kTelemetryLevels[index].value;
    });
    stack.Children().Append(levels);
    stack.Children().Append(ui::Text("You can change this any time in Settings under Privacy. Nothing about your recipes "
                                     "is ever sent.", ui::Kind::caption));

    ui::DialogOptions options;
    options.title = "Welcome to Kettle Logic";
    options.content = stack;
    options.primaryText = "Continue";
    options.closeText = "";
    options.primary = [&host, simple, telemetryLevel] {
        host.store.preferences.setBoolean(PrefKey::simpleMode, *simple);
        host.store.preferences.setBoolean(PrefKey::interfaceChosen, true);
        SetTelemetryLevel(host, *telemetryLevel);
        telemetry::Start(host.store.preferences);
        host.refresh();
        return true;
    };
    ui::ShowDialog(host.xamlRoot(), options);
}

// MARK: Settings

void ShowSettings(Host& host) {
    // The settings window changes each setting as it is set, so the rest of the app follows at once.
    auto content = std::make_shared<ctl::ContentControl>();
    auto render = std::make_shared<std::function<void()>>();
    *render = [&host, content, render] {
        Preferences& prefs = host.store.preferences;
        ui::Form form;

        form.Section("Interface",
                     "Shows only the basics for a new brewer: ingredients, results, style, and the brew-day procedure. Water "
                     "chemistry, mash, yeast starter, boil-off, and detailed ingredient fields are hidden, and the app assumes "
                     "typical values for them. Nothing is deleted, and anything a recipe already uses stays visible. Also in "
                     "the View menu. Grouping puts the recipe's tabs under four headings, Recipe, Ingredients, Process, and "
                     "Results, which Ctrl+1 to Ctrl+4 switch between.");
        form.Labeled("Simple mode", ui::Switch(prefs.boolean(PrefKey::simpleMode), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::simpleMode, on);
                         host.refresh();
                     }));
        form.Labeled("Group recipe tabs (experimental)", ui::Switch(prefs.boolean(PrefKey::groupedTabs), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::groupedTabs, on);
                         host.refresh();
                     }));

        form.Section("Updates", "Checks the update list on GitHub when the app starts, and offers a newer version if there is "
                                "one. Check for Updates in the Help menu always checks. An update is run only when it is "
                                "signed by the same publisher as this app.");
        form.Labeled("Check for updates when Kettle Logic starts", ui::Switch(prefs.boolean(PrefKey::checkForUpdates), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::checkForUpdates, on);
                     }));

        std::string privacy = "Sends anonymous error reports and usage data to help find and fix problems. Nothing about your "
                              "recipes is ever sent, and nothing identifies you. ";
        for (const TelemetryLevelText& level : kTelemetryLevels) {
            privacy += std::string(level.title) + ": " + level.detail + " ";
        }
        form.Section("Privacy", privacy);
        form.Labeled("Share anonymous data", ui::Picker(TelemetryChoices(), prefs.string(PrefKey::telemetryLevel),
                                                        [&host](std::string value) { SetTelemetryLevel(host, value); }, 160));
        form.Labeled("Anonymous ID", ui::Button("Reset", [] { telemetry::ResetId(); }),
                     "Replaces the random ID that groups this app's reports, so later reports are not linked to earlier ones.");

        form.Section("Saving", "Autosave writes changes to the file the recipe was opened from or saved to, a few seconds after "
                               "each change. A new recipe is saved only with Save As.");
        form.Labeled("Autosave changes", ui::Switch(prefs.boolean(PrefKey::autosave), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::autosave, on);
                     }));

        form.Section("Recipe Folder",
                     "The recipes in this folder open when the app starts, and new recipes are saved here. The Open, Save As, "
                     "and Journal CSV dialogs start in it. A new folder takes effect at the next launch. The default is "
                     "KettleLogic\\Recipes in your user folder.");
        form.Labeled("Folder", ui::Text(host.store.recipeLibrary.folder().string(), ui::Kind::mono));
        ctl::Button useDefault = ui::Button("Use Default", [&host, render] {
            host.store.preferences.setString(PrefKey::recipeFolder, "");
            (*render)();
        });
        useDefault.IsEnabled(!prefs.string(PrefKey::recipeFolder).empty());
        form.Wide(ui::HStack({ui::Button("Choose…", [&host, render] {
            const auto folder = platform::ChooseFolder(host.hwnd(), host.store.recipeLibrary.folder(),
                                                       L"Choose the folder where new recipes are saved.");
            if (folder) {
                host.store.preferences.setString(PrefKey::recipeFolder, platform::U(folder->wstring()));
                (*render)();
            }
        }), useDefault}, 8));

        form.Section("Units", "Sets the units a new ingredient starts in, the units volumes are shown in, and the batch size a "
                              "brewing system is applied with. Existing ingredients keep their units.");
        form.Labeled("Units", ui::Picker({{"metric", "Metric"}, {"us", "US customary"}}, prefs.string(PrefKey::units),
                                         [&host](std::string value) {
                                             host.store.preferences.setString(PrefKey::units, value);
                                             host.refresh();
                                         }, 160));

        form.Section("Recipes", "The brewing system a recipe starts with. Deleting a recipe can always be undone.");
        std::vector<ui::Choice> systems;
        for (const EquipmentProfile& profile : host.store.equipment.all()) systems.push_back({profile.name, profile.name});
        // A saved name that no system has shows the system a recipe falls back to.
        const std::string current = host.store.equipment.profile(prefs.defaultEquipmentName()).name;
        form.Labeled("Default brewing system", ui::Picker(systems, current, [&host](std::string name) {
                         host.store.preferences.setString(PrefKey::defaultEquipment, name);
                     }, 300));
        form.Labeled("Confirm before deleting a recipe", ui::Switch(prefs.boolean(PrefKey::confirmDelete), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::confirmDelete, on);
                     }));
        content->Content(InDialog(form.BuildPanel(), 620));
    };
    (*render)();

    ui::DialogOptions options;
    options.title = "Settings";
    options.content = *content;
    options.closeText = "Done";
    options.width = 600;
    ui::ShowDialog(host.xamlRoot(), options, [render] { *render = nullptr; });
}

// MARK: New recipe wizard

void ShowWizard(Host& host) {
    enum class Step { style, setup, review };
    struct State {
        Step step = Step::style;
        std::string templateId = RecipeTemplate::all()[0].id;
        std::string equipmentName;
        std::string batchValue;
        std::string error;
        std::shared_ptr<RecipeDocument> preview;
        ctl::ContentControl body;
        ctl::TextBlock subtitle{nullptr};
        ctl::TextBlock errorText{nullptr};
        ctl::ContentDialog dialog{nullptr};
        std::function<void()> render;
    };
    auto state = std::make_shared<State>();
    const Units units = host.units();
    state->equipmentName = host.store.preferences.defaultEquipmentName();
    state->batchValue = units.volumeTextFromLiters(host.store.equipment.profile(state->equipmentName).batchLiters);

    const auto currentTemplate = [state]() -> const RecipeTemplate& {
        for (const RecipeTemplate& t : RecipeTemplate::all()) {
            if (t.id == state->templateId) return t;
        }
        return RecipeTemplate::all()[0];
    };
    const auto request = [&host, state, currentTemplate, units] {
        return RecipeBuildRequest{currentTemplate(), state->batchValue, host.store.equipment.profile(state->equipmentName),
                                  units.system};
    };

    state->subtitle = ui::Text("", ui::Kind::secondary);
    state->errorText = ui::Text("", ui::Kind::error);
    ctl::StackPanel frame;
    frame.Spacing(10);
    frame.Width(500);
    frame.Children().Append(state->subtitle);
    frame.Children().Append(state->body);
    frame.Children().Append(state->errorText);

    // Builds the recipe in a throwaway document, so the review shows the engine's own numbers.
    const auto buildPreview = [&host, state, request] {
        state->preview.reset();
        state->errorText.Text(L"");
        try {
            auto scratch = RecipeDocument::makeNew();
            RecipeBuilder::populate(request(), *scratch, scratch->recipes().front().id);
            state->preview = scratch;
        } catch (const std::exception& e) {
            state->errorText.Text(H(e.what()));
        }
    };

    state->render = [&host, state, currentTemplate, request, units, buildPreview] {
        const bool batchValid = request().batchLiters().has_value();
        switch (state->step) {
            case Step::style: state->subtitle.Text(L"Step 1 of 3. What do you want to brew?"); break;
            case Step::setup: state->subtitle.Text(L"Step 2 of 3. How much, and on what equipment?"); break;
            case Step::review: state->subtitle.Text(L"Step 3 of 3. Check the recipe. You can change anything afterwards."); break;
        }

        ui::Form form;
        if (state->step == Step::style) {
            ctl::ListView list;
            list.SelectionMode(ctl::ListViewSelectionMode::Single);
            list.MaxHeight(420);
            int index = 0;
            int selectedIndex = 0;
            for (const RecipeTemplate& t : RecipeTemplate::all()) {
                ctl::StackPanel item;
                item.Spacing(2);
                item.Padding(xaml::Thickness{0, 4, 0, 4});
                item.Children().Append(ui::Text(t.name, ui::Kind::strong));
                item.Children().Append(ui::Text(t.summary, ui::Kind::secondary));
                std::string yeast = t.yeast;
                const std::string prefix = "Fermentis SafAle ";
                if (yeast.rfind(prefix, 0) == 0) yeast.erase(0, prefix.size());
                item.Children().Append(ui::Text("OG " + number(t.targetOG, 3, 3) + " \xC2\xB7 " + std::to_string(static_cast<int>(t.targetIBU)) +
                                                    " IBU \xC2\xB7 " + yeast, ui::Kind::caption));
                list.Items().Append(item);
                if (t.id == state->templateId) selectedIndex = index;
                ++index;
            }
            list.SelectedIndex(selectedIndex);
            list.SelectionChanged([state](auto const& sender, auto const&) {
                const int chosen = sender.template as<ctl::ListView>().SelectedIndex();
                if (chosen >= 0) state->templateId = RecipeTemplate::all()[static_cast<std::size_t>(chosen)].id;
            });
            state->body.Content(list);
        } else if (state->step == Step::setup) {
            form.Section({}, "The batch size is what you end up with in the fermenter. The amounts of grain and hops are worked "
                             "out from it and from the efficiency of your system, to hit the style's strength and bitterness.");
            std::vector<ui::Choice> systems;
            for (const EquipmentProfile& profile : host.store.equipment.all()) systems.push_back({profile.name, profile.name});
            form.Labeled("Brewing system", ui::Picker(systems, host.store.equipment.profile(state->equipmentName).name,
                                                      [&host, state, units](std::string name) {
                                                          state->equipmentName = name;
                                                          state->batchValue = units.volumeTextFromLiters(host.store.equipment.profile(name).batchLiters);
                                                          state->render();
                                                      }, 280));
            form.Note(host.store.equipment.profile(state->equipmentName).note, ui::Kind::caption);
            form.Labeled("Batch size", ui::HStack({ui::Field(state->batchValue, [state](std::string text) {
                                                       state->batchValue = text;
                                                       state->render();
                                                   }, 100), ui::Text(units.volumeSymbol())}, 8));
            state->body.Content(form.BuildPanel());
        } else {
            if (!state->preview) {
                state->body.Content(ctl::ProgressRing());
            } else {
                const model::Recipe& built = state->preview->recipes().front();
                const calc::Results& r = state->preview->results(built.id);
                form.Section();
                const auto shown = [](std::optional<double> v, int places) { return v ? number(*v, places, places) : std::string("\xE2\x80\x94"); };
                form.Labeled("OG / FG", ui::Text(shown(r.originalGravity, 3) + " / " + shown(r.finalGravity, 3)));
                form.Labeled("ABV / IBU / Color",
                             ui::Text(shown(r.abv, 1) + "% / " + shown(r.ibu, 0) + " IBU / " + shown(r.srm, 1) + " SRM"));
                form.Section("Grain");
                for (const auto& f : built.fermentables) form.Note(describe(f.amount).display() + "  " + f.name, ui::Kind::body);
                form.Section("Hops");
                for (const auto& h : built.hops.value_or(std::vector<model::Hop>{})) form.Note(h.name + ": " + hopDetail(h), ui::Kind::body);
                form.Section("Yeast");
                for (const auto& cu : built.cultures.value_or(std::vector<model::Culture>{})) form.Note(cu.name, ui::Kind::body);
                form.Section("Process");
                const RecipeTemplate& t = currentTemplate();
                form.Note("Mash at " + units.temperatureText(t.mashTempC) + " for 60 minutes. Boil " + std::to_string(t.boilMinutes) + " minutes.");
                form.Note("Ferment at " + units.temperatureText(t.fermentTempC.lower) + "\xE2\x80\x93" + units.temperatureText(t.fermentTempC.upper) + ".");
                for (const std::string& tip : tipsIn(t, units.system)) form.Note(tip, ui::Kind::secondary);
                state->body.Content(InDialog(form.BuildPanel(), 400));
            }
        }

        if (state->dialog) {
            state->dialog.PrimaryButtonText(state->step == Step::review ? L"Create Recipe" : L"Next");
            state->dialog.IsPrimaryButtonEnabled(state->step == Step::review ? state->preview != nullptr
                                                 : (state->step != Step::setup || batchValid));
            state->dialog.IsSecondaryButtonEnabled(state->step != Step::style);
            state->dialog.SecondaryButtonText(state->step == Step::style ? L"" : L"Back");
        }
        (void)buildPreview;
    };

    ui::DialogOptions options;
    options.title = "New Recipe from a Style";
    options.content = frame;
    options.primaryText = "Next";
    options.width = 560;
    options.primary = [&host, state, request, buildPreview] {
        state->errorText.Text(L"");
        switch (state->step) {
            case Step::style:
                state->step = Step::setup;
                break;
            case Step::setup:
                state->step = Step::review;
                buildPreview();
                break;
            case Step::review:
                try {
                    host.store.newRecipe(request());
                    return true;
                } catch (const std::exception& e) {
                    state->errorText.Text(H(e.what()));
                    return false;
                }
        }
        state->render();
        return false;
    };
    state->dialog = ui::ShowDialog(host.xamlRoot(), options, [state] {
        state->dialog = nullptr;
        state->render = nullptr;
    });
    state->dialog.SecondaryButtonClick([state](auto const&, ctl::ContentDialogButtonClickEventArgs const& args) {
        args.Cancel(true);
        state->errorText.Text(L"");
        state->step = state->step == Step::review ? Step::setup : Step::style;
        state->render();
    });
    state->render();
}

// MARK: Calculator in a dialog

void ShowToolDialog(Host& host, ToolKind kind, std::map<std::string, std::string> prefill) {
    auto view = std::make_shared<ToolView>(kind, std::move(prefill), host.store.waterProfiles, true);
    ui::DialogOptions options;
    options.title = toolSpec(kind).title;
    options.content = view->Root();
    options.closeText = "Done";
    options.width = 600;
    ui::ShowDialog(host.xamlRoot(), options, [view] {});
}

}  // namespace kl::dialogs
