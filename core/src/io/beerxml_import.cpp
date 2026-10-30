#include "kettlelogic/io/beerxml_import.hpp"

#include "kettlelogic/measure.hpp"
#include "kettlelogic/quantity.hpp"
#include "kettlelogic/rational.hpp"
#include "kettlelogic/uuid.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

using kettlelogic::Measure;
using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;
using kettlelogic::Uuid;
using kettlelogic::model::Document;

namespace kettlelogic::io {
namespace {

// ---- Text --------------------------------------------------------------------

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// The file's text as UTF-8. BeerXML is UTF-8 in practice; ISO-8859-1 is converted, and
// anything else is refused, so that names are never read wrongly.
std::string toUtf8(std::string_view bytes) {
    if (bytes.substr(0, 2) == "\xFF\xFE" || bytes.substr(0, 2) == "\xFE\xFF") {
        throw BeerXmlError("the file is UTF-16; save it as UTF-8");
    }
    if (bytes.substr(0, 3) == "\xEF\xBB\xBF") {
        bytes.remove_prefix(3);
    }

    std::string declared = "utf-8";
    if (bytes.substr(0, 5) == "<?xml") {
        const std::size_t end = bytes.find("?>");
        const std::string_view prolog = bytes.substr(0, end == std::string_view::npos ? bytes.size() : end);
        const std::size_t at = prolog.find("encoding");
        if (at != std::string_view::npos) {
            const std::size_t open = prolog.find_first_of("\"'", at);
            const std::size_t close = open == std::string_view::npos ? open : prolog.find(prolog[open], open + 1);
            if (close == std::string_view::npos) {
                throw BeerXmlError("the XML declaration's encoding is not quoted");
            }
            declared = lowered(prolog.substr(open + 1, close - open - 1));
        }
    }

    std::string name;
    for (const char c : declared) {
        if (c != '-' && c != '_') {
            name.push_back(c);
        }
    }
    if (name == "utf8" || name == "ascii" || name == "usascii") {
        return std::string(bytes);
    }
    if (name == "iso88591" || name == "latin1") {
        std::string out;
        out.reserve(bytes.size());
        for (const char c : bytes) {
            appendUtf8(out, static_cast<std::uint8_t>(c));
        }
        return out;
    }
    throw BeerXmlError("unsupported encoding \"" + declared + "\"; save the file as UTF-8");
}

// ---- XML ---------------------------------------------------------------------

// One element. Its text is the character data in order, with entities decoded, and
// its children are in the order they appear.
struct Element {
    std::string name;
    std::string text;
    std::vector<Element> children;
    std::size_t line = 1;
};

constexpr std::size_t kMaxDepth = 64;

// A small XML reader for BeerXML: elements, attributes (read past, not kept), text,
// CDATA, comments, processing instructions, and a DOCTYPE (skipped). Namespaces and
// DTD entities are not supported.
class XmlParser {
public:
    explicit XmlParser(std::string_view text) : in_(text) {}

    Element document() {
        skipMisc();
        if (atEnd() || peek() != '<') {
            fail("the document has no root element");
        }
        Element root = element(1);
        skipMisc();
        if (!atEnd()) {
            fail("there is text after the root element");
        }
        return root;
    }

private:
    std::string_view in_;
    std::size_t pos_ = 0;
    std::size_t lineScan_ = 0;  // how far the line count has been taken
    std::size_t line_ = 1;

    bool atEnd() const { return pos_ >= in_.size(); }
    char peek() const { return in_[pos_]; }
    bool startsWith(std::string_view s) const { return in_.substr(pos_, s.size()) == s; }

    // Positions only move forward, so the line count is taken incrementally.
    std::size_t lineAt(std::size_t pos) {
        for (; lineScan_ < pos && lineScan_ < in_.size(); ++lineScan_) {
            if (in_[lineScan_] == '\n') {
                ++line_;
            }
        }
        return line_;
    }

    [[noreturn]] void fail(const std::string& message) {
        throw BeerXmlError("line " + std::to_string(lineAt(pos_)) + ": " + message);
    }

