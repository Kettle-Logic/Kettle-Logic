#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Searching, filtering, and sorting the ingredient lists that the add dialogs offer. A list holds the
// built-in catalog entries and the user's own, so the caller describes each one as an `IngredientEntry`
// and the engine decides which entries show and in what order. Every front end asks this one function,
// so all of them find, filter, and sort alike.
//
// Each kind has up to two numbers worth sorting and filtering on, called `primary` and `secondary`.
// `ingredientMetrics` names them. The caller converts to the metric's unit before it fills them in.
namespace kettlelogic::catalog {

struct IngredientEntry {
    std::string id;     // the caller's key, such as "Base malts/Maris Otter"
    std::string name;
    std::string group;  // the heading the entry sits under
    std::string type;   // a BeerJSON type, or empty
    std::string form;   // a BeerJSON form, or empty
    bool custom = false;  // the user's own, not built in
    std::optional<double> primary;
    std::optional<double> secondary;
};

enum class IngredientSort {
    catalog,    // the order the caller gave, which is the catalog's menu order
    name,
    group,      // groups in the order they first appear, then names
    primary,
    secondary,
};

enum class IngredientSource { any, builtIn, custom };

struct IngredientQuery {
    // Words, all of which must appear in the name or the group. Case, accents, and punctuation are ignored,
    // so "2 row", "2-ROW", and "2row" all find "2-Row (Briess)".
    std::string text;
    // An entry passes when its value is any one of those listed. An empty list does not filter.
    std::vector<std::string> groups;
    std::vector<std::string> types;
    std::vector<std::string> forms;
    IngredientSource source = IngredientSource::any;
    // A bound on the primary number. An entry without one is left out when either bound is set.
    std::optional<double> minPrimary;
    std::optional<double> maxPrimary;
    IngredientSort sort = IngredientSort::catalog;
    // Reverses the order of the sort key. An entry without a number still comes last, and ties keep name order.
    bool descending = false;
};

// What the entries offer to filter on, each value once, in the order it first appears.
struct IngredientFacets {
    std::vector<std::string> groups;
    std::vector<std::string> types;
    std::vector<std::string> forms;
    std::optional<double> primaryMin;
    std::optional<double> primaryMax;
};

struct IngredientMetric {
    std::string label;  // such as "Color"
    std::string unit;   // such as "SRM", or empty for a bare number
};

struct IngredientMetrics {
    std::optional<IngredientMetric> primary;
    std::optional<IngredientMetric> secondary;
};

// The text with case folded, accents removed, punctuation dropped, and runs of spaces made one. This is
// what the search and the name sort compare.
std::string foldForSearch(std::string_view text);

// The positions in `entries` of those that pass the query, in the order the query asks for.
std::vector<std::size_t> queryIngredients(const std::vector<IngredientEntry>& entries, const IngredientQuery& query);

IngredientFacets ingredientFacets(const std::vector<IngredientEntry>& entries);

// The numbers a kind of ingredient sorts and filters on. `kind` is "fermentable", "hop", "culture", "water", or
// "miscellaneous". A fermentable's primary is its color in SRM and its secondary is its yield as a specific
// gravity. A hop's are its alpha and beta acid in percent. A culture's primary is its attenuation in percent.
// Water and miscellaneous ingredients have neither.
IngredientMetrics ingredientMetrics(std::string_view kind);

}  // namespace kettlelogic::catalog
