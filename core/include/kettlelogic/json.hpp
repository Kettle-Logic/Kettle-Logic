#pragma once

#include "kettlelogic/rational.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// A JSON document model that keeps numbers as their original text. A BeerJSON
// number like 1.10 is stored as "1.10" and converted to an exact Rational only
// when read, so nothing passes through a double.
namespace kettlelogic::json {

class Value;
struct Member;

using Array = std::vector<Value>;
using Object = std::vector<Member>;  // ordered; keys are unique

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;  // null

    static Value boolean(bool b);
    // Throws std::invalid_argument unless `lexeme` is a valid JSON number.
    static Value number(std::string lexeme);
    static Value fromRational(const Rational& r);
    static Value string(std::string s);
    // Take their container explicitly. A default argument here would need
    // Member to be complete, and it is defined after Value.
    static Value array(Array items);
    static Value object(Object members);

    Kind kind() const;
    bool isNull() const { return kind() == Kind::Null; }
    bool isBool() const { return kind() == Kind::Bool; }
    bool isNumber() const { return kind() == Kind::Number; }
    bool isString() const { return kind() == Kind::String; }
    bool isArray() const { return kind() == Kind::Array; }
    bool isObject() const { return kind() == Kind::Object; }

    // Accessors throw std::logic_error when the kind does not match.
    bool asBool() const;
    const std::string& asString() const;
    const std::string& numberLexeme() const;
    Rational asRational() const;  // exact
    const Array& asArray() const;
    Array& asArray();
    const Object& asObject() const;
    Object& asObject();

    // Object lookup. Returns nullptr when the key is absent or this is not an object.
    const Value* find(std::string_view key) const;
    Value* find(std::string_view key);

    // Object only. Replaces an existing member or appends a new one.
    void set(std::string key, Value value);
    // Object only. Returns true if a member was removed.
    bool erase(std::string_view key);

    friend bool operator==(const Value& a, const Value& b);

private:
    struct Number {
        std::string lexeme;
    };
    using Storage = std::variant<std::monostate, bool, Number, std::string, Array, Object>;

    // Defined in json.cpp: moving the storage needs Member to be complete.
    explicit Value(Storage storage);

    Storage storage_;
};

struct Member {
    std::string key;
    Value value;
};

// Thrown by parse(). `line` and `column` are 1-based.
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, std::size_t line, std::size_t column);

    std::size_t line() const { return line_; }
    std::size_t column() const { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

// Strict RFC 8259 parsing. Rejects duplicate object keys, trailing commas,
// leading zeros, and nesting deeper than 512 levels.
Value parse(std::string_view text);

// indent < 0 writes compact output. Numbers are written as their lexemes.
std::string serialize(const Value& value, int indent = 2);

}  // namespace kettlelogic::json
