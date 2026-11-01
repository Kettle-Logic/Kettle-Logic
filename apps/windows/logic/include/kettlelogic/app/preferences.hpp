#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace kettlelogic::app {

// The unit system for measured values the user types or reads.
enum class UnitSystem { metric, us };

// The keys and defaults of the app's settings (the macOS app's `Preferences`). Settings are kept as JSON in
// the app's data folder, and read with the default whenever a key has no value.
namespace PrefKey {
inline constexpr const char* autosave = "autosave";
inline constexpr const char* confirmDelete = "confirmDelete";
inline constexpr const char* units = "units";
inline constexpr const char* defaultEquipment = "defaultEquipment";
// The folder new recipes are saved in, as a path. Empty means the default folder.
inline constexpr const char* recipeFolder = "recipeFolder";
// Whether the brew-day window keeps the display awake while it is open.
inline constexpr const char* keepDisplayOn = "keepDisplayOn";
// Whether the recipe view shows only what a new brewer needs. Advanced tabs and fields stay reachable by
// turning this off, and a tab that already holds data is never hidden.
inline constexpr const char* simpleMode = "simpleMode";
// Whether the user has been asked which interface they want. Unset on a first launch, which shows the
// choice dialog. The answer is stored in `simpleMode`, and can be changed in Settings.
inline constexpr const char* interfaceChosen = "interfaceChosen";
// The color theme: "system" follows the operating system, "light" and "dark" override it.
inline constexpr const char* appearance = "appearance";
// Whether the recipe's tabs are grouped under Recipe, Ingredients, Process, and Results.
inline constexpr const char* groupedTabs = "groupedTabs";
// Whether the app checks for a newer version when it starts. A check the user asks for always runs.
inline constexpr const char* checkForUpdates = "checkForUpdates";
// How much anonymous usage and error data the app sends: "off", "limited", or "enhanced" (see analytics.hpp).
inline constexpr const char* telemetryLevel = "telemetryLevel";
// Whether the user has been told about telemetry, by the first-launch choice dialog or the notice shown after an
// upgrade. Unset shows the notice once.
inline constexpr const char* telemetryNoticeShown = "telemetryNoticeShown";
// The random id that groups one install's events. It is made on first use, and Settings can replace it.
inline constexpr const char* analyticsClientId = "analyticsClientId";
}  // namespace PrefKey

class Preferences {
public:
    // `file` is where the settings are kept. An empty path keeps them in memory only.
    explicit Preferences(std::filesystem::path file = {});

    bool boolean(const std::string& key) const;
    std::string string(const std::string& key) const;
    void setBoolean(const std::string& key, bool value);
    void setString(const std::string& key, const std::string& value);

    UnitSystem units() const;
    std::string defaultEquipmentName() const;

private:
    void load();
    void write() const;

    std::map<std::string, std::string> values_;
    std::filesystem::path file_;
};

// The default for a key, as text: "true" and "false" for a flag.
std::string defaultPreference(const std::string& key);

// Metric or US customary, and every conversion the forms use between them.
struct Units {
    UnitSystem system = UnitSystem::metric;

    static constexpr double litersPerGallon = 3.785411784;
    static constexpr double quartsPerLiter = 1.056688;
    static constexpr double poundsPerKilogram = 2.204623;

    bool metric() const { return system == UnitSystem::metric; }
    std::string title() const { return metric() ? "Metric" : "US customary"; }

    // The engine's unit for a volume, such as the batch size.
    std::string volumeUnit() const { return metric() ? "l" : "gal"; }
    // The mass unit for a grain or other bulk fermentable.
    std::string bulkMassUnit() const { return metric() ? "kg" : "lb"; }
    // The mass unit for hops and other small weights.
    std::string smallMassUnit() const { return metric() ? "g" : "oz"; }

    // A volume in liters, converted to this system's volume unit for display.
    double displayVolume(double liters) const { return metric() ? liters : liters / litersPerGallon; }
    // The symbol shown beside a volume the user types or reads.
    std::string volumeSymbol() const { return metric() ? "L" : "gal"; }
    // A volume ratio, per unit of grain, as the mash views show it.
    std::string grainRatioSymbol() const { return metric() ? "L/kg" : "qt/lb"; }
    // How many display volume units one liter is: 1 for metric, gallons per liter for US.
    double volumeFactor() const { return metric() ? 1 : 1 / litersPerGallon; }
    // How many display ratio units one L/kg is: 1 for metric, quarts per pound for US.
    double grainRatioFactor() const { return metric() ? 1 : quartsPerLiter / poundsPerKilogram; }

    std::string temperatureSymbol() const { return metric() ? "°C" : "°F"; }
    double displayTemperature(double celsius) const { return metric() ? celsius : celsius * 9 / 5 + 32; }
    double celsius(double display) const { return metric() ? display : (display - 32) * 5 / 9; }
    // A temperature in °C as text in this system's unit, such as "66 °C" or "151 °F".
    std::string temperatureText(double celsius) const;

    // The unit a yeast starter is sized in: liters, or quarts in the US system.
    std::string starterSymbol() const { return metric() ? "L" : "qt"; }
    double starterFactor() const { return metric() ? 1 : quartsPerLiter; }

    // A starter volume typed in this system's unit, as liters text. Metric text passes through unchanged.
    std::string litersTextFromStarter(const std::string& text) const;
    // A starter volume in liters text, as this system's starter unit to two places.
    std::string starterTextFromLiters(const std::string& text) const;
    // A volume typed in this system's unit, as liters text for the engine. Text that is not a number is
    // returned as typed.
    std::string litersTextFromVolume(const std::string& text) const;
    // A volume in liters, as text in this system's volume unit, to two decimal places.
    std::string volumeTextFromLiters(const std::string& text) const;
};

}  // namespace kettlelogic::app
