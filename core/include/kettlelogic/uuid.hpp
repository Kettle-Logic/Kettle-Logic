#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kettlelogic {

// A 128-bit identifier in the RFC 9562 format. Entities carry one so that they
// keep their identity across saves and reloads, independent of where they sit
// in the file.
class Uuid {
public:
    Uuid() = default;  // the nil UUID, all zeros

    // A version 7 UUID: a 48-bit millisecond timestamp, then random bits. Time
    // order makes them sort roughly by creation.
    static Uuid generate();

    // Accepts the canonical 8-4-4-4-12 hex form, in either case.
    static std::optional<Uuid> parse(std::string_view text);

    bool isNil() const { return *this == Uuid{}; }

    // Lowercase canonical form, for example "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f50".
    std::string toString() const;

    friend bool operator==(const Uuid& a, const Uuid& b) { return a.bytes_ == b.bytes_; }
    friend bool operator!=(const Uuid& a, const Uuid& b) { return !(a == b); }

private:
    std::array<std::uint8_t, 16> bytes_{};
};

}  // namespace kettlelogic
