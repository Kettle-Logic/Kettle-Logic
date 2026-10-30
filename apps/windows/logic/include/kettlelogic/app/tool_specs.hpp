#pragma once

#include "kettlelogic/catalog/tool_specs.hpp"

// The tool and converter form descriptions live in the engine (`catalog/tool_specs.hpp`), so every front
// end shows the same tools.
namespace kettlelogic::app {

using catalog::calculators;
using catalog::converters;
using catalog::toolInputs;
using catalog::ToolField;
using catalog::ToolKind;
using catalog::toolKindFromName;
using catalog::toolKindName;
using catalog::ToolReadout;
using catalog::ToolSection;
using catalog::toolShows;
using catalog::ToolSpec;
using catalog::toolSpec;
using catalog::toolValue;

}  // namespace kettlelogic::app
