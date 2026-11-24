#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace document {

auto Value::Null() -> Value { return Value{}; }

auto Value::Bool(bool b) -> Value {
    Value v;
    v.type = Type::Bool;
    v.boolean = b;
    return v;
}

auto Value::Number(double n) -> Value { return NumberText(FormatNumber(n)); }

auto Value::Integer(std::int64_t n) -> Value { return NumberText(std::to_string(n)); }

auto Value::NumberText(std::string literal) -> Value {
    Value v;
    v.type = Type::Number;
    v.text = std::move(literal);
    return v;
}

auto Value::String(std::string s) -> Value {
    Value v;
    v.type = Type::String;
    v.text = std::move(s);
    return v;
}

auto Value::Array() -> Value {
    Value v;
    v.type = Type::Array;
    return v;
}

auto Value::Object() -> Value {
    Value v;
    v.type = Type::Object;
    return v;
}

auto Value::Array(std::vector<Value> items) -> Value {
    Value v;
    v.type = Type::Array;
    v.array = std::move(items);
    return v;
}

auto Value::Object(std::vector<Member> members) -> Value {
    Value v;
    v.type = Type::Object;
    v.object = std::move(members);
    return v;
}

auto Value::Find(std::string_view key) const -> const Value* {
    const Value* found = nullptr;
    for (const Member& m : object) {
        if (m.key == key) found = &m.value;
    }
    return found;
}

auto Value::Find(std::string_view key) -> Value* {
    Value* found = nullptr;
    for (Member& m : object) {
        if (m.key == key) found = &m.value;
    }
    return found;
}

auto Value::Set(std::string_view key, Value v) -> Value& {
    if (Value* found = Find(key)) {
        *found = std::move(v);
        return *found;
    }
    object.push_back(Member{std::string(key), std::move(v)});
    return object.back().value;
}

auto Value::Erase(std::string_view key) -> bool {
    const auto before = object.size();
    std::erase_if(object, [&](const Member& m) { return m.key == key; });
    return object.size() != before;
}

auto Value::AsDouble() const -> double {
    if (type != Type::Number) return 0;
    return std::strtod(text.c_str(), nullptr);
}

auto Value::AsInteger(std::int64_t& out) const -> bool {
    if (type != Type::Number) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec == std::errc() && ptr == end) return true;
    // 1.0 or 1e2 are whole numbers too
    const double d = AsDouble();
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15) {
        out = static_cast<std::int64_t>(d);
        return true;
    }
    return false;
}

auto operator==(const Value& a, const Value& b) -> bool {
    if (a.type != b.type) return false;
    switch (a.type) {
    case Type::Null: return true;
    case Type::Bool: return a.boolean == b.boolean;
    case Type::Number: return a.text == b.text || a.AsDouble() == b.AsDouble();
    case Type::String: return a.text == b.text;
    case Type::Array: return a.array == b.array;
    case Type::Object:
        if (a.object.size() != b.object.size()) return false;
        for (std::size_t i = 0; i < a.object.size(); ++i) {
            if (a.object[i].key != b.object[i].key || !(a.object[i].value == b.object[i].value)) return false;
        }
        return true;
    }
    return false;
}

namespace {

void RenderTo(const Value& v, std::string& out) {
    switch (v.type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += v.boolean ? "true" : "false"; break;
    case Type::Number: out += v.text.empty() ? "0" : v.text; break;
    case Type::String: out += EncodeString(v.text); break;
    case Type::Array:
        out += '[';
        for (std::size_t i = 0; i < v.array.size(); ++i) {
            if (i) out += ", ";
            RenderTo(v.array[i], out);
        }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (std::size_t i = 0; i < v.object.size(); ++i) {
            if (i) out += ", ";
            out += EncodeString(v.object[i].key);
            out += ": ";
            RenderTo(v.object[i].value, out);
        }
        out += '}';
        break;
    }
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
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

auto Hex4(std::string_view s, std::size_t at, std::uint32_t& out) -> bool {
    if (at + 4 > s.size()) return false;
    out = 0;
    for (std::size_t i = at; i < at + 4; ++i) {
        const char c = s[i];
        std::uint32_t d = 0;
        if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
        out = (out << 4) | d;
    }
    return true;
}

}  // namespace

auto Render(const Value& v) -> std::string {
    std::string out;
    RenderTo(v, out);
    return out;
}

auto EncodeString(std::string_view s) -> std::string {
    static const char* const hex = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (u < 0x20) {
                out += "\\u00";
                out += hex[u >> 4];
                out += hex[u & 15];
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

auto DecodeString(std::string_view lit, std::string& out) -> bool {
    out.clear();
    if (lit.size() < 2 || lit.front() != '"' || lit.back() != '"') return false;
    const std::string_view s = lit.substr(1, lit.size() - 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i >= s.size()) return false;
        switch (s[i]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
            std::uint32_t cp = 0;
            if (!Hex4(s, i + 1, cp)) return false;
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                std::uint32_t low = 0;
                if (i + 2 >= s.size() || s[i + 1] != '\\' || s[i + 2] != 'u' || !Hex4(s, i + 3, low)) return false;
                if (low < 0xDC00 || low > 0xDFFF) return false;
                i += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;
            }
            AppendUtf8(out, cp);
            break;
        }
        default: return false;
        }
    }
    return true;
}

auto FormatNumber(double n) -> std::string {
    if (!std::isfinite(n)) return "0";
    if (n == std::floor(n) && std::fabs(n) < 9.0e15) {
        return std::to_string(static_cast<std::int64_t>(n));
    }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, n);
    if (ec != std::errc()) return "0";
    return std::string(buf, ptr);
}

}  // namespace document