    void skipSpace() {
        while (!atEnd() && isSpace(peek())) {
            ++pos_;
        }
    }

    // Moves past `end`, which must occur. `what` names the construct in the error.
    void skipPast(std::string_view end, std::string_view what) {
        const std::size_t found = in_.find(end, pos_);
        if (found == std::string_view::npos) {
            fail("unterminated " + std::string(what));
        }
        pos_ = found + end.size();
    }

    // A DOCTYPE can hold an internal subset in brackets, so the ">" that ends it is the
    // first one outside them.
    void skipDoctype() {
        int depth = 0;
        for (pos_ += 9; !atEnd(); ++pos_) {  // 9 is the length of "<!DOCTYPE"
            if (peek() == '[') {
                ++depth;
            } else if (peek() == ']') {
                --depth;
            } else if (peek() == '>' && depth == 0) {
                ++pos_;
                return;
            }
        }
        fail("unterminated DOCTYPE");
    }

    // Whitespace, comments, processing instructions, and the DOCTYPE, outside the root.
    void skipMisc() {
        while (true) {
            skipSpace();
            if (startsWith("<?")) {
                skipPast("?>", "processing instruction");
            } else if (startsWith("<!--")) {
                skipPast("-->", "comment");
            } else if (startsWith("<!DOCTYPE")) {
                skipDoctype();
            } else {
                return;
            }
        }
    }

    static bool isNameChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':';
    }

    std::string name() {
        const std::size_t start = pos_;
        while (!atEnd() && isNameChar(peek())) {
            ++pos_;
        }
        return std::string(in_.substr(start, pos_ - start));
    }

    // Reads attributes up to the end of the start tag, and checks each has a quoted value.
    void attributes() {
        while (true) {
            skipSpace();
            if (atEnd()) {
                fail("unterminated tag");
            }
            if (peek() == '>' || peek() == '/') {
                return;
            }
            const std::string attribute = name();
            if (attribute.empty()) {
                fail("malformed tag");
            }
            skipSpace();
            if (atEnd() || peek() != '=') {
                fail("attribute " + attribute + " has no value");
            }
            ++pos_;
            skipSpace();
            if (atEnd() || (peek() != '"' && peek() != '\'')) {
                fail("attribute " + attribute + " has no quoted value");
            }
            const char quote = peek();
            const std::size_t close = in_.find(quote, pos_ + 1);
            if (close == std::string_view::npos) {
                fail("unterminated attribute " + attribute);
            }
            pos_ = close + 1;
        }
    }

    Element element(std::size_t depth) {
        if (depth > kMaxDepth) {
            fail("the elements are nested more than " + std::to_string(kMaxDepth) + " deep");
        }
        Element e;
        e.line = lineAt(pos_);
        ++pos_;  // the '<'
        e.name = name();
        if (e.name.empty()) {
            fail("expected an element name");
        }
        attributes();
        if (startsWith("/>")) {
            pos_ += 2;
            return e;
        }
        if (atEnd() || peek() != '>') {
            fail("malformed tag <" + e.name + ">");
        }
        ++pos_;
        content(e, depth);
        return e;
    }

    void content(Element& e, std::size_t depth) {
        while (true) {
            if (atEnd()) {
                fail("<" + e.name + "> is never closed");
            }
            if (startsWith("</")) {
                pos_ += 2;
                const std::string close = name();
                skipSpace();
                if (atEnd() || peek() != '>') {
                    fail("malformed closing tag </" + close + ">");
                }
                ++pos_;
                if (close != e.name) {
                    fail("</" + close + "> closes <" + e.name + ">");
                }
                return;
            }
            if (startsWith("<!--")) {
                skipPast("-->", "comment");
            } else if (startsWith("<?")) {
                skipPast("?>", "processing instruction");
            } else if (startsWith("<![CDATA[")) {
                pos_ += 9;  // the length of "<![CDATA["
                const std::size_t close = in_.find("]]>", pos_);
                if (close == std::string_view::npos) {
                    fail("unterminated CDATA section");
                }
                e.text.append(in_.substr(pos_, close - pos_));
                pos_ = close + 3;
            } else if (peek() == '<') {
                e.children.push_back(element(depth + 1));
            } else {
                const std::size_t stop = std::min(in_.find('<', pos_), in_.size());
                e.text += decode(in_.substr(pos_, stop - pos_));
                pos_ = stop;
            }
        }
    }

