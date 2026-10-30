#include "kettlelogic/catalog/ingredient_query.hpp"

#include <algorithm>
#include <cstdint>
#include <set>

namespace kettlelogic::catalog {
namespace {

// The plain letters for a Latin-1 or Latin Extended-A letter, or nothing for a sign such as "×".
const char* plainLetters(std::uint32_t cp) {
    static const char* const latin1[] = {
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // C0
        "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",   // D0
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0
        "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y"};   // F0
    if (cp >= 0xC0 && cp <= 0xFF) return latin1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F) {
        struct Span {
            std::uint32_t last;
            const char* letters;
        };
        static const Span spans[] = {
            {0x105, "a"}, {0x10D, "c"}, {0x111, "d"}, {0x11B, "e"}, {0x123, "g"}, {0x127, "h"}, {0x131, "i"},
            {0x133, "ij"}, {0x135, "j"}, {0x138, "k"}, {0x142, "l"}, {0x14B, "n"}, {0x151, "o"}, {0x153, "oe"},
            {0x159, "r"}, {0x161, "s"}, {0x167, "t"}, {0x173, "u"}, {0x175, "w"}, {0x178, "y"}, {0x17E, "z"},
            {0x17F, "s"}};
        for (const Span& span : spans) {
            if (cp <= span.last) return span.letters;
        }
    }
    return nullptr;
}

// Decodes one UTF-8 character at `i`, moving `i` past it. A stray byte stands for itself.
std::uint32_t nextCodePoint(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t at) { return static_cast<std::uint8_t>(text[at]); };
    const std::uint8_t lead = byte(i);
    std::size_t length = 1;
    std::uint32_t cp = lead;
    if (lead >= 0xF0) {
        length = 4;
        cp = lead & 0x07;
    } else if (lead >= 0xE0) {
        length = 3;
        cp = lead & 0x0F;
    } else if (lead >= 0xC0) {
        length = 2;
        cp = lead & 0x1F;
    }
    if (i + length > text.size()) {
        ++i;
        return lead;
    }
    for (std::size_t k = 1; k < length; ++k) {
        cp = (cp << 6) | (byte(i + k) & 0x3F);
    }
    i += length;
    return cp;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The words of folded text.
std::vector<std::string> words(const std::string& folded) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < folded.size()) {
        const std::size_t end = folded.find(' ', start);
        const std::size_t stop = end == std::string::npos ? folded.size() : end;
        if (stop > start) out.push_back(folded.substr(start, stop - start));
        start = stop + 1;
    }
    return out;
}

bool oneOf(const std::vector<std::string>& allowed, const std::string& value) {
    return allowed.empty() || std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

// Orders two optional numbers, with a missing one last whichever way the sort runs. Returns 0 on a tie.
int compareNumbers(const std::optional<double>& a, const std::optional<double>& b, bool descending) {
    if (a.has_value() != b.has_value()) return a.has_value() ? -1 : 1;
    if (!a || *a == *b) return 0;
    return (*a < *b) != descending ? -1 : 1;
}

}  // namespace

std::string foldForSearch(std::string_view text) {
    std::string out;
    bool pendingSpace = false;
    const auto emit = [&](std::string_view piece) {
        if (pendingSpace && !out.empty()) out += ' ';
        pendingSpace = false;
        out += piece;
    };
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t cp = nextCodePoint(text, i);
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (c >= 'A' && c <= 'Z') {
                emit(std::string(1, static_cast<char>(c - 'A' + 'a')));
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                emit(std::string(1, c));
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                pendingSpace = true;
            }
            // Other punctuation is dropped, so "2-Row" is "2row".
        } else if (cp == 0xA0) {
            pendingSpace = true;
        } else if (const char* plain = plainLetters(cp)) {
            if (*plain != '\0') emit(plain);
        } else {
            std::string raw;
            appendUtf8(raw, cp);
            emit(raw);
        }
    }
    return out;
}

