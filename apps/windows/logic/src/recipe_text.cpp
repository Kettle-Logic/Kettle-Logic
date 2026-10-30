#include "kettlelogic/app/recipe_text.hpp"

#include "kettlelogic/app/preferences.hpp"
#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cctype>

namespace kettlelogic::app {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

std::string hopDetail(const model::Hop& hop) {
    std::vector<std::string> parts = {describe(hop.alphaAcid).display() + " AA"};
    if (hop.betaAcid) {
        parts.push_back(describe(*hop.betaAcid).display() + " BA");
    }
    parts.push_back(describe(hop.amount).display());
    std::vector<std::string> when;
    if (hop.timing.use) {
        when.push_back(hopUseName(*hop.timing.use));
    }
    if (hop.timing.time) {
        when.push_back(describe(*hop.timing.time).display());
    }
    if (!when.empty()) {
        std::string joined;
        for (const std::string& part : when) {
            joined += (joined.empty() ? "" : " ") + part;
        }
        parts.push_back(joined);
    }
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : " \xC2\xB7 ") + part;
    }
    return out;
}

std::string fermentableDetail(const model::Fermentable& f) {
    std::string out = describe(f.amount).display();
    if (f.yield.potential) {
        const QuantityText potential = describe(*f.yield.potential);
        out += " \xC2\xB7 " + potential.value + " " + potential.unit;
    }
    out += " \xC2\xB7 " + describe(f.color).display();
    return out;
}

std::string cultureDetail(const model::Culture& c) {
    std::string out = c.type + " \xC2\xB7 " + c.form + " \xC2\xB7 " + describe(c.amount).display();
    if (c.attenuation) {
        out += " \xC2\xB7 " + describe(*c.attenuation).display() + " attenuation";
    }
    return out;
}

std::string waterDetail(const model::WaterAddition& w) {
    std::string out = "Ca " + describe(w.water.calcium).value + " Mg " + describe(w.water.magnesium).value + " Na " +
                      describe(w.water.sodium).value + " Cl " + describe(w.water.chloride).value + " SO4 " +
                      describe(w.water.sulfate).value + " HCO3 " + describe(w.water.bicarbonate).value + " mg/L";
    if (w.amount) {
        out += " \xC2\xB7 " + describe(*w.amount).display();
    }
    return out;
}

std::string priceDetail(const model::Recipe& recipe, const calc::Results& results,
                        const std::optional<model::Price>& price, const Uuid& uuid) {
    const auto text = describe(price);
    if (!text) {
        return "";
    }
    const auto amount = parseNumber(text->amount);
    if (!amount) {
        return "";
    }
    std::string out = " \xC2\xB7 " + money(*amount, recipe.currency) + " / " + text->per;
    if (results.cost) {
        for (const calc::IngredientCost& line : results.cost->byIngredient) {
            if (line.ingredient == uuid) {
                out += " \xC2\xB7 " + money(line.cost, recipe.currency);
                break;
            }
        }
    }
    return out;
}

std::optional<double> litersOf(const Quantity& quantity) {
    const QuantityText q = describe(quantity);
    const auto number = parseNumber(q.value);
    if (!number) {
        return std::nullopt;
    }
    const std::string unit = lower(q.unit);
    if (unit == "l") return *number;
    if (unit == "ml") return *number / 1000;
    if (unit == "gal") return *number * Units::litersPerGallon;
    if (unit == "qt") return *number * Units::litersPerGallon / 4;
    if (unit == "pt") return *number * Units::litersPerGallon / 8;
    if (unit == "floz") return *number * Units::litersPerGallon / 128;
    return std::nullopt;
}

std::optional<double> gramsOf(const Quantity& quantity) {
    const QuantityText q = describe(quantity);
    const auto number = parseNumber(q.value);
    if (!number) {
        return std::nullopt;
    }
    const std::string unit = lower(q.unit);
    if (unit == "g") return *number;
    if (unit == "kg") return *number * 1000;
    if (unit == "oz") return *number * 28.349523125;
    if (unit == "lb") return *number * 453.59237;
    return std::nullopt;
}

std::map<std::string, std::string> starterPrefill(const model::Recipe& recipe, const calc::Results& results) {
    std::map<std::string, std::string> fields;
    if (const auto liters = litersOf(recipe.batchSize)) {
        fields["batchLiters"] = fixed(*liters, 2);
    }
    if (results.originalGravity) {
        fields["originalGravity"] = fixed(*results.originalGravity, 3);
    }
    if (recipe.cultures && !recipe.cultures->empty() && recipe.cultures->front().type == "lager") {
        fields["lager"] = "1";
    }
    return fields;
}

std::map<std::string, std::string> bitternessPrefill(const model::Recipe& recipe, const calc::Results& results,
                                                     const model::Hop& hop) {
    std::map<std::string, std::string> fields = {{"alphaPercent", describe(hop.alphaAcid).value}};
    if (const auto grams = gramsOf(hop.amount)) {
        fields["grams"] = fixed(*grams, 2);
    }
    const std::optional<double> liters = results.postBoilLiters ? results.postBoilLiters : litersOf(recipe.batchSize);
    if (liters) {
        fields["postBoilLiters"] = fixed(*liters, 2);
    }
    if (hop.timing.time) {
        const QuantityText minutes = describe(*hop.timing.time);
        if (minutes.unit == "min") {
            fields["boilMinutes"] = minutes.value;
        }
    }
    if (results.originalGravity) {
        fields["gravity"] = fixed(*results.originalGravity, 3);
    }
    return fields;
}

double batchLiters(const model::Recipe& recipe) {
    const QuantityText q = describe(recipe.batchSize);
    const double value = parseNumber(q.value).value_or(0);
    return q.unit == "gal" ? value * Units::litersPerGallon : value;
}

Rgb srmColor(double srm) {
    struct Stop {
        double srm;
        double r, g, b;
    };
    static const Stop stops[] = {
        {1, 255, 230, 153}, {3, 255, 216, 120}, {6, 255, 191, 66}, {10, 248, 166, 0}, {15, 239, 148, 0},
        {20, 229, 133, 0},  {25, 194, 107, 0},  {30, 143, 68, 0},  {35, 90, 42, 6},   {40, 46, 21, 7},
        {50, 18, 10, 5},
    };
    constexpr std::size_t count = sizeof stops / sizeof stops[0];
    const double value = std::min(std::max(srm, stops[0].srm), stops[count - 1].srm);
    std::size_t upperIndex = count - 1;
    for (std::size_t i = 0; i < count; ++i) {
        if (stops[i].srm >= value) {
            upperIndex = i;
            break;
        }
    }
    const Stop& upper = stops[upperIndex];
    const Stop& lowerStop = stops[upperIndex > 0 ? upperIndex - 1 : 0];
    const double t = upper.srm == lowerStop.srm ? 0 : (value - lowerStop.srm) / (upper.srm - lowerStop.srm);
    const auto mix = [&](double a, double b) { return static_cast<int>(a + (b - a) * t + 0.5); };
    return {mix(lowerStop.r, upper.r), mix(lowerStop.g, upper.g), mix(lowerStop.b, upper.b)};
}

}  // namespace kettlelogic::app
