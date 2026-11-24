#pragma once
// A plain JSON value: what a part of the filter file means, without its layout.
// Numbers keep their original text, objects keep their key order.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace document {

enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

struct Member;

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    std::string text;             // a number's literal, or a string's decoded UTF-8
    std::vector<Value> array;
    std::vector<Member> object;   // in file order

    static auto Null() -> Value;
    static auto Bool(bool b) -> Value;
    static auto Number(double n) -> Value;          // integral values are written as integers
    static auto Integer(std::int64_t n) -> Value;
    static auto NumberText(std::string literal) -> Value;
    static auto String(std::string s) -> Value;
    static auto Array() -> Value;
    static auto Array(std::vector<Value> items) -> Value;
    static auto Object() -> Value;
    static auto Object(std::vector<Member> members) -> Value;

    auto IsNull() const -> bool { return type == Type::Null; }
    auto IsBool() const -> bool { return type == Type::Bool; }
    auto IsNumber() const -> bool { return type == Type::Number; }
    auto IsString() const -> bool { return type == Type::String; }
    auto IsArray() const -> bool { return type == Type::Array; }
    auto IsObject() const -> bool { return type == Type::Object; }

    // The last member with this key (the plugin's rule for duplicates), or nullptr.
    auto Find(std::string_view key) const -> const Value*;
    auto Find(std::string_view key) -> Value*;
    // Replaces the last member with this key, or appends one.
    auto Set(std::string_view key, Value v) -> Value&;
    // Removes every member with this key; false if there was none.
    auto Erase(std::string_view key) -> bool;

    auto AsDouble() const -> double;                  // 0 when not a number
    auto AsInteger(std::int64_t& out) const -> bool;  // false when not a whole number
    // The string for strings, empty otherwise.
    auto Str() const -> const std::string& { return text; }
};

struct Member {
    std::string key;
    Value value;
};

// Same meaning: numbers compare by value, objects by members in order.
auto operator==(const Value& a, const Value& b) -> bool;

// One line, the file's own spacing: {"a": 1, "b": [1, 2]}.
auto Render(const Value& v) -> std::string;

// A JSON string literal with quotes. UTF-8 is written as is; only ", \ and
// control characters are escaped.
auto EncodeString(std::string_view s) -> std::string;

// Decodes a string literal (with its quotes) into UTF-8. False on a bad escape.
auto DecodeString(std::string_view literal, std::string& out) -> bool;

// Integral values as integers, others in the shortest form that reads back the same.
auto FormatNumber(double n) -> std::string;

}  // namespace document
