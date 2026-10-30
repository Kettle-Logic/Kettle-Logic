#include "dialogs.hpp"

#include "ingredient_picker.hpp"
#include "views.hpp"

#include "kettlelogic/app/glossary.hpp"
#include "kettlelogic/app/recipe_builder.hpp"
#include "kettlelogic/app/recipe_text.hpp"
#include "kettlelogic/app/text.hpp"

namespace kl::dialogs {

using namespace kettlelogic::app;
namespace styles = kettlelogic::styles;

namespace {

GtkWidget* Row(std::initializer_list<GtkWidget*> items) {
    return ui::HStack(items, 8);
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
    std::shared_ptr<ui::Dialog> dialog;
    GtkWidget* body = adw_bin_new();
    GtkWidget* errorText = nullptr;
    bool customTiming = false;
    bool showsMoreDetails = false;
    ui::FocusKeeper focus;

    bool simple() const { return host->simpleMode(); }
    bool quantities() const { return request.showsQuantities; }

    void UpdateSave() {
        if (dialog) dialog->SetPrimaryEnabled(draft.isComplete(quantities()));
    }

    // The editor is drawn again when the signal that asked for it has finished, since drawing replaces the widgets.
    void RenderLater() {
        auto self = weak_from_this();
        ui::Later([self] {
            if (auto state = self.lock()) state->Render();
        });
    }

    // A text field bound to one of the draft's values.
    GtkWidget* Bound(std::string IngredientDraft::*member, double width = 100, bool right = true) {
        auto self = weak_from_this();
        return ui::LiveField(draft.*member, [self, member](std::string text) {
            if (auto state = self.lock()) {
                state->draft.*member = std::move(text);
                state->UpdateSave();
            }
        }, width, right);
    }

    // A pick bound to one of the draft's values. `render` draws the editor again, for a pick that changes the fields.
    GtkWidget* Pick(std::string IngredientDraft::*member, const std::vector<ui::Choice>& choices, double width = 0,
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
        if (redraw) RenderLater();
    }

    // The amount, with a stepper beside it in simple mode, and its unit.
    GtkWidget* AmountRow(const std::vector<std::string>& units) {
        GtkWidget* box = Bound(&IngredientDraft::amount, 100);
        GtkWidget* field = Row({box});
        if (simple()) {
            auto self = weak_from_this();
            const auto nudge = [self, box](int direction) {
                if (auto state = self.lock()) {
                    state->draft.amount = nudgedAmount(state->draft.amount, state->draft.unit, direction);
                    gtk_editable_set_text(GTK_EDITABLE(box), state->draft.amount.c_str());
                }
            };
            ui::Append(field, ui::IconButton("go-up-symbolic", "More", [nudge] { nudge(1); }));
            ui::Append(field, ui::IconButton("go-down-symbolic", "Less", [nudge] { nudge(-1); }));
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
        GtkWidget* expander = gtk_expander_new("More details");
        gtk_expander_set_expanded(GTK_EXPANDER(expander), showsMoreDetails);
        gtk_expander_set_child(GTK_EXPANDER(expander), inner.BuildPanel());
        auto self = weak_from_this();
        ui::OnNotify(expander, "expanded", [self, expander] {
            if (auto s = self.lock()) s->showsMoreDetails = gtk_expander_get_expanded(GTK_EXPANDER(expander));
        });
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
                                 state->UpdateSave();
                                 state->RenderLater();
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
        if (!dialog || !dialog->IsOpen()) return;
        focus.Capture(adw_bin_get_child(ADW_BIN(body)));
        ui::Form form;
        form.Section();

        if (request.menu.size() >= kIngredientPickerThreshold) {
            // A long list gets the picker, which searches, sorts, and filters it.
            auto self = weak_from_this();
            form.Labeled("Common ingredients", ui::Button("Choose…", [self] {
                if (auto state = self.lock()) {
                    ShowIngredientPicker(state->body, state->draft.kind, state->request.menu,
                                         [self](const MenuEntry& entry) {
                                             if (auto s = self.lock()) {
                                                 s->draft = s->draft.adopting(entry.draft);
                                                 s->UpdateSave();
                                                 s->RenderLater();
                                             }
                                         });
                }
            }));
        } else if (!request.menu.empty()) {
            auto self = weak_from_this();
            form.Labeled("Common ingredients", ui::MenuButton(nullptr, "Choose…", "", [self] {
                             std::vector<ui::MenuLine> groups;
                             auto state = self.lock();
                             if (!state) return groups;
                             std::vector<std::string> names;
                             for (const MenuEntry& entry : state->request.menu) {
                                 if (std::find(names.begin(), names.end(), entry.group) == names.end()) names.push_back(entry.group);
                             }
                             for (const std::string& group : names) {
                                 std::vector<ui::MenuLine> items;
                                 for (const MenuEntry& entry : state->request.menu) {
                                     if (entry.group != group) continue;
                                     items.push_back(ui::MenuLine::Item(entry.name, [self, chosen = entry.draft] {
                                         if (auto s = self.lock()) {
                                             s->draft = s->draft.adopting(chosen);
                                             s->UpdateSave();
                                             s->RenderLater();
                                         }
                                     }));
                                 }
                                 groups.push_back(ui::MenuLine::Sub(group, std::move(items)));
                             }
                             return groups;
                         }));
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
        gtk_label_set_selectable(GTK_LABEL(errorText), TRUE);
        GtkWidget* stack = ui::Box(GTK_ORIENTATION_VERTICAL, 8);
        ui::Margins(stack, 16, 12, 16, 16);
        ui::Append(stack, form.BuildPanel());
        ui::Append(stack, errorText);
        adw_bin_set_child(ADW_BIN(body), ui::InDialog(stack));
        focus.Restore(adw_bin_get_child(ADW_BIN(body)));
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
            ui::SetText(state->errorText, e.what());
            return false;
        }
    };
    state->dialog = ui::ShowDialog(host.widget(), options, [state] {
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
        GtkWidget* errorText = nullptr;
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
        return ui::LiveField(state->draft.*member, [state, member](std::string text) { state->draft.*member = std::move(text); },
                             width, right);
    };

    ui::Form form;
    form.Section({}, "The figures are starting points for a recipe, so measure your own system. A recipe applies them with its "
                     "batch size, efficiency, and boil-off in one undo step.");
    form.Labeled("Name", bind(&EquipmentProfile::name, 260));
    form.Labeled("Batch size (" + units.volumeSymbol() + ")", bind(&EquipmentProfile::batchLiters, 110, true));
    form.Labeled("Brewhouse efficiency (%)", bind(&EquipmentProfile::efficiencyPercent, 110, true));
    form.Labeled("Boil-off (% per hour)", bind(&EquipmentProfile::evaporationPercent, 110, true));
    form.Labeled("Trub and chiller loss (" + units.volumeSymbol() + ")", bind(&EquipmentProfile::trubLossLiters, 110, true));
    form.Labeled("Boil length (minutes)", ui::LiveField(state->boilMinutes, [state](std::string text) { state->boilMinutes = std::move(text); }, 110));
    form.Labeled("Mash system", ui::Picker({{"all in one", "All in one"}, {"separate tun", "Separate tun"}}, state->draft.mashSystem,
                                           [state](std::string value) { state->draft.mashSystem = value; }, 160));
    form.Section("Note");
    GtkWidget* note = ui::MultilineField(state->draft.note, [state](std::string text) { state->draft.note = std::move(text); }, 64);
    // The note is part of the draft as it is typed, so a click on Save right after typing keeps it.
    GtkWidget* noteView = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(gtk_frame_get_child(GTK_FRAME(note))));
    if (GTK_IS_VIEWPORT(noteView)) noteView = gtk_viewport_get_child(GTK_VIEWPORT(noteView));
    if (GTK_IS_TEXT_VIEW(noteView)) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(noteView));
        ui::On(buffer, "changed", [state, buffer] {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            char* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            state->draft.note = text;
            g_free(text);
        });
    }
    form.Wide(note);

