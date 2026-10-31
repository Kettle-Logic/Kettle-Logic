#include "kettlelogic/catalog/recipe_templates.hpp"

#include "kettlelogic/calc/style.hpp"
#include "kettlelogic/catalog/ingredients.hpp"

#include <algorithm>
#include <cmath>

namespace kettlelogic::catalog {

const std::vector<RecipeTemplate>& RecipeTemplate::all() {
    static const std::vector<RecipeTemplate> items = {
        RecipeTemplate{
            .id = "american-pale-ale", .name = "American Pale Ale",
            .summary = "Citrusy, balanced, and easy to like. A good first brew.",
            .bjcpStyle = "18B", .targetOG = 1.054, .targetIBU = 38, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "2-Row (Briess)", .percent = 90}, TemplateGrain{.name = "Crystal 40", .percent = 10}},
            .boilHops = {
                TemplateBoilHop{.name = "Centennial", .minutes = 60, .ibuPercent = 60},
                TemplateBoilHop{.name = "Cascade", .minutes = 15, .ibuPercent = 40},
            },
            .aromaHops = {
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1, .timing = TemplateAromaHop::Timing::boil( 5)},
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1.5, .timing = TemplateAromaHop::Timing::flameout( 20)},
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 2, .timing = TemplateAromaHop::Timing::dryHop( 3)},
            },
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 66, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"US-05 is forgiving. Keep fermentation under {22} to avoid fruity, solvent-like flavors.",
                   "The dry hop is optional. Skip it for a milder beer."}},

        RecipeTemplate{
            .id = "american-ipa", .name = "American IPA",
            .summary = "Bold, bitter, and tropical. More hops than a pale ale.",
            .bjcpStyle = "21A", .targetOG = 1.064, .targetIBU = 60, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "2-Row (Briess)", .percent = 92}, TemplateGrain{.name = "Crystal 40", .percent = 8}},
            .boilHops = {
                TemplateBoilHop{.name = "Columbus", .minutes = 60, .ibuPercent = 65},
                TemplateBoilHop{.name = "Centennial", .minutes = 15, .ibuPercent = 35},
            },
            .aromaHops = {
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1.5, .timing = TemplateAromaHop::Timing::boil( 5)},
                TemplateAromaHop{.name = "Citra", .gramsPerLiter = 3, .timing = TemplateAromaHop::Timing::flameout( 20)},
                TemplateAromaHop{.name = "Citra", .gramsPerLiter = 3, .timing = TemplateAromaHop::Timing::dryHop( 4)},
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 2, .timing = TemplateAromaHop::Timing::dryHop( 4)},
            },
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 65.5, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"Hoppy beers fade fast. Drink it fresh and keep it cold.",
                   "Dry hop for 3 to 5 days, then package. Longer can add grassy flavors."}},

        RecipeTemplate{
            .id = "blonde-ale", .name = "Blonde Ale",
            .summary = "Light, smooth, and low in bitterness. Hard to get wrong.",
            .bjcpStyle = "18A", .targetOG = 1.046, .targetIBU = 20, .boilMinutes = 60,
            .grains = {
                TemplateGrain{.name = "2-Row (Briess)", .percent = 90},
                TemplateGrain{.name = "Carapils (dextrin malt)", .percent = 5},
                TemplateGrain{.name = "Crystal 20", .percent = 5},
            },
            .boilHops = {
                TemplateBoilHop{.name = "Willamette", .minutes = 60, .ibuPercent = 70},
                TemplateBoilHop{.name = "Cascade", .minutes = 10, .ibuPercent = 30},
            },
            .aromaHops = {},
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 66, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"A clean, simple recipe, so any off flavor is from process, not ingredients. Good for learning."}},

        RecipeTemplate{
            .id = "american-amber-ale", .name = "American Amber Ale",
            .summary = "Malty and caramel-sweet, with a hop finish.",
            .bjcpStyle = "19A", .targetOG = 1.056, .targetIBU = 32, .boilMinutes = 60,
            .grains = {
                TemplateGrain{.name = "2-Row (Briess)", .percent = 83},
                TemplateGrain{.name = "Munich malt", .percent = 5},
                TemplateGrain{.name = "Crystal 60", .percent = 12},
            },
            .boilHops = {
                TemplateBoilHop{.name = "Centennial", .minutes = 60, .ibuPercent = 55},
                TemplateBoilHop{.name = "Cascade", .minutes = 15, .ibuPercent = 45},
            },
            .aromaHops = {
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1, .timing = TemplateAromaHop::Timing::boil( 5)},
                TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1.5, .timing = TemplateAromaHop::Timing::flameout( 20)},
            },
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 66, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"Crystal malt adds sweetness the yeast cannot ferment. Keep it near 12 percent or below on a first try."}},

        RecipeTemplate{
            .id = "american-wheat", .name = "American Wheat Beer",
            .summary = "Soft, refreshing, and lightly hopped. A summer beer.",
            .bjcpStyle = "1D", .targetOG = 1.048, .targetIBU = 18, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "Wheat malt", .percent = 50}, TemplateGrain{.name = "2-Row (Briess)", .percent = 50}},
            .boilHops = {TemplateBoilHop{.name = "Cascade", .minutes = 60, .ibuPercent = 100}},
            .aromaHops = {TemplateAromaHop{.name = "Cascade", .gramsPerLiter = 1, .timing = TemplateAromaHop::Timing::flameout( 15)}},
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 66, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"Wheat is sticky in the mash. If the runoff slows, stir gently and wait. Rice hulls help, if you have them."}},

        RecipeTemplate{
            .id = "cream-ale", .name = "Cream Ale",
            .summary = "Crisp, pale, and easy-drinking, with a clean finish.",
            .bjcpStyle = "1C", .targetOG = 1.049, .targetIBU = 18, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "2-Row (Briess)", .percent = 90}, TemplateGrain{.name = "Corn sugar (dextrose)", .percent = 10}},
            .boilHops = {
                TemplateBoilHop{.name = "Willamette", .minutes = 60, .ibuPercent = 60},
                TemplateBoilHop{.name = "Willamette", .minutes = 15, .ibuPercent = 40},
            },
            .aromaHops = {},
            .yeast = "Fermentis SafAle US-05", .yeastAttenuationPercent = std::nullopt, .mashTempC = 65, .fermentTempC = {17, 19}, .usesWhirlfloc = true,
            .tips = {"The sugar goes into the boil. It keeps the beer light and dry. Ferment cool for a clean flavor."}},

        RecipeTemplate{
            .id = "irish-stout", .name = "Irish Stout",
            .summary = "Dark, roasty, and light-bodied, with low alcohol.",
            .bjcpStyle = "15B", .targetOG = 1.042, .targetIBU = 36, .boilMinutes = 60,
            .grains = {
                TemplateGrain{.name = "2-Row (Briess)", .percent = 83},
                TemplateGrain{.name = "Roasted barley", .percent = 10},
                TemplateGrain{.name = "Chocolate malt", .percent = 7},
            },
            .boilHops = {TemplateBoilHop{.name = "East Kent Goldings", .minutes = 60, .ibuPercent = 100}},
            .aromaHops = {},
            .yeast = "Fermentis SafAle S-04", .yeastAttenuationPercent = std::nullopt, .mashTempC = 67, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"Roasted barley gives the stout its color and dry, coffee-like bite.",
                   "Traditional recipes use flaked barley for body. It is not in the ingredient list yet."}},

        RecipeTemplate{
            .id = "american-porter", .name = "American Porter",
            .summary = "Dark and malty, with chocolate notes and a smooth finish.",
            .bjcpStyle = "20A", .targetOG = 1.058, .targetIBU = 35, .boilMinutes = 60,
            .grains = {
                TemplateGrain{.name = "2-Row (Briess)", .percent = 78},
                TemplateGrain{.name = "Munich malt", .percent = 8},
                TemplateGrain{.name = "Crystal 60", .percent = 6},
                TemplateGrain{.name = "Chocolate malt", .percent = 8},
            },
            .boilHops = {
                TemplateBoilHop{.name = "Northern Brewer", .minutes = 60, .ibuPercent = 60},
                TemplateBoilHop{.name = "Willamette", .minutes = 15, .ibuPercent = 40},
            },
            .aromaHops = {},
            .yeast = "Fermentis SafAle S-04", .yeastAttenuationPercent = std::nullopt, .mashTempC = 67, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"Chocolate malt can taste harsh if overused. Eight percent is a safe amount."}},

        RecipeTemplate{
            .id = "best-bitter", .name = "English Best Bitter",
            .summary = "Gentle, bready, and low in alcohol. A pub-style session beer.",
            .bjcpStyle = "11B", .targetOG = 1.044, .targetIBU = 32, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "Maris Otter", .percent = 90}, TemplateGrain{.name = "Crystal 60", .percent = 10}},
            .boilHops = {
                TemplateBoilHop{.name = "East Kent Goldings", .minutes = 60, .ibuPercent = 70},
                TemplateBoilHop{.name = "East Kent Goldings", .minutes = 15, .ibuPercent = 30},
            },
            .aromaHops = {TemplateAromaHop{.name = "East Kent Goldings", .gramsPerLiter = 0.8, .timing = TemplateAromaHop::Timing::boil( 5)}},
            .yeast = "Fermentis SafAle S-04", .yeastAttenuationPercent = std::nullopt, .mashTempC = 67, .fermentTempC = {18, 20}, .usesWhirlfloc = true,
            .tips = {"English ales are served a little warmer and less fizzy than American ones, so carbonate lightly."}},

        RecipeTemplate{
            .id = "hefeweizen", .name = "Hefeweizen",
            .summary = "Cloudy German wheat beer with banana and clove from the yeast.",
            .bjcpStyle = "10A", .targetOG = 1.050, .targetIBU = 12, .boilMinutes = 60,
            .grains = {TemplateGrain{.name = "Wheat malt", .percent = 55}, TemplateGrain{.name = "Pilsen malt", .percent = 45}},
            .boilHops = {TemplateBoilHop{.name = "Hallertau Mittelfrüh", .minutes = 60, .ibuPercent = 100}},
            .aromaHops = {},
            .yeast = "Fermentis SafAle WB-06", .yeastAttenuationPercent = "76", .mashTempC = 66, .fermentTempC = {19, 22}, .usesWhirlfloc = false,
            .tips = {"The yeast makes the flavor, so keep it near {20}. Warmer is more banana, cooler is more clove.",
                   "Skip the fining tablet. A hazy beer is correct for the style."}},

        RecipeTemplate{
            .id = "saison", .name = "Saison",
            .summary = "Dry, peppery, and fruity. A farmhouse ale that likes warm fermentation.",
            .bjcpStyle = "25B", .targetOG = 1.054, .targetIBU = 26, .boilMinutes = 60,
            .grains = {
                TemplateGrain{.name = "Pilsen malt", .percent = 78},
                TemplateGrain{.name = "Wheat malt", .percent = 10},
                TemplateGrain{.name = "Munich malt", .percent = 5},
                TemplateGrain{.name = "White granulated sugar", .percent = 7},
            },
            .boilHops = {
                TemplateBoilHop{.name = "Saaz", .minutes = 60, .ibuPercent = 70},
                TemplateBoilHop{.name = "Saaz", .minutes = 10, .ibuPercent = 30},
            },
            .aromaHops = {},
            .yeast = "Lallemand LalBrew Belle Saison", .yeastAttenuationPercent = std::nullopt, .mashTempC = 64, .fermentTempC = {24, 30}, .usesWhirlfloc = true,
            .tips = {"Start at about {24} and let it rise. Saison yeast finishes very dry and is slow to start, so be patient.",
                   "A low mash temperature and the sugar both help the beer finish dry."}},
    };
    return items;
}

