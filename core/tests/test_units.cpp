#include "check.hpp"

#include "kettlelogic/measure.hpp"
#include "kettlelogic/model/schema.hpp"
#include "kettlelogic/quantity.hpp"
#include "kettlelogic/rational.hpp"
#include "kettlelogic/crypto/sha256.hpp"
#include "kettlelogic/uuid.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace kettlelogic::test {

namespace {

using kettlelogic::Dimension;
using kettlelogic::Measure;
using kettlelogic::Quantity;
using kettlelogic::Rational;
using kettlelogic::Unit;

void testDecimalParsing() {
    CHECK(Rational::fromDecimal("1.1") == Rational(11, 10));
    CHECK(Rational::fromDecimal("-12.50") == Rational(-25, 2));
    CHECK(Rational::fromDecimal("3e-2") == Rational(3, 100));
    CHECK(Rational::fromDecimal("1.5E2") == Rational(150));
    CHECK(Rational::fromDecimal("+7") == Rational(7));
    CHECK(throws([] { Rational::fromDecimal(""); }));
    CHECK(throws([] { Rational::fromDecimal("1.2.3"); }));
    CHECK(throws([] { Rational::fromDecimal("1e"); }));
}

void testToFixed() {
    CHECK(Rational(1).toFixed(2) == "1.00");
    CHECK(Rational(1, 8).toFixed(2) == "0.13");  // half rounds away from zero
    CHECK(Rational(-1, 8).toFixed(2) == "-0.13");
    CHECK(Rational(1, 3).toFixed(4) == "0.3333");
    CHECK(Rational(2, 3).toFixed(0) == "1");
    CHECK(Rational(-1, 3).toFixed(0) == "0");  // no "-0"
    CHECK(Rational(5, 100).toFixed(2) == "0.05");
    CHECK(Rational(125).toFixed(0) == "125");
}

void testToDecimal() {
    CHECK(Rational::fromDecimal("1.1").toDecimal() == "1.1");
    CHECK(Rational::fromDecimal("1.10").toDecimal() == "1.1");
    CHECK(Rational(16).toDecimal() == "16");
    CHECK(Rational(1, 8).toDecimal() == "0.125");
    CHECK(Rational(-5, 2).toDecimal() == "-2.5");
    CHECK(Rational(0).toDecimal() == "0");
    // A repeating decimal is rounded to 12 places with trailing zeros removed.
    CHECK(Rational(1, 3).toDecimal() == "0.333333333333");
    CHECK(Rational(2, 1).toDecimal() == "2");
}

void testUnitTable() {
    // Every BeerJSON name resolves, and back again.
    const char* names[] = {"mg",  "g",    "kg",   "lb",    "oz",     "ml",    "l",     "tsp",
                           "tbsp", "floz", "cup", "pt",    "qt",     "gal",   "bbl",   "ifloz",
                           "ipt", "iqt",  "igal", "ibbl",  "C",      "F",     "sec",   "min",
                           "hr",  "day",  "week", "%",     "ppm",    "ppb",   "mg/l",  "1",
                           "unit", "each", "dimensionless", "pkg"};
    for (const char* name : names) {
        auto unit = kettlelogic::unitFromBeerJson(name);
        CHECK(unit.has_value());
        if (unit) {
            CHECK(kettlelogic::beerJsonName(*unit) == name);
        }
    }
    CHECK(!kettlelogic::unitFromBeerJson("stone").has_value());
}

void testExactConversions() {
    CHECK(Quantity::fromUnit(1, Unit::Pound).valueIn(Unit::Ounce) == Rational(16));
    CHECK(Quantity::fromUnit(16, Unit::Ounce).valueIn(Unit::Pound) == Rational(1));
    CHECK(Quantity::fromUnit(1, Unit::Pound).valueIn(Unit::Kilogram) ==
          Rational::fromDecimal("0.45359237"));
    CHECK(Quantity::fromUnit(1, Unit::Gallon).valueIn(Unit::FluidOunce) == Rational(128));
    CHECK(Quantity::fromUnit(1, Unit::ImperialGallon).valueIn(Unit::ImperialFluidOunce) == Rational(160));
    CHECK(Quantity::fromUnit(1, Unit::Teaspoon).valueIn(Unit::Milliliter) ==
          Rational::fromDecimal("4.92892159375"));
    // US beer barrel is 31 US gallons.
    CHECK(Quantity::fromUnit(1, Unit::Barrel).valueIn(Unit::Gallon) == Rational(31));
}

void testTemperature() {
    CHECK(Quantity::fromUnit(212, Unit::Fahrenheit).valueIn(Unit::Celsius) == Rational(100));
    CHECK(Quantity::fromUnit(68, Unit::Fahrenheit).valueIn(Unit::Celsius) == Rational(20));
    CHECK(Quantity::fromUnit(100, Unit::Celsius).valueIn(Unit::Fahrenheit) == Rational(212));
    CHECK(Quantity::fromUnit(-40, Unit::Fahrenheit).valueIn(Unit::Celsius) == Rational(-40));
}

void testTimePercentConcentrationCount() {
    CHECK(Quantity::fromUnit(1, Unit::Hour).valueIn(Unit::Minute) == Rational(60));
    CHECK(Quantity::fromUnit(2, Unit::Week).valueIn(Unit::Day) == Rational(14));
    CHECK(Quantity::fromUnit(72, Unit::Percent).valueIn(Unit::Percent) == Rational(72));
    // Canonical fraction: 72 % is 72/100 = 18/25.
    CHECK(Quantity::fromUnit(72, Unit::Percent).canonical() == Rational(18, 25));
    CHECK(Quantity::fromUnit(1, Unit::PartsPerBillion).valueIn(Unit::MilligramPerLiter) ==
          Rational::fromDecimal("0.001"));
    CHECK(Quantity::fromUnit(3, Unit::CountPackage).valueIn(Unit::CountEach) == Rational(3));
    // Count and concentration are different dimensions.
    CHECK(throws([] { Quantity::fromUnit(1, Unit::CountOne).valueIn(Unit::MilligramPerLiter); }));
}

// The bug this design exists to prevent: a value drifting through conversions.
void testNoDrift() {
    const Rational original = Rational::fromDecimal("1.1");
    Quantity q = Quantity::fromUnit(original, Unit::Pound);
    Rational back = q.valueIn(Unit::Kilogram);
    back = Quantity::fromUnit(back, Unit::Kilogram).valueIn(Unit::Ounce);
    back = Quantity::fromUnit(back, Unit::Ounce).valueIn(Unit::Pound);
    CHECK(back == original);
    CHECK(back.toFixed(2) == "1.10");

    // Displayed value of exactly one pound never becomes 0.99999...
    CHECK(Quantity::fromUnit(16, Unit::Ounce).valueIn(Unit::Pound).toFixed(2) == "1.00");
}

void testEveryConvertibleUnitRoundTrips() {
    const Unit all[] = {
        Unit::Milligram,         Unit::Gram,           Unit::Kilogram,       Unit::Pound,
        Unit::Ounce,             Unit::Milliliter,     Unit::Liter,          Unit::Teaspoon,
        Unit::Tablespoon,        Unit::FluidOunce,     Unit::Cup,            Unit::Pint,
        Unit::Quart,             Unit::Gallon,         Unit::Barrel,         Unit::ImperialFluidOunce,
        Unit::ImperialPint,      Unit::ImperialQuart,  Unit::ImperialGallon, Unit::ImperialBarrel,
        Unit::Celsius,           Unit::Fahrenheit,     Unit::Second,         Unit::Minute,
        Unit::Hour,              Unit::Day,            Unit::Week,           Unit::Percent,
        Unit::PartsPerMillion,   Unit::PartsPerBillion, Unit::MilligramPerLiter, Unit::CountOne,
        Unit::CountUnit,         Unit::CountEach,      Unit::CountDimensionless, Unit::CountPackage};
    const Rational samples[] = {Rational::fromDecimal("1.1"), Rational(37, 3), Rational(-5), Rational(0)};
    for (Unit unit : all) {
        for (const Rational& v : samples) {
            CHECK(Quantity::fromUnit(v, unit).valueIn(unit) == v);
        }
    }
}

// Every unit name in the schema's convertible unit enums must be known to the
// unit table, so a schema update that adds a unit cannot go unnoticed.
void testSchemaUnitsAreKnown() {
    const std::span<const std::string_view> convertible[] = {
        schema::kMassUnits,  schema::kVolumeUnits,        schema::kTemperatureUnits, schema::kTimeUnits,
        schema::kPercentUnits, schema::kConcentrationUnits, schema::kCountUnits};
    std::size_t total = 0;
    for (const auto& names : convertible) {
        for (const std::string_view name : names) {
            const auto unit = kettlelogic::unitFromBeerJson(name);
            CHECK(unit.has_value());
            if (unit) {
                CHECK(kettlelogic::beerJsonName(*unit) == name);
            }
            ++total;
        }
    }
    CHECK(total == 36);  // the unit table's convertible units; update with the table

    // The non-convertible units are not in the table, but Measure must accept them.
    const std::span<const std::string_view> opaque[] = {
        schema::kGravityUnits, schema::kColorUnits, schema::kAcidityUnits,
        schema::kPressureUnits, schema::kSpecificVolumeUnits, schema::kBitternessUnits};
    for (const auto& names : opaque) {
        for (const std::string_view name : names) {
            CHECK(isOpaqueUnit(name));
            CHECK(!kettlelogic::unitFromBeerJson(name).has_value());
        }
    }
}

void testDimensionChecks() {
    CHECK(kettlelogic::dimensionOf(Unit::Ounce) == Dimension::Mass);
    CHECK(kettlelogic::dimensionOf(Unit::Gallon) == Dimension::Volume);
    CHECK(kettlelogic::dimensionOf(Unit::Fahrenheit) == Dimension::Temperature);
    CHECK(kettlelogic::dimensionOf(Unit::Day) == Dimension::Time);
    CHECK(throws([] { Quantity::fromUnit(1, Unit::Pound).valueIn(Unit::Liter); }));
    CHECK(throws([] {
        auto sum = Quantity::fromUnit(1, Unit::Pound) + Quantity::fromUnit(1, Unit::Liter);
        (void)sum;
    }));
}

void testArithmeticKeepsAuthoredUnit() {
    Quantity pound = Quantity::fromUnit(1, Unit::Pound);
    Quantity ounces = Quantity::fromUnit(8, Unit::Ounce);
    CHECK((pound + ounces).valueIn(Unit::Ounce) == Rational(24));
    CHECK((pound - ounces).valueIn(Unit::Ounce) == Rational(8));
    CHECK((pound + ounces).unit() == Unit::Pound);
}

void testMeasure() {
    CHECK(isOpaqueUnit("sg"));
    CHECK(isOpaqueUnit("qt/lb"));
    CHECK(!isOpaqueUnit("lb"));

    Measure gravity = Measure::fromUnit(Rational::fromDecimal("1.050"), "sg");
    CHECK(gravity.value() == Rational::fromDecimal("1.05"));
    CHECK(gravity.unit() == "sg");
    CHECK(throws([] { Measure::fromUnit(1, "lb"); }));
    CHECK(throws([] { Measure::fromUnit(1, "stone"); }));
}

}  // namespace

