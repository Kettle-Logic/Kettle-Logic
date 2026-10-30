#pragma once

#include <string>
#include <string_view>

// Beer styles a recipe can be compared against. The BJCP guideline styles are a
// built-in table (calc/style.cpp). A user can also define styles, which are kept in
// the document and saved in its sidecar.
namespace kettlelogic::styles {

struct Range {
    double low = 0;
    double high = 0;
};

// The vital statistics a style gives a range for, in the order the comparison reports them.
struct Ranges {
    Range og;   // original gravity, specific gravity
    Range fg;   // final gravity, specific gravity
    Range ibu;  // bitterness
    Range srm;  // color
    Range abv;  // alcohol, percent by volume
};

// A style the user defined. Its id begins with "custom-", so it never matches a guideline id such as "21A".
struct UserStyle {
    std::string id;    // empty until the command that adds it assigns one
    std::string name;  // not blank
    Ranges ranges;
};

inline constexpr std::string_view kUserStylePrefix = "custom-";

// True if `id` has the form of a user style id. It does not check that the style exists.
bool isUserStyleId(std::string_view id);

// A new id for a user style: the prefix and a fresh UUID.
std::string newUserStyleId();

// Throws std::invalid_argument if the style cannot be stored: the name is blank, or a
// range is negative or runs backwards (low above high).
void validateUserStyle(const UserStyle& style);

}  // namespace kettlelogic::styles
