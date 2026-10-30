#pragma once

#include "kettlelogic/calc/brew_plan.hpp"
#include "kettlelogic/calc/journal.hpp"

#include <optional>
#include <string>
#include <vector>

// The planning data a recipe keeps: its yeast, water, mash, and brew sessions. It is stored with the recipe
// as JSON text (model::Recipe::planning), so it survives a reopen. Each tab reads and writes only its own
// part. The JSON is the same as the macOS app writes, so a recipe opens in either app: object keys match,
// a UUID is its uppercase text, and a date is the number of seconds since 1 January 2001 UTC.
namespace kettlelogic::app {

// A new random identifier, as uppercase text in the 8-4-4-4-12 form.
std::string newUuidText();

// A moment, as seconds since 1 January 2001 00:00 UTC, which is how the macOS app stores a date.
double nowReferenceSeconds();

// A calendar day in the user's time zone.
struct CivilDate {
    int year = 1970;
    int month = 1;
    int day = 1;
};
CivilDate civilDateOf(double referenceSeconds);
// Midday on the day, so it falls on the same day in any time zone.
double referenceSecondsAtMidday(const CivilDate& date);
// The whole days from brew day to the reading's day.
int daysBetween(double fromReferenceSeconds, double toReferenceSeconds);

struct StarterSettings {
    bool lager = false;
    std::string packCells = "100";
    std::string growth = "15";
    std::string starterLiters = "2";
    std::string starterGravity = "1.040";
    bool twoStage = false;
    std::string firstStageLiters = "1";
    bool addToBottling = false;

    friend bool operator==(const StarterSettings&, const StarterSettings&) = default;
};

// One mineral reading, in mg/L.
struct IonsInput {
    double calcium = 0;
    double magnesium = 0;
    double sodium = 0;
    double sulfate = 0;
    double chloride = 0;
    double bicarbonate = 0;

    friend bool operator==(const IonsInput&, const IonsInput&) = default;
};

// A water salt the user adds, and whether it goes in the sparge water.
struct SaltInput {
    std::string id = newUuidText();
    std::string kind = "gypsum";
    double grams = 1;
    bool sparge = false;

    friend bool operator==(const SaltInput&, const SaltInput&) = default;
};

// An acid the user adds to the mash, in grams of the acid as sold.
struct AcidInput {
    std::string id = newUuidText();
    std::string kind = "lactic acid";
    double grams = 1;

    friend bool operator==(const AcidInput&, const AcidInput&) = default;
};

struct WaterSettings {
    std::string baseName = "Distilled or reverse osmosis";
    IonsInput base;
    std::vector<SaltInput> salts;
    bool leaveOutSalts = false;
    bool holdSpargeSaltsUntilBoil = false;
    double roPercent = 0;  // the share of reverse osmosis water mixed into the base water
    // Whether the desired water is compared with the mash water, and the desired water itself.
    bool useTarget = false;
    IonsInput target{140, 18, 25, 300, 55, 110};

    friend bool operator==(const WaterSettings&, const WaterSettings&) = default;
};

// One step of the mash: its kind, temperature, time, and any addition made during it.
struct MashStepSetting {
    std::string id = newUuidText();
    std::string name;
    std::string kind;
    double tempC = 67;
    double minutes = 60;
    std::string addition;

    static const std::vector<std::string>& kinds();  // from the engine: Infusion, Temperature, Decoction

    friend bool operator==(const MashStepSetting&, const MashStepSetting&) = default;
};

// The mash and sparge the user sets up. The volumes and temperatures are the planning inputs.
struct MashSettings {
    std::vector<MashStepSetting> steps;
    std::string spargeType = "Batch";
    double spargeTempC = 76;
    double grainTempC = 20;
    double tunTempC = 20;
    double decoctionTempC = 100;
    double mashRatioLPerKg = 3;
    double absorptionLPerKg = 1;
    double deadspaceLiters = 0.5;
    bool adjustForDeadspace = true;
    std::vector<AcidInput> acids;
    std::string acidForTarget = "lactic acid";
    double targetMashPh = 5.4;  // the mash pH the acid works toward
    std::string spargeAcid = "phosphoric acid";
    double spargeAcidStrength = 85;
    double spargeStartPh = 7;
    double spargeTargetPh = 5.6;

    MashSettings();

    friend bool operator==(const MashSettings&, const MashSettings&) = default;
};

// A step of a fermentation temperature profile: how many days, at what temperature.
struct ProfileStep {
    std::string id = newUuidText();
    double days = 1;
    double tempC = 20;

    friend bool operator==(const ProfileStep&, const ProfileStep&) = default;
};

// A fermentation temperature profile the user can pick, with its steps in order.
struct FermentationPreset {
    std::string name;
    std::vector<ProfileStep> steps;

    static const std::vector<FermentationPreset>& all();
};

// One fermentation reading: when it was taken, and the temperature and gravity.
struct FermentationReading {
    std::string id = newUuidText();
    double date = nowReferenceSeconds();
    std::optional<double> temperatureC;
    std::optional<double> gravity;

    friend bool operator==(const FermentationReading&, const FermentationReading&) = default;
};

// One brew session of a recipe: the values measured on the day. Every measured value is optional, so a
// session is filled in as the brew goes.
struct BrewSession {
    std::string id = newUuidText();
    std::string name = "Session 1";
    double brewDate = nowReferenceSeconds();

    std::optional<double> mashPh;
    std::optional<double> spargeRunoffPh;
    std::optional<double> endOfRunningsGravity;

    std::optional<double> postMashGravity;
    std::optional<double> preBoilLiters;
    std::optional<double> preBoilGravity;
    std::optional<double> batchLiters;
    std::optional<double> originalGravity;
    std::optional<double> finalGravity;
    std::optional<double> bottlingLiters;

    std::string carbonationMethod = carbonationMethods()[0];
    std::optional<double> carbonationVolumes;
    std::optional<double> servingTempC = 4;  // while force carbonating, in °C

    std::optional<std::string> profileName;
    std::vector<ProfileStep> profileSteps;
    std::vector<FermentationReading> readings;

    static const std::vector<std::string>& carbonationMethods();

    friend bool operator==(const BrewSession&, const BrewSession&) = default;
};

// Everything the planning tabs keep for one recipe.
struct RecipePlanning {
    StarterSettings starter;
    WaterSettings water;
    MashSettings mash;
    std::vector<BrewSession> sessions;
    // The brewer's own notes for the recipe. A recipe built from a style starts with its tips.
    std::string notes;

    // The planning stored in a recipe's text. Text that does not read back gives the defaults.
    static RecipePlanning read(const std::string& text);
    // The planning as the JSON text a recipe stores.
    std::string text() const;
};

// The inputs the engine's brewing plan reads, from a recipe's water and mash settings.
calc::BrewPlanInputs brewingInputs(const WaterSettings& water, const MashSettings& mash);

// The measured values a session sends to the engine's journal calculations.
calc::JournalInputs journalInputs(const BrewSession& session);

}  // namespace kettlelogic::app
