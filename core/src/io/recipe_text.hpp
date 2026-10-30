#pragma once

#include "kettlelogic/quantity.hpp"

#include <string>

// Text the recipe exports share, so the text summary and the PDF show a quantity the same way.
namespace kettlelogic::io {

// A quantity in the unit it was entered in, such as "5 kg".
inline std::string display(const Quantity& q) {
    return q.valueIn(q.unit()).toDecimal() + " " + std::string(kettlelogic::beerJsonName(q.unit()));
}

}  // namespace kettlelogic::io
