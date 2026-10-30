#include "kettlelogic/rational.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <stdexcept>

namespace kettlelogic {

namespace {

using Int = boost::multiprecision::cpp_int;

Int pow10(int exponent) {
    Int result = 1;
    for (int i = 0; i < exponent; ++i) {
        result *= 10;
    }
    return result;
}

}  // namespace

Rational::Rational(long long numerator, long long denominator)
    : value_(Exact(numerator) / Exact(denominator)) {}

Rational Rational::fromDecimal(std::string_view text) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        negative = text[i] == '-';
        ++i;
    }

    std::string digits;
    int fractionDigits = 0;
    bool seenPoint = false;
    bool seenDigit = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
            seenDigit = true;
            if (seenPoint) {
                ++fractionDigits;
            }
        } else if (c == '.' && !seenPoint) {
            seenPoint = true;
        } else {
            break;
        }
    }
    if (!seenDigit) {
        throw std::invalid_argument("malformed decimal: no digits");
    }

    int exponent = 0;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && text[i] == '+') {
            ++i;
        }
        const char* first = text.data() + i;
        const char* last = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(first, last, exponent);
        if (ec != std::errc{} || ptr != last) {
            throw std::invalid_argument("malformed decimal: bad exponent");
        }
        i = text.size();
    }
    if (i != text.size()) {
        throw std::invalid_argument("malformed decimal: trailing characters");
    }

    // Build the digits by hand; constructing cpp_int from a string threw here.
    Int mantissa = 0;
    for (const char c : digits) {
        mantissa = mantissa * 10 + (c - '0');
    }
    if (negative) {
        mantissa = -mantissa;
    }
    const int shift = exponent - fractionDigits;
    Exact value(mantissa);
    if (shift >= 0) {
        value *= Exact(pow10(shift));
    } else {
        value /= Exact(pow10(-shift));
    }
    return Rational{value};
}

std::string Rational::toDecimal() const {
    // A terminating decimal has a denominator of the form 2^a * 5^b, and needs
    // max(a, b) places to be exact.
    Int den = boost::multiprecision::denominator(value_);
    int twos = 0;
    int fives = 0;
    while (den % 2 == 0) {
        den /= 2;
        ++twos;
    }
    while (den % 5 == 0) {
        den /= 5;
        ++fives;
    }
    if (den == 1) {
        return toFixed(std::max(twos, fives));
    }

    std::string text = toFixed(12);
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

double Rational::toDouble() const {
    return value_.convert_to<double>();
}

std::string Rational::toFixed(int places) const {
    const bool negative = value_ < Exact(0);
    const Exact magnitude = negative ? Exact(-value_) : value_;

    const Exact scaled = magnitude * Exact(pow10(places));
    const Int num = boost::multiprecision::numerator(scaled);
    const Int den = boost::multiprecision::denominator(scaled);

    Int quotient = num / den;
    const Int remainder = num % den;
    if (2 * remainder >= den) {  // half away from zero
        quotient += 1;
    }

    std::string digits = quotient.str();
    if (places > 0) {
        const auto width = static_cast<std::size_t>(places);
        if (digits.size() <= width) {
            digits.insert(0, width + 1 - digits.size(), '0');
        }
        digits.insert(digits.size() - width, 1, '.');
    }
    if (negative && quotient != 0) {
        digits.insert(0, 1, '-');
    }
    return digits;
}

}  // namespace kettlelogic
