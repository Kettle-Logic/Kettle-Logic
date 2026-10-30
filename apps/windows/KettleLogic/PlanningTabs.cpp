#include "pch.h"

#include "RecipeContext.h"
#include "Views.h"

#include "kettlelogic/app/glossary.hpp"
#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

const char* const kDash = "\xE2\x80\x94";

// A readout for a value the engine worked out, or a dash when it has none.
std::string Shown(std::optional<double> value, int places) {
    return value ? number(*value, places, places) : kDash;
}

template <typename T>
T* FindById(std::vector<T>& list, const std::string& id) {
    for (T& item : list) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

ctl::StackPanel Row(std::initializer_list<xaml::UIElement> items) {
    return ui::HStack(items, 8);
}

// A number field for a temperature kept in °C, shown in the user's unit.
ctl::TextBox TemperatureField(RecipeContext& c, double celsius, std::function<void(RecipePlanning&, double)> apply,
                              double width = 100) {
    const Units units = c.units();
    return ui::Field(number(units.displayTemperature(celsius), 0, 1), [&c, units, apply = std::move(apply)](std::string text) {
        if (const auto typed = parseNumber(text)) {
            const double value = units.celsius(*typed);
            c.updatePlanning([&](RecipePlanning& planning) { apply(planning, value); });
        }
    }, width);
}

// A number field for a plain value in the planning.
ctl::TextBox NumberFieldFor(RecipeContext& c, double value, int places, double factor,
                            std::function<void(RecipePlanning&, double)> apply, double width = 100) {
    return ui::Field(NumberText(value, places, factor), [&c, factor, apply = std::move(apply)](std::string text) {
        if (const auto typed = TypedNumber(text, factor)) {
            const double value = *typed;
            c.updatePlanning([&](RecipePlanning& planning) { apply(planning, value); });
        }
    }, width);
}

ctl::ToggleSwitch ToggleFor(RecipeContext& c, bool on, std::function<void(RecipePlanning&, bool)> apply) {
    return ui::Switch(on, [&c, apply = std::move(apply)](bool value) {
        c.updatePlanning([&](RecipePlanning& planning) { apply(planning, value); });
    });
}

const std::vector<std::string>& SaltKinds() {
    static const std::vector<std::string> kinds = {"gypsum", "calcium chloride", "epsom salt", "magnesium chloride",
                                                   "baking soda", "table salt", "chalk", "pickling lime"};
    return kinds;
}

const std::vector<std::string>& AcidKinds() {
    static const std::vector<std::string> kinds = {"lactic acid", "phosphoric acid", "acid blend", "malic acid",
                                                   "ascorbic acid"};
    return kinds;
}

const std::vector<std::string>& SpargeAcidKinds() {
    static const std::vector<std::string> kinds = {"phosphoric acid", "lactic acid", "sulfuric acid",
                                                   "hydrochloric acid", "acetic acid", "citric acid",
                                                   "tartaric acid", "malic acid"};
    return kinds;
}

struct IonColumn {
    const char* title;
    double IonsInput::*member;
};

const std::vector<IonColumn>& IonColumns() {
    static const std::vector<IonColumn> columns = {
        {"Calcium", &IonsInput::calcium},     {"Magnesium", &IonsInput::magnesium},
        {"Sodium", &IonsInput::sodium},       {"Sulfate", &IonsInput::sulfate},
        {"Chloride", &IonsInput::chloride},   {"Bicarbonate", &IonsInput::bicarbonate},
    };
    return columns;
}

std::optional<calc::BrewPlan> PlanOrNone(RecipeContext& c, std::string& error) {
    try {
        const RecipePlanning planning = c.planning();
        return c.document->brewingPlan(c.id(), brewingInputs(planning.water, planning.mash));
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

}  // namespace

// MARK: Yeast starter

void BuildStarterTab(RecipeContext& c, ui::Form& form) {
    const Units units = c.units();
    const RecipePlanning planning = c.planning();
    const StarterSettings& settings = planning.starter;

    calc::StarterPlan plan;
    try {
        plan = c.document->starterPlan(c.id(), settings);
    } catch (const std::exception& e) {
        form.Section();
        form.Note(e.what());
        return;
    }

    const auto volume = [&](double liters) { return number(liters * units.starterFactor(), 2, 2) + " " + units.starterSymbol(); };
    const auto readout = [](double value) { return number(value, 2, 2); };
    // The liters to store for a field. A field left as it was shown keeps its stored text, so rounding for display
    // never changes a figure the user did not touch.
    const auto litersKeeping = [units](const std::string& stored, const std::string& typed) {
        return typed == units.starterTextFromLiters(stored) ? stored : units.litersTextFromStarter(typed);
    };
    const auto textField = [&c](const std::string& value, std::function<void(StarterSettings&, std::string)> apply) {
        return ui::Field(value, [&c, apply = std::move(apply)](std::string text) {
            c.updatePlanning([&](RecipePlanning& p) { apply(p.starter, text); });
        });
    };

    form.Section("Yeast Plan", "The summary of the sections below. The plan in use is the two-stage starter when it is switched on.");
    form.Labeled("Pitch", ui::Text(plural(plan.packsWithoutStarter, "pack") + " without a starter"));
    form.Labeled("Plan in use", ui::Text(settings.twoStage ? "Two-stage starter"
                                         : plan.oneStageLiters > 0 ? "One-stage starter" : "The pack alone"));
    if (settings.twoStage) {
        form.Labeled("Starters", ui::Text(volume(plan.twoStageFirstLiters) + ", then " + volume(plan.twoStageSecondLiters)));
    } else {
        form.Labeled("Starter", ui::Text(volume(plan.oneStageLiters)));
    }

    form.Section("Yeast Cells Needed", "Cells needed = pitch rate \xC3\x97 batch \xC3\x97 degrees Plato, from the recipe's original gravity of " +
                                           number(plan.targetGravity, 3, 3) + ". The packs are rounded up.");
    form.Labeled("Lager yeast (1.5 million cells per mL per \xC2\xB0P, not 0.75)",
                 ui::Switch(settings.lager, [&c](bool on) { c.updatePlanning([&](RecipePlanning& p) { p.starter.lager = on; }); }));
    form.Labeled("Cells per pack (billion)", textField(settings.packCells, [](StarterSettings& s, std::string t) { s.packCells = t; }));
    form.Labeled("Cells needed (billion)", ui::Text(readout(plan.cellsNeededBillion)));
    form.Labeled("Cells without starter (billion)", ui::Text(readout(plan.cellsWithoutStarterBillion)));
    form.Labeled("Yeast packs to use without a starter", ui::Text(std::to_string(plan.packsWithoutStarter)));

    form.Section("Liquid Yeast Starter Size", "Dry malt uses 44 points per pound per gallon. Growth is an estimate: measure your own yeast before relying on it.");
    form.Labeled("Starter size (" + units.starterSymbol() + ")",
                 ui::Field(units.starterTextFromLiters(settings.starterLiters), [&c, litersKeeping](std::string text) {
                     c.updatePlanning([&](RecipePlanning& p) { p.starter.starterLiters = litersKeeping(p.starter.starterLiters, text); });
                 }));
    form.Labeled("Starter gravity (SG)", textField(settings.starterGravity, [](StarterSettings& s, std::string t) { s.starterGravity = t; }));
    form.Labeled("Growth (billion cells per L per \xC2\xB0P)", textField(settings.growth, [](StarterSettings& s, std::string t) { s.growth = t; }));
    form.Labeled("Dry malt needed (oz)", ui::Text(readout(plan.dryMaltOunces)));
    form.Labeled("Dry malt needed (g)", ui::Text(readout(plan.dryMaltGrams)));
    form.Labeled("Yeast cells with starter (billion)", ui::Text(readout(plan.cellsWithStarterBillion)));
    form.Labeled("The starter is added to the bottling volume",
                 ui::Switch(settings.addToBottling, [&c](bool on) { c.updatePlanning([&](RecipePlanning& p) { p.starter.addToBottling = on; }); }));
    form.Labeled("Volume the starter adds to the batch (" + units.starterSymbol() + ")",
                 ui::Text(readout(plan.addToBottlingLiters * units.starterFactor())));

    form.Section("Starter Volume for One Stage Starter", "The size of one starter that grows the pack to the cells needed.");
    form.Labeled("Recommended starter size (" + units.starterSymbol() + ")", ui::Text(readout(plan.oneStageLiters * units.starterFactor())));

    form.Section("Starter Volumes for Two Stage Starter", "The first stage grows the pack. The second stage, made from the first, reaches the cells needed.");
    form.Labeled("Use a two-stage starter",
                 ui::Switch(settings.twoStage, [&c](bool on) { c.updatePlanning([&](RecipePlanning& p) { p.starter.twoStage = on; }); }));
    form.Labeled("First stage (" + units.starterSymbol() + ")",
                 ui::Field(units.starterTextFromLiters(settings.firstStageLiters), [&c, litersKeeping](std::string text) {
                     c.updatePlanning([&](RecipePlanning& p) { p.starter.firstStageLiters = litersKeeping(p.starter.firstStageLiters, text); });
                 }));
    form.Labeled("Recommended first starter (" + units.starterSymbol() + ")", ui::Text(readout(plan.twoStageFirstLiters * units.starterFactor())));
    form.Labeled("Recommended second starter (" + units.starterSymbol() + ")", ui::Text(readout(plan.twoStageSecondLiters * units.starterFactor())));

    if (!plan.notes.empty()) {
        form.Section("Notes");
        for (const std::string& note : plan.notes) form.Note(note);
    }
}

// MARK: Water

void BuildWaterTab(RecipeContext& c, ui::Form& form) {
    const Units units = c.units();
    const RecipePlanning planning = c.planning();
    const WaterSettings& water = planning.water;

    std::string error;
    const std::optional<calc::BrewPlan> plan = PlanOrNone(c, error);
    WaterProfileLibrary& library = c.store().waterProfiles;

    // Saved water profiles to load.
    form.Section("Saved Water Profiles",
                 "Loading a profile replaces the base water, the reverse osmosis share, and the salts. The salts are scaled "
                 "from grams per liter to this recipe's mash water. A profile with a target sets the target; one without "
                 "keeps this recipe's target.");
    if (library.profiles().empty()) {
        form.Note("No saved profiles yet. Save one from the Water Profile tool in the Tools menu.");
    } else {
        std::vector<ui::Choice> choices = {{"", "None"}};
        for (const SavedWaterProfile& profile : library.profiles()) choices.push_back({profile.id, profile.name});
        form.Labeled("Profile", ui::Picker(choices, c.chosenProfile, [&c](std::string id) {
                         c.chosenProfile = id;
                         c.redraw();
                     }, 240));
        ctl::Button load = ui::Button("Load Profile", [&c, plan] {
            for (const SavedWaterProfile& profile : c.store().waterProfiles.profiles()) {
                if (profile.id != c.chosenProfile || !plan) continue;
                const double mashLiters = plan->strikeLiters;
                c.updatePlanning([&](RecipePlanning& p) { profile.apply(p.water, mashLiters); });
                c.libraryMessage = "Loaded \xE2\x80\x9C" + profile.name + "\xE2\x80\x9D.";
                break;
            }
        });
        load.IsEnabled(!c.chosenProfile.empty() && plan.has_value());
        ctl::Button remove = ui::Button("Remove Profile", [&c] {
            const std::string id = c.chosenProfile;
            c.store().attempt([&] { c.store().waterProfiles.remove(id); });
            c.chosenProfile.clear();
            c.libraryMessage.clear();
        });
        remove.IsEnabled(!c.chosenProfile.empty());
        form.Wide(ui::HStack({load, remove}, 8));
    }
    if (!c.libraryMessage.empty()) form.Note(c.libraryMessage);

    // Base water.
    form.Section("Base Water", "The brewing water before any salts. The reverse osmosis share mixes distilled water in first. "
                               "Pick a start point or type your own water report.");
    std::vector<ui::Choice> bases = {{"Custom", "Custom"}};
    for (const WaterTemplate& t : Catalog::waters()) bases.push_back({t.name, t.name});
    form.Labeled("Start from", ui::Picker(bases, water.baseName, [&c](std::string name) {
                     c.updatePlanning([&](RecipePlanning& p) {
                         p.water.baseName = name;
                         for (const WaterTemplate& t : Catalog::waters()) {
                             if (t.name == name) {
                                 p.water.base = IonsInput{parseNumber(t.calcium).value_or(0), parseNumber(t.magnesium).value_or(0),
                                                          parseNumber(t.sodium).value_or(0),  parseNumber(t.sulfate).value_or(0),
                                                          parseNumber(t.chloride).value_or(0), parseNumber(t.bicarbonate).value_or(0)};
                             }
                         }
                     });
                 }, 260));
    for (const IonColumn& column : IonColumns()) {
        const auto member = column.member;
        form.Labeled(std::string(column.title) + " (ppm)",
                     NumberFieldFor(c, water.base.*member, 1, 1, [member](RecipePlanning& p, double v) { p.water.base.*member = v; }));
    }
    form.Labeled("Reverse osmosis share (%)",
                 NumberFieldFor(c, water.roPercent, 0, 1, [](RecipePlanning& p, double v) { p.water.roPercent = v; }));

    // Salts.
    form.Section("Water Salts", "Grams of each salt, added to the mash or the sparge water. Salts are shown in the profile below.");
    form.Labeled("Leave out water salts", ToggleFor(c, water.leaveOutSalts, [](RecipePlanning& p, bool on) { p.water.leaveOutSalts = on; }));
    form.Labeled("Hold sparge salts until the boil",
                 ToggleFor(c, water.holdSpargeSaltsUntilBoil, [](RecipePlanning& p, bool on) { p.water.holdSpargeSaltsUntilBoil = on; }));
    for (const SaltInput& salt : water.salts) {
        const std::string id = salt.id;
        ctl::StackPanel row = Row({
            ui::Picker(ui::Choices(options(SaltKinds(), salt.kind)), salt.kind, [&c, id](std::string kind) {
                c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.water.salts, id)) s->kind = kind; });
            }, 170),
            ui::Field(NumberText(salt.grams, 2), [&c, id](std::string text) {
                if (const auto grams = TypedNumber(text)) {
                    c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.water.salts, id)) s->grams = *grams; });
                }
            }, 80),
            ui::Picker({{"mash", "Mash"}, {"sparge", "Sparge"}}, salt.sparge ? "sparge" : "mash", [&c, id](std::string where) {
                c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.water.salts, id)) s->sparge = where == "sparge"; });
            }, 110),
            ui::IconButton(ctl::Symbol::Remove, "Remove this salt", [&c, id] {
                c.updatePlanning([&](RecipePlanning& p) {
                    auto& salts = p.water.salts;
                    salts.erase(std::remove_if(salts.begin(), salts.end(), [&](const SaltInput& s) { return s.id == id; }), salts.end());
                });
            })});
        form.Wide(row);
    }
    form.Wide(ui::Button("Add Salt", [&c] { c.updatePlanning([](RecipePlanning& p) { p.water.salts.push_back(SaltInput{}); }); }));

    // Target.
    form.Section("Target Water", "The desired mash water. The adjusted profile shows how far the salts leave it from this. "
                                 "The default is the Pale Ale profile.");
    form.Labeled("Compare with a target water", ToggleFor(c, water.useTarget, [](RecipePlanning& p, bool on) { p.water.useTarget = on; }));
    if (water.useTarget) {
        for (const IonColumn& column : IonColumns()) {
            const auto member = column.member;
            form.Labeled("Target " + lowerCopy(column.title) + " (ppm)",
                         NumberFieldFor(c, water.target.*member, 1, 1, [member](RecipePlanning& p, double v) { p.water.target.*member = v; }));
        }
    }

    if (!plan) {
        form.Section();
        form.Note(error, ui::Kind::error);
        return;
    }

    // The mash water after its salts, and its analysis.
    form.Section("Adjusted Water Profile", "The mash water after its salts. The sparge makes up the pre-boil volume the recipe asks for.");
    form.Labeled("Total mash water (" + units.volumeSymbol() + ")", ui::Text(Shown(units.displayVolume(plan->strikeLiters), 1)));
    const auto ions = [&](const calc::Ions& values, const std::string& prefix) {
        form.Labeled(prefix + "calcium (ppm)", ui::Text(Shown(values.calcium, 0)));
        form.Labeled(prefix + "magnesium (ppm)", ui::Text(Shown(values.magnesium, 0)));
        form.Labeled(prefix + "sodium (ppm)", ui::Text(Shown(values.sodium, 0)));
        form.Labeled(prefix + "sulfate (ppm)", ui::Text(Shown(values.sulfate, 0)));
        form.Labeled(prefix + "chloride (ppm)", ui::Text(Shown(values.chloride, 0)));
        form.Labeled(prefix + "bicarbonate (ppm)", ui::Text(Shown(values.bicarbonate, 0)));
    };
    {
        const calc::Ions& mash = plan->mashWater.ions;
        form.Labeled("Calcium (ppm)", ui::Text(Shown(mash.calcium, 0)));
        form.Labeled("Magnesium (ppm)", ui::Text(Shown(mash.magnesium, 0)));
        form.Labeled("Sodium (ppm)", ui::Text(Shown(mash.sodium, 0)));
        form.Labeled("Sulfate (ppm)", ui::Text(Shown(mash.sulfate, 0)));
        form.Labeled("Chloride (ppm)", ui::Text(Shown(mash.chloride, 0)));
        form.Labeled("Bicarbonate (ppm)", ui::Text(Shown(mash.bicarbonate, 0)));
    }
    form.Labeled("Sparge volume (" + units.volumeSymbol() + ")",
                 ui::Text(Shown(plan->spargeLiters ? std::optional<double>(units.displayVolume(*plan->spargeLiters)) : std::nullopt, 1)));
    if (plan->mashDifference) {
        ions(*plan->mashDifference, "Target less mash, ");
    }

    form.Section("Water Analysis",
                 "Effective hardness is calcium and magnesium as CaCO\xE2\x82\x83. The balance uses sulfate to chloride below 0.5 "
                 "(malt-forward), 0.5 to 1.5 (balanced), and above 1.5 (hop-forward). The color range is Palmer's rough "
                 "mapping from the residual alkalinity after the acids.");
    form.Labeled("Alkalinity (ppm as CaCO\xE2\x82\x83)", ui::Text(Shown(plan->mashWater.alkalinity, 0)));
    form.Labeled("Effective hardness (ppm as CaCO\xE2\x82\x83)", ui::Text(Shown(plan->mashWater.effectiveHardness, 0)));
    form.Labeled("Residual alkalinity (ppm as CaCO\xE2\x82\x83)", ui::Text(Shown(plan->mashWater.residualAlkalinity, 0)));
    form.Labeled("Sulfate to chloride ratio", ui::Text(Shown(plan->mashWater.sulfateToChloride, 2)));
    form.Labeled("Sulfate and chloride balance", ui::Text(plan->mashWater.balance));
    form.Labeled("Water color range (SRM)",
                 ui::Text(plan->colorLowSrm && plan->colorHighSrm
                              ? number(*plan->colorLowSrm, 1, 1) + "\xE2\x80\x93" + number(*plan->colorHighSrm, 1, 1)
                              : kDash));

    if (!plan->notes.empty()) {
        form.Section("Notes");
        for (const std::string& note : plan->notes) form.Note(note);
    }
}

