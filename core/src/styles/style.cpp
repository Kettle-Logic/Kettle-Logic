#include "kettlelogic/styles/style.hpp"

#include "kettlelogic/uuid.hpp"

#include <stdexcept>

namespace kettlelogic::styles {

namespace {

void validateRange(const char* name, const Range& range) {
    if (!(range.low >= 0) || !(range.high >= range.low)) {
        throw std::invalid_argument(std::string("the ") + name + " range must not be negative, and its low end must not be above its high end");
    }
}

}  // namespace

bool isUserStyleId(std::string_view id) {
    return id.size() > kUserStylePrefix.size() && id.substr(0, kUserStylePrefix.size()) == kUserStylePrefix;
}

std::string newUserStyleId() {
    return std::string(kUserStylePrefix) + Uuid::generate().toString();
}

void validateUserStyle(const UserStyle& style) {
    if (style.name.find_first_not_of(" \t") == std::string::npos) {
        throw std::invalid_argument("a style needs a name");
    }
    validateRange("original gravity", style.ranges.og);
    validateRange("final gravity", style.ranges.fg);
    validateRange("bitterness", style.ranges.ibu);
    validateRange("color", style.ranges.srm);
    validateRange("alcohol", style.ranges.abv);
}

}  // namespace kettlelogic::styles