std::vector<std::string> RecipeTemplate::problems() const {
    std::vector<std::string> found;
    double grainTotal = 0;
    for (const TemplateGrain& grain : grains) {
        grainTotal += grain.percent;
    }
    if (std::fabs(grainTotal - 100) > 0.01) {
        found.push_back("Grain percentages do not add to 100.");
    }
    double hopTotal = 0;
    for (const TemplateBoilHop& hop : boilHops) {
        hopTotal += hop.ibuPercent;
    }
    if (std::fabs(hopTotal - 100) > 0.01) {
        found.push_back("Hop bitterness shares do not add to 100.");
    }
    for (const TemplateGrain& grain : grains) {
        const auto& all = fermentables();
        if (std::none_of(all.begin(), all.end(), [&](const auto& f) { return f.name == grain.name; })) {
            found.push_back(grain.name + " is not a fermentable in the catalog.");
        }
    }
    std::vector<std::string> hopNames;
    for (const TemplateBoilHop& hop : boilHops) hopNames.push_back(hop.name);
    for (const TemplateAromaHop& hop : aromaHops) hopNames.push_back(hop.name);
    for (const std::string& hopName : hopNames) {
        const auto& all = hops();
        if (std::none_of(all.begin(), all.end(), [&](const auto& h) { return h.name == hopName; })) {
            found.push_back(hopName + " is not a hop in the catalog.");
        }
    }
    {
        const auto& all = cultures();
        if (std::none_of(all.begin(), all.end(), [&](const auto& c) { return c.name == yeast; })) {
            found.push_back(yeast + " is not a culture in the catalog.");
        }
    }
    if (const calc::BjcpStyle* style = calc::findBjcpStyle(bjcpStyle)) {
        const std::string title = std::string(style->id) + " " + std::string(style->name);
        if (targetOG < style->og.low || targetOG > style->og.high) {
            found.push_back("Target OG is outside the " + title + " range.");
        }
        if (targetIBU < style->ibu.low || targetIBU > style->ibu.high) {
            found.push_back("Target IBU is outside the " + title + " range.");
        }
    } else {
        found.push_back(bjcpStyle + " is not a BJCP style the engine knows.");
    }
    return found;
}

}  // namespace kettlelogic::catalog