    std::string decode(std::string_view raw) {
        std::string out;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') {
                out.push_back(raw[i]);
                continue;
            }
            const std::size_t semi = raw.find(';', i);
            if (semi == std::string_view::npos) {
                fail("a & is not followed by a ;");
            }
            const std::string_view entity = raw.substr(i + 1, semi - i - 1);
            if (entity == "amp") {
                out.push_back('&');
            } else if (entity == "lt") {
                out.push_back('<');
            } else if (entity == "gt") {
                out.push_back('>');
            } else if (entity == "quot") {
                out.push_back('"');
            } else if (entity == "apos") {
                out.push_back('\'');
            } else if (entity.size() > 1 && entity[0] == '#') {
                appendUtf8(out, characterReference(entity.substr(1)));
            } else {
                fail("unknown entity &" + std::string(entity) + ";");
            }
            i = semi;
        }
        return out;
    }

    // The code point of "x41" or "65" (the text after the '#').
    std::uint32_t characterReference(std::string_view digits) {
        const bool hex = digits[0] == 'x';
        const std::string_view body = hex ? digits.substr(1) : digits;
        if (body.empty()) {
            fail("an empty character reference");
        }
        std::uint32_t value = 0;
        for (const char c : body) {
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (hex && c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (hex && c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("bad character reference &" + std::string(digits) + ";");
            }
            value = value * (hex ? 16 : 10) + digit;
            if (value > 0x10FFFF) {
                fail("character reference &" + std::string(digits) + "; is out of range");
            }
        }
        return value;
    }
};

// ---- BeerXML names -----------------------------------------------------------

// A value as BeerXML writes it, and the BeerJSON or Kettle Logic name it maps to.
struct Choice {
    std::string_view beerXml;
    std::string_view kettle;
};

constexpr std::array<Choice, 3> kRecipeTypes = {{
    {"All Grain", "all grain"}, {"Partial Mash", "partial mash"}, {"Extract", "extract"}}};

constexpr std::array<Choice, 5> kFermentableTypes = {{
    {"Grain", "grain"}, {"Sugar", "sugar"}, {"Extract", "extract"}, {"Dry Extract", "dry extract"},
    {"Adjunct", "other"}}};

constexpr std::array<Choice, 5> kHopUses = {{
    {"Boil", "add_to_boil"}, {"Aroma", "add_to_boil"}, {"First Wort", "add_to_boil"},
    {"Dry Hop", "add_to_fermentation"}, {"Mash", "add_to_mash"}}};

constexpr std::array<Choice, 3> kHopForms = {{{"Pellet", "pellet"}, {"Plug", "plug"}, {"Leaf", "leaf"}}};

constexpr std::array<Choice, 6> kMiscellaneousTypes = {{
    {"Spice", "spice"}, {"Fining", "fining"}, {"Water Agent", "water agent"}, {"Herb", "herb"},
    {"Flavor", "flavor"}, {"Other", "other"}}};

constexpr std::array<Choice, 5> kMiscellaneousUses = {{
    {"Boil", "add_to_boil"}, {"Mash", "add_to_mash"}, {"Primary", "add_to_fermentation"},
    {"Secondary", "add_to_fermentation"}, {"Bottling", "add_to_package"}}};

constexpr std::array<Choice, 6> kCultureTypes = {{
    {"Ale", "ale"}, {"Lager", "lager"}, {"Wheat", "wheat"}, {"Wine", "wine"},
    {"Champagne", "champagne"}, {"Other", "other"}}};

