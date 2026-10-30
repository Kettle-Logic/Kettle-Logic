#include "check.hpp"

#include "kettlelogic/json.hpp"

#include <stdexcept>
#include <string>

namespace kettlelogic::test {

namespace {

using kettlelogic::Rational;
using kettlelogic::json::parse;
using kettlelogic::json::ParseError;
using kettlelogic::json::serialize;
using kettlelogic::json::Value;

void testNumbersKeepTheirLexeme() {
    const Value v = parse(R"({"a": 1.10, "b": -0, "c": 2E+3})");
    CHECK(v.find("a")->numberLexeme() == "1.10");
    CHECK(v.find("a")->asRational() == Rational(11, 10));
    CHECK(v.find("b")->numberLexeme() == "-0");
    CHECK(v.find("c")->numberLexeme() == "2E+3");
    CHECK(v.find("c")->asRational() == Rational(2000));
}

void testRationalToJson() {
    CHECK(Value::fromRational(Rational(11, 10)).numberLexeme() == "1.1");
    CHECK(Value::fromRational(Rational(16)).numberLexeme() == "16");
    CHECK(Value::fromRational(Rational(-5, 2)).numberLexeme() == "-2.5");
}

void testStructure() {
    const Value v = parse(R"({"name": "Pale", "ok": true, "none": null, "list": [1, "two", {}]})");
    CHECK(v.isObject());
    CHECK(v.find("name")->asString() == "Pale");
    CHECK(v.find("ok")->asBool());
    CHECK(v.find("none")->isNull());
    CHECK(v.find("list")->asArray().size() == 3);
    CHECK(v.find("missing") == nullptr);
    CHECK(v.find("name")->find("anything") == nullptr);
}

void testStringEscapes() {
    const Value v = parse(R"("\u00e9\n\ud83c\udf7a\/\"")");
    CHECK(v.asString() == std::string("\xc3\xa9\n\xf0\x9f\x8d\xba/\""));
}

void testRejectsMalformed() {
    const char* bad[] = {
        "",           "{",         "[1,]",       R"({"a":1,})", "01",         "1.",
        "1e",         "+1",        ".5",         "NaN",         "1 2",        R"("\x")",
        "\"a\nb\"",   R"({"a":1,"a":2})",        R"("\ud800")", R"("\udc00")", "tru",
        "[1 2]",      R"({"a" 1})", "\"\\u12\""};
    for (const char* text : bad) {
        CHECK(throws([text] { parse(text); }));
    }
}

void testParseErrorLocation() {
    try {
        parse("{\n  \"a\": x\n}");
        CHECK(false);
    } catch (const ParseError& e) {
        CHECK(e.line() == 2);
        CHECK(e.column() == 8);
    }
}

void testDepthLimit() {
    std::string deep(600, '[');
    deep.append(600, ']');
    CHECK(throws([&] { parse(deep); }));
}

void testSerializeRoundTrip() {
    const std::string text = R"({"a": [1.10, {"b": "x\ty"}], "c": false})";
    const Value v = parse(text);
    CHECK(parse(serialize(v, -1)) == v);
    CHECK(parse(serialize(v, 2)) == v);
    CHECK(serialize(v, -1).find("1.10") != std::string::npos);  // lexeme kept on output
}

void testSerializeEscapesControlCharacters() {
    CHECK(serialize(Value::string(std::string("a\x01"
                                              "b")),
                    -1) == "\"a\\u0001b\"");
}

void testEquality() {
    CHECK(parse(R"({"a": 1, "b": 2})") == parse(R"({"b": 2, "a": 1})"));  // key order ignored
    CHECK(parse("1.0") == parse("1"));                                       // numbers compare by value
    CHECK(!(parse(R"({"a": 1})") == parse(R"({"a": 2})")));
}

void testObjectEditing() {
    Value v = Value::object(kettlelogic::json::Object{});
    v.set("a", Value::fromRational(Rational(1)));
    v.set("a", Value::fromRational(Rational(2)));
    CHECK(v.asObject().size() == 1);
    CHECK(v.find("a")->numberLexeme() == "2");
    CHECK(v.erase("a"));
    CHECK(!v.erase("a"));
    CHECK(throws([] { Value::string("x").set("k", Value{}); }));
    CHECK(throws([] { Value::number("1").asString(); }));
    CHECK(throws([] { Value::number("x"); }));
}

}  // namespace

void runJsonTests() {
    testNumbersKeepTheirLexeme();
    testRationalToJson();
    testStructure();
    testStringEscapes();
    testRejectsMalformed();
    testParseErrorLocation();
    testDepthLimit();
    testSerializeRoundTrip();
    testSerializeEscapesControlCharacters();
    testEquality();
    testObjectEditing();
}

}  // namespace kettlelogic::test
