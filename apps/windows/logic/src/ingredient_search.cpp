#include "kettlelogic/app/ingredient_search.hpp"

#include "kettlelogic/app/text.hpp"

#include <optional>

namespace kettlelogic::app {
namespace {

// The color in SRM. The Lovibond conversion is the one the engine uses for a recipe's color, SRM = 1.3 * L - 0.6.
std::optional<double> srmFrom(const std::string& value, const std::string& unit) {
    const std::optional<double> number = parseNumber(value);
    if (!number) return std::nullopt;
    if (unit == "SRM") return *number;
    if (unit == "EBC") return *number / 1.97;
    if (unit == "Lovi") return 1.3 * *number - 0.6;
    return std::nullopt;
}

}  // namespace

std::string searchKindName(IngredientKind kind) {
    switch (kind) {
        case IngredientKind::fermentable: return "fermentable";
        case IngredientKind::hop: return "hop";
        case IngredientKind::culture: return "culture";
        case IngredientKind::water: return "water";
        case IngredientKind::miscellaneous: break;
    }
    return "miscellaneous";
}

catalog::IngredientEntry searchEntry(const MenuEntry& entry) {
    const IngredientDraft& draft = entry.draft;
    catalog::IngredientEntry out;
    out.id = entry.id;
    out.name = entry.name;
    out.group = entry.group;
    out.type = draft.type;
    out.form = draft.form;
    out.custom = entry.id.rfind("custom-", 0) == 0;
    switch (draft.kind) {
        case IngredientKind::fermentable:
            out.primary = srmFrom(draft.colorValue, draft.colorUnit);
            if (draft.yieldUnit == "sg") out.secondary = parseNumber(draft.yieldValue);
            break;
        case IngredientKind::hop:
            out.primary = parseNumber(draft.alpha);
            out.secondary = parseNumber(draft.beta);
            break;
        case IngredientKind::culture:
            out.primary = parseNumber(draft.attenuation);
            break;
        case IngredientKind::water:
        case IngredientKind::miscellaneous:
            break;
    }
    return out;
}

}  // namespace kettlelogic::app
