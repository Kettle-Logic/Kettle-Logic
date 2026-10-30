#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kettlelogic::app {

std::string trimmed(std::string_view text) {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && blank(text[begin])) {
        ++begin;
    }
    while (end > begin && blank(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::optional<double> parseNumber(std::string_view text) {
    std::string cleaned = trimmed(text);
    for (char& c : cleaned) {
        if (c == ',') {
            c = '.';
        }
    }
    if (cleaned.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(cleaned.c_str(), &end);
    if (end == cleaned.c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::string fixed(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    std::string out = buffer;
    // A value that rounds to zero is written "0.00", not "-0.00".
    if (out.size() > 1 && out[0] == '-' && out.find_first_not_of("-0.") == std::string::npos) {
        out.erase(0, 1);
    }
    return out;
}

std::string number(double value, int minPlaces, int maxPlaces) {
    std::string out = fixed(value, maxPlaces);
    const std::size_t point = out.find('.');
    if (point == std::string::npos) {
        return out;
    }
    std::size_t keep = out.size();
    const std::size_t floor = point + 1 + static_cast<std::size_t>(minPlaces);
    while (keep > floor && out[keep - 1] == '0') {
        --keep;
    }
    out.resize(keep);
    if (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    return out;
}

std::string significant(double value) {
    if (value == 0) {
        return "0";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    std::string out = buffer;
    // %g can pick an exponent form for very small or large values. Write those out in full.
    if (out.find('e') != std::string::npos) {
        const int magnitude = static_cast<int>(std::floor(std::log10(std::fabs(value))));
        const int places = std::max(0, 5 - magnitude);
        out = number(value, 0, std::min(places, 12));
    }
    return out;
}

std::string money(double value, const std::string& currency) {
    struct Symbol {
        const char* code;
        const char* symbol;
        int places;
    };
    static const Symbol symbols[] = {
        {"USD", "$", 2},       {"EUR", "\xE2\x82\xAC", 2}, {"GBP", "\xC2\xA3", 2}, {"CAD", "CA$", 2},
        {"AUD", "A$", 2},      {"NZD", "NZ$", 2},          {"JPY", "\xC2\xA5", 0},
    };
    for (const Symbol& s : symbols) {
        if (currency == s.code) {
            const bool negative = value < 0;
            return (negative ? "-" : "") + std::string(s.symbol) + fixed(std::fabs(value), s.places);
        }
    }
    return fixed(value, 2) + " " + currency;
}

std::string plural(long long count, const std::string& noun) {
    return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
}

std::string stem(const std::string& fileName) {
    const std::size_t dot = fileName.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? fileName : fileName.substr(0, dot);
}

}  // namespace kettlelogic::app
