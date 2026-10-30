#pragma once

#include "kettlelogic/app/catalog.hpp"
#include "kettlelogic/app/engine.hpp"
#include "kettlelogic/app/preferences.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kettlelogic::app {

// Every value an ingredient editor shows, as the text its fields hold. A catalog entry, an ingredient in a recipe,
// and a custom ingredient all convert to a draft, so each starts from the same values. A draft is saved to a recipe
// with `commit`, or to the user's library as it is.
struct IngredientDraft {
    IngredientKind kind = IngredientKind::fermentable;
    std::string name, type, form, use, amount, unit, alpha, beta;
    // What the ingredient costs for each one `priceUnit`, in the recipe's currency. Empty means no price.
    std::string price, priceUnit;
    std::string time, timeUnit, attenuation;
    std::string yieldValue = "", yieldUnit = "sg", colorValue = "", colorUnit = "SRM";
    std::string calcium = "0", bicarbonate = "0", sulfate = "0", chloride = "0", sodium = "0", magnesium = "0";
    std::string waterVolume;

    IngredientDraft() = default;
    // A blank ingredient of `kind`, with the defaults a new one starts from, in the user's unit system.
    IngredientDraft(IngredientKind kind, UnitSystem units);

    static IngredientDraft from(const model::Fermentable& f, UnitSystem units);
    static IngredientDraft from(const model::Hop& h, UnitSystem units);
    static IngredientDraft from(const model::Culture& c, UnitSystem units);
    static IngredientDraft from(const model::WaterAddition& w, UnitSystem units);
    static IngredientDraft from(const model::Miscellaneous& m, UnitSystem units);
    static IngredientDraft from(const FermentableTemplate& t, UnitSystem units);
    static IngredientDraft from(const HopTemplate& t, UnitSystem units);
    static IngredientDraft from(const CultureTemplate& t, UnitSystem units);
    static IngredientDraft from(const WaterTemplate& t, UnitSystem units);
    static IngredientDraft from(const MiscellaneousTemplate& t, UnitSystem units);

    // This draft with the descriptive values of a catalog or library entry. The amounts and times already set here
    // are kept, so picking an entry does not clear what the brewer entered.
    IngredientDraft adopting(const IngredientDraft& entry) const;

    // Whether the draft has what it needs. A recipe needs its amounts. A library entry is a template, so it does
    // not need them.
    bool isComplete(bool quantities) const;

    // The key figures, for a list of library entries.
    std::string summary() const;

    // Adds the ingredient to a recipe, or replaces the recipe's ingredient `existing` with it. The engine checks
    // every value, and throws with its message if one is refused. The price is set with it.
    void commit(RecipeDocument& document, EntityId recipe, std::optional<EntityId> existing) const;

private:
    void adoptPrice(const std::optional<PriceText>& existing);
    void savePrice(RecipeDocument& document, EntityId recipe, std::optional<EntityId> existing) const;
};

// An entry in the Common ingredients menu: a catalog or library ingredient, with the group it sits under.
struct MenuEntry {
    std::string id;
    std::string group;
    std::string name;
    IngredientDraft draft;
};

// The built-in catalog entries of one kind.
std::vector<MenuEntry> catalogEntries(IngredientKind kind, UnitSystem units);

// An ingredient the user made, kept in their library. Its draft holds the values an editor shows, so it starts
// from the same place as a catalog entry.
struct CustomIngredient {
    std::string id;
    IngredientDraft draft;
};

// The catalog entries of one kind, then the user's own, under Custom.
std::vector<MenuEntry> menuEntries(IngredientKind kind, const std::vector<CustomIngredient>& custom,
                                   UnitSystem units);

// The user's custom ingredients, saved as JSON in the app's data folder. A file that cannot be read starts the
// library empty, and the next save replaces it.
class CustomIngredientStore {
public:
    // `file` is where the library is kept. An empty path keeps it in memory only.
    explicit CustomIngredientStore(std::filesystem::path file = {});

    const std::vector<CustomIngredient>& ingredients() const { return ingredients_; }

    // Saves a draft as a new custom ingredient, or as a change to the one with `id`.
    void save(IngredientDraft draft, const std::optional<std::string>& replacing);
    // Adds a copy of the ingredient with `id`, named after it with " copy" added.
    void duplicate(const std::string& id);
    void remove(const std::string& id);

private:
    void write() const;

    std::vector<CustomIngredient> ingredients_;
    std::filesystem::path file_;
};

// The ways a new brewer thinks about when a hop goes in. Each sets the hop's use, time, and time unit.
enum class HopTiming { start, thirty, fifteen, five, flameout, dryHop, custom };

const std::vector<HopTiming>& allHopTimings();
std::string hopTimingTitle(HopTiming timing);
// Minutes left in the boil, for the boil presets.
int hopTimingMinutes(HopTiming timing);

struct HopTimingSetting {
    std::string use;
    std::optional<std::string> time;  // empty keeps the time already there, which the dry hop does for its days
    std::string unit;
};
// What picking a timing sets, or nothing for Custom.
std::optional<HopTimingSetting> hopTimingSetting(HopTiming timing);

// The timing a draft's use, time, and time unit amount to. Custom when they match no preset.
HopTiming timingOf(const IngredientDraft& draft);

// Picks a timing for a hop draft, setting its use, time, and unit together.
void chooseHopTiming(IngredientDraft& draft, HopTiming choice);

// The amount a stepper nudges to, in simple mode: a few grams, a quarter ounce, or half a pound.
std::string nudgedAmount(const std::string& text, const std::string& unit, int direction);

}  // namespace kettlelogic::app