constexpr std::array<Choice, 4> kCultureForms = {{
    {"Liquid", "liquid"}, {"Dry", "dry"}, {"Slant", "slant"}, {"Culture", "culture"}}};

constexpr std::array<Choice, 3> kMashStepTypes = {{
    {"Infusion", "infusion"}, {"Temperature", "temperature"}, {"Decoction", "decoction"}}};

// The entry whose BeerXML name matches `text`, ignoring case, or nothing.
template <std::size_t N>
std::optional<std::string_view> lookup(std::string_view text, const std::array<Choice, N>& table) {
    const std::string wanted = lowered(trimmed(text));
    for (const Choice& choice : table) {
        if (lowered(choice.beerXml) == wanted) {
            return choice.kettle;
        }
    }
    return std::nullopt;
}

// Percent, as BeerXML writes it: 75 is 75 %.
Quantity percent(const Rational& value) {
    return Quantity::fromUnit(value, Unit::Percent);
}

// ---- The mapping -------------------------------------------------------------

// One record in a list, with the path that says where it is, for diagnostics.
struct Item {
    const Element* element;
    std::string path;
};

// Maps the parsed XML onto the model. Each record is read with the rules in
// docs/formats/beerxml-import.md: a missing required field fails the import, and a
// missing optional field or an unmapped tag is noted and the read goes on.
class Importer {
public:
    explicit Importer(Document& document) : document_(document) {}

    void root(const Element& e) {
        if (e.name == "RECIPE") {
            recipe(e, "RECIPE");
            return;
        }
        if (e.name != "RECIPES") {
            failAt(e, "the root must be <RECIPES> or <RECIPE>, not <" + e.name + ">");
        }
        for (const Item& item : items(e, "RECIPE", "RECIPES")) {
            recipe(*item.element, item.path);
        }
        if (document_.recipes.empty()) {
            warn("RECIPES", "the file has no recipes");
        }
    }

private:
    Document& document_;

    // ---- Diagnostics and lookups ----

    void warn(std::string pointer, std::string message) {
        document_.diagnostics.push_back(model::Diagnostic{std::move(pointer), std::move(message)});
    }

    [[noreturn]] static void failAt(const Element& e, const std::string& message) {
        throw BeerXmlError("line " + std::to_string(e.line) + ": <" + e.name + "> " + message);
    }

    static std::string childPath(const std::string& path, std::string_view tag) {
        return path + "/" + std::string(tag);
    }

    static const Element* firstChild(const Element& e, std::string_view tag) {
        for (const Element& child : e.children) {
            if (child.name == tag) {
                return &child;
            }
        }
        return nullptr;
    }

    // The trimmed text of a child, or nothing when the child is absent or blank.
    static std::optional<std::string> optionalText(const Element& e, std::string_view tag) {
        const Element* child = firstChild(e, tag);
        if (child == nullptr) {
            return std::nullopt;
        }
        std::string text = trimmed(child->text);
        if (text.empty()) {
            return std::nullopt;
        }
        return text;
    }

    std::string requiredText(const Element& e, std::string_view tag) {
        if (const auto text = optionalText(e, tag)) {
            return *text;
        }
        failAt(e, "has no <" + std::string(tag) + ">");
    }

    static Rational parseNumber(const Element& child) {
        const std::string text = trimmed(child.text);
        try {
            return Rational::fromDecimal(text);
        } catch (const std::exception&) {
            failAt(child, "\"" + text + "\" is not a number");
        }
    }

    static std::optional<Rational> optNumber(const Element& e, std::string_view tag) {
        const Element* child = firstChild(e, tag);
        if (child == nullptr || trimmed(child->text).empty()) {
            return std::nullopt;
        }
        return parseNumber(*child);
    }

    static Rational requiredNumber(const Element& e, std::string_view tag) {
        if (const auto value = optNumber(e, tag)) {
            return *value;
        }
        failAt(e, "has no <" + std::string(tag) + ">");
    }

