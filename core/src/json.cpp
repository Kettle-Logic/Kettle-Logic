#include "kettlelogic/json.hpp"

#include <cstdint>
#include <string>

namespace kettlelogic::json {

namespace {

constexpr int kMaxDepth = 512;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// RFC 8259: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
bool isJsonNumber(std::string_view s) {
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') {
        ++i;
    }
    if (i >= s.size() || !isDigit(s[i])) {
        return false;
    }
    if (s[i] == '0') {
        ++i;
    } else {
        while (i < s.size() && isDigit(s[i])) {
            ++i;
        }
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || !isDigit(s[i])) {
            return false;
        }
        while (i < s.size() && isDigit(s[i])) {
            ++i;
        }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            ++i;
        }
        if (i >= s.size() || !isDigit(s[i])) {
            return false;
        }
        while (i < s.size() && isDigit(s[i])) {
            ++i;
        }
    }
    return i == s.size();
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

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parseDocument() {
        skipWhitespace();
        Value value = parseValue(0);
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected content after the JSON value");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw ParseError(message, line, column);
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++pos_;
        }
    }

    char peek() {
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
        }
        return text_[pos_];
    }

    void expect(char c) {
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos_;
    }

    void expectLiteral(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal) {
            fail("invalid literal");
        }
        pos_ += literal.size();
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) {
            fail("nesting is too deep");
        }
        const char c = peek();
        switch (c) {
            case '{':
                return parseObject(depth);
            case '[':
                return parseArray(depth);
            case '"':
                return Value::string(parseString());
            case 't':
                expectLiteral("true");
                return Value::boolean(true);
            case 'f':
                expectLiteral("false");
                return Value::boolean(false);
            case 'n':
                expectLiteral("null");
                return Value{};
            default:
                if (c == '-' || isDigit(c)) {
                    return parseNumber();
                }
                fail("unexpected character");
        }
    }

    Value parseNumber() {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
            fail("invalid number");
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
                fail("digits required after decimal point");
            }
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
                fail("digits required in exponent");
            }
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        }
        return Value::number(std::string(text_.substr(start, pos_ - start)));
    }

    std::uint32_t parseHex4() {
        if (pos_ + 4 > text_.size()) {
            fail("incomplete \\u escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("invalid hex digit in \\u escape");
            }
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            const char c = peek();
            ++pos_;
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            const char esc = peek();
            ++pos_;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                            fail("unpaired surrogate in \\u escape");
                        }
                        pos_ += 2;
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("unpaired surrogate in \\u escape");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("unpaired surrogate in \\u escape");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    fail("invalid escape sequence");
            }
        }
    }

    Value parseArray(int depth) {
        expect('[');
        Array items;
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            return Value::array(std::move(items));
        }
        while (true) {
            skipWhitespace();
            items.push_back(parseValue(depth + 1));
            skipWhitespace();
            const char c = peek();
            ++pos_;
            if (c == ']') {
                return Value::array(std::move(items));
            }
            if (c != ',') {
                --pos_;
                fail("expected ',' or ']'");
            }
        }
    }

    Value parseObject(int depth) {
        expect('{');
        Object members;
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            return Value::object(std::move(members));
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("expected a string key");
            }
            std::string key = parseString();
            for (const Member& m : members) {
                if (m.key == key) {
                    fail("duplicate key \"" + key + "\"");
                }
            }
            skipWhitespace();
            expect(':');
            skipWhitespace();
            Value value = parseValue(depth + 1);
            members.push_back(Member{std::move(key), std::move(value)});
            skipWhitespace();
            const char c = peek();
            ++pos_;
            if (c == '}') {
                return Value::object(std::move(members));
            }
            if (c != ',') {
                --pos_;
                fail("expected ',' or '}'");
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

void appendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                    out.push_back(hex[static_cast<unsigned char>(c) & 0xF]);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void emit(std::string& out, const Value& value, int indent, int depth) {
    const bool pretty = indent >= 0;
    auto newline = [&](int level) {
        if (pretty) {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(level * indent), ' ');
        }
    };

    switch (value.kind()) {
        case Value::Kind::Null:
            out += "null";
            break;
        case Value::Kind::Bool:
            out += value.asBool() ? "true" : "false";
            break;
        case Value::Kind::Number:
            out += value.numberLexeme();
            break;
        case Value::Kind::String:
            appendEscaped(out, value.asString());
            break;
        case Value::Kind::Array: {
            const Array& items = value.asArray();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out.push_back(',');
                }
                newline(depth + 1);
                emit(out, items[i], indent, depth + 1);
            }
            newline(depth);
            out.push_back(']');
            break;
        }
        case Value::Kind::Object: {
            const Object& members = value.asObject();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (i > 0) {
                    out.push_back(',');
                }
                newline(depth + 1);
                appendEscaped(out, members[i].key);
                out += pretty ? ": " : ":";
                emit(out, members[i].value, indent, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
        }
    }
}

