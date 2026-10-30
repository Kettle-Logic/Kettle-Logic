#pragma once

#include <string>
#include <string_view>

namespace kettlelogic::crypto {

// SHA-256 (FIPS 180-4) as lowercase hex. Used to bind a sidecar to the BeerJSON
// bytes it was written with, so a half-finished save can be detected. It is
// not used for secrecy.
std::string sha256Hex(std::string_view data);

}  // namespace kettlelogic::crypto
