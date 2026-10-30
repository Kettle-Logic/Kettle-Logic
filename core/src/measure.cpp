#include "kettlelogic/measure.hpp"
#include "kettlelogic/model/schema.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace kettlelogic {

namespace {

// The units Quantity does not convert, from the schema's unit enums.
bool contains(std::span<const std::string_view> values, std::string_view unit) {
    return std::find(values.begin(), values.end(), unit) != values.end();
}

}  // namespace

bool isOpaqueUnit(std::string_view unit) {
    return contains(schema::kGravityUnits, unit) || contains(schema::kColorUnits, unit) ||
           contains(schema::kAcidityUnits, unit) || contains(schema::kPressureUnits, unit) ||
           contains(schema::kSpecificVolumeUnits, unit) || contains(schema::kBitternessUnits, unit);
}

Measure Measure::fromUnit(const Rational& value, std::string_view unit) {
    if (!isOpaqueUnit(unit)) {
        throw std::invalid_argument("not a non-convertible unit: " + std::string(unit));
    }
    return Measure{value, std::string(unit)};
}

}  // namespace kettlelogic
