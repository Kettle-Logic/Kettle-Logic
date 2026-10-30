#include "kettlelogic/io/recipe_export.hpp"

#include "kettlelogic/calc/fermented.hpp"
#include "kettlelogic/measure.hpp"
#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/quantity.hpp"

#include "recipe_text.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using kettlelogic::Dimension;
using kettlelogic::Measure;
using kettlelogic::Quantity;
using kettlelogic::Unit;
using kettlelogic::model::Recipe;

namespace kettlelogic::io {
namespace {

// A number with up to six decimals, and no trailing zeros, so that it reads as text.
std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6f", value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// Removes "--" from a comment, which XML does not allow inside one.
std::string commentText(std::string text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            out += "- ";
            ++i;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// Writes BeerXML text: nested elements, one per line, indented two spaces per level.
class Xml {
public:
    void open(int depth, const std::string& tag) {
        line(depth, "<" + tag + ">");
    }

    void close(int depth, const std::string& tag) {
        line(depth, "</" + tag + ">");
    }

    void leaf(int depth, const std::string& tag, const std::string& text) {
        line(depth, "<" + tag + ">" + escape(text) + "</" + tag + ">");
    }

    void comment(int depth, const std::string& text) {
        line(depth, "<!-- " + commentText(text) + " -->");
    }

    const std::string& str() const { return out_; }

private:
    void line(int depth, const std::string& text) {
        out_.append(static_cast<std::size_t>(depth) * 2, ' ');
        out_ += text;
        out_.push_back('\n');
    }

    std::string out_;
};

std::string recipeTypeForXml(model::RecipeType type, std::vector<std::string>& dropped) {
    switch (type) {
        case model::RecipeType::AllGrain: return "All Grain";
        case model::RecipeType::PartialMash: return "Partial Mash";
        case model::RecipeType::Extract: return "Extract";
        default:
            dropped.push_back("the recipe type \"" + std::string(model::recipeTypeName(type)) +
                              "\" has no BeerXML equivalent, so it is written as All Grain");
            return "All Grain";
    }
}

std::string fermentableTypeForXml(const std::string& type) {
    if (type == "grain") return "Grain";
    if (type == "sugar") return "Sugar";
    if (type == "extract") return "Extract";
    if (type == "dry extract") return "Dry Extract";
    return "Adjunct";
}

std::string hopUseForXml(const std::optional<std::string>& use) {
    if (use && *use == "add_to_mash") return "Mash";
    if (use && *use == "add_to_fermentation") return "Dry Hop";
    if (use && *use == "add_to_package") return "Dry Hop";
    return "Boil";
}

std::string hopFormForXml(const std::optional<std::string>& form) {
    if (form && *form == "leaf") return "Leaf";
    if (form && *form == "plug") return "Plug";
    return "Pellet";
}

std::string miscTypeForXml(const std::string& type) {
    if (type == "spice") return "Spice";
    if (type == "fining") return "Fining";
    if (type == "water agent") return "Water Agent";
    if (type == "herb") return "Herb";
    if (type == "flavor") return "Flavor";
    return "Other";
}

std::string miscUseForXml(const std::optional<std::string>& use) {
    if (use && *use == "add_to_mash") return "Mash";
    if (use && *use == "add_to_fermentation") return "Primary";
    if (use && *use == "add_to_package") return "Bottling";
    return "Boil";
}

std::string cultureTypeForXml(const std::string& type) {
    if (type == "lager") return "Lager";
    if (type == "wheat") return "Wheat";
    if (type == "wine") return "Wine";
    if (type == "champagne") return "Champagne";
    return "Ale";
}

std::string cultureFormForXml(const std::string& form) {
    if (form == "dry") return "Dry";
    if (form == "slant") return "Slant";
    if (form == "culture") return "Culture";
    return "Liquid";
}

std::string mashStepTypeForXml(const std::string& type) {
    if (type == "temperature") return "Temperature";
    if (type == "decoction") return "Decoction";
    return "Infusion";
}

// Yield as a percent of sucrose's 46.214 points per pound per gallon, from a fine-grind yield or a potential.
std::optional<double> yieldPercent(const model::Fermentable& f) {
    if (f.yield.fineGrind && f.yield.fineGrind->dimension() == Dimension::Fraction) {
        return f.yield.fineGrind->valueIn(Unit::Percent).toDouble();
    }
    if (f.yield.potential && f.yield.potential->unit() == "sg") {
        const double points = (f.yield.potential->value().toDouble() - 1.0) * 1000.0;
        return points / 46.214 * 100.0;
    }
    return std::nullopt;
}

// Color as SRM. EBC divides by 1.97, and Lovibond gives SRM = 1.3 L - 0.6.
std::optional<double> colorSrm(const Measure& color) {
    const double value = color.value().toDouble();
    if (color.unit() == "SRM") return value;
    if (color.unit() == "EBC") return value / 1.97;
    if (color.unit() == "Lovi") return 1.3 * value - 0.6;
    return std::nullopt;
}

// The COLOR a fermentable is written with: degrees Lovibond, except a liquid extract, which BeerXML
// gives in SRM. Lovibond is the inverse of the SRM formula above, L = (SRM + 0.6) / 1.3.
std::optional<double> colorForXml(const model::Fermentable& f) {
    const bool liquidExtract = f.type == "extract";
    if (!liquidExtract && f.color.unit() == "Lovi") return f.color.value().toDouble();
    const std::optional<double> srm = colorSrm(f.color);
    if (!srm || liquidExtract) return srm;
    return (*srm + 0.6) / 1.3;
}

bool isMass(const Quantity& q) {
    return q.dimension() == Dimension::Mass;
}

bool isVolume(const Quantity& q) {
    return q.dimension() == Dimension::Volume;
}

std::string minutesOf(const std::optional<Quantity>& time) {
    return time ? number(time->valueIn(Unit::Minute).toDouble()) : "0";
}

}  // namespace

std::string exportBeerJson(const Recipe& recipe) {
    model::Document document;
    document.recipes.push_back(recipe);
    return model::writeBeerJson(document);
}

std::string exportBeerXml(const Recipe& recipe, const calc::Results& results) {
    std::vector<std::string> dropped;
    Xml body;
    body.open(1, "RECIPE");
    body.leaf(2, "VERSION", "1");
    body.leaf(2, "NAME", recipe.name);
    body.leaf(2, "TYPE", recipeTypeForXml(recipe.type, dropped));
    body.leaf(2, "BREWER", recipe.author);
    const double batchLiters = recipe.batchSize.valueIn(Unit::Liter).toDouble();
    body.leaf(2, "BATCH_SIZE", number(batchLiters));
    body.leaf(2, "BOIL_SIZE", number(results.preBoilLiters ? *results.preBoilLiters : batchLiters));
    body.leaf(2, "BOIL_TIME", recipe.boil ? number(recipe.boil->boilTime.valueIn(Unit::Minute).toDouble()) : "0");
    body.leaf(2, "EFFICIENCY", number(recipe.efficiency.brewhouse.valueIn(Unit::Percent).toDouble()));
    if (results.originalGravity) body.leaf(2, "EST_OG", number(*results.originalGravity));
    if (results.finalGravity) body.leaf(2, "EST_FG", number(*results.finalGravity));
    if (results.abv) body.leaf(2, "EST_ABV", number(*results.abv));
    if (results.srm) body.leaf(2, "EST_COLOR", number(*results.srm));
    if (results.ibu) body.leaf(2, "IBU", number(*results.ibu));
    if (recipe.notes) body.leaf(2, "NOTES", *recipe.notes);

    body.open(2, "FERMENTABLES");
    for (const model::Fermentable& f : recipe.fermentables) {
        if (!isMass(f.amount)) {
            dropped.push_back("fermentable \"" + f.name + "\" is measured by volume, and BeerXML gives fermentables in kilograms");
            continue;
        }
        body.open(3, "FERMENTABLE");
        body.leaf(4, "VERSION", "1");
        body.leaf(4, "NAME", f.name);
        body.leaf(4, "TYPE", fermentableTypeForXml(f.type));
        body.leaf(4, "AMOUNT", number(f.amount.valueIn(Unit::Kilogram).toDouble()));
        if (const auto yield = yieldPercent(f)) body.leaf(4, "YIELD", number(*yield));
        if (const auto color = colorForXml(f)) body.leaf(4, "COLOR", number(*color));
        body.close(3, "FERMENTABLE");
    }
    body.close(2, "FERMENTABLES");

    body.open(2, "HOPS");
    if (recipe.hops) {
        for (const model::Hop& hop : *recipe.hops) {
            if (!isMass(hop.amount)) {
                dropped.push_back("hop \"" + hop.name + "\" is measured by volume, and BeerXML gives hops in kilograms");
                continue;
            }
            body.open(3, "HOP");
            body.leaf(4, "VERSION", "1");
            body.leaf(4, "NAME", hop.name);
            body.leaf(4, "ALPHA", number(hop.alphaAcid.valueIn(Unit::Percent).toDouble()));
            body.leaf(4, "AMOUNT", number(hop.amount.valueIn(Unit::Kilogram).toDouble()));
            body.leaf(4, "USE", hopUseForXml(hop.timing.use));
            body.leaf(4, "TIME", minutesOf(hop.timing.time));
            body.leaf(4, "FORM", hopFormForXml(hop.form));
            if (hop.betaAcid) body.leaf(4, "BETA", number(hop.betaAcid->valueIn(Unit::Percent).toDouble()));
            body.close(3, "HOP");
        }
    }
    body.close(2, "HOPS");

    body.open(2, "MISCS");
    if (recipe.miscellaneous) {
        for (const model::Miscellaneous& misc : *recipe.miscellaneous) {
            const bool weight = isMass(misc.amount);
            if (!weight && !isVolume(misc.amount)) {
                dropped.push_back("\"" + misc.name + "\" is counted, not measured, and BeerXML miscellaneous ingredients are weighed or measured by volume");
                continue;
            }
            body.open(3, "MISC");
            body.leaf(4, "VERSION", "1");
            body.leaf(4, "NAME", misc.name);
            body.leaf(4, "TYPE", miscTypeForXml(misc.type));
            body.leaf(4, "USE", miscUseForXml(misc.timing ? misc.timing->use : std::nullopt));
            body.leaf(4, "TIME", misc.timing ? minutesOf(misc.timing->time) : "0");
            body.leaf(4, "AMOUNT", weight ? number(misc.amount.valueIn(Unit::Kilogram).toDouble())
                                          : number(misc.amount.valueIn(Unit::Liter).toDouble()));
            body.leaf(4, "AMOUNT_IS_WEIGHT", weight ? "TRUE" : "FALSE");
            body.close(3, "MISC");
        }
    }
    body.close(2, "MISCS");

    body.open(2, "YEASTS");
    if (recipe.cultures) {
        for (const model::Culture& culture : *recipe.cultures) {
            const bool weight = isMass(culture.amount);
            if (!weight && !isVolume(culture.amount)) {
                dropped.push_back("yeast \"" + culture.name + "\" is counted, and BeerXML gives yeast by volume or weight");
                continue;
            }
            body.open(3, "YEAST");
            body.leaf(4, "VERSION", "1");
            body.leaf(4, "NAME", culture.name);
            body.leaf(4, "TYPE", cultureTypeForXml(culture.type));
            body.leaf(4, "FORM", cultureFormForXml(culture.form));
            body.leaf(4, "AMOUNT", weight ? number(culture.amount.valueIn(Unit::Kilogram).toDouble())
                                          : number(culture.amount.valueIn(Unit::Liter).toDouble()));
            body.leaf(4, "AMOUNT_IS_WEIGHT", weight ? "TRUE" : "FALSE");
            if (culture.attenuation) {
                body.leaf(4, "ATTENUATION", number(culture.attenuation->valueIn(Unit::Percent).toDouble()));
            }
            body.close(3, "YEAST");
        }
    }
    body.close(2, "YEASTS");

    if (recipe.waters) {
        body.open(2, "WATERS");
        for (const model::WaterAddition& addition : *recipe.waters) {
            const model::WaterProfile& w = addition.water;
            const auto ion = [](const Quantity& q) { return number(q.valueIn(Unit::MilligramPerLiter).toDouble()); };
            body.open(3, "WATER");
            body.leaf(4, "VERSION", "1");
            body.leaf(4, "NAME", w.name);
            body.leaf(4, "AMOUNT", addition.amount && isVolume(*addition.amount)
                                       ? number(addition.amount->valueIn(Unit::Liter).toDouble())
                                       : "0");
            body.leaf(4, "CALCIUM", ion(w.calcium));
            body.leaf(4, "BICARBONATE", ion(w.bicarbonate));
            body.leaf(4, "SULFATE", ion(w.sulfate));
            body.leaf(4, "CHLORIDE", ion(w.chloride));
            body.leaf(4, "SODIUM", ion(w.sodium));
            body.leaf(4, "MAGNESIUM", ion(w.magnesium));
            body.close(3, "WATER");
        }
        body.close(2, "WATERS");
    }

    if (recipe.mash) {
        body.open(2, "MASH");
        body.leaf(3, "VERSION", "1");
        body.leaf(3, "NAME", recipe.mash->name);
        body.leaf(3, "GRAIN_TEMP", number(recipe.mash->grainTemperature.valueIn(Unit::Celsius).toDouble()));
        body.open(3, "MASH_STEPS");
        for (const model::MashStep& step : recipe.mash->steps) {
            body.open(4, "MASH_STEP");
            body.leaf(5, "VERSION", "1");
            body.leaf(5, "NAME", step.name);
            body.leaf(5, "TYPE", mashStepTypeForXml(step.type));
            body.leaf(5, "STEP_TEMP", number(step.stepTemperature.valueIn(Unit::Celsius).toDouble()));
            body.leaf(5, "STEP_TIME", number(step.stepTime.valueIn(Unit::Minute).toDouble()));
            if (step.amount && isVolume(*step.amount)) {
                body.leaf(5, "INFUSE_AMOUNT", number(step.amount->valueIn(Unit::Liter).toDouble()));
            }
            body.close(4, "MASH_STEP");
        }
        body.close(3, "MASH_STEPS");
        body.close(2, "MASH");
    }

    body.close(1, "RECIPE");

    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<RECIPES>\n";
    if (!dropped.empty()) {
        Xml notes;
        notes.comment(1, "Not exported, because BeerXML has no place for them:");
        for (const std::string& item : dropped) {
            notes.comment(1, "  " + item);
        }
        out += notes.str();
    }
    out += body.str();
    out += "</RECIPES>\n";
    return out;
}

namespace {

// An amount of money to the cent, with its currency code, such as "12.34 USD".
std::string money(double amount, const std::string& currency) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.2f", amount);
    return std::string(buffer) + " " + currency;
}

}  // namespace

std::string exportText(const Recipe& recipe, const calc::Results& results) {
    std::string out;
    const auto line = [&out](const std::string& text) {
        out += text;
        out.push_back('\n');
    };
    const auto estimate = [&](const char* label, const std::optional<double>& value, const char* unit) {
        if (value) {
            line(std::string("  ") + label + ": " + number(*value) + unit);
        }
    };

    line(recipe.name);
    line(std::string("Type: ") + std::string(model::recipeTypeName(recipe.type)));
    if (!recipe.author.empty()) line("Author: " + recipe.author);
    line("Batch size: " + display(recipe.batchSize));
    line("");

    line("Estimates");
    estimate("Original gravity", results.originalGravity, "");
    estimate("Final gravity", results.finalGravity, "");
    estimate("Alcohol", results.abv, " % ABV");
    estimate("Bitterness", results.ibu, " IBU");
    estimate("Color", results.srm, " SRM");
    estimate("Pre-boil volume", results.preBoilLiters, " L");
    estimate("Post-boil volume", results.postBoilLiters, " L");
    line("");

    // Only a recipe with prices has a cost. The prices are Kettle Logic data, so BeerXML does not carry them.
    if (results.cost) {
        const calc::RecipeCost& cost = *results.cost;
        line("Cost");
        line("  Ingredients: " + money(cost.total, cost.currency));
        if (cost.perLiter) line("  Per liter: " + money(*cost.perLiter, cost.currency));
        if (cost.perGallon) line("  Per US gallon: " + money(*cost.perGallon, cost.currency));
        if (cost.unpriced > 0) {
            line("  " + std::to_string(cost.unpriced) + (cost.unpriced == 1 ? " ingredient is" : " ingredients are") +
                 " not included, because " + (cost.unpriced == 1 ? "it has" : "they have") + " no usable price");
        }
        line("");
    }

    if (!recipe.fermentables.empty()) {
        line("Fermentables");
        for (const model::Fermentable& f : recipe.fermentables) {
            line("  " + display(f.amount) + "  " + f.name + " (" + f.type + ")");
        }
        line("");
    }

    if (recipe.hops && !recipe.hops->empty()) {
        line("Hops");
        for (const model::Hop& hop : *recipe.hops) {
            std::string text = "  " + display(hop.amount) + "  " + hop.name + ", " +
                               display(hop.alphaAcid) + " alpha";
            if (hop.betaAcid) text += ", " + display(*hop.betaAcid) + " beta";
            if (hop.timing.time) text += ", " + display(*hop.timing.time);
            if (hop.timing.use) text += ", " + *hop.timing.use;
            line(text);
        }
        line("");
    }

    if (recipe.miscellaneous && !recipe.miscellaneous->empty()) {
        line("Miscellaneous");
        for (const model::Miscellaneous& misc : *recipe.miscellaneous) {
            line("  " + display(misc.amount) + "  " + misc.name + " (" + misc.type + ")");
        }
        line("");
    }

    if (recipe.cultures && !recipe.cultures->empty()) {
        line("Yeast");
        for (const model::Culture& culture : *recipe.cultures) {
            line("  " + culture.name + " (" + culture.type + ", " + culture.form + ")");
        }
        line("");
    }

    if (recipe.mash && !recipe.mash->steps.empty()) {
        line("Mash");
        line("  Grain temperature: " + display(recipe.mash->grainTemperature));
        for (const model::MashStep& step : recipe.mash->steps) {
            line("  " + step.name + " (" + step.type + "): " + display(step.stepTemperature) + " for " +
                 display(step.stepTime));
        }
        line("");
    }

    if (recipe.notes && !recipe.notes->empty()) {
        line("Notes");
        line(*recipe.notes);
    }
    return out;
}

}  // namespace kettlelogic::io