[[noreturn]] void kindMismatch(const char* wanted) {
    throw std::logic_error(std::string("json::Value is not ") + wanted);
}

}  // namespace

ParseError::ParseError(const std::string& message, std::size_t line, std::size_t column)
    : std::runtime_error(std::to_string(line) + ":" + std::to_string(column) + ": " + message),
      line_(line),
      column_(column) {}

Value::Value(Storage storage) : storage_(std::move(storage)) {}

Value Value::boolean(bool b) {
    return Value{Storage{b}};
}

Value Value::number(std::string lexeme) {
    if (!isJsonNumber(lexeme)) {
        throw std::invalid_argument("not a JSON number: " + lexeme);
    }
    return Value{Storage{Number{std::move(lexeme)}}};
}

Value Value::fromRational(const Rational& r) {
    return number(r.toDecimal());
}

Value Value::string(std::string s) {
    return Value{Storage{std::move(s)}};
}

Value Value::array(Array items) {
    return Value{Storage{std::move(items)}};
}

Value Value::object(Object members) {
    return Value{Storage{std::move(members)}};
}

Value::Kind Value::kind() const {
    // Variant alternatives are declared in the same order as Kind.
    return static_cast<Kind>(storage_.index());
}

bool Value::asBool() const {
    if (const auto* b = std::get_if<bool>(&storage_)) {
        return *b;
    }
    kindMismatch("a boolean");
}

const std::string& Value::asString() const {
    if (const auto* s = std::get_if<std::string>(&storage_)) {
        return *s;
    }
    kindMismatch("a string");
}

const std::string& Value::numberLexeme() const {
    if (const auto* n = std::get_if<Number>(&storage_)) {
        return n->lexeme;
    }
    kindMismatch("a number");
}

Rational Value::asRational() const {
    return Rational::fromDecimal(numberLexeme());
}

const Array& Value::asArray() const {
    if (const auto* a = std::get_if<Array>(&storage_)) {
        return *a;
    }
    kindMismatch("an array");
}

Array& Value::asArray() {
    if (auto* a = std::get_if<Array>(&storage_)) {
        return *a;
    }
    kindMismatch("an array");
}

const Object& Value::asObject() const {
    if (const auto* o = std::get_if<Object>(&storage_)) {
        return *o;
    }
    kindMismatch("an object");
}

Object& Value::asObject() {
    if (auto* o = std::get_if<Object>(&storage_)) {
        return *o;
    }
    kindMismatch("an object");
}

const Value* Value::find(std::string_view key) const {
    const auto* o = std::get_if<Object>(&storage_);
    if (o == nullptr) {
        return nullptr;
    }
    for (const Member& m : *o) {
        if (m.key == key) {
            return &m.value;
        }
    }
    return nullptr;
}

Value* Value::find(std::string_view key) {
    auto* o = std::get_if<Object>(&storage_);
    if (o == nullptr) {
        return nullptr;
    }
    for (Member& m : *o) {
        if (m.key == key) {
            return &m.value;
        }
    }
    return nullptr;
}

void Value::set(std::string key, Value value) {
    auto* o = std::get_if<Object>(&storage_);
    if (o == nullptr) {
        kindMismatch("an object");
    }
    if (Value* existing = find(key)) {
        *existing = std::move(value);
        return;
    }
    o->push_back(Member{std::move(key), std::move(value)});
}

bool Value::erase(std::string_view key) {
    auto* o = std::get_if<Object>(&storage_);
    if (o == nullptr) {
        kindMismatch("an object");
    }
    for (auto it = o->begin(); it != o->end(); ++it) {
        if (it->key == key) {
            o->erase(it);
            return true;
        }
    }
    return false;
}

bool operator==(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) {
        return false;
    }
    switch (a.kind()) {
        case Value::Kind::Null:
            return true;
        case Value::Kind::Bool:
            return a.asBool() == b.asBool();
        case Value::Kind::Number:
            // Numbers compare by exact value, so "1.0" equals "1".
            return a.asRational() == b.asRational();
        case Value::Kind::String:
            return a.asString() == b.asString();
        case Value::Kind::Array:
            return a.asArray() == b.asArray();
        case Value::Kind::Object: {
            // Key order does not affect equality.
            const Object& ma = a.asObject();
            const Object& mb = b.asObject();
            if (ma.size() != mb.size()) {
                return false;
            }
            for (const Member& m : ma) {
                const Value* other = b.find(m.key);
                if (other == nullptr || !(m.value == *other)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

Value parse(std::string_view text) {
    return Parser(text).parseDocument();
}

std::string serialize(const Value& value, int indent) {
    std::string out;
    emit(out, value, indent, 0);
    if (indent >= 0) {
        out.push_back('\n');
    }
    return out;
}

}  // namespace kettlelogic::json