// MARK: Mash

void BuildMashTab(RecipeContext& c, ui::Form& form) {
    const Units units = c.units();
    const RecipePlanning planning = c.planning();
    const MashSettings& mash = planning.mash;

    std::string error;
    const std::optional<calc::BrewPlan> plan = PlanOrNone(c, error);

    // The mash steps.
    double totalMinutes = 0;
    for (const MashStepSetting& step : mash.steps) totalMinutes += step.minutes;
    form.Section("Mash Process", "Total mash time: " + std::to_string(static_cast<int>(totalMinutes)) +
                                     " minutes. Infusion adds hot water, temperature changes only the heat, and decoction "
                                     "moves part of the mash to boil.");
    for (const MashStepSetting& step : mash.steps) {
        const std::string id = step.id;
        ctl::StackPanel panel;
        panel.Spacing(6);
        ctl::TextBox name = ui::Field(step.name, [&c, id](std::string text) {
            c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.mash.steps, id)) s->name = text; });
        }, 220, false);
        name.PlaceholderText(L"Step name");
        panel.Children().Append(Row({
            name,
            ui::Picker(ui::Choices(MashStepSetting::kinds()), step.kind, [&c, id](std::string kind) {
                c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.mash.steps, id)) s->kind = kind; });
            }, 130),
            ui::IconButton(ctl::Symbol::Remove, "Remove this step", [&c, id] {
                c.updatePlanning([&](RecipePlanning& p) {
                    auto& steps = p.mash.steps;
                    steps.erase(std::remove_if(steps.begin(), steps.end(), [&](const MashStepSetting& s) { return s.id == id; }), steps.end());
                });
            })}));
        panel.Children().Append(Row({
            ui::Text("Temperature (" + units.temperatureSymbol() + ")"),
            TemperatureField(c, step.tempC, [id](RecipePlanning& p, double v) { if (auto* s = FindById(p.mash.steps, id)) s->tempC = v; }, 80),
            ui::Text("Time (min)"),
            NumberFieldFor(c, step.minutes, 0, 1, [id](RecipePlanning& p, double v) { if (auto* s = FindById(p.mash.steps, id)) s->minutes = v; }, 80)}));
        ctl::TextBox addition = ui::Field(step.addition, [&c, id](std::string text) {
            c.updatePlanning([&](RecipePlanning& p) { if (auto* s = FindById(p.mash.steps, id)) s->addition = text; });
        }, 0, false);
        addition.PlaceholderText(L"Addition during this step, such as boiling water");
        panel.Children().Append(addition);
        form.Wide(panel);
    }
    form.Wide(ui::Button("Add Mash Step", [&c] {
        c.updatePlanning([](RecipePlanning& p) {
            MashStepSetting step;
            step.name = "Rest";
            step.kind = "Temperature";
            step.tempC = 72;
            step.minutes = 15;
            p.mash.steps.push_back(step);
        });
    }));

    // The sparge.
    form.Section("Sparge", "The sparge water is worked out from the pre-boil volume and the first runnings.");
    form.Labeled("Sparge type", ui::Picker({{"Batch", "Batch"}, {"Fly", "Fly"}, {"No sparge", "No sparge"}}, mash.spargeType,
                                           [&c](std::string type) { c.updatePlanning([&](RecipePlanning& p) { p.mash.spargeType = type; }); }, 140));
    form.Labeled("Sparge temperature (" + units.temperatureSymbol() + ")",
                 TemperatureField(c, mash.spargeTempC, [](RecipePlanning& p, double v) { p.mash.spargeTempC = v; }));
    form.Labeled("Sparge water (" + units.volumeSymbol() + ")",
                 ui::Text(Shown(plan && plan->spargeLiters ? std::optional<double>(units.displayVolume(*plan->spargeLiters)) : std::nullopt, 1)));

    // The starting conditions.
    form.Section("Initial Mash Conditions");
    form.Labeled("Grain temperature (" + units.temperatureSymbol() + ")",
                 TemperatureField(c, mash.grainTempC, [](RecipePlanning& p, double v) { p.mash.grainTempC = v; }));
    form.Labeled("Mash tun temperature (" + units.temperatureSymbol() + ")",
                 TemperatureField(c, mash.tunTempC, [](RecipePlanning& p, double v) { p.mash.tunTempC = v; }));
    form.Labeled("Decoction boil temperature (" + units.temperatureSymbol() + ")",
                 TemperatureField(c, mash.decoctionTempC, [](RecipePlanning& p, double v) { p.mash.decoctionTempC = v; }));
    form.Labeled("Mash water (" + units.grainRatioSymbol() + " of grain)",
                 NumberFieldFor(c, mash.mashRatioLPerKg, 2, units.grainRatioFactor(), [](RecipePlanning& p, double v) { p.mash.mashRatioLPerKg = v; }));
    form.Labeled("Grain weight (kg)", ui::Text(Shown(plan ? std::optional<double>(plan->grainKg) : std::nullopt, 2)));
    form.Labeled("Grain absorption (" + units.grainRatioSymbol() + ")",
                 NumberFieldFor(c, mash.absorptionLPerKg, 2, units.grainRatioFactor(), [](RecipePlanning& p, double v) { p.mash.absorptionLPerKg = v; }));
    form.Labeled("Water the grain absorbs (" + units.volumeSymbol() + ")",
                 ui::Text(Shown(plan ? std::optional<double>(units.displayVolume(plan->absorptionLiters)) : std::nullopt, 1)));

    if (!plan) {
        form.Section();
        form.Note(error, ui::Kind::error);
        return;
    }

    // The mash volume.
    form.Section("Mash Volume Needed", "The mash tun volume adds the grain's displacement, about 0.7 L per kg, to the mash volume.");
    form.Labeled("Mash tun addition (" + units.volumeSymbol() + ")", ui::Text(Shown(units.displayVolume(plan->strikeLiters), 1)));
    form.Labeled("Tun deadspace (" + units.volumeSymbol() + ")",
                 NumberFieldFor(c, mash.deadspaceLiters, 2, units.volumeFactor(), [](RecipePlanning& p, double v) { p.mash.deadspaceLiters = v; }));
    form.Labeled("Adjust mash volume for deadspace",
                 ToggleFor(c, mash.adjustForDeadspace, [](RecipePlanning& p, bool on) { p.mash.adjustForDeadspace = on; }));
    form.Labeled("Mash volume needed (" + units.volumeSymbol() + ")", ui::Text(Shown(units.displayVolume(plan->mashVolumeLiters), 1)));
    form.Labeled("Mash tun volume (" + units.volumeSymbol() + ")", ui::Text(Shown(units.displayVolume(plan->mashTunVolumeLiters), 1)));
    form.Labeled("Strike water temperature (" + units.temperatureSymbol() + ")",
                 ui::Text(Shown(plan->strikeTempC ? std::optional<double>(units.displayTemperature(*plan->strikeTempC)) : std::nullopt, 1)));

    // The acids that bring the mash alkalinity down.
    form.Section("Mash pH Acid Additions",
                 "The malt's acidity and the mash water's residual alkalinity, with baking soda's bicarbonate scaled by the pH. "
                 "It is an estimate, so measure the pH. The acid blend is 45 % malic, 45 % citric, and 10 % tartaric, a wine "
                 "blend, since no brewing blend is published.");
    for (const AcidInput& acid : mash.acids) {
        const std::string id = acid.id;
        form.Wide(Row({
            ui::Picker(ui::Choices(options(AcidKinds(), acid.kind)), acid.kind, [&c, id](std::string kind) {
                c.updatePlanning([&](RecipePlanning& p) { if (auto* a = FindById(p.mash.acids, id)) a->kind = kind; });
            }, 170),
            ui::Field(NumberText(acid.grams, 2), [&c, id](std::string text) {
                if (const auto grams = TypedNumber(text)) {
                    c.updatePlanning([&](RecipePlanning& p) { if (auto* a = FindById(p.mash.acids, id)) a->grams = *grams; });
                }
            }, 80),
            ui::IconButton(ctl::Symbol::Remove, "Remove this acid", [&c, id] {
                c.updatePlanning([&](RecipePlanning& p) {
                    auto& acids = p.mash.acids;
                    acids.erase(std::remove_if(acids.begin(), acids.end(), [&](const AcidInput& a) { return a.id == id; }), acids.end());
                });
            })}));
    }
    form.Wide(ui::Button("Add Acid", [&c] { c.updatePlanning([](RecipePlanning& p) { p.mash.acids.push_back(AcidInput{}); }); }));
    form.Labeled("Target mash pH", NumberFieldFor(c, mash.targetMashPh, 2, 1, [](RecipePlanning& p, double v) { p.mash.targetMashPh = v; }));
    form.Labeled("Acid for the target", ui::Picker(ui::Choices(options(AcidKinds(), mash.acidForTarget)), mash.acidForTarget,
                                                   [&c](std::string kind) { c.updatePlanning([&](RecipePlanning& p) { p.mash.acidForTarget = kind; }); }, 170));
    form.Labeled("Milliequivalents from the acids", ui::Text(Shown(plan->acidMilliequivalents, 1)));
    form.Labeled("Alkalinity after the acids (ppm as CaCO\xE2\x82\x83)", ui::Text(Shown(plan->alkalinityAfterAcid, 0)));
    form.Labeled("Grams of the target acid to reach the target pH", ui::Text(Shown(plan->acidGramsForTarget, 2)));
    form.Labeled("Estimated mash pH before the acids", ui::Text(Shown(plan->estimatedMashPhBeforeAcid, 2)));
    form.Labeled("Estimated mash pH after the acids", ui::Text(Shown(plan->estimatedMashPh, 2)));

    // The sparge acid.
    form.Section("Sparge Acidification",
                 "The acid that takes the sparge water to its target pH, from its alkalinity using carbonate titration. "
                 "Phosphoric and lactic acid are the usual choices.");
    form.Labeled("Sparge acid", ui::Picker(ui::Choices(options(SpargeAcidKinds(), mash.spargeAcid)), mash.spargeAcid,
                                           [&c](std::string kind) { c.updatePlanning([&](RecipePlanning& p) { p.mash.spargeAcid = kind; }); }, 170));
    form.Labeled("Strength (%)", NumberFieldFor(c, mash.spargeAcidStrength, 0, 1, [](RecipePlanning& p, double v) { p.mash.spargeAcidStrength = v; }));
    form.Labeled("Sparge water pH before the acid", NumberFieldFor(c, mash.spargeStartPh, 2, 1, [](RecipePlanning& p, double v) { p.mash.spargeStartPh = v; }));
    form.Labeled("Target sparge pH", NumberFieldFor(c, mash.spargeTargetPh, 2, 1, [](RecipePlanning& p, double v) { p.mash.spargeTargetPh = v; }));
    form.Labeled("Sparge acid (mL)", ui::Text(Shown(plan->spargeAcidMilliliters, 2)));
    form.Labeled("Sparge acid, as sold (g)", ui::Text(Shown(plan->spargeAcidGrams, 2)));
}

}  // namespace kl
