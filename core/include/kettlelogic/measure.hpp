#pragma once

#include "kettlelogic/rational.hpp"

#include <string>
#include <string_view>

namespace kettlelogic {

// A value whose unit does not convert linearly: gravity (sg, plato, brix),
// color (EBC, Lovi, SRM), pH, pressure, specific volume, and bitterness.
// Stored exactly as authored, so the unit is never silently converted.
class Measure {
public:
    // An empty placeholder for default-constructed model structs. Readers
    // always overwrite it. Its unit is not a valid opaque unit.
    Measure() = default;

    // Throws std::invalid_argument if `unit` is not one of the opaque units.
    static Measure fromUnit(const Rational& value, std::string_view unit);

    const Rational& value() const { return value_; }
    const std::string& unit() const { return unit_; }

    friend bool operator==(const Measure& a, const Measure& b) {
        return a.value_ == b.value_ && a.unit_ == b.unit_;
    }

private:
    Measure(Rational value, std::string unit) : value_(std::move(value)), unit_(std::move(unit)) {}

    Rational value_;
    std::string unit_;
};

// True for the BeerJSON units that Measure accepts.
bool isOpaqueUnit(std::string_view unit);

}  // namespace kettlelogic
