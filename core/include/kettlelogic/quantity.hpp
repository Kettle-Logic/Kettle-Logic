#pragma once

#include "kettlelogic/rational.hpp"

#include <optional>
#include <string_view>

namespace kettlelogic {

// Dimensions whose units convert exactly to one canonical unit.
// Gravity, color, pH, pressure, and specific volume are not linear and are
// handled by Measure (measure.hpp) instead.
enum class Dimension { Mass, Volume, Temperature, Time, Fraction, Concentration, Count };

// Every convertible unit BeerJSON 1.0 allows.
enum class Unit {
    // Mass (canonical: kg)
    Milligram,
    Gram,
    Kilogram,
    Pound,
    Ounce,
    // Volume (canonical: L)
    Milliliter,
    Liter,
    Teaspoon,
    Tablespoon,
    FluidOunce,
    Cup,
    Pint,
    Quart,
    Gallon,
    Barrel,  // US beer barrel, 31 US gallons
    ImperialFluidOunce,
    ImperialPint,
    ImperialQuart,
    ImperialGallon,
    ImperialBarrel,
    // Temperature (canonical: °C)
    Celsius,
    Fahrenheit,
    // Time (canonical: s)
    Second,
    Minute,
    Hour,
    Day,
    Week,
    // Fraction (canonical: 1, so 100 % is 1)
    Percent,
    // Concentration (canonical: mg/L)
    PartsPerMillion,
    PartsPerBillion,
    MilligramPerLiter,
    // Count (canonical: 1)
    CountOne,
    CountUnit,
    CountEach,
    CountDimensionless,
    CountPackage,
};

std::optional<Unit> unitFromBeerJson(std::string_view name);
std::string_view beerJsonName(Unit unit);
Dimension dimensionOf(Unit unit);

// An exact measurement stored in its canonical unit as an exact rational, so
// converting to another unit and back never drifts. The authored unit is kept
// so the value can be written back in the unit it was entered in.
class Quantity {
public:
    // A zero placeholder so that model structs can be default-constructed and
    // then filled in. Readers always overwrite it, so do not rely on the value.
    Quantity() : Quantity(Dimension::Mass, Rational(0), Unit::Kilogram) {}

    static Quantity fromUnit(const Rational& value, Unit unit);

    // Exact value in `unit`. Throws std::invalid_argument if the dimension differs.
    Rational valueIn(Unit unit) const;

    Dimension dimension() const { return dimension_; }
    const Rational& canonical() const { return canonical_; }
    // The unit this quantity was authored in. Arithmetic keeps the left operand's unit.
    Unit unit() const { return unit_; }

    // Both operands must share a dimension, or std::invalid_argument is thrown.
    friend Quantity operator+(const Quantity& a, const Quantity& b);
    friend Quantity operator-(const Quantity& a, const Quantity& b);

    // Compares values, not authored units: 1 lb equals 16 oz.
    friend bool operator==(const Quantity& a, const Quantity& b) {
        return a.dimension_ == b.dimension_ && a.canonical_ == b.canonical_;
    }

private:
    Quantity(Dimension dimension, Rational canonical, Unit unit)
        : dimension_(dimension), canonical_(std::move(canonical)), unit_(unit) {}

    Dimension dimension_;
    Rational canonical_;
    Unit unit_;
};

}  // namespace kettlelogic