    // A flag: TRUE or FALSE. Absent means FALSE, which is what BeerXML means by it.
    bool flag(const Element& e, std::string_view tag, const std::string& path) {
        const auto value = optionalText(e, tag);
        if (!value) {
            return false;
        }
        const std::string word = lowered(*value);
        if (word == "true") {
            return true;
        }
        if (word != "false") {
            warn(childPath(path, tag), "\"" + *value + "\" is not TRUE or FALSE; using FALSE");
        }
        return false;
    }

    // The value of an enumerated tag. A missing or unknown value uses `fallback`, and is noted.
    template <std::size_t N>
    std::string_view choice(const Element& e, std::string_view tag, const std::array<Choice, N>& table,
                            std::string_view fallback, const std::string& path) {
        const auto value = optionalText(e, tag);
        if (!value) {
            warn(childPath(path, tag), "is missing; using \"" + std::string(fallback) + "\"");
            return fallback;
        }
        if (const auto mapped = lookup(*value, table)) {
            return *mapped;
        }
        warn(childPath(path, tag), "\"" + *value + "\" is not a known value; using \"" + std::string(fallback) + "\"");
        return fallback;
    }

    // An enumerated tag that may be absent. Absent or unknown gives nothing.
    template <std::size_t N>
    std::optional<std::string_view> optChoice(const Element& e, std::string_view tag,
                                              const std::array<Choice, N>& table, const std::string& path) {
        const auto value = optionalText(e, tag);
        if (!value) {
            return std::nullopt;
        }
        if (const auto mapped = lookup(*value, table)) {
            return mapped;
        }
        warn(childPath(path, tag), "\"" + *value + "\" is not a known value, so it was left out");
        return std::nullopt;
    }

    // Notes each child whose tag is not in `known`. Those tags are skipped.
    void checkKnown(const Element& e, const std::string& path, std::initializer_list<std::string_view> known) {
        for (const Element& child : e.children) {
            if (std::find(known.begin(), known.end(), std::string_view(child.name)) == known.end()) {
                warn(childPath(path, child.name), "is not read by Kettle Logic and was skipped");
            }
        }
    }

    // The records of a list, each with its path. A child that is not `tag` is noted and skipped.
    std::vector<Item> items(const Element& list, std::string_view tag, const std::string& listPath) {
        std::vector<Item> out;
        for (const Element& child : list.children) {
            if (child.name == tag) {
                out.push_back(Item{&child, childPath(listPath, tag) + "[" + std::to_string(out.size()) + "]"});
            } else {
                warn(childPath(listPath, child.name), "is not a <" + std::string(tag) + ">, so it was skipped");
            }
        }
        return out;
    }

    // ---- Times and amounts ----

    // BeerJSON times are whole numbers, so a fraction of a minute is written in seconds.
    Quantity minutes(const Rational& value, const std::string& path) {
        if (value.isInteger()) {
            return Quantity::fromUnit(value, Unit::Minute);
        }
        const Rational seconds = value * Rational(60);
        if (!seconds.isInteger()) {
            warn(path, "has a fraction of a second, which is rounded to a whole second");
        }
        return Quantity::fromUnit(Rational(std::llround(seconds.toDouble())), Unit::Second);
    }

    std::optional<Quantity> optMinutes(const Element& e, std::string_view tag, const std::string& path) {
        const auto value = optNumber(e, tag);
        if (!value) {
            return std::nullopt;
        }
        return minutes(*value, childPath(path, tag));
    }

    Quantity requiredMinutes(const Element& e, std::string_view tag, const std::string& path) {
        return minutes(requiredNumber(e, tag), childPath(path, tag));
    }

    // AMOUNT is in kilograms when AMOUNT_IS_WEIGHT is TRUE, and in liters otherwise.
    Quantity weightOrVolume(const Element& e, const std::string& path) {
        const Rational amount = requiredNumber(e, "AMOUNT");
        return flag(e, "AMOUNT_IS_WEIGHT", path) ? Quantity::fromUnit(amount, Unit::Kilogram)
                                                 : Quantity::fromUnit(amount, Unit::Liter);
    }