    state->errorText = ui::Text("", ui::Kind::error);
    GtkWidget* stack = ui::Box(GTK_ORIENTATION_VERTICAL, 8);
    ui::Margins(stack, 16, 12, 16, 16);
    ui::Append(stack, form.BuildPanel());
    ui::Append(stack, state->errorText);

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
    options.content = ui::InDialog(stack);
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
                ui::SetText(state->errorText, figure.message);
                return false;
            }
        }
        const std::string minutesText = trimmed(state->boilMinutes);
        const bool whole = !minutesText.empty() && minutesText.size() <= 4 && minutesText.find_first_not_of("0123456789") == std::string::npos;
        const int minutes = whole ? std::stoi(minutesText) : 0;
        if (!whole || minutes < 1 || minutes > 600) {
            ui::SetText(state->errorText, "The boil length must be a whole number of minutes, from 1 to 600.");
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
            ui::SetText(state->errorText, e.what());
            return false;
        }
    };
    ui::ShowDialog(host.widget(), options);
}

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

    // The fields are kept as the user typed them, so a refused save leaves the text where it was.
    struct State {
        std::string name;
        std::vector<std::pair<std::string, std::string>> ends;  // the low and high text of each figure
        GtkWidget* errorText = nullptr;
    };
    auto state = std::make_shared<State>();
    state->name = request.seed.name;
    for (const auto& figure : figures) {
        const styles::Range& range = request.seed.ranges.*figure.second;
        state->ends.emplace_back(request.seed.id.empty() ? std::string() : significant(range.low),
                                 request.seed.id.empty() ? std::string() : significant(range.high));
    }

    ui::Form form;
    form.Section({}, "Your style is compared with a recipe the same way as a BJCP style. Enter each range as a plain number, "
                     "such as 1.040, with its low end first.");
    form.Labeled("Name", ui::LiveField(state->name, [state](std::string text) { state->name = std::move(text); }, 260, false));
    for (std::size_t i = 0; i < figures.size(); ++i) {
        const auto field = [state, i](bool high) {
            return ui::LiveField(high ? state->ends[i].second : state->ends[i].first,
                                 [state, i, high](std::string text) {
                                     (high ? state->ends[i].second : state->ends[i].first) = std::move(text);
                                 },
                                 90);
        };
        form.Labeled(figures[i].first, ui::HStack({field(false), ui::Text("\xE2\x80\x93", ui::Kind::secondary), field(true)}));
    }

    state->errorText = ui::Text("", ui::Kind::error);
    GtkWidget* stack = ui::Box(GTK_ORIENTATION_VERTICAL, 8);
    ui::Margins(stack, 16, 12, 16, 16);
    ui::Append(stack, form.BuildPanel());
    ui::Append(stack, state->errorText);

    // A plain decimal number such as "1.040": digits with at most one point, and no sign or exponent.
    const auto plain = [](const std::string& text) {
        return !text.empty() && text.find_first_not_of("0123456789.") == std::string::npos &&
               std::count(text.begin(), text.end(), '.') <= 1 && parseNumber(text).has_value();
    };

    ui::DialogOptions options;
    options.title = request.title;
    options.content = ui::InDialog(stack);
    options.primaryText = "Save";
    options.width = 520;
    options.primary = [state, request, figures, plain] {
        const std::string name = trimmed(state->name);
        if (name.empty()) {
            ui::SetText(state->errorText, "A style needs a name.");
            return false;
        }
        styles::UserStyle saved = request.seed;
        saved.name = name;
        for (std::size_t i = 0; i < figures.size(); ++i) {
            const std::string low = trimmed(state->ends[i].first);
            const std::string high = trimmed(state->ends[i].second);
            if (!plain(low) || !plain(high)) {
                ui::SetText(state->errorText, "The " + figures[i].first + " range needs two plain numbers, such as 1.040.");
                return false;
            }
            if (*parseNumber(low) > *parseNumber(high)) {
                ui::SetText(state->errorText, "The low end of the " + figures[i].first + " range is above its high end.");
                return false;
            }
            saved.ranges.*figures[i].second = styles::Range{*parseNumber(low), *parseNumber(high)};
        }
        try {
            request.save(saved);
            return true;
        } catch (const std::exception& e) {
            ui::SetText(state->errorText, e.what());
            return false;
        }
    };
    auto dialog = ui::ShowDialog(host.widget(), options);
    if (request.remove) {
        std::weak_ptr<ui::Dialog> weak = dialog;
        dialog->SetSecondary("Delete", [weak, remove = request.remove] {
            remove();
            if (auto shown = weak.lock()) shown->Close();
        });
    }
}

