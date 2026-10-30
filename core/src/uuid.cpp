#include "kettlelogic/uuid.hpp"

#include <chrono>
#include <random>

namespace kettlelogic {

namespace {

// One generator per process. Random bits identify entities; they are not
// secrets, so a seeded Mersenne Twister is enough.
std::mt19937_64& generator() {
    static std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

Uuid Uuid::generate() {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const auto millis = static_cast<std::uint64_t>(now) & 0xFFFFFFFFFFFFULL;  // 48 bits

    Uuid uuid;
    uuid.bytes_[0] = static_cast<std::uint8_t>(millis >> 40);
    uuid.bytes_[1] = static_cast<std::uint8_t>(millis >> 32);
    uuid.bytes_[2] = static_cast<std::uint8_t>(millis >> 24);
    uuid.bytes_[3] = static_cast<std::uint8_t>(millis >> 16);
    uuid.bytes_[4] = static_cast<std::uint8_t>(millis >> 8);
    uuid.bytes_[5] = static_cast<std::uint8_t>(millis);

    const std::uint64_t randomA = generator()();
    const std::uint64_t randomB = generator()();
    for (int i = 0; i < 8; ++i) {
        uuid.bytes_[6 + i] = static_cast<std::uint8_t>(randomA >> (56 - 8 * i));
        uuid.bytes_[8 + i] = static_cast<std::uint8_t>(randomB >> (56 - 8 * i));
    }

    uuid.bytes_[6] = static_cast<std::uint8_t>((uuid.bytes_[6] & 0x0F) | 0x70);  // version 7
    uuid.bytes_[8] = static_cast<std::uint8_t>((uuid.bytes_[8] & 0x3F) | 0x80);  // RFC variant
    return uuid;
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) {
        return std::nullopt;
    }
    Uuid uuid;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        if (pos == 8 || pos == 13 || pos == 18 || pos == 23) {
            if (text[pos] != '-') {
                return std::nullopt;
            }
            ++pos;
        }
        const int high = hexValue(text[pos]);
        const int low = hexValue(text[pos + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        uuid.bytes_[i] = static_cast<std::uint8_t>((high << 4) | low);
        pos += 2;
    }
    return uuid;
}

std::string Uuid::toString() const {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(digits[bytes_[i] >> 4]);
        out.push_back(digits[bytes_[i] & 0x0F]);
    }
    return out;
}

}  // namespace kettlelogic