    Quantity ion(const Element& e, std::string_view tag, const std::string& path) {
        if (const auto value = optNumber(e, tag)) {
            return Quantity::fromUnit(*value, Unit::MilligramPerLiter);
        }
        warn(childPath(path, tag), "is missing; using 0 mg/L");
        return Quantity::fromUnit(Rational(0), Unit::MilligramPerLiter);
    }

    // ---- Records ----

    void recipe(const Element& e, const std::string& path) {
        // EST_* and IBU are Kettle Logic's own estimates, worked out from the ingredients, so
        // they are recognized and not read.
        checkKnown(e, path, {"VERSION", "NAME", "TYPE", "BREWER", "BATCH_SIZE", "BOIL_SIZE", "BOIL_TIME",
                             "EFFICIENCY", "NOTES", "OG", "FG", "FERMENTABLES", "HOPS", "MISCS", "YEASTS",
                             "WATERS", "MASH", "EST_OG", "EST_FG", "EST_ABV", "EST_COLOR", "IBU"});

        Document& document = document_;
        model::Recipe r;
        r.id = document.allocateId();
        r.uuid = Uuid::generate();
        r.name = requiredText(e, "NAME");
        r.type = *model::recipeTypeFromName(choice(e, "TYPE", kRecipeTypes, "all grain", path));
        r.author = optionalText(e, "BREWER").value_or("");
        r.batchSize = Quantity::fromUnit(requiredNumber(e, "BATCH_SIZE"), Unit::Liter);
        r.efficiency.brewhouse = brewhouseEfficiency(e, path);
        r.notes = optionalText(e, "NOTES");
        if (const auto og = optNumber(e, "OG")) {
            r.originalGravity = Measure::fromUnit(*og, "sg");
        }
        if (const auto fg = optNumber(e, "FG")) {
            r.finalGravity = Measure::fromUnit(*fg, "sg");
        }

        if (const auto boilTime = optMinutes(e, "BOIL_TIME", path)) {
            model::Boil boil;
            boil.boilTime = *boilTime;
            if (const auto preBoil = optNumber(e, "BOIL_SIZE")) {
                boil.preBoilSize = Quantity::fromUnit(*preBoil, Unit::Liter);
            }
            r.boil = boil;
        } else if (firstChild(e, "BOIL_SIZE") != nullptr) {
            warn(childPath(path, "BOIL_SIZE"), "is left out, because there is no BOIL_TIME");
        }

        if (const Element* list = firstChild(e, "FERMENTABLES")) {
            for (const Item& item : items(*list, "FERMENTABLE", childPath(path, "FERMENTABLES"))) {
                r.fermentables.push_back(fermentable(*item.element, item.path));
            }
        }
        if (const Element* list = firstChild(e, "HOPS")) {
            r.hops.emplace();
            for (const Item& item : items(*list, "HOP", childPath(path, "HOPS"))) {
                r.hops->push_back(hop(*item.element, item.path));
            }
        }
        if (const Element* list = firstChild(e, "MISCS")) {
            r.miscellaneous.emplace();
            for (const Item& item : items(*list, "MISC", childPath(path, "MISCS"))) {
                r.miscellaneous->push_back(miscellaneous(*item.element, item.path));
            }
        }
        if (const Element* list = firstChild(e, "YEASTS")) {
            r.cultures.emplace();
            for (const Item& item : items(*list, "YEAST", childPath(path, "YEASTS"))) {
                r.cultures->push_back(culture(*item.element, item.path));
            }
        }
        if (const Element* list = firstChild(e, "WATERS")) {
            r.waters.emplace();
            for (const Item& item : items(*list, "WATER", childPath(path, "WATERS"))) {
                r.waters->push_back(water(*item.element, item.path));
            }
        }
        if (const Element* mash = firstChild(e, "MASH")) {
            r.mash = mashProcedure(*mash, childPath(path, "MASH"));
        }

        document.recipes.push_back(std::move(r));
    }

