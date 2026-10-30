#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace kettlelogic {

// Exact rational number. Arithmetic never rounds and never overflows.
// Only toDouble() and toFixed() lose information, and both are explicit.
class Rational {
public:
    using Exact = boost::multiprecision::cpp_rational;

    Rational() = default;
    Rational(long long whole) : value_(whole) {}  // NOLINT(google-explicit-constructor)
    Rational(long long numerator, long long denominator);
    explicit Rational(Exact value) : value_(std::move(value)) {}

    // Parses a decimal lexeme exactly: optional sign, digits, optional
    // fraction, optional exponent. "1.1" is 11/10, not the nearest double.
    // Throws std::invalid_argument on malformed input.
    static Rational fromDecimal(std::string_view text);

    // Lossy. Use only to hand a value to the calculation engine.
    double toDouble() const;

    // Decimal with `places` digits after the point, rounded half away from
    // zero using integer arithmetic.
    std::string toFixed(int places) const;

    // Minimal decimal text. Exact when the value terminates in decimal (every
    // BeerJSON unit definition does). Otherwise rounded to 12 places and
    // trailing zeros are removed.
    std::string toDecimal() const;

    const Exact& exact() const { return value_; }

    bool isInteger() const { return boost::multiprecision::denominator(value_) == 1; }

    friend Rational operator+(const Rational& a, const Rational& b) { return Rational{a.value_ + b.value_}; }
    friend Rational operator-(const Rational& a, const Rational& b) { return Rational{a.value_ - b.value_}; }
    friend Rational operator*(const Rational& a, const Rational& b) { return Rational{a.value_ * b.value_}; }
    friend Rational operator/(const Rational& a, const Rational& b) { return Rational{a.value_ / b.value_}; }
    friend Rational operator-(const Rational& a) { return Rational{-a.value_}; }

    friend bool operator==(const Rational& a, const Rational& b) { return a.value_ == b.value_; }
    friend bool operator<(const Rational& a, const Rational& b) { return a.value_ < b.value_; }

private:
    Exact value_;
};

}  // namespace kettlelogic