std::vector<std::size_t> queryIngredients(const std::vector<IngredientEntry>& entries, const IngredientQuery& query) {
    const std::vector<std::string> wanted = words(foldForSearch(query.text));
    const bool bounded = query.minPrimary || query.maxPrimary;

    std::vector<std::size_t> kept;
    std::vector<std::string> names(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const IngredientEntry& e = entries[i];
        names[i] = foldForSearch(e.name);
        if (query.source == IngredientSource::builtIn && e.custom) continue;
        if (query.source == IngredientSource::custom && !e.custom) continue;
        if (!oneOf(query.groups, e.group) || !oneOf(query.types, e.type) || !oneOf(query.forms, e.form)) continue;
        if (bounded) {
            if (!e.primary) continue;
            if (query.minPrimary && *e.primary < *query.minPrimary) continue;
            if (query.maxPrimary && *e.primary > *query.maxPrimary) continue;
        }
        if (!wanted.empty()) {
            const std::string haystack = names[i] + ' ' + foldForSearch(e.group);
            const bool all = std::all_of(wanted.begin(), wanted.end(), [&](const std::string& word) {
                return haystack.find(word) != std::string::npos;
            });
            if (!all) continue;
        }
        kept.push_back(i);
    }

    if (query.sort == IngredientSort::catalog) {
        if (query.descending) std::reverse(kept.begin(), kept.end());
        return kept;
    }

    std::vector<std::string> groupOrder;
    if (query.sort == IngredientSort::group) {
        for (const IngredientEntry& e : entries) {
            if (std::find(groupOrder.begin(), groupOrder.end(), e.group) == groupOrder.end()) groupOrder.push_back(e.group);
        }
    }
    const auto groupRank = [&](const IngredientEntry& e) {
        return std::find(groupOrder.begin(), groupOrder.end(), e.group) - groupOrder.begin();
    };

    std::stable_sort(kept.begin(), kept.end(), [&](std::size_t a, std::size_t b) {
        int order = 0;
        switch (query.sort) {
            case IngredientSort::primary: order = compareNumbers(entries[a].primary, entries[b].primary, query.descending); break;
            case IngredientSort::secondary:
                order = compareNumbers(entries[a].secondary, entries[b].secondary, query.descending);
                break;
            case IngredientSort::group: {
                const auto ra = groupRank(entries[a]);
                const auto rb = groupRank(entries[b]);
                order = ra == rb ? 0 : (ra < rb) != query.descending ? -1 : 1;
                break;
            }
            case IngredientSort::name: {
                const int c = names[a].compare(names[b]);
                order = c == 0 ? 0 : (c < 0) != query.descending ? -1 : 1;
                break;
            }
            case IngredientSort::catalog: break;
        }
        if (order != 0) return order < 0;
        // Ties read in name order, whichever way the key runs.
        return names[a] < names[b];
    });
    return kept;
}

IngredientFacets ingredientFacets(const std::vector<IngredientEntry>& entries) {
    IngredientFacets facets;
    const auto note = [](std::vector<std::string>& list, const std::string& value) {
        if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
    };
    for (const IngredientEntry& e : entries) {
        note(facets.groups, e.group);
        note(facets.types, e.type);
        note(facets.forms, e.form);
        if (e.primary) {
            facets.primaryMin = facets.primaryMin ? std::min(*facets.primaryMin, *e.primary) : *e.primary;
            facets.primaryMax = facets.primaryMax ? std::max(*facets.primaryMax, *e.primary) : *e.primary;
        }
    }
    return facets;
}

IngredientMetrics ingredientMetrics(std::string_view kind) {
    if (kind == "fermentable") return {IngredientMetric{"Color", "SRM"}, IngredientMetric{"Yield", ""}};
    if (kind == "hop") return {IngredientMetric{"Alpha acid", "%"}, IngredientMetric{"Beta acid", "%"}};
    if (kind == "culture") return {IngredientMetric{"Attenuation", "%"}, std::nullopt};
    return {};
}

}  // namespace kettlelogic::catalog