// MARK: Folder name

void ShowNameDialog(Host& host, std::string title, std::string actionTitle, std::string placeholder, std::string current,
                    std::function<void(const std::string&)> accept) {
    auto name = std::make_shared<std::string>(current);
    auto dialog = std::make_shared<std::weak_ptr<ui::Dialog>>();
    GtkWidget* box = ui::LiveField(current, [name, dialog](std::string text) {
        *name = std::move(text);
        if (auto shown = dialog->lock()) shown->SetPrimaryEnabled(!trimmed(*name).empty());
    }, 300, false);
    ui::Placeholder(box, placeholder);
    ui::Margins(box, 16, 16, 16, 16);

    ui::DialogOptions options;
    options.title = title;
    options.content = box;
    options.primaryText = actionTitle;
    options.primaryEnabled = !trimmed(current).empty();
    options.width = 360;
    options.primary = [name, accept = std::move(accept)] {
        const std::string text = trimmed(*name);
        if (text.empty()) return false;
        accept(text);
        return true;
    };
    *dialog = ui::ShowDialog(host.widget(), options);
    gtk_widget_grab_focus(box);
}

// MARK: Interface choice

void ShowInterfaceChoice(Host& host) {
    auto simple = std::make_shared<bool>(true);
    GtkWidget* stack = ui::Box(GTK_ORIENTATION_VERTICAL, 14);
    ui::Margins(stack, 20, 8, 20, 20);
    ui::Append(stack, ui::Text("How much would you like to see?", ui::Kind::secondary));

    GtkWidget* first = nullptr;
    const auto option = [&](const std::string& title, const std::string& detail, bool selected, bool isSimple) {
        GtkWidget* panel = ui::Box(GTK_ORIENTATION_VERTICAL, 2);
        ui::Append(panel, ui::Text(title, ui::Kind::strong));
        ui::Append(panel, ui::Text(detail, ui::Kind::caption));
        GtkWidget* radio = gtk_check_button_new();
        gtk_check_button_set_child(GTK_CHECK_BUTTON(radio), panel);
        if (first != nullptr) gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), GTK_CHECK_BUTTON(first));
        if (first == nullptr) first = radio;
        gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), selected);
        ui::OnNotify(radio, "active", [radio, simple, isSimple] {
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(radio))) *simple = isSimple;
        });
        ui::Append(stack, radio);
    };
    option("Simple", "Recommended if you are new to brewing. Shows ingredients, results, style, and a "
                     "brew-day procedure, and assumes typical values for the rest.", true, true);
    option("Full", "Every setting: mash steps, water chemistry, yeast starters, boil-off, and detailed "
                   "ingredient fields.", false, false);
    ui::Append(stack, ui::Text("You can change this any time in Settings under Interface, or from the main menu's Simple Mode.",
                               ui::Kind::caption));

    ui::DialogOptions options;
    options.title = "Welcome to Kettle Logic";
    options.content = stack;
    options.primaryText = "Continue";
    options.closeText = "";
    options.width = 460;
    options.primary = [&host, simple] {
        host.store.preferences.setBoolean(PrefKey::simpleMode, *simple);
        host.store.preferences.setBoolean(PrefKey::interfaceChosen, true);
        host.refresh();
        return true;
    };
    ui::ShowDialog(host.widget(), options);
}

