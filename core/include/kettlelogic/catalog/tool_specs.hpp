#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The brewing tools and unit converters in the Tools and Unit Tools menus. Each one is a small
// description of its inputs and results. The engine does the arithmetic (calc/tools.hpp), and the front
// end's tool window shows the form.
namespace kettlelogic::catalog {

// One input on a tool's form.
struct ToolField {
    enum class Kind { number, toggle, choice };

    struct Choice {
        std::string value;
        std::string label;
    };

    std::string key;
    std::string label;
    Kind kind = Kind::number;
    std::string defaultValue;
    std::vector<Choice> choices;  // for a choice
    // The toggle that must be on for this field to show and to count as an input. Empty: always shown.
    std::string enabledBy;

    // The field shows only while the toggle with `toggleKey` is on.
    ToolField enabled(std::string toggleKey) const {
        ToolField copy = *this;
        copy.enabledBy = std::move(toggleKey);
        return copy;
    }
};

// One result on a tool's form, with the unit it is shown in.
struct ToolReadout {
    std::string key;
    std::string label;
    std::string unit;
    std::string enabledBy;

    ToolReadout enabled(std::string toggleKey) const {
        ToolReadout copy = *this;
        copy.enabledBy = std::move(toggleKey);
        return copy;
    }
};

// A group of inputs that runs one engine tool, with its results and a note under them.
struct ToolSection {
    std::string title;
    std::optional<std::string> footer;
    std::string tool;
    std::vector<ToolField> fields;
    std::vector<ToolReadout> readouts;
};

// A tool window: its sections, in order.
struct ToolSpec {
    std::string title;
    std::vector<ToolSection> sections;
};

// The windows that the Tools and Unit Tools menus open. The names are stable.
enum class ToolKind {
    // Tools
    waterProfile, infusion, decoction, mashAdjustment, mashPh, spargeAcid, yeastStarter, hydrometer,
    refractometer, alcohol, hopBitterness, hopAge, meadNutrient, backsweetening, sulfiting, boilOff, dilution,
    gravityAdjustment, weightVolume, carbonation,
    // Unit tools
    temperature, gravityUnits, pressure, weight, volume,
};

const std::vector<ToolKind>& calculators();
const std::vector<ToolKind>& converters();

// The stable name of a tool, such as "waterProfile".
std::string toolKindName(ToolKind kind);
std::optional<ToolKind> toolKindFromName(const std::string& name);

ToolSpec toolSpec(ToolKind kind);

// The text now in a field, by its key: what the user typed, or the field's default when it has not been changed.
std::string toolValue(const ToolSpec& spec, const std::map<std::string, std::string>& values, const std::string& key);

// Whether a field or readout shows: the toggle it names must be on. One with no toggle always shows.
bool toolShows(const ToolSpec& spec, const std::map<std::string, std::string>& values, const std::string& enabledBy);

// The inputs a section's tool runs on: the shown fields of every section of that tool, so the inputs can sit in
// several groups, one per step, and every section's readouts follow all of them.
std::map<std::string, std::string> toolInputs(const ToolSpec& spec, const ToolSection& section,
                                              const std::map<std::string, std::string>& values);

}  // namespace kettlelogic::catalog
