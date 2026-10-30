#pragma once

#include <optional>
#include <string>
#include <vector>


// Starter recipes for the new-recipe wizard, which each front end turns into a real recipe. These are
// typical, conventional recipes for each style, written to be forgiving for a first-time brewer, not
// tuned recipes. A template does not store amounts: the builder works them out from the batch size and the
// brewing system's efficiency, so one template fits any batch size.
//   - grain: each fermentable is a share of the total fermentable weight, and the total is the weight that
//     reaches `targetOG`.
//   - boil hops: each addition is a share of `targetIBU`, and the weight that gives that share is worked
//     out from the hop's alpha acid and the boil time.
//   - aroma hops: a fixed weight per liter of finished beer, since aroma does not follow a formula.
//
// Names must match the ingredient catalog (ingredients.hpp), so the add dialogs and the wizard agree on
// alpha acid, yield, and color. `problems()` checks that, and that each recipe sits inside its BJCP 2021
// ranges.
//
// Left out on purpose: lagers (they need cold, steady fermentation most first-time brewers cannot give),
// sours, and anything that needs a yeast starter. Extract versions are not here: the engine applies
// brewhouse efficiency to extract and sugar too, so it would size them about 30 percent too heavy.
namespace kettlelogic::catalog {

// One fermentable in a template, as a share of the fermentable weight.
struct TemplateGrain {
    std::string name;
    double percent = 0;
};

// A hop added during the boil, as a share of the recipe's bitterness.
struct TemplateBoilHop {
    std::string name;
    int minutes = 0;  // before the end of the boil
    double ibuPercent = 0;
};

// A hop added after the boil for aroma, by weight per liter of finished beer.
struct TemplateAromaHop {
    struct Timing {
        enum class Kind { boil, flameout, dryHop };
        Kind kind = Kind::boil;
        int value = 0;  // minutes left in the boil, minutes steeped at flameout, or days in the fermenter

        static Timing boil(int minutes) { return {Kind::boil, minutes}; }
        static Timing flameout(int minutes) { return {Kind::flameout, minutes}; }
        static Timing dryHop(int days) { return {Kind::dryHop, days}; }
    };

    std::string name;
    double gramsPerLiter = 0;
    Timing timing;
};

struct TempRange {
    double lower = 0;
    double upper = 0;
};

struct RecipeTemplate {
    std::string id;
    std::string name;
    std::string summary;  // one plain sentence for the wizard's list
    std::string bjcpStyle;
    double targetOG = 1;
    double targetIBU = 0;
    int boilMinutes = 60;
    std::vector<TemplateGrain> grains;
    std::vector<TemplateBoilHop> boilHops;
    std::vector<TemplateAromaHop> aromaHops;
    std::string yeast;  // by cultures() name
    std::optional<std::string> yeastAttenuationPercent;
    double mashTempC = 66;
    TempRange fermentTempC;
    bool usesWhirlfloc = false;
    // What the brewer should know that the numbers do not say. A temperature is written `{24}` for 24 °C,
    // and the front end shows it in the user's unit.
    std::vector<std::string> tips;

    // Whatever is wrong with the template, as sentences. Empty when it is sound.
    std::vector<std::string> problems() const;

    static const std::vector<RecipeTemplate>& all();
};

}  // namespace kettlelogic::catalog
