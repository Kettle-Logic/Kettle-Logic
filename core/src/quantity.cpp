#include "kettlelogic/quantity.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace kettlelogic {

namespace {

// Exact definitions, matching docs/units.md.
Rational poundScale() { return Rational::fromDecimal("0.45359237"); }        // kg
Rational usFluidOunce() { return Rational::fromDecimal("0.0295735295625"); }  // L
Rational usGallon() { return Rational(128) * usFluidOunce(); }                // L
Rational imperialGallon() { return Rational::fromDecimal("4.54609"); }        // L
Rational imperialFluidOunce() { return imperialGallon() / Rational(160); }    // L

struct UnitDef {
    Unit unit;
    Dimension dimension;
    std::string_view name;  // BeerJSON enum value
    Rational scale;         // canonical = value * scale + offset
    Rational offset;
};

const std::vector<UnitDef>& unitTable() {
    static const std::vector<UnitDef> table = {
        {Unit::Milligram, Dimension::Mass, "mg", Rational(1, 1000000), {}},
        {Unit::Gram, Dimension::Mass, "g", Rational(1, 1000), {}},
        {Unit::Kilogram, Dimension::Mass, "kg", Rational(1), {}},
        {Unit::Pound, Dimension::Mass, "lb", poundScale(), {}},
        {Unit::Ounce, Dimension::Mass, "oz", poundScale() / Rational(16), {}},

        {Unit::Milliliter, Dimension::Volume, "ml", Rational(1, 1000), {}},
        {Unit::Liter, Dimension::Volume, "l", Rational(1), {}},
        {Unit::Teaspoon, Dimension::Volume, "tsp", usFluidOunce() / Rational(6), {}},
        {Unit::Tablespoon, Dimension::Volume, "tbsp", usFluidOunce() / Rational(2), {}},
        {Unit::FluidOunce, Dimension::Volume, "floz", usFluidOunce(), {}},
        {Unit::Cup, Dimension::Volume, "cup", usFluidOunce() * Rational(8), {}},
        {Unit::Pint, Dimension::Volume, "pt", usFluidOunce() * Rational(16), {}},
        {Unit::Quart, Dimension::Volume, "qt", usFluidOunce() * Rational(32), {}},
        {Unit::Gallon, Dimension::Volume, "gal", usGallon(), {}},
        {Unit::Barrel, Dimension::Volume, "bbl", usGallon() * Rational(31), {}},
        {Unit::ImperialFluidOunce, Dimension::Volume, "ifloz", imperialFluidOunce(), {}},
        {Unit::ImperialPint, Dimension::Volume, "ipt", imperialFluidOunce() * Rational(20), {}},
        {Unit::ImperialQuart, Dimension::Volume, "iqt", imperialFluidOunce() * Rational(40), {}},
        {Unit::ImperialGallon, Dimension::Volume, "igal", imperialGallon(), {}},
        {Unit::ImperialBarrel, Dimension::Volume, "ibbl", imperialGallon() * Rational(36), {}},

        {Unit::Celsius, Dimension::Temperature, "C", Rational(1), {}},
        // C = F * 5/9 - 160/9
        {Unit::Fahrenheit, Dimension::Temperature, "F", Rational(5, 9), Rational(-160, 9)},

        {Unit::Second, Dimension::Time, "sec", Rational(1), {}},
        {Unit::Minute, Dimension::Time, "min", Rational(60), {}},
        {Unit::Hour, Dimension::Time, "hr", Rational(3600), {}},
        {Unit::Day, Dimension::Time, "day", Rational(86400), {}},
        {Unit::Week, Dimension::Time, "week", Rational(604800), {}},

        {Unit::Percent, Dimension::Fraction, "%", Rational(1, 100), {}},

        // In dilute aqueous solution, 1 ppm is 1 mg/L and 1 ppb is 1 µg/L.
        {Unit::PartsPerMillion, Dimension::Concentration, "ppm", Rational(1), {}},
        {Unit::PartsPerBillion, Dimension::Concentration, "ppb", Rational(1, 1000), {}},
        {Unit::MilligramPerLiter, Dimension::Concentration, "mg/l", Rational(1), {}},

        {Unit::CountOne, Dimension::Count, "1", Rational(1), {}},
        {Unit::CountUnit, Dimension::Count, "unit", Rational(1), {}},
        {Unit::CountEach, Dimension::Count, "each", Rational(1), {}},
        {Unit::CountDimensionless, Dimension::Count, "dimensionless", Rational(1), {}},
        {Unit::CountPackage, Dimension::Count, "pkg", Rational(1), {}},
    };
    return table;
}

const UnitDef& defOf(Unit unit) {
    const auto& table = unitTable();
    const auto it = std::find_if(table.begin(), table.end(),
                                 [unit](const UnitDef& def) { return def.unit == unit; });
    return *it;  // every Unit has a row; the table test enforces it
}

}  // namespace

std::optional<Unit> unitFromBeerJson(std::string_view name) {
    for (const auto& def : unitTable()) {
        if (def.name == name) {
            return def.unit;
        }
    }
    return std::nullopt;
}

std::string_view beerJsonName(Unit unit) {
    return defOf(unit).name;
}

Dimension dimensionOf(Unit unit) {
    return defOf(unit).dimension;
}

Quantity Quantity::fromUnit(const Rational& value, Unit unit) {
    const UnitDef& def = defOf(unit);
    return Quantity{def.dimension, value * def.scale + def.offset, unit};
}

Rational Quantity::valueIn(Unit unit) const {
    const UnitDef& def = defOf(unit);
    if (def.dimension != dimension_) {
        throw std::invalid_argument("unit dimension does not match quantity");
    }
    return (canonical_ - defOf(unit).offset) / defOf(unit).scale;
}

Quantity operator+(const Quantity& a, const Quantity& b) {
    if (a.dimension_ != b.dimension_) {
        throw std::invalid_argument("cannot add quantities of different dimensions");
    }
    return Quantity{a.dimension_, a.canonical_ + b.canonical_, a.unit_};
}

Quantity operator-(const Quantity& a, const Quantity& b) {
    if (a.dimension_ != b.dimension_) {
        throw std::invalid_argument("cannot subtract quantities of different dimensions");
    }
    return Quantity{a.dimension_, a.canonical_ - b.canonical_, a.unit_};
}

}  // namespace kettlelogic