// MARK: Settings

void ShowSettings(Host& host) {
    // The settings window changes each setting as it is set, so the rest of the app follows at once.
    GtkWidget* content = adw_bin_new();
    auto render = std::make_shared<std::function<void()>>();
    *render = [&host, content, render] {
        Preferences& prefs = host.store.preferences;
        ui::Form form;

        form.Section("Interface",
                     "Shows only the basics for a new brewer: ingredients, results, style, and the brew-day procedure. Water "
                     "chemistry, mash, yeast starter, boil-off, and detailed ingredient fields are hidden, and the app assumes "
                     "typical values for them. Nothing is deleted, and anything a recipe already uses stays visible. Also in "
                     "the main menu. Grouping puts the recipe's tabs under four headings, Recipe, Ingredients, Process, and "
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
                                "one. A Flatpak app is updated with flatpak update. Check for Updates in the main menu always checks.");
        form.Labeled("Check for updates when Kettle Logic starts", ui::Switch(prefs.boolean(PrefKey::checkForUpdates), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::checkForUpdates, on);
                     }));

        form.Section("Saving", "Autosave writes changes to the file the recipe was opened from or saved to, a few seconds after "
                               "each change. A new recipe is saved only with Save As.");
        form.Labeled("Autosave changes", ui::Switch(prefs.boolean(PrefKey::autosave), [&host](bool on) {
                         host.store.preferences.setBoolean(PrefKey::autosave, on);
                     }));

        form.Section("Recipe Folder",
                     "The recipes in this folder open when the app starts, and new recipes are saved here. The Open, Save As, "
                     "and Journal CSV dialogs start in it. A new folder takes effect at the next launch. The default is "
                     "KettleLogic/Recipes in your home folder.");
        GtkWidget* folder = ui::Text(host.store.recipeLibrary.folder().string(), ui::Kind::mono);
        gtk_label_set_wrap_mode(GTK_LABEL(folder), PANGO_WRAP_CHAR);
        gtk_label_set_selectable(GTK_LABEL(folder), TRUE);
        form.Wide(folder);
        GtkWidget* useDefault = ui::Button("Use Default", [&host, render] {
            host.store.preferences.setString(PrefKey::recipeFolder, "");
            ui::Later([render] { if (*render) (*render)(); });
        });
        ui::Enable(useDefault, !prefs.string(PrefKey::recipeFolder).empty());
        form.Wide(ui::HStack({ui::Button("Choose…", [&host, render] {
            platform::ChooseFolder(host.window, host.store.recipeLibrary.folder(), "Choose the folder where new recipes are saved.",
                                   [&host, render](std::optional<std::filesystem::path> chosen) {
                                       if (!chosen) return;
                                       host.store.preferences.setString(PrefKey::recipeFolder, chosen->string());
                                       if (*render) (*render)();
                                   });
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
        GtkWidget* panel = form.BuildPanel();
        ui::Margins(panel, 16, 8, 16, 16);
        adw_bin_set_child(ADW_BIN(content), ui::InDialog(panel, 620));
    };
    (*render)();

    ui::DialogOptions options;
    options.title = "Settings";
    options.content = content;
    options.closeText = "Done";
    options.width = 620;
    ui::ShowDialog(host.widget(), options, [render] { *render = nullptr; });
}

// MARK: New recipe wizard

void ShowWizard(Host& host) {
    enum class Step { style, setup, review };
    struct State {
        Step step = Step::style;
        std::string templateId = RecipeTemplate::all()[0].id;
        std::string equipmentName;
        std::string batchValue;
        std::shared_ptr<RecipeDocument> preview;
        GtkWidget* body = adw_bin_new();
        GtkWidget* subtitle = nullptr;
        GtkWidget* errorText = nullptr;
        std::shared_ptr<ui::Dialog> dialog;
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
    GtkWidget* frame = ui::Box(GTK_ORIENTATION_VERTICAL, 10);
    ui::Margins(frame, 20, 8, 20, 20);
    ui::Append(frame, state->subtitle);
    ui::Append(frame, state->body);
    ui::Append(frame, state->errorText);

    // Builds the recipe in a throwaway document, so the review shows the engine's own numbers.
    const auto buildPreview = [state, request] {
        state->preview.reset();
        ui::SetText(state->errorText, "");
        try {
            auto scratch = RecipeDocument::makeNew();
            RecipeBuilder::populate(request(), *scratch, scratch->recipes().front().id);
            state->preview = scratch;
        } catch (const std::exception& e) {
            ui::SetText(state->errorText, e.what());
        }
    };

    state->render = [&host, state, currentTemplate, request, units] {
        if (!state->dialog || !state->dialog->IsOpen()) return;
        const bool batchValid = request().batchLiters().has_value();
        switch (state->step) {
            case Step::style: ui::SetText(state->subtitle, "Step 1 of 3. What do you want to brew?"); break;
            case Step::setup: ui::SetText(state->subtitle, "Step 2 of 3. How much, and on what equipment?"); break;
            case Step::review: ui::SetText(state->subtitle, "Step 3 of 3. Check the recipe. You can change anything afterwards."); break;
        }

        ui::Form form;
        if (state->step == Step::style) {
            GtkWidget* list = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
            ui::Style(list, "boxed-list");
            int index = 0;
            int selectedIndex = 0;
            for (const RecipeTemplate& t : RecipeTemplate::all()) {
                GtkWidget* item = ui::Box(GTK_ORIENTATION_VERTICAL, 2);
                ui::Margins(item, 12, 8, 12, 8);
                ui::Append(item, ui::Text(t.name, ui::Kind::strong));
                ui::Append(item, ui::Text(t.summary, ui::Kind::secondary));
                std::string yeast = t.yeast;
                const std::string prefix = "Fermentis SafAle ";
                if (yeast.rfind(prefix, 0) == 0) yeast.erase(0, prefix.size());
                ui::Append(item, ui::Text("OG " + number(t.targetOG, 3, 3) + " \xC2\xB7 " + std::to_string(static_cast<int>(t.targetIBU)) +
                                              " IBU \xC2\xB7 " + yeast, ui::Kind::caption));
                gtk_list_box_append(GTK_LIST_BOX(list), item);
                if (t.id == state->templateId) selectedIndex = index;
                ++index;
            }
            gtk_list_box_select_row(GTK_LIST_BOX(list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), selectedIndex));
            g_signal_connect_data(
                list, "row-selected",
                G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
                    if (row == nullptr) return;
                    const int chosen = gtk_list_box_row_get_index(row);
                    if (chosen >= 0) static_cast<State*>(data)->templateId = RecipeTemplate::all()[static_cast<std::size_t>(chosen)].id;
                }),
                state.get(), nullptr, static_cast<GConnectFlags>(0));
            adw_bin_set_child(ADW_BIN(state->body), ui::InDialog(list, 420));
        } else if (state->step == Step::setup) {
            form.Section({}, "The batch size is what you end up with in the fermenter. The amounts of grain and hops are worked "
                             "out from it and from the efficiency of your system, to hit the style's strength and bitterness.");
            std::vector<ui::Choice> systems;
            for (const EquipmentProfile& profile : host.store.equipment.all()) systems.push_back({profile.name, profile.name});
            form.Labeled("Brewing system", ui::Picker(systems, host.store.equipment.profile(state->equipmentName).name,
                                                      [&host, state, units](std::string name) {
                                                          state->equipmentName = name;
                                                          state->batchValue = units.volumeTextFromLiters(host.store.equipment.profile(name).batchLiters);
                                                          ui::Later([state] { if (state->render) state->render(); });
                                                      }, 280));
            form.Note(host.store.equipment.profile(state->equipmentName).note, ui::Kind::caption);
            form.Labeled("Batch size", ui::HStack({ui::LiveField(state->batchValue, [state, request](std::string text) {
                                                       state->batchValue = std::move(text);
                                                       if (state->dialog) state->dialog->SetPrimaryEnabled(request().batchLiters().has_value());
                                                   }, 100), ui::Text(units.volumeSymbol())}, 8));
            adw_bin_set_child(ADW_BIN(state->body), form.BuildPanel());
        } else {
            if (!state->preview) {
                GtkWidget* spinner = gtk_spinner_new();
                gtk_spinner_set_spinning(GTK_SPINNER(spinner), TRUE);
                adw_bin_set_child(ADW_BIN(state->body), spinner);
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
                adw_bin_set_child(ADW_BIN(state->body), ui::InDialog(form.BuildPanel(), 400));
            }
        }

        state->dialog->SetPrimaryText(state->step == Step::review ? "Create Recipe" : "Next");
        state->dialog->SetPrimaryEnabled(state->step == Step::review ? state->preview != nullptr
                                         : (state->step != Step::setup || batchValid));
        if (state->step == Step::style) {
            state->dialog->SetSecondary("", nullptr);
        } else {
            state->dialog->SetSecondary("Back", [state] {
                ui::SetText(state->errorText, "");
                state->step = state->step == Step::review ? Step::setup : Step::style;
                state->render();
            });
        }
    };

    ui::DialogOptions options;
    options.title = "New Recipe from a Style";
    options.content = frame;
    options.primaryText = "Next";
    options.width = 580;
    options.primary = [&host, state, request, buildPreview] {
        ui::SetText(state->errorText, "");
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
                    ui::SetText(state->errorText, e.what());
                    return false;
                }
        }
        ui::Later([state] { if (state->render) state->render(); });
        return false;
    };
    state->dialog = ui::ShowDialog(host.widget(), options, [state] {
        state->dialog = nullptr;
        state->render = nullptr;
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
    options.width = 620;
    ui::ShowDialog(host.widget(), options, [view] {});
}

}  // namespace kl::dialogs
