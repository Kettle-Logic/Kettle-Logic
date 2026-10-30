#include "kettlelogic/catalog/tool_specs.hpp"

namespace kettlelogic::catalog {
namespace {

using Choice = ToolField::Choice;

const std::vector<std::string> maltCategories = {"base", "crystal", "roast", "acid"};
const std::vector<std::string> mashAcids = {"lactic acid", "phosphoric acid", "malic acid", "citric acid",
                                            "tartaric acid", "acetic acid", "hydrochloric acid", "sulfuric acid"};

std::vector<std::string> withNone(std::vector<std::string> list) {
    list.push_back("none");
    return list;
}

ToolField number(const char* key, const char* label, const char* value) {
    return ToolField{.key = key, .label = label, .kind = ToolField::Kind::number, .defaultValue = value};
}

ToolField toggle(const char* key, const char* label, bool on) {
    return ToolField{.key = key, .label = label, .kind = ToolField::Kind::toggle, .defaultValue = on ? "1" : "0"};
}

ToolReadout readout(const char* key, const char* label, const char* unit = "") {
    return ToolReadout{.key = key, .label = label, .unit = unit};
}

// A choice of one value from a list, shown by its label.
ToolField choice(const char* key, const char* label, const std::vector<std::string>& options, const char* value) {
    ToolField field{.key = key, .label = label, .kind = ToolField::Kind::choice, .defaultValue = value};
    for (const std::string& option : options) {
        field.choices.push_back(Choice{option, option});
    }
    return field;
}

// A converter: a value, and the units to convert it from and to.
ToolSpec converter(const char* title, const char* tool, const std::vector<std::pair<std::string, std::string>>& units,
                   const std::pair<std::string, std::string>& defaults) {
    std::vector<Choice> choices;
    for (const auto& [value, label] : units) {
        choices.push_back(Choice{value, label});
    }
    ToolField from{.key = "from", .label = "From", .kind = ToolField::Kind::choice, .defaultValue = defaults.first};
    from.choices = choices;
    ToolField to{.key = "to", .label = "To", .kind = ToolField::Kind::choice, .defaultValue = defaults.second};
    to.choices = choices;
    return ToolSpec{.title = title,
                    .sections = {ToolSection{.title = "Convert",
                                             .footer = std::nullopt,
                                             .tool = tool,
                                             .fields = {number("value", "Value", "1"), from, to},
                                             .readouts = {readout("value", "Result")}}}};
}

}  // namespace

const std::vector<ToolKind>& calculators() {
    static const std::vector<ToolKind> v = {
        ToolKind::waterProfile, ToolKind::infusion, ToolKind::decoction, ToolKind::mashAdjustment, ToolKind::mashPh,
        ToolKind::spargeAcid, ToolKind::yeastStarter, ToolKind::hydrometer, ToolKind::refractometer,
        ToolKind::alcohol, ToolKind::hopBitterness, ToolKind::hopAge, ToolKind::meadNutrient,
        ToolKind::backsweetening, ToolKind::sulfiting, ToolKind::boilOff, ToolKind::dilution,
        ToolKind::gravityAdjustment, ToolKind::weightVolume, ToolKind::carbonation,
    };
    return v;
}

const std::vector<ToolKind>& converters() {
    static const std::vector<ToolKind> v = {ToolKind::temperature, ToolKind::gravityUnits, ToolKind::pressure,
                                            ToolKind::weight, ToolKind::volume};
    return v;
}

std::string toolKindName(ToolKind kind) {
    switch (kind) {
        case ToolKind::waterProfile: return "waterProfile";
        case ToolKind::infusion: return "infusion";
        case ToolKind::decoction: return "decoction";
        case ToolKind::mashAdjustment: return "mashAdjustment";
        case ToolKind::mashPh: return "mashPh";
        case ToolKind::spargeAcid: return "spargeAcid";
        case ToolKind::yeastStarter: return "yeastStarter";
        case ToolKind::hydrometer: return "hydrometer";
        case ToolKind::refractometer: return "refractometer";
        case ToolKind::alcohol: return "alcohol";
        case ToolKind::hopBitterness: return "hopBitterness";
        case ToolKind::hopAge: return "hopAge";
        case ToolKind::meadNutrient: return "meadNutrient";
        case ToolKind::backsweetening: return "backsweetening";
        case ToolKind::sulfiting: return "sulfiting";
        case ToolKind::boilOff: return "boilOff";
        case ToolKind::dilution: return "dilution";
        case ToolKind::gravityAdjustment: return "gravityAdjustment";
        case ToolKind::weightVolume: return "weightVolume";
        case ToolKind::carbonation: return "carbonation";
        case ToolKind::temperature: return "temperature";
        case ToolKind::gravityUnits: return "gravityUnits";
        case ToolKind::pressure: return "pressure";
        case ToolKind::weight: return "weight";
        case ToolKind::volume: return "volume";
    }
    return "";
}

std::optional<ToolKind> toolKindFromName(const std::string& name) {
    for (const auto* list : {&calculators(), &converters()}) {
        for (const ToolKind kind : *list) {
            if (toolKindName(kind) == name) {
                return kind;
            }
        }
    }
    return std::nullopt;
}

ToolSpec toolSpec(ToolKind kind) {
    switch (kind) {
        case ToolKind::waterProfile:
            return ToolSpec{.title = "Water Profile", .sections = {
                ToolSection{
                    .title = "How to Use",
                    .footer = "1. Enter the start water from its report, in mg/L (the same as ppm). Enter 0 for an ion the report does not give.\n"
                         "2. Add reverse osmosis or distilled water as a share of the volume, if you dilute the start water.\n"
                         "3. Enter each salt in grams, for the whole volume. Enter 0 for a salt you do not use.\n"
                         "4. Read the result below. Two toggles add a step: one scales baking soda by mash pH, the other compares the result with a target.\n"
                         "Save the profile at the bottom to load it into the Water tab of any recipe.",
                    .tool = "water.profile",
                    .fields = {},
                    .readouts = {}
                },
                ToolSection{
                    .title = "Start Water (mg/L)",
                    .footer = "The ions in the water before anything is added, from its report. Distilled and reverse osmosis "
                         "water have none, so leave every ion at 0.",
                    .tool = "water.profile",
                    .fields = {
                        number("calcium", "Calcium", "0"),
                        number("magnesium", "Magnesium", "0"),
                        number("sodium", "Sodium", "0"),
                        number("sulfate", "Sulfate", "0"),
                        number("chloride", "Chloride", "0"),
                        number("bicarbonate", "Bicarbonate", "0"),
                    },
                    .readouts = {}
                },
                ToolSection{
                    .title = "Reverse Osmosis",
                    .footer = "The share of reverse osmosis or distilled water mixed into the start water before the salts. "
                         "0 uses the start water as it is. A higher share dilutes it.",
                    .tool = "water.profile",
                    .fields = {
                        number("roPercent", "Share of reverse osmosis (%)", "0"),
                    },
                    .readouts = {}
                },
                ToolSection{
                    .title = "Volume and Salts (g)",
                    .footer = "The volume the salts dissolve in. The grams are for this whole volume, so twice the volume "
                         "needs twice the salt. What each salt adds: gypsum, calcium and sulfate; calcium chloride, calcium "
                         "and chloride; Epsom salt, magnesium and sulfate; magnesium "
                         "chloride, magnesium and chloride; table salt, sodium and chloride; baking soda, sodium and "
                         "bicarbonate; chalk, calcium and bicarbonate; pickling lime, calcium and bicarbonate.",
                    .tool = "water.profile",
                    .fields = {
                        number("volume", "Water volume (L)", "20"),
                        number("gypsum", "Gypsum", "0"),
                        number("calciumChloride", "Calcium chloride", "0"),
                        number("epsomSalt", "Epsom salt", "0"),
                        number("magnesiumChloride", "Magnesium chloride", "0"),
                        number("tableSalt", "Table salt", "0"),
                        number("bakingSoda", "Baking soda", "0"),
                        number("chalk", "Chalk", "0"),
                        number("picklingLime", "Pickling lime", "0"),
                    },
                    .readouts = {}
                },
                ToolSection{
                    .title = "Baking Soda and Mash pH",
                    .footer = "Baking soda's bicarbonate weakens as the mash pH rises. Turn this on and enter the mash pH to "
                         "scale it. Off counts all of the bicarbonate, as it does for water with no grist.",
                    .tool = "water.profile",
                    .fields = {
                        toggle("usesMashPh", "Scale baking soda by mash pH", false),
                        number("mashPh", "Mash pH", "5.4").enabled( "usesMashPh"),
                    },
                    .readouts = {}
                },
                ToolSection{
                    .title = "Target Water (mg/L)",
                    .footer = "Turn this on to compare the result with a target. The defaults are the Pale Ale profile.",
                    .tool = "water.profile",
                    .fields = {
                        toggle("useTarget", "Compare with a target water", false),
                        number("targetCalcium", "Target calcium", "140").enabled( "useTarget"),
                        number("targetMagnesium", "Target magnesium", "18").enabled( "useTarget"),
                        number("targetSodium", "Target sodium", "25").enabled( "useTarget"),
                        number("targetSulfate", "Target sulfate", "300").enabled( "useTarget"),
                        number("targetChloride", "Target chloride", "55").enabled( "useTarget"),
                        number("targetBicarbonate", "Target bicarbonate", "110").enabled( "useTarget"),
                    },
                    .readouts = {}
                },
                ToolSection{
                    .title = "Result",
                    .footer = "The water after the reverse osmosis share and the salts, in mg/L. Residual alkalinity (as CaCO₃) "
                         "sets the mash pH: low suits pale beers, high suits dark ones. Sulfate to chloride sets the balance: "
                         "below 0.5 is malt-forward, 0.5 to 1.5 is balanced, and above 1.5 is hop-forward. A target difference "
                         "is positive where the water needs more of an ion.",
                    .tool = "water.profile",
                    .fields = {},
                    .readouts = {
                        readout("calcium", "Calcium", "mg/L"),
                        readout("magnesium", "Magnesium", "mg/L"),
                        readout("sodium", "Sodium", "mg/L"),
                        readout("sulfate", "Sulfate", "mg/L"),
                        readout("chloride", "Chloride", "mg/L"),
                        readout("bicarbonate", "Bicarbonate", "mg/L"),
                        readout("residualAlkalinity", "Residual alkalinity", "ppm as CaCO₃"),
                        readout("sulfateToChloride", "Sulfate to chloride", ""),
                        readout("differenceCalcium", "Target less calcium", "mg/L").enabled( "useTarget"),
                        readout("differenceMagnesium", "Target less magnesium", "mg/L").enabled( "useTarget"),
                        readout("differenceSodium", "Target less sodium", "mg/L").enabled( "useTarget"),
                        readout("differenceSulfate", "Target less sulfate", "mg/L").enabled( "useTarget"),
                        readout("differenceChloride", "Target less chloride", "mg/L").enabled( "useTarget"),
                        readout("differenceBicarbonate", "Target less bicarbonate", "mg/L").enabled( "useTarget"),
                    }
                },
            }};

        case ToolKind::infusion:
            return ToolSpec{.title = "Infusion", .sections = {
                ToolSection{
                    .title = "Strike Water",
                    .footer = "Thermal balance with grain at 0.2 heat capacity. Heat lost to the tun is not included.",
                    .tool = "water.infusion",
                    .fields = {
                        number("grainKilograms", "Grain (kg)", "5"),
                        number("grainTemperature", "Grain temperature (°C)", "18"),
                        number("mashTemperature", "Mash temperature (°C)", "66"),
                        number("waterToGrain", "Water to grain (L/kg)", "3"),
                    },
                    .readouts = {
                        readout("strikeTemperature", "Strike water temperature", "°C"),
                        readout("waterLiters", "Strike water volume", "L"),
                        readout("quartsPerPound", "Water to grain", "qt/lb"),
                    }
                },
            }};

        case ToolKind::decoction:
            return ToolSpec{.title = "Decoction Volume", .sections = {
                ToolSection{
                    .title = "Decoction",
                    .footer = "The decoction leaves the mash at its temperature, boils to 100 °C, and goes back in.",
                    .tool = "mash.decoction",
                    .fields = {
                        number("mashLiters", "Mash volume (L)", "20"),
                        number("currentTemperature", "Mash temperature (°C)", "63"),
                        number("targetTemperature", "Target temperature (°C)", "72"),
                    },
                    .readouts = {readout("decoctionLiters", "Decoction volume", "L")}
                },
            }};

        case ToolKind::mashAdjustment:
            return ToolSpec{.title = "Mash Adjustment", .sections = {
                ToolSection{
                    .title = "Temperature by Adding Water",
                    .footer = "Use hot water to raise the mash, or cold water to lower it.",
                    .tool = "mash.temperature",
                    .fields = {
                        number("mashLiters", "Mash volume (L)", "20"),
                        number("mashTemperature", "Mash temperature (°C)", "60"),
                        number("targetTemperature", "Target temperature (°C)", "66"),
                        number("waterTemperature", "Added water temperature (°C)", "75"),
                    },
                    .readouts = {
                        readout("waterLiters", "Water to add", "L"),
                        readout("totalLiters", "Mash volume after", "L"),
                    }
                },
            }};

        case ToolKind::mashPh:
            return ToolSpec{.title = "Mash pH Calculator", .sections = {
                ToolSection{
                    .title = "Grist and Mash Water",
                    .footer = "Calculated mash pH: the grist's acidity and the mash water's residual alkalinity, "
                         "with baking soda's bicarbonate scaled by the pH. Enter the water without baking soda, and give "
                         "the baking soda in grams. Malt categories are base, crystal, roast, and acid. Acid malt is not "
                         "in BeerJSON, so set it here by hand.",
                    .tool = "brew.mash",
                    .fields = {
                        number("grain1Kg", "Grain 1 (kg)", "5"),
                        number("grain1Lovibond", "Grain 1 color (°L)", "2"),
                        choice("grain1Category", "Grain 1 category", maltCategories, "base"),
                        number("grain2Kg", "Grain 2 (kg)", "0"),
                        number("grain2Lovibond", "Grain 2 color (°L)", "0"),
                        choice("grain2Category", "Grain 2 category", maltCategories, "crystal"),
                        number("grain3Kg", "Grain 3 (kg)", "0"),
                        number("grain3Lovibond", "Grain 3 color (°L)", "0"),
                        choice("grain3Category", "Grain 3 category", maltCategories, "roast"),
                        number("grain4Kg", "Grain 4 (kg)", "0"),
                        number("grain4Lovibond", "Grain 4 color (°L)", "0"),
                        choice("grain4Category", "Grain 4 category", maltCategories, "acid"),
                        number("waterLiters", "Mash water (L)", "15"),
                        number("calcium", "Calcium (ppm)", "46.56"),
                        number("magnesium", "Magnesium (ppm)", "0"),
                        number("sodium", "Sodium (ppm, without baking soda)", "0"),
                        number("sulfate", "Sulfate (ppm)", "111.59"),
                        number("chloride", "Chloride (ppm)", "0"),
                        number("bicarbonate", "Bicarbonate (ppm, without baking soda)", "0"),
                        number("bakingSodaGrams", "Baking soda (g)", "1"),
                        number("targetPh", "Target mash pH", "5.4"),
                    },
                    .readouts = {
                        readout("maltAcidityMeq", "Malt acidity", "mEq"),
                        readout("quartsPerPound", "Water to grist", "qt/lb"),
                        readout("residualAlkalinityBefore", "Residual alkalinity", "ppm as CaCO₃"),
                        readout("phBefore", "Mash pH", ""),
                    }
                },
                ToolSection{
                    .title = "Acid",
                    .footer = "The acid that reaches the target pH, and the effect of an acid you add. Acids are "
                         "counted at the pH they reach, so the grams follow the mash's own pH.",
                    .tool = "brew.mash",
                    .fields = {
                        choice("targetAcid", "Acid for the target", mashAcids, "lactic acid"),
                        number("targetAcidStrength", "Target acid strength (%)", "88"),
                        choice("mashAcid", "Acid added", withNone(mashAcids), "none"),
                        number("mashAcidGrams", "Acid added (g)", "0"),
                        number("mashAcidStrength", "Added acid strength (%)", "88"),
                    },
                    .readouts = {
                        readout("acidGramsForTarget", "Target acid to reach the target pH", "g"),
                        readout("phAfter", "Mash pH after the added acid", ""),
                    }
                },
            }};

        case ToolKind::spargeAcid:
            return ToolSpec{.title = "Sparge Acid", .sections = {
                ToolSection{
                    .title = "Sparge Water",
                    .footer = "Carbonate titration. The acid is counted from the sparge water's alkalinity "
                         "and its pH and adds 0.01 mmol/L to every requirement, as it does.",
                    .tool = "brew.sparge",
                    .fields = {
                        number("alkalinity", "Alkalinity (ppm as CaCO₃)", "100"),
                        number("startPh", "Sparge water pH", "7"),
                        number("targetPh", "Target sparge pH", "5.6"),
                        number("liters", "Sparge volume (L)", "12"),
                        choice("acid", "Acid", mashAcids, "phosphoric acid"),
                        number("strength", "Acid strength (%)", "85"),
                    },
                    .readouts = {
                        readout("milliliters", "Acid", "mL"),
                        readout("solutionGrams", "Acid as sold", "g"),
                        readout("pureGrams", "Pure acid", "g"),
                        readout("acidMmolPerLiter", "Acid requirement", "mmol/L"),
                    }
                },
            }};

        case ToolKind::yeastStarter:
            return ToolSpec{.title = "Yeast Starter", .sections = {
                ToolSection{
                    .title = "Batch",
                    .footer = "Pitch rates are about 0.75 million cells per mL per °P for ales and 1.5 for lagers. "
                         "The growth figure is an estimate: measure your own yeast before relying on it.",
                    .tool = "yeast.starter",
                    .fields = {
                        number("batchLiters", "Batch (L)", "20"),
                        number("originalGravity", "Original gravity (SG)", "1.050"),
                        toggle("lager", "Lager yeast", false),
                        number("packCellsBillion", "Cells per pack (billion)", "100"),
                        number("growth", "Growth (billion cells per L per °P)", "15"),
                        number("starterLiters", "Starter size (L)", "2"),
                        number("starterGravity", "Starter gravity (SG)", "1.040"),
                        toggle("twoStage", "Two-stage starter", false),
                        number("firstStageLiters", "First stage (L)", "1"),
                    },
                    .readouts = {
                        readout("cellsNeededBillion", "Cells needed", "billion"),
                        readout("packsWithoutStarter", "Packs without a starter", ""),
                        readout("cellsWithoutStarterBillion", "Cells without a starter", "billion"),
                        readout("oneStageLiters", "One-stage starter", "L"),
                        readout("twoStageFirstLiters", "Two-stage: first starter", "L"),
                        readout("twoStageSecondLiters", "Two-stage: second starter", "L"),
                        readout("dryMaltGrams", "Dry malt for the starter", "g"),
                        readout("dryMaltOunces", "Dry malt for the starter", "oz"),
                        readout("cellsWithStarterBillion", "Cells with the starter", "billion"),
                    }
                },
            }};

        case ToolKind::hydrometer:
            return ToolSpec{.title = "Hydrometer Temperature Adjustment", .sections = {
                ToolSection{
                    .title = "Correction",
                    .footer = "A hydrometer reads low when the sample is warm. The correction uses water's density, "
                         "which is close for wort.",
                    .tool = "gravity.hydrometer",
                    .fields = {
                        number("reading", "Hydrometer reading (SG)", "1.050"),
                        number("sampleTemperature", "Sample temperature (°C)", "30"),
                        number("calibrationTemperature", "Calibration temperature (°C)", "20"),
                    },
                    .readouts = {readout("correctedGravity", "Corrected gravity", "SG")}
                },
            }};

        case ToolKind::refractometer:
            return ToolSpec{.title = "Refractometer to Gravity", .sections = {
                ToolSection{
                    .title = "Wort",
                    .footer = "For wort before fermentation. After fermentation, alcohol changes the reading, and no "
                         "correction is applied yet.",
                    .tool = "gravity.refractometer",
                    .fields = {number("brix", "Refractometer reading (°Brix)", "12")},
                    .readouts = {readout("specificGravity", "Specific gravity", "SG")}
                },
            }};

        case ToolKind::alcohol:
            return ToolSpec{.title = "Gravity to Alcohol", .sections = {
                ToolSection{
                    .title = "Alcohol by Volume",
                    .footer = "The advanced formula is more accurate at higher gravities.",
                    .tool = "alcohol.gravities",
                    .fields = {
                        number("originalGravity", "Original gravity (SG)", "1.050"),
                        number("finalGravity", "Final gravity (SG)", "1.010"),
                    },
                    .readouts = {
                        readout("standardAbv", "Standard formula", "% ABV"),
                        readout("advancedAbv", "Advanced formula", "% ABV"),
                    }
                },
            }};

        case ToolKind::hopBitterness:
            return ToolSpec{.title = "Hop Bitterness", .sections = {
                ToolSection{
                    .title = "Tinseth",
                    .footer = "Uses the post-boil volume. Hop form is not corrected.",
                    .tool = "hop.bitterness",
                    .fields = {
                        number("alphaPercent", "Alpha acid (%)", "10"),
                        number("grams", "Amount (g)", "28"),
                        number("postBoilLiters", "Post-boil volume (L)", "19"),
                        number("boilMinutes", "Boil time (min)", "60"),
                        number("gravity", "Original gravity (SG)", "1.050"),
                    },
                    .readouts = {
                        readout("utilizationPercent", "Utilization", "%"),
                        readout("ibu", "Bitterness", "IBU"),
                    }
                },
            }};

        case ToolKind::hopAge:
            return ToolSpec{.title = "Hop Age", .sections = {
                ToolSection{
                    .title = "Alpha Acid Loss",
                    .footer = "The annual loss is an assumption you set, not a measurement. The loss is assumed to double "
                         "for every 10 °C of storage.",
                    .tool = "hop.age",
                    .fields = {
                        number("alphaPercent", "Alpha acid when new (%)", "10"),
                        number("months", "Age (months)", "6"),
                        number("storageTemperature", "Storage temperature (°C)", "20"),
                        number("annualLossPercent", "Assumed loss per year at 20 °C (%)", "15"),
                    },
                    .readouts = {
                        readout("remainingAlphaPercent", "Alpha acid now", "%"),
                        readout("lostPercent", "Lost", "%"),
                    }
                },
            }};

        case ToolKind::meadNutrient:
            return ToolSpec{.title = "Mead Nutrient", .sections = {
                ToolSection{
                    .title = "Diammonium Phosphate (DAP)",
                    .footer = "DAP is 21 % nitrogen. The YAN target is yours to set.",
                    .tool = "mead.nutrient",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("targetYan", "Target YAN (mg/L)", "250"),
                        number("currentYan", "Current YAN (mg/L)", "0"),
                    },
                    .readouts = {
                        readout("dapGrams", "DAP to add", "g"),
                        readout("dapGramsPerLiter", "DAP per liter", "g/L"),
                    }
                },
            }};

        case ToolKind::backsweetening:
            return ToolSpec{.title = "Backsweetening", .sections = {
                ToolSection{
                    .title = "Sugar",
                    .footer = "Add sugar only once the drink is stable, or the yeast can ferment it. Honey gives about 0.8 "
                         "of sucrose's gravity.",
                    .tool = "sweeten.backsweeten",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("currentGravity", "Current gravity (SG)", "1.000"),
                        number("targetGravity", "Target gravity (SG)", "1.010"),
                        number("sugarYield", "Sugar yield (sucrose = 1)", "1"),
                    },
                    .readouts = {
                        readout("sugarGrams", "Sugar to add", "g"),
                        readout("sugarGramsPerLiter", "Sugar per liter", "g/L"),
                    }
                },
            }};