    Quantity brewhouseEfficiency(const Element& e, const std::string& path) {
        if (const auto value = optNumber(e, "EFFICIENCY")) {
            return percent(*value);
        }
        warn(childPath(path, "EFFICIENCY"), "is missing; using 75 %");
        return percent(Rational(75));
    }

    model::Fermentable fermentable(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "TYPE", "AMOUNT", "YIELD", "COLOR", "ORIGIN", "SUPPLIER"});
        model::Fermentable f;
        f.id = document_.allocateId();
        f.uuid = Uuid::generate();
        f.name = requiredText(e, "NAME");
        f.type = std::string(choice(e, "TYPE", kFermentableTypes, "other", path));
        f.origin = optionalText(e, "ORIGIN");
        f.producer = optionalText(e, "SUPPLIER");
        f.amount = Quantity::fromUnit(requiredNumber(e, "AMOUNT"), Unit::Kilogram);

        // BeerXML gives the yield of the grain as fine grind. When it is missing, sugars and
        // extracts yield all of their weight, and other fermentables are taken as 75 %.
        const bool extract = f.type == "sugar" || f.type == "extract" || f.type == "dry extract";
        if (const auto yield = optNumber(e, "YIELD")) {
            f.yield.fineGrind = percent(*yield);
        } else {
            warn(childPath(path, "YIELD"), std::string("is missing; using ") + (extract ? "100" : "75") + " %");
            f.yield.fineGrind = percent(Rational(extract ? 100 : 75));
        }

        // BeerXML gives a fermentable's color in degrees Lovibond, or in SRM for a liquid extract.
        const std::string_view colorUnit = f.type == "extract" ? "SRM" : "Lovi";
        if (const auto color = optNumber(e, "COLOR")) {
            f.color = Measure::fromUnit(*color, colorUnit);
        } else {
            warn(childPath(path, "COLOR"), "is missing; using 0 " + std::string(colorUnit));
            f.color = Measure::fromUnit(Rational(0), colorUnit);
        }
        return f;
    }

    model::Hop hop(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "ALPHA", "AMOUNT", "USE", "TIME", "FORM", "BETA", "ORIGIN"});
        model::Hop h;
        h.id = document_.allocateId();
        h.uuid = Uuid::generate();
        h.name = requiredText(e, "NAME");
        h.alphaAcid = percent(requiredNumber(e, "ALPHA"));
        if (const auto beta = optNumber(e, "BETA")) {
            h.betaAcid = percent(*beta);
        }
        h.origin = optionalText(e, "ORIGIN");
        if (const auto form = optChoice(e, "FORM", kHopForms, path)) {
            h.form = std::string(*form);
        }
        h.amount = Quantity::fromUnit(requiredNumber(e, "AMOUNT"), Unit::Kilogram);
        h.timing.use = std::string(choice(e, "USE", kHopUses, "add_to_boil", path));
        h.timing.time = optMinutes(e, "TIME", path);
        return h;
    }

    model::Miscellaneous miscellaneous(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "TYPE", "USE", "TIME", "AMOUNT", "AMOUNT_IS_WEIGHT"});
        model::Miscellaneous m;
        m.id = document_.allocateId();
        m.uuid = Uuid::generate();
        m.name = requiredText(e, "NAME");
        m.type = std::string(choice(e, "TYPE", kMiscellaneousTypes, "other", path));
        m.amount = weightOrVolume(e, path);
        const auto use = optChoice(e, "USE", kMiscellaneousUses, path);
        const auto time = optMinutes(e, "TIME", path);
        if (use || time) {
            model::Timing timing;
            if (use) {
                timing.use = std::string(*use);
            }
            timing.time = time;
            m.timing = timing;
        }
        return m;
    }

    model::Culture culture(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "TYPE", "FORM", "AMOUNT", "AMOUNT_IS_WEIGHT", "ATTENUATION",
                             "LABORATORY", "PRODUCT_ID", "TIMES_CULTURED"});
        model::Culture c;
        c.id = document_.allocateId();
        c.uuid = Uuid::generate();
        c.name = requiredText(e, "NAME");
        c.type = std::string(choice(e, "TYPE", kCultureTypes, "other", path));
        c.form = std::string(choice(e, "FORM", kCultureForms, "liquid", path));
        c.producer = optionalText(e, "LABORATORY");
        c.productId = optionalText(e, "PRODUCT_ID");
        if (const auto attenuation = optNumber(e, "ATTENUATION")) {
            c.attenuation = percent(*attenuation);
        }
        if (const auto times = optNumber(e, "TIMES_CULTURED")) {
            if (times->isInteger()) {
                c.timesCultured = static_cast<long long>(times->toDouble());
            } else {
                warn(childPath(path, "TIMES_CULTURED"), "is not a whole number, so it was left out");
            }
        }
        c.amount = weightOrVolume(e, path);
        return c;
    }

    model::WaterAddition water(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "AMOUNT", "CALCIUM", "BICARBONATE", "SULFATE", "CHLORIDE",
                             "SODIUM", "MAGNESIUM"});
        model::WaterAddition w;
        w.id = document_.allocateId();
        w.uuid = Uuid::generate();
        w.water.name = requiredText(e, "NAME");
        if (const auto amount = optNumber(e, "AMOUNT")) {
            w.amount = Quantity::fromUnit(*amount, Unit::Liter);
        }
        w.water.calcium = ion(e, "CALCIUM", path);
        w.water.bicarbonate = ion(e, "BICARBONATE", path);
        w.water.sulfate = ion(e, "SULFATE", path);
        w.water.chloride = ion(e, "CHLORIDE", path);
        w.water.sodium = ion(e, "SODIUM", path);
        w.water.magnesium = ion(e, "MAGNESIUM", path);
        return w;
    }

    model::Mash mashProcedure(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "GRAIN_TEMP", "NOTES", "MASH_STEPS"});
        model::Mash m;
        m.name = requiredText(e, "NAME");
        m.notes = optionalText(e, "NOTES");
        if (const auto grain = optNumber(e, "GRAIN_TEMP")) {
            m.grainTemperature = Quantity::fromUnit(*grain, Unit::Celsius);
        } else {
            warn(childPath(path, "GRAIN_TEMP"), "is missing; using 20 C");
            m.grainTemperature = Quantity::fromUnit(Rational(20), Unit::Celsius);
        }
        if (const Element* steps = firstChild(e, "MASH_STEPS")) {
            for (const Item& item : items(*steps, "MASH_STEP", childPath(path, "MASH_STEPS"))) {
                m.steps.push_back(mashStep(*item.element, item.path));
            }
        }
        return m;
    }

    model::MashStep mashStep(const Element& e, const std::string& path) {
        checkKnown(e, path, {"VERSION", "NAME", "TYPE", "STEP_TEMP", "STEP_TIME", "RAMP_TIME", "INFUSE_AMOUNT",
                             "END_TEMP"});
        model::MashStep s;
        s.name = requiredText(e, "NAME");
        s.type = std::string(choice(e, "TYPE", kMashStepTypes, "infusion", path));
        s.stepTemperature = Quantity::fromUnit(requiredNumber(e, "STEP_TEMP"), Unit::Celsius);
        s.stepTime = requiredMinutes(e, "STEP_TIME", path);
        s.rampTime = optMinutes(e, "RAMP_TIME", path);
        if (const auto amount = optNumber(e, "INFUSE_AMOUNT")) {
            s.amount = Quantity::fromUnit(*amount, Unit::Liter);
        }
        if (const auto end = optNumber(e, "END_TEMP")) {
            s.endTemperature = Quantity::fromUnit(*end, Unit::Celsius);
        }
        return s;
    }
};

}  // namespace

Document importBeerXml(std::string_view text) {
    const std::string utf8 = toUtf8(text);
    const Element root = XmlParser(utf8).document();
    Document document;
    Importer(document).root(root);
    return document;
}

}  // namespace kettlelogic::io
