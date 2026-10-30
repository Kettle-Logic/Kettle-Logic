#pragma once

#include <map>
#include <string>
#include <vector>

// The brewing tools that work outside a recipe: a water profile, an infusion,
// a yeast starter, the gravity and alcohol conversions, and the unit converters.
//
// Each tool is a pure function of its inputs. An input is decimal text, as the
// user typed it, and is read exactly before any floating point arithmetic. A unit
// name, such as "F" or "psi", is text too. Results are numbers for display.
//
// Tools are named "group.name", such as "water.profile" or "convert.volume". The
// names, their inputs, and their results are listed in docs/calc.md, "Tools".
//
// Run throws std::invalid_argument for an unknown tool, a missing or malformed
// input, or an input outside the range the formula can use.
namespace kettlelogic::calc::tools {

// Input name to its text.
using Inputs = std::map<std::string, std::string>;

struct Output {
    std::map<std::string, double> results;  // result name to value
    std::vector<std::string> notes;         // assumptions and limits that apply to the result
};

Output run(const std::string& tool, const Inputs& inputs);

}  // namespace kettlelogic::calc::tools