        case ToolKind::sulfiting:
            return ToolSpec{.title = "Sulfiting", .sections = {
                ToolSection{
                    .title = "Potassium Metabisulfite",
                    .footer = "The result is theoretical. Sulfite binds to the wine, so less free SO₂ remains than added.",
                    .tool = "sweeten.sulfite",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("molecularSo2", "Target molecular SO₂ (mg/L)", "0.8"),
                        number("pH", "pH", "3.4"),
                    },
                    .readouts = {
                        readout("freeSo2Ppm", "Free SO₂ needed", "mg/L"),
                        readout("metabisulfiteGrams", "Potassium metabisulfite", "g"),
                        readout("molecularFractionPercent", "Molecular fraction", "%"),
                    }
                },
            }};

        case ToolKind::boilOff:
            return ToolSpec{.title = "Boil-Off", .sections = {
                ToolSection{
                    .title = "Predict the Post-Boil Volume",
                    .footer = "Assumes the evaporation is a fixed share of the pre-boil volume each hour.",
                    .tool = "boil.volume",
                    .fields = {
                        number("preBoilLiters", "Pre-boil volume (L)", "25"),
                        number("boilMinutes", "Boil time (min)", "60"),
                        number("evaporationPercentPerHour", "Evaporation (% per hour)", "10"),
                    },
                    .readouts = {
                        readout("postBoilLiters", "Post-boil volume", "L"),
                        readout("evaporatedLiters", "Evaporated", "L"),
                    }
                },
                ToolSection{
                    .title = "Measure the Boil-Off Rate",
                    .footer = std::nullopt,
                    .tool = "boil.rate",
                    .fields = {
                        number("preBoilLiters", "Pre-boil volume (L)", "25"),
                        number("postBoilLiters", "Post-boil volume (L)", "22.5"),
                        number("boilMinutes", "Boil time (min)", "60"),
                    },
                    .readouts = {readout("evaporationPercentPerHour", "Evaporation", "% per hour")}
                },
            }};

        case ToolKind::dilution:
            return ToolSpec{.title = "Dilution", .sections = {
                ToolSection{
                    .title = "Water to Add",
                    .footer = "Works for any concentration, such as ABV. Assumes the volumes add exactly, so ethanol "
                         "contraction is ignored.",
                    .tool = "dilute.dilution",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("currentValue", "Current value (for example ABV %)", "8"),
                        number("targetValue", "Target value", "4"),
                    },
                    .readouts = {
                        readout("waterLiters", "Water to add", "L"),
                        readout("finalLiters", "Final volume", "L"),
                    }
                },
            }};

        case ToolKind::gravityAdjustment:
            return ToolSpec{.title = "Gravity Adjustment", .sections = {
                ToolSection{
                    .title = "Sugar or Water",
                    .footer = "Sugar raises the gravity. Water lowers it.",
                    .tool = "gravity.adjust",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("currentGravity", "Current gravity (SG)", "1.040"),
                        number("targetGravity", "Target gravity (SG)", "1.050"),
                        number("sugarYield", "Sugar yield (sucrose = 1)", "1"),
                    },
                    .readouts = {
                        readout("sugarGrams", "Sugar to add", "g"),
                        readout("waterLiters", "Water to add", "L"),
                    }
                },
            }};

        case ToolKind::weightVolume:
            return ToolSpec{.title = "Weight and Volume", .sections = {
                ToolSection{
                    .title = "Volume to Weight",
                    .footer = "Water is 1.0 g/mL.",
                    .tool = "weight.mass",
                    .fields = {
                        number("density", "Density (g/mL)", "1.0"),
                        number("milliliters", "Volume (mL)", "500"),
                    },
                    .readouts = {readout("grams", "Weight", "g")}
                },
                ToolSection{
                    .title = "Weight to Volume",
                    .footer = std::nullopt,
                    .tool = "weight.volume",
                    .fields = {
                        number("density", "Density (g/mL)", "1.0"),
                        number("grams", "Weight (g)", "500"),
                    },
                    .readouts = {readout("milliliters", "Volume", "mL")}
                },
            }};

        case ToolKind::carbonation:
            return ToolSpec{.title = "Carbonation", .sections = {
                ToolSection{
                    .title = "Pressure for Volumes",
                    .footer = "Henry's law for water. Beer differs by a psi or so, so check with a keg gauge.",
                    .tool = "carbonation.pressure",
                    .fields = {
                        number("volumes", "Target volumes of CO₂", "2.5"),
                        number("temperature", "Beer temperature (°C)", "4"),
                    },
                    .readouts = {
                        readout("gaugePsi", "Gauge pressure", "psi"),
                        readout("gaugeKilopascals", "Gauge pressure", "kPa"),
                    }
                },
                ToolSection{
                    .title = "Volumes at a Pressure",
                    .footer = std::nullopt,
                    .tool = "carbonation.volumes",
                    .fields = {
                        number("gaugePsi", "Gauge pressure (psi)", "10"),
                        number("temperature", "Beer temperature (°C)", "4"),
                    },
                    .readouts = {readout("volumes", "Volumes of CO₂", "")}
                },
                ToolSection{
                    .title = "Priming Sugar",
                    .footer = "Assumes all the sugar ferments. Residual volumes are the CO₂ already in the beer at bottling.",
                    .tool = "carbonation.priming",
                    .fields = {
                        number("volumeLiters", "Volume (L)", "19"),
                        number("targetVolumes", "Target volumes", "2.5"),
                        number("residualVolumes", "Residual volumes", "0"),
                    },
                    .readouts = {
                        readout("dextroseGrams", "Dextrose", "g"),
                        readout("sucroseGrams", "Sucrose", "g"),
                    }
                },
            }};

        case ToolKind::temperature:
            return converter("Temperature Converter",  "convert.temperature",
                       {{"C", "Celsius"}, {"F", "Fahrenheit"}},  {"C", "F"});

        case ToolKind::gravityUnits:
            return converter("Gravity Unit Converter",  "convert.gravity",
                       {{"sg", "Specific gravity"}, {"plato", "Degrees Plato"}, {"brix", "Degrees Brix"},
                              {"points", "Gravity points"}},
                       {"sg", "plato"});

        case ToolKind::pressure:
            return converter("Pressure Unit Converter",  "convert.pressure",
                       {{"psi", "psi"}, {"kPa", "Kilopascals"}, {"bar", "Bar"}, {"atm", "Atmospheres"}},
                       {"psi", "kPa"});

        case ToolKind::weight:
            return converter("Weight Unit Converter",  "convert.weight",
                       {{"mg", "Milligrams"}, {"g", "Grams"}, {"kg", "Kilograms"}, {"oz", "Ounces"}, {"lb", "Pounds"}},
                       {"kg", "lb"});

        case ToolKind::volume:
            return converter("Volume Unit Converter",  "convert.volume",
                       {{"ml", "Milliliters"}, {"l", "Liters"}, {"tsp", "US teaspoons"}, {"tbsp", "US tablespoons"},
                              {"floz", "US fluid ounces"}, {"cup", "US cups"}, {"pt", "US pints"}, {"qt", "US quarts"},
                              {"gal", "US gallons"}, {"bbl", "US barrels"}, {"ifloz", "Imperial fluid ounces"},
                              {"ipt", "Imperial pints"}, {"iqt", "Imperial quarts"}, {"igal", "Imperial gallons"},
                              {"ibbl", "Imperial barrels"}},
                       {"l", "gal"});
    }
    return {};
}

std::string toolValue(const ToolSpec& spec, const std::map<std::string, std::string>& values,
                      const std::string& key) {
    const auto typed = values.find(key);
    if (typed != values.end()) {
        return typed->second;
    }
    for (const ToolSection& section : spec.sections) {
        for (const ToolField& field : section.fields) {
            if (field.key == key) {
                return field.defaultValue;
            }
        }
    }
    return "";
}

bool toolShows(const ToolSpec& spec, const std::map<std::string, std::string>& values,
               const std::string& enabledBy) {
    return enabledBy.empty() || toolValue(spec, values, enabledBy) == "1";
}

std::map<std::string, std::string> toolInputs(const ToolSpec& spec, const ToolSection& section,
                                              const std::map<std::string, std::string>& values) {
    std::map<std::string, std::string> inputs;
    for (const ToolSection& other : spec.sections) {
        if (other.tool != section.tool) {
            continue;
        }
        for (const ToolField& field : other.fields) {
            if (toolShows(spec, values, field.enabledBy)) {
                inputs[field.key] = toolValue(spec, values, field.key);
            }
        }
    }
    return inputs;
}

}  // namespace kettlelogic::catalog
