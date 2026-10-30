#pragma once

#include <optional>
#include <string>
#include <string_view>

// Text helpers shared by the app's forms. Numbers are always written and read with a period for the
// decimal point and no grouping, so the text the engine receives is exact decimal text.
namespace kettlelogic::app {

// Trims spaces and tabs from both ends.
std::string trimmed(std::string_view text);

// A number typed by the user, or nothing when it is not one. A comma is read as a decimal point.
std::optional<double> parseNumber(std::string_view text);

// The value with `places` digits after the point, always.
std::string fixed(double value, int places);

// The value with at most `maxPlaces` digits after the point, and at least `minPlaces`. Trailing zeros
// beyond `minPlaces` are dropped, as the macOS app's `.precision(.fractionLength(min...max))` does.
std::string number(double value, int minPlaces, int maxPlaces);

// The value with between 1 and 6 significant digits, trailing zeros dropped (`.significantDigits(1...6)`).
std::string significant(double value);

// A price or cost in a currency, such as "$4.50" for USD or "4.50 CHF" for a code without a symbol.
std::string money(double value, const std::string& currency);

// "1 hop", "2 hops": a count with its noun, by a plain "s" plural.
std::string plural(long long count, const std::string& noun);

// The stem of a file name, without its last extension.
std::string stem(const std::string& fileName);

}  // namespace kettlelogic::app
