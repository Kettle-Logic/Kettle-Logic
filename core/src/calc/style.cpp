#include "kettlelogic/calc/style.hpp"

#include <array>
#include <cstddef>

namespace kettlelogic::calc {

namespace {

// The vital statistics of each style in the BJCP 2021 Beer Style Guidelines, in
// guideline order. Each range is the low and high end printed for the style.
// Source: https://www.bjcp.org/style/2021/beer/ (BJCP 2021 Beer Style Guidelines).
// Styles 28 to 33 are not listed yet: their statistics are set out differently.
constexpr std::array<BjcpStyle, 86> kStyles{{
    {"1A", "American Light Lager", {1.028, 1.040}, {0.998, 1.008}, {8, 12}, {2, 3}, {2.8, 4.2}},
    {"1B", "American Lager", {1.040, 1.050}, {1.004, 1.010}, {8, 18}, {2, 3.5}, {4.2, 5.3}},
    {"1C", "Cream Ale", {1.042, 1.055}, {1.006, 1.012}, {8, 20}, {2, 5}, {4.2, 5.6}},
    {"1D", "American Wheat Beer", {1.040, 1.055}, {1.008, 1.013}, {15, 30}, {3, 6}, {4, 5.5}},
    {"2A", "International Pale Lager", {1.042, 1.050}, {1.008, 1.012}, {18, 25}, {2, 6}, {4.5, 6}},
    {"2B", "International Amber Lager", {1.042, 1.055}, {1.008, 1.014}, {8, 25}, {6, 14}, {4.5, 6}},
    {"2C", "International Dark Lager", {1.044, 1.056}, {1.008, 1.012}, {8, 20}, {14, 30}, {4.2, 6}},
    {"3A", "Czech Pale Lager", {1.028, 1.044}, {1.008, 1.014}, {20, 35}, {3, 6}, {3, 4.1}},
    {"3B", "Czech Premium Pale Lager", {1.044, 1.060}, {1.013, 1.017}, {30, 45}, {3.5, 6}, {4.2, 5.8}},
    {"3C", "Czech Amber Lager", {1.044, 1.060}, {1.013, 1.017}, {20, 35}, {10, 16}, {4.4, 5.8}},
    {"3D", "Czech Dark Lager", {1.044, 1.060}, {1.013, 1.017}, {18, 34}, {17, 35}, {4.4, 5.8}},
    {"4A", "Munich Helles", {1.044, 1.048}, {1.006, 1.012}, {16, 22}, {3, 5}, {4.7, 5.4}},
    {"4B", "Festbier", {1.054, 1.057}, {1.010, 1.012}, {18, 25}, {4, 6}, {5.8, 6.3}},
    {"4C", "Helles Bock", {1.064, 1.072}, {1.011, 1.018}, {23, 35}, {6, 9}, {6.3, 7.4}},
    {"5A", "German Leichtbier", {1.026, 1.034}, {1.006, 1.010}, {15, 28}, {1.5, 4}, {2.4, 3.6}},
    {"5B", "Kölsch", {1.044, 1.050}, {1.007, 1.011}, {18, 30}, {3.5, 5}, {4.4, 5.2}},
    {"5C", "German Helles Exportbier", {1.050, 1.058}, {1.008, 1.015}, {20, 30}, {4, 6}, {5, 6}},
    {"5D", "German Pils", {1.044, 1.050}, {1.008, 1.013}, {22, 40}, {2, 4}, {4.4, 5.2}},
    {"6A", "Märzen", {1.054, 1.060}, {1.010, 1.014}, {18, 24}, {8, 17}, {5.6, 6.3}},
    {"6B", "Rauchbier", {1.050, 1.057}, {1.012, 1.016}, {20, 30}, {12, 22}, {4.8, 6}},
    {"6C", "Dunkles Bock", {1.064, 1.072}, {1.013, 1.019}, {20, 27}, {14, 22}, {6.3, 7.2}},
    {"7A", "Vienna Lager", {1.048, 1.055}, {1.010, 1.014}, {18, 30}, {9, 15}, {4.7, 5.5}},
    {"7B", "Altbier", {1.044, 1.052}, {1.008, 1.014}, {25, 50}, {9, 17}, {4.3, 5.5}},
    {"8A", "Munich Dunkel", {1.048, 1.056}, {1.010, 1.016}, {18, 28}, {17, 28}, {4.5, 5.6}},
    {"8B", "Schwarzbier", {1.046, 1.052}, {1.010, 1.016}, {20, 35}, {19, 30}, {4.4, 5.4}},
    {"9A", "Doppelbock", {1.072, 1.112}, {1.016, 1.024}, {16, 26}, {6, 25}, {7, 10}},
    {"9B", "Eisbock", {1.078, 1.120}, {1.020, 1.035}, {25, 35}, {17, 30}, {9, 14}},
    {"9C", "Baltic Porter", {1.060, 1.090}, {1.016, 1.024}, {20, 40}, {17, 30}, {6.5, 9.5}},
    {"10A", "Weissbier", {1.044, 1.053}, {1.008, 1.014}, {8, 15}, {2, 6}, {4.3, 5.6}},
    {"10B", "Dunkles Weissbier", {1.044, 1.057}, {1.008, 1.014}, {10, 18}, {14, 23}, {4.3, 5.6}},
    {"10C", "Weizenbock", {1.064, 1.090}, {1.015, 1.022}, {15, 30}, {6, 25}, {6.5, 9}},
    {"11A", "Ordinary Bitter", {1.030, 1.039}, {1.007, 1.011}, {25, 35}, {8, 14}, {3.2, 3.8}},
    {"11B", "Best Bitter", {1.040, 1.048}, {1.008, 1.012}, {25, 40}, {8, 16}, {3.8, 4.6}},
    {"11C", "Strong Bitter", {1.048, 1.060}, {1.010, 1.016}, {30, 50}, {8, 18}, {4.6, 6.2}},
    {"12A", "British Golden Ale", {1.038, 1.053}, {1.006, 1.012}, {20, 45}, {2, 5}, {3.8, 5}},
    {"12B", "Australian Sparkling Ale", {1.038, 1.050}, {1.004, 1.006}, {20, 35}, {4, 7}, {4.5, 6}},
    {"12C", "English IPA", {1.050, 1.070}, {1.010, 1.015}, {40, 60}, {6, 14}, {5, 7.5}},
    {"13A", "Dark Mild", {1.030, 1.038}, {1.008, 1.013}, {10, 25}, {14, 25}, {3, 3.8}},
    {"13B", "British Brown Ale", {1.040, 1.052}, {1.008, 1.013}, {20, 30}, {12, 22}, {4.2, 5.9}},
    {"13C", "English Porter", {1.040, 1.052}, {1.008, 1.014}, {18, 35}, {20, 30}, {4, 5.4}},
    {"14A", "Scottish Light", {1.030, 1.035}, {1.010, 1.013}, {10, 20}, {17, 25}, {2.5, 3.3}},
    {"14B", "Scottish Heavy", {1.035, 1.040}, {1.010, 1.015}, {10, 20}, {12, 20}, {3.3, 3.9}},
    {"14C", "Scottish Export", {1.040, 1.060}, {1.010, 1.016}, {15, 30}, {12, 20}, {3.9, 6}},
    {"15A", "Irish Red Ale", {1.036, 1.046}, {1.010, 1.014}, {18, 28}, {9, 14}, {3.8, 5}},
    {"15B", "Irish Stout", {1.036, 1.044}, {1.007, 1.011}, {25, 45}, {25, 40}, {3.8, 5}},
    {"15C", "Irish Extra Stout", {1.052, 1.062}, {1.010, 1.014}, {35, 50}, {30, 40}, {5, 6.5}},
    {"16A", "Sweet Stout", {1.044, 1.060}, {1.012, 1.024}, {20, 40}, {30, 40}, {4, 6}},
    {"16B", "Oatmeal Stout", {1.045, 1.065}, {1.010, 1.018}, {25, 40}, {22, 40}, {4.2, 5.9}},
    {"16C", "Tropical Stout", {1.056, 1.075}, {1.010, 1.018}, {30, 50}, {30, 40}, {5.5, 8}},
    {"16D", "Foreign Extra Stout", {1.056, 1.075}, {1.010, 1.018}, {50, 70}, {30, 40}, {6.3, 8}},
    {"17A", "British Strong Ale", {1.055, 1.080}, {1.015, 1.022}, {30, 60}, {8, 22}, {5.5, 8}},
    {"17B", "Old Ale", {1.055, 1.088}, {1.015, 1.022}, {30, 60}, {10, 22}, {5.5, 9}},
    {"17C", "Wee Heavy", {1.070, 1.130}, {1.018, 1.040}, {17, 35}, {14, 25}, {6.5, 10}},
    {"17D", "English Barley Wine", {1.080, 1.120}, {1.018, 1.030}, {35, 70}, {8, 22}, {8, 12}},
    {"18A", "Blonde Ale", {1.038, 1.054}, {1.008, 1.013}, {15, 28}, {3, 6}, {3.8, 5.5}},
    {"18B", "American Pale Ale", {1.045, 1.060}, {1.010, 1.015}, {30, 50}, {5, 10}, {4.5, 6.2}},
    {"19A", "American Amber Ale", {1.045, 1.060}, {1.010, 1.015}, {25, 40}, {10, 17}, {4.5, 6.2}},
    {"19B", "California Common", {1.048, 1.054}, {1.011, 1.014}, {30, 45}, {9, 14}, {4.5, 5.5}},
    {"19C", "American Brown Ale", {1.045, 1.060}, {1.010, 1.016}, {20, 30}, {18, 35}, {4.3, 6.2}},
    {"20A", "American Porter", {1.050, 1.070}, {1.012, 1.018}, {25, 50}, {22, 40}, {4.8, 6.5}},
    {"20B", "American Stout", {1.050, 1.075}, {1.010, 1.022}, {35, 75}, {30, 40}, {5, 7}},
    {"20C", "Imperial Stout", {1.075, 1.115}, {1.018, 1.030}, {50, 90}, {30, 40}, {8, 12}},
    {"21A", "American IPA", {1.056, 1.070}, {1.008, 1.014}, {40, 70}, {6, 14}, {5.5, 7.5}},
    {"21B", "Specialty IPA", {1.058, 1.080}, {1.008, 1.016}, {50, 100}, {5, 8}, {6.2, 9.5}},
    {"21C", "Hazy IPA", {1.060, 1.085}, {1.010, 1.015}, {25, 60}, {3, 7}, {6, 9}},
    {"22A", "Double IPA", {1.065, 1.085}, {1.008, 1.018}, {60, 100}, {6, 14}, {7.5, 10}},
    {"22B", "American Strong Ale", {1.062, 1.090}, {1.014, 1.024}, {50, 100}, {7, 18}, {6.3, 10}},
    {"22C", "American Barleywine", {1.080, 1.120}, {1.016, 1.030}, {50, 100}, {9, 18}, {8, 12}},
    {"22D", "Wheatwine", {1.080, 1.120}, {1.016, 1.030}, {30, 60}, {6, 14}, {8, 12}},
    {"23A", "Berliner Weisse", {1.028, 1.032}, {1.003, 1.006}, {3, 8}, {2, 3}, {2.8, 3.8}},
    {"23B", "Flanders Red Ale", {1.048, 1.057}, {1.002, 1.012}, {10, 25}, {10, 17}, {4.6, 6.5}},
    {"23C", "Oud Bruin", {1.040, 1.074}, {1.008, 1.012}, {20, 25}, {17, 22}, {4, 8}},
    {"23D", "Lambic", {1.040, 1.054}, {1.001, 1.010}, {0, 10}, {3, 6}, {5, 6.5}},
    {"23E", "Gueuze", {1.040, 1.054}, {1.000, 1.006}, {0, 10}, {5, 6}, {5, 8}},
    {"23F", "Fruit Lambic", {1.040, 1.060}, {1.000, 1.010}, {0, 10}, {3, 7}, {5, 7}},
    {"23G", "Gose", {1.036, 1.056}, {1.006, 1.010}, {5, 12}, {3, 4}, {4.2, 4.8}},
    {"24A", "Witbier", {1.044, 1.052}, {1.008, 1.012}, {8, 20}, {2, 4}, {4.5, 5.5}},
    {"24B", "Belgian Pale Ale", {1.048, 1.054}, {1.010, 1.014}, {20, 30}, {8, 14}, {4.8, 5.5}},
    {"24C", "Bière de Garde", {1.060, 1.080}, {1.008, 1.016}, {18, 28}, {6, 19}, {6, 8.5}},
    {"25A", "Belgian Blond Ale", {1.062, 1.075}, {1.008, 1.018}, {15, 30}, {4, 6}, {6, 7.5}},
    {"25B", "Saison", {1.048, 1.065}, {1.002, 1.008}, {20, 35}, {5, 14}, {3.5, 5}},
    {"25C", "Belgian Golden Strong Ale", {1.070, 1.095}, {1.005, 1.016}, {22, 35}, {3, 6}, {7.5, 10.5}},
    {"26A", "Belgian Single", {1.044, 1.054}, {1.004, 1.010}, {25, 45}, {3, 5}, {4.8, 6}},
    {"26B", "Belgian Dubbel", {1.062, 1.075}, {1.008, 1.018}, {15, 25}, {10, 17}, {6, 7.6}},
    {"26C", "Belgian Tripel", {1.075, 1.085}, {1.008, 1.014}, {20, 40}, {4.5, 7}, {7.5, 9.5}},
    {"26D", "Belgian Dark Strong Ale", {1.075, 1.110}, {1.010, 1.024}, {20, 35}, {12, 22}, {8, 12}},
}};

}  // namespace

std::span<const BjcpStyle> bjcpStyles() {
    return kStyles;
}

const BjcpStyle* findBjcpStyle(std::string_view id) {
    for (const BjcpStyle& style : kStyles) {
        if (style.id == id) {
            return &style;
        }
    }
    return nullptr;
}

namespace {

Check checkOf(Metric metric, std::optional<double> value, Range range) {
    Check check;
    check.metric = metric;
    check.value = value;
    check.range = range;
    if (!value) {
        check.fit = Fit::Unknown;
    } else if (*value < range.low) {
        check.fit = Fit::Below;
    } else if (*value > range.high) {
        check.fit = Fit::Above;
    } else {
        check.fit = Fit::Within;
    }
    return check;
}

}  // namespace

std::optional<StyleRef> findStyle(const model::Document& document, std::string_view id) {
    if (const BjcpStyle* guideline = findBjcpStyle(id)) {
        return StyleRef{std::string(guideline->id), std::string(guideline->name),
                        styles::Ranges{guideline->og, guideline->fg, guideline->ibu, guideline->srm, guideline->abv}};
    }
    if (const styles::UserStyle* user = document.findUserStyle(id)) {
        return StyleRef{user->id, user->name, user->ranges};
    }
    return std::nullopt;
}

std::vector<Check> compareToStyle(const Results& results, const styles::Ranges& ranges) {
    return {
        checkOf(Metric::OriginalGravity, results.originalGravity, ranges.og),
        checkOf(Metric::FinalGravity, results.finalGravity, ranges.fg),
        checkOf(Metric::Abv, results.abv, ranges.abv),
        checkOf(Metric::Ibu, results.ibu, ranges.ibu),
        checkOf(Metric::Srm, results.srm, ranges.srm),
    };
}

std::vector<Check> compareToStyle(const Results& results, const BjcpStyle& style) {
    return compareToStyle(results, styles::Ranges{style.og, style.fg, style.ibu, style.srm, style.abv});
}

}  // namespace kettlelogic::calc