void testUuid() {
    using kettlelogic::Uuid;
    const Uuid a = Uuid::generate();
    const Uuid b = Uuid::generate();
    CHECK(a != b);
    CHECK(!a.isNil());
    CHECK(Uuid{}.isNil());

    const std::string text = a.toString();
    CHECK(text.size() == 36);
    CHECK(text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-');
    CHECK(text[14] == '7');                              // version 7
    CHECK(std::string("89ab").find(text[19]) != std::string::npos);  // RFC variant

    const auto parsed = Uuid::parse(text);
    CHECK(parsed.has_value() && *parsed == a);
    CHECK(Uuid::parse(std::string(text).substr(0, 35)) == std::nullopt);
    CHECK(Uuid::parse("0195e6f0x3c4a7d2b9a1f6c3e8b2d4f50") == std::nullopt);
    CHECK(Uuid::parse("zzzzzzzz-3c4a-7d2b-9a1f-6c3e8b2d4f50") == std::nullopt);
    const auto upper = Uuid::parse("0195E6F0-3C4A-7D2B-9A1F-6C3E8B2D4F50");
    CHECK(upper.has_value() && upper->toString() == "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f50");
}

// FIPS 180-4 test vectors: empty input, "abc", a two-block message, and a
// message whose padding spills into a second block.
void testSha256() {
    using kettlelogic::crypto::sha256Hex;
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256Hex(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

void runUnitTests() {
    testDecimalParsing();
    testToFixed();
    testToDecimal();
    testUnitTable();
    testExactConversions();
    testTemperature();
    testTimePercentConcentrationCount();
    testNoDrift();
    testEveryConvertibleUnitRoundTrips();
    testSchemaUnitsAreKnown();
    testDimensionChecks();
    testArithmeticKeepsAuthoredUnit();
    testMeasure();
    testUuid();
    testSha256();
}

}  // namespace kettlelogic::test
