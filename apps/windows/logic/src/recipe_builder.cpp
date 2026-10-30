#include "kettlelogic/app/recipe_builder.hpp"

#include "kettlelogic/app/catalog.hpp"
#include "kettlelogic/app/ingredient_draft.hpp"
#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cmath>

namespace kettlelogic::app {

std::optional<double> RecipeBuildRequest::batchLiters() const {
    const auto value = parseNumber(batchValue);
    if (!value || !(*value > 0)) {
        return std::nullopt;
    }
    return units == UnitSystem::metric ? *value : *value * Units::litersPerGallon;
}

namespace RecipeBuilder {

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw EngineError(message);
}

template <typename T, typename Pred>
const T* findIn(const std::vector<T>& list, Pred pred) {
    const auto found = std::find_if(list.begin(), list.end(), pred);
    return found == list.end() ? nullptr : &*found;
}

// A hop weight in the user's small mass unit: whole grams, or ounces to the nearest 0.05. `rounded` is false for the
// trial weight, which only has to be exact enough to measure with.
std::string hopAmount(double grams, UnitSystem units, bool rounded = true) {
    if (units == UnitSystem::metric) {
        return rounded ? fixed(std::max(std::round(grams), 1.0), 0) : fixed(grams, 1);
    }
    const double ounces = grams / 28.349523125;
    return rounded ? fixed(std::max(std::round(ounces * 20) / 20, 0.05), 2) : fixed(ounces, 3);
}

// The process and tips that go with the style, as the recipe's starting notes.
std::string notesFor(const RecipeTemplate& t, UnitSystem system) {
    const Units units{system};
    std::string text = "Mash at " + units.temperatureText(t.mashTempC) + " for 60 minutes. Boil " +
                       std::to_string(t.boilMinutes) + " minutes. Ferment at " +
                       units.temperatureText(t.fermentTempC.lower) + " to " +
                       units.temperatureText(t.fermentTempC.upper) + ".";
    for (const std::string& tip : tipsIn(t, system)) {
        text += "\n\n" + tip;
    }
    return text;
}

// Adds each grain at a trial weight, reads the original gravity, then rescales every grain by the same factor. The
// gravity is proportional to the weight, so one pass is exact before rounding.
void addGrains(const RecipeTemplate& t, UnitSystem system, RecipeDocument& document, EntityId recipe) {
    const Units units{system};
    const double trialTotal = 3.0;
    struct Added {
        IngredientDraft draft;
        EntityId id;
        double percent;
    };
    std::vector<Added> added;
    for (const TemplateGrain& grain : t.grains) {
        const FermentableTemplate* entry =
            findIn(Catalog::fermentables(), [&](const auto& f) { return f.name == grain.name; });
        if (entry == nullptr) {
            fail(grain.name + " is not in the ingredient catalog.");
        }
        IngredientDraft draft = IngredientDraft::from(*entry, system);
        draft.unit = units.bulkMassUnit();
        draft.amount = fixed(trialTotal * grain.percent / 100, 3);
        draft.commit(document, recipe, std::nullopt);
        const model::Recipe* current = document.recipe(recipe);
        if (current == nullptr || current->fermentables.empty()) {
            fail("The engine could not work out a gravity for these fermentables.");
        }
        added.push_back({draft, current->fermentables.back().id, grain.percent});
    }
    const auto og = document.results(recipe).originalGravity;
    if (!og || !(*og > 1)) {
        fail("The engine could not work out a gravity for these fermentables.");
    }
    const double scale = (t.targetOG - 1) / (*og - 1);
    for (Added& item : added) {
        item.draft.amount = fixed(trialTotal * scale * item.percent / 100, 2);
        item.draft.commit(document, recipe, item.id);
    }
}

void addYeast(const RecipeTemplate& t, UnitSystem system, RecipeDocument& document, EntityId recipe) {
    const CultureTemplate* entry = findIn(Catalog::cultures(), [&](const auto& c) { return c.name == t.yeast; });
    if (entry == nullptr) {
        fail(t.yeast + " is not in the ingredient catalog.");
    }
    IngredientDraft draft = IngredientDraft::from(*entry, system);
    draft.amount = "1";
    if (draft.attenuation.empty() && t.yeastAttenuationPercent) {
        draft.attenuation = *t.yeastAttenuationPercent;
    }
    draft.commit(document, recipe, std::nullopt);
}

// Boil hops go in first, in template order, then the aroma hops, so the list reads in the order of the brew day. Each
// addition is added at a trial weight and its bitterness read from the change in the total. That bitterness depends
// only on the gravity, the time, and the volume, not on the other hops, so it can be read in any order. Aroma hops
// are then set to their weight per liter, and each boil hop to the weight that gives its share of the bitterness the
// aroma hops leave to reach the target.
void addHops(const RecipeTemplate& t, double batchLiters, UnitSystem system, RecipeDocument& document,
             EntityId recipe) {
    const Units units{system};
    const auto hopDraft = [&](const std::string& name) {
        const HopTemplate* entry = findIn(Catalog::hops(), [&](const auto& h) { return h.name == name; });
        if (entry == nullptr) {
            fail(name + " is not in the ingredient catalog.");
        }
        IngredientDraft draft = IngredientDraft::from(*entry, system);
        draft.unit = units.smallMassUnit();
        return draft;
    };

    const double trialGrams = 10.0;
    const auto currentIbu = [&] { return document.results(recipe).ibu.value_or(0); };

    struct Trial {
        EntityId id;
        double gained;
    };
    // Adds `draft` at the trial weight, and returns its id and the bitterness it adds per trial weight.
    const auto addTrial = [&](IngredientDraft draft) {
        draft.amount = hopAmount(trialGrams, system, false);
        const double before = currentIbu();
        draft.commit(document, recipe, std::nullopt);
        const model::Recipe* current = document.recipe(recipe);
        if (current == nullptr || !current->hops || current->hops->empty()) {
            fail(draft.name + " adds no bitterness, so it cannot be sized.");
        }
        return Trial{current->hops->back().id, currentIbu() - before};
    };

    struct BoilItem {
        IngredientDraft draft;
        EntityId id;
        double gained;
        double percent;
    };
    std::vector<BoilItem> boil;
    for (const TemplateBoilHop& hop : t.boilHops) {
        IngredientDraft draft = hopDraft(hop.name);
        draft.use = "add_to_boil";
        draft.time = std::to_string(hop.minutes);
        draft.timeUnit = "min";
        const Trial trial = addTrial(draft);
        if (!(trial.gained > 0.0001)) {
            fail(hop.name + " adds no bitterness, so it cannot be sized.");
        }
        boil.push_back({draft, trial.id, trial.gained, hop.ibuPercent});
    }

    double aromaIbu = 0;
    for (const TemplateAromaHop& aroma : t.aromaHops) {
        IngredientDraft draft = hopDraft(aroma.name);
        switch (aroma.timing.kind) {
            case TemplateAromaHop::Timing::Kind::boil:
                draft.use = "add_to_boil";
                draft.time = std::to_string(aroma.timing.value);
                draft.timeUnit = "min";
                break;
            case TemplateAromaHop::Timing::Kind::flameout:
                draft.use = "add_to_boil";
                draft.time = "0";
                draft.timeUnit = "min";
                break;
            case TemplateAromaHop::Timing::Kind::dryHop:
                draft.use = "add_to_fermentation";
                draft.time = std::to_string(aroma.timing.value);
                draft.timeUnit = "day";
                break;
        }
        const Trial trial = addTrial(draft);
        const double grams = aroma.gramsPerLiter * batchLiters;
        draft.amount = hopAmount(grams, system);
        draft.commit(document, recipe, trial.id);
        aromaIbu += trial.gained * grams / trialGrams;
    }

    const double remaining = t.targetIBU - aromaIbu;
    for (BoilItem& hop : boil) {
        const double wanted = remaining * hop.percent / 100;
        hop.draft.amount = hopAmount(trialGrams * wanted / hop.gained, system);
        hop.draft.commit(document, recipe, hop.id);
    }
}

void fill(const RecipeBuildRequest& request, RecipeDocument& document, EntityId recipe) {
    const auto batchLiters = request.batchLiters();
    if (!batchLiters) {
        fail("Enter a batch size greater than zero.");
    }
    const RecipeTemplate& t = request.recipeTemplate;
    const UnitSystem system = request.units;
    const Units units{system};
    const EquipmentProfile& profile = request.profile;

    document.rename(recipe, t.name);
    document.setType(recipe, "all grain");
    document.applyEquipment(recipe, request.batchValue, units.volumeUnit(), profile.efficiencyPercent,
                            profile.evaporationPercent, profile.trubLossLiters);
    document.setStyle(recipe, t.bjcpStyle);

    addGrains(t, system, document, recipe);
    addYeast(t, system, document, recipe);
    addHops(t, *batchLiters, system, document, recipe);

    if (t.usesWhirlfloc) {
        if (const MiscellaneousTemplate* tablet = findIn(Catalog::miscellaneous(), [](const auto& m) {
                return m.name == "Whirlfloc tablet";
            })) {
            IngredientDraft draft = IngredientDraft::from(*tablet, system);
            draft.amount = "1";
            draft.commit(document, recipe, std::nullopt);
        }
    }

    if (const model::Recipe* current = document.recipe(recipe)) {
        RecipePlanning planning = RecipePlanning::read(current->planning);
        if (!planning.mash.steps.empty()) {
            planning.mash.steps[0].tempC = t.mashTempC;
        }
        planning.notes = notesFor(t, system);
        document.setPlanning(recipe, planning.text());
    }
}

}  // namespace

EntityId add(const RecipeBuildRequest& request, RecipeDocument& document) {
    EntityId created = 0;
    document.grouped("New Recipe from Style", [&] {
        created = document.addRecipe();
        fill(request, document, created);
    });
    return created;
}

void populate(const RecipeBuildRequest& request, RecipeDocument& document, EntityId recipe) {
    document.grouped("New Recipe from Style", [&] { fill(request, document, recipe); });
}

}  // namespace RecipeBuilder
}  // namespace kettlelogic::app
