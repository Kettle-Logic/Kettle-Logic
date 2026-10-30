#!/usr/bin/env python3
"""Generates core/include/kettlelogic/model/schema.hpp from the vendored BeerJSON schema.

Usage:
    gen_schema.py          write the header
    gen_schema.py --check  exit 1 if the checked-in header is out of date

Two kinds of entry are generated:

- ENUMS: the allowed string values of an enum, for validating typed fields.
- OBJECTS: the property names a schema object defines, and whether the schema
  is closed (forbids properties it does not list). The reader uses these to
  tell a key the schema defines but the model does not type yet (kept inline)
  from a key the schema does not define at all (kept out of the saved file).

Every entry names a schema location by file and JSON path. The generator fails
if a path is missing or does not have the expected shape, so a schema update
that moves something shows up as an error rather than a silently stale table.
Standard library only.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCHEMA_DIR = ROOT / "schemas" / "beerjson"
OUTPUT = ROOT / "include" / "kettlelogic" / "model" / "schema.hpp"

# (C++ constant name, schema file, path to the node that has "enum")
ENUMS = [
    ("kRecipeTypes", "recipe.json", ["$defs", "RecipeType", "properties", "type"]),
    ("kFermentableTypes", "fermentable.json", ["$defs", "FermentableBase", "properties", "type"]),
    ("kGrainGroups", "fermentable.json", ["$defs", "FermentableBase", "properties", "grain_group"]),
    ("kHopForms", "hop.json", ["$defs", "HopVarietyBase", "properties", "form"]),
    ("kCultureTypes", "culture.json", ["$defs", "CultureBase", "properties", "type"]),
    ("kCultureForms", "culture.json", ["$defs", "CultureBase", "properties", "form"]),
    ("kMiscellaneousTypes", "misc.json", ["$defs", "MiscellaneousBase", "properties", "type"]),
    ("kMashStepTypes", "mash_step.json", ["$defs", "MashStepType", "properties", "type"]),
    ("kChillingTypes", "boil_step.json", ["$defs", "BoilStepType", "properties", "chilling_type"]),
    ("kTimingUses", "timing.json", ["$defs", "UseType"]),
    # Convertible units. Each must map to a Unit in quantity.cpp (tested).
    ("kMassUnits", "measureable_units.json", ["$defs", "MassUnitType"]),
    ("kVolumeUnits", "measureable_units.json", ["$defs", "VolumeUnitType"]),
    ("kTemperatureUnits", "measureable_units.json", ["$defs", "TemperatureUnitType"]),
    ("kTimeUnits", "measureable_units.json", ["$defs", "TimeUnitType"]),
    ("kPercentUnits", "measureable_units.json", ["$defs", "PercentUnitType"]),
    ("kConcentrationUnits", "measureable_units.json", ["$defs", "ConcentrationUnitType"]),
    ("kCountUnits", "measureable_units.json", ["$defs", "UnitUnitType"]),
    # Non-convertible units, stored as Measure.
    ("kGravityUnits", "measureable_units.json", ["$defs", "GravityUnitType"]),
    ("kColorUnits", "measureable_units.json", ["$defs", "ColorUnitType"]),
    ("kAcidityUnits", "measureable_units.json", ["$defs", "AcidityUnitType"]),
    ("kPressureUnits", "measureable_units.json", ["$defs", "PressureUnitType"]),
    ("kSpecificVolumeUnits", "measureable_units.json", ["$defs", "SpecificVolumeUnitType"]),
    ("kBitternessUnits", "measureable_units.json", ["$defs", "BitternessUnitType"]),
]

# (C++ constant name, schema file, path to the object schema)
OBJECTS = [
    ("kBeerJson", "beer.json", ["properties", "beerjson"]),
    ("kRecipe", "recipe.json", ["$defs", "RecipeType"]),
    ("kEfficiency", "recipe.json", ["$defs", "EfficiencyType"]),
    ("kIngredients", "recipe.json", ["$defs", "IngredientsType"]),
    ("kYield", "fermentable.json", ["$defs", "YieldType"]),
    ("kFermentableAddition", "fermentable.json", ["$defs", "FermentableAdditionType"]),
    ("kHopAddition", "hop.json", ["$defs", "HopAdditionType"]),
    ("kMiscellaneousAddition", "misc.json", ["$defs", "MiscellaneousAdditionType"]),
    ("kCultureAddition", "culture.json", ["$defs", "CultureAdditionType"]),
    ("kWaterAddition", "water.json", ["$defs", "WaterAdditionType"]),
    ("kTiming", "timing.json", ["$defs", "TimingType"]),
    ("kMashProcedure", "mash.json", ["$defs", "MashProcedureType"]),
    ("kMashStep", "mash_step.json", ["$defs", "MashStepType"]),
    ("kFermentationProcedure", "fermentation.json", ["$defs", "FermentationProcedureType"]),
    ("kFermentationStep", "fermentation_step.json", ["$defs", "FermentationStepType"]),
    ("kBoilProcedure", "boil.json", ["$defs", "BoilProcedureType"]),
    ("kBoilStep", "boil_step.json", ["$defs", "BoilStepType"]),
    # Quantity and measure objects. Every quantity type has the same {value, unit} shape.
    ("kQuantity", "measureable_units.json", ["$defs", "VolumeType"]),
    ("kMeasure", "measureable_units.json", ["$defs", "GravityType"]),
]


def load(file_name):
    return json.loads((SCHEMA_DIR / file_name).read_text())


def walk(file_name, path):
    node = load(file_name)
    for key in path:
        if not isinstance(node, dict) or key not in node:
            sys.exit(f"gen_schema: {file_name} has no node {'/'.join(path)}")
        node = node[key]
    return node


def read_enum(file_name, path):
    node = walk(file_name, path)
    values = node.get("enum") if isinstance(node, dict) else node
    if not (isinstance(values, list) and values and all(isinstance(v, str) for v in values)):
        sys.exit(f"gen_schema: {file_name}:{'/'.join(path)} is not a list of strings")
    return values


def resolve_ref(current_file, ref):
    file_part, _, pointer = ref.partition("#")
    file_name = file_part or current_file
    node = load(file_name)
    for part in [p for p in pointer.split("/") if p]:
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            sys.exit(f"gen_schema: unresolved $ref {ref} in {current_file}")
        node = node[part]
    return file_name, node


def property_names(file_name, node, seen_refs):
    """Property names a schema node defines, following $ref and allOf.

    oneOf and anyOf are not followed: none of the typed objects define
    properties there.
    """
    names = set()
    if "$ref" in node:
        ref = node["$ref"]
        if ref not in seen_refs:
            target_file, target = resolve_ref(file_name, ref)
            names |= property_names(target_file, target, seen_refs | {ref})
    for part in node.get("allOf", []):
        names |= property_names(file_name, part, seen_refs)
    names |= set(node.get("properties", {}))
    return names


def is_closed(node):
    """True if the schema forbids properties it does not list."""
    return node.get("additionalProperties") is False or node.get("unevaluatedProperties") is False


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def pinned_commit():
    readme = (SCHEMA_DIR / "README.md").read_text()
    match = re.search(r"Pinned commit: `([0-9a-f]{40})`", readme)
    if not match:
        sys.exit("gen_schema: no pinned commit in schemas/beerjson/README.md")
    return match.group(1)


def render_array(name, values):
    items = ", ".join(cpp_string(v) for v in values)
    return f"inline constexpr std::array<std::string_view, {len(values)}> {name} = {{{items}}};"


def render():
    lines = [
        "// Generated by core/tools/gen_schema.py from core/schemas/beerjson/",
        f"// (upstream commit {pinned_commit()}). Do not edit by hand; regenerate instead.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace kettlelogic::schema {",
        "",
        "// The properties a schema object defines. `closed` means the schema forbids",
        "// any property not in `properties`.",
        "struct ObjectSchema {",
        "    std::string_view name;",
        "    bool closed;",
        "    std::span<const std::string_view> properties;",
        "};",
        "",
        "// Enums.",
    ]
    for name, file_name, path in ENUMS:
        lines.append(render_array(name, read_enum(file_name, path)))

    lines += ["", "// Objects."]
    for name, file_name, path in OBJECTS:
        node = walk(file_name, path)
        props = sorted(property_names(file_name, node, frozenset()))
        if not props:
            sys.exit(f"gen_schema: {file_name}:{'/'.join(path)} defines no properties")
        closed = "true" if is_closed(node) else "false"
        lines.append(render_array(f"{name}Properties", props))
        lines.append(f'inline constexpr ObjectSchema {name} = {{"{name[1:]}", {closed}, {name}Properties}};')

    lines += ["", "}  // namespace kettlelogic::schema", ""]
    return "\n".join(lines)


def main(argv):
    text = render()
    if "--check" in argv:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != text:
            print(f"{OUTPUT} is out of date. Run core/tools/gen_schema.py.")
            return 1
        print(f"{OUTPUT} is up to date.")
        return 0
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
