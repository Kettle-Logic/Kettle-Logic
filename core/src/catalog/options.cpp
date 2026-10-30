#include "kettlelogic/catalog/options.hpp"

#include <algorithm>

namespace kettlelogic::catalog {

const std::vector<std::string>& mashStepKinds() {
    static const std::vector<std::string> v = {"Infusion", "Temperature", "Decoction"};
    return v;
}
const std::vector<std::string>& carbonationMethods() {
    static const std::vector<std::string> v = {"Bottle conditioning (corn sugar)", "Force carbonation", "None"};
    return v;
}
const std::vector<std::string>& currencyCodes() {
    static const std::vector<std::string> v = {"USD", "EUR", "GBP", "CAD", "AUD", "NZD", "CHF", "JPY", "SEK", "NOK", "DKK", "PLN", "CZK"};
    return v;
}

const std::vector<std::string>& recipeTypes() {
    static const std::vector<std::string> v = {"all grain", "partial mash", "extract", "cider", "mead", "wine", "kombucha", "soda", "other"};
    return v;
}

const std::vector<std::string>& fermentableTypes() {
    static const std::vector<std::string> v = {"grain", "extract", "dry extract", "sugar", "honey", "fruit", "juice", "other"};
    return v;
}

const std::vector<std::string>& fermentableUnits() {
    static const std::vector<std::string> v = {"kg", "lb", "g", "oz", "l", "gal", "ml", "floz"};
    return v;
}

const std::vector<std::string>& miscellaneousTypes() {
    static const std::vector<std::string> v = {"spice", "fining", "water agent", "herb", "flavor", "wood", "other"};
    return v;
}

const std::vector<std::string>& miscellaneousUnits() {
    static const std::vector<std::string> v = {"g", "oz", "kg", "lb", "mg", "ml", "l", "tsp", "tbsp", "each"};
    return v;
}

const std::vector<std::string>& hopForms() {
    static const std::vector<std::string> v = {"pellet", "leaf", "leaf (wet)", "plug", "powder", "extract"};
    return v;
}

const std::vector<std::string>& hopUses() {
    static const std::vector<std::string> v = {"add_to_boil", "add_to_mash", "add_to_fermentation", "add_to_package"};
    return v;
}

const std::vector<std::string>& hopUnits() {
    static const std::vector<std::string> v = {"g", "oz", "kg", "lb"};
    return v;
}

const std::vector<std::string>& hopTimeUnits() {
    static const std::vector<std::string> v = {"min", "hr", "day"};
    return v;
}

const std::vector<std::string>& cultureTypes() {
    static const std::vector<std::string> v = {"ale", "lager", "wheat", "kveik", "wine", "champagne", "brett", "lacto", "pedio", "malolactic", "bacteria", "mixed-culture", "spontaneous", "other"};
    return v;
}

const std::vector<std::string>& cultureForms() {
    static const std::vector<std::string> v = {"dry", "liquid", "slant", "culture", "dregs"};
    return v;
}

const std::vector<std::string>& cultureUnits() {
    static const std::vector<std::string> v = {"pkg", "g", "ml", "each"};
    return v;
}

std::vector<std::string> priceUnits(const std::string& amountUnit) {
    const std::vector<std::string> mass = {"kg", "lb", "g", "oz"};
    const std::vector<std::string> volume = {"l", "gal", "ml", "floz"};
    const std::vector<std::string> count = {"pkg", "each"};
    const auto has = [&](const std::vector<std::string>& list) {
        return std::find(list.begin(), list.end(), amountUnit) != list.end();
    };
    if (amountUnit == "mg" || has(mass)) return mass;
    if (amountUnit == "tsp" || amountUnit == "tbsp" || has(volume)) return volume;
    if (has(count)) return count;
    return {amountUnit};
}

}  // namespace kettlelogic::catalog
