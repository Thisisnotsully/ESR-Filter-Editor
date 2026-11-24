#include "syntax.h"

#include <cstring>

namespace document {

namespace {

constexpr int MaximumDepth = 64;

// Mirrors the plugin's json_value.cpp reader step by step, so it accepts and
// refuses the same files, and records every byte as a token on the way.
class Parser {
public:
    Parser(std::string_view text, Syntax& out) : text_(text), out_(out) {}

    auto Run(Diag& error) -> bool {
        out_ = Syntax{};
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            out_.tokens.push_back(Token{Tok::Bom, 0, 3, 1});
            pos_ = 3;
        }
        bool ok = SkipSpace();
        if (ok && AtEnd()) {
            failure_ = "the file is empty";
            ok = false;
        }
        if (ok) {
            int root = -1;
            ok = ReadValue(root, 0);
            out_.root = root;
        }
        if (ok) ok = SkipSpace();
        if (ok && !AtEnd()) {
            failure_ = "unexpected text after the end of the filter";
            ok = false;
        }
        if (ok) return true;
        if (failureLine_ == 0) {
            failureLine_ = line_;
            failureColumn_ = Column();
        }
        error.severity = Diag::Severity::Error;
        error.line = failureLine_;
        error.column = failureColumn_;
        error.message = failure_.empty() ? std::string("invalid JSON") : failure_;
        return false;
    }

private:
    std::string_view text_;
    Syntax& out_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::size_t lineStart_ = 0;
    std::string failure_;
    std::uint32_t failureLine_ = 0;
    std::uint32_t failureColumn_ = 0;

    auto AtEnd() const -> bool { return pos_ >= text_.size(); }
    auto Peek() const -> char { return AtEnd() ? '\0' : text_[pos_]; }
    auto PeekAt(std::size_t i) const -> char { return i < text_.size() ? text_[i] : '\0'; }

    auto Column() const -> std::uint32_t {
        std::uint32_t column = 1;
        for (std::size_t i = lineStart_; i < pos_ && i < text_.size(); ++i) {
            if ((static_cast<unsigned char>(text_[i]) & 0xC0) != 0x80) ++column;
        }
        return column;
    }

    void Advance() {
        if (AtEnd()) return;
        if (text_[pos_] == '\n') {
            ++line_;
            lineStart_ = pos_ + 1;
        }
        ++pos_;
    }

    auto Error(const char* message) -> bool {
        if (failure_.empty()) {
            failure_ = message;
            failureLine_ = line_;
            failureColumn_ = Column();
        }
        return false;
    }

    void Emit(Tok kind, std::size_t begin, std::uint32_t line) {
        out_.tokens.push_back(Token{kind, begin, pos_, line});
    }

    auto SkipSpace() -> bool {
        while (!AtEnd()) {
            const std::size_t begin = pos_;
            const std::uint32_t line = line_;
            const char c = Peek();
            if (c == '\n' || (c == '\r' && PeekAt(pos_ + 1) == '\n')) {
                if (c == '\r') Advance();
                Advance();
                Emit(Tok::Newline, begin, line);
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                while (!AtEnd()) {
                    const char d = Peek();
                    if (d == ' ' || d == '\t' || (d == '\r' && PeekAt(pos_ + 1) != '\n')) Advance();
                    else break;
                }
                Emit(Tok::Space, begin, line);
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size()) {
                const char next = text_[pos_ + 1];
                if (next == '/') {
                    while (!AtEnd() && Peek() != '\n' && !(Peek() == '\r' && PeekAt(pos_ + 1) == '\n')) Advance();
                    Emit(Tok::LineComment, begin, line);
                    continue;
                }
                if (next == '*') {
                    const std::uint32_t startColumn = Column();
                    Advance();
                    Advance();
                    bool closed = false;
                    while (!AtEnd()) {
                        if (Peek() == '*' && PeekAt(pos_ + 1) == '/') {
                            Advance();
                            Advance();
                            closed = true;
                            break;
                        }
                        Advance();
                    }
                    if (!closed) {
                        failure_ = "a /* comment is never closed with */";
                        failureLine_ = line;
                        failureColumn_ = startColumn;
                        return false;
                    }
                    Emit(Tok::BlockComment, begin, line);
                    continue;
                }
            }
            break;
        }
        return true;
    }

    auto NewNode(Type type) -> int {
        out_.nodes.push_back(Node{});
        out_.nodes.back().type = type;
        out_.nodes.back().first = static_cast<int>(out_.tokens.size());
        return static_cast<int>(out_.nodes.size()) - 1;
    }

    auto LastToken() const -> int { return static_cast<int>(out_.tokens.size()) - 1; }

    // A one-character token at the current position.
    void Punct(Tok kind) {
        const std::size_t begin = pos_;
        const std::uint32_t line = line_;
        Advance();
        Emit(kind, begin, line);
    }

    auto ReadValue(int& node, int depth) -> bool {
        if (depth > MaximumDepth) return Error("the filter nests deeper than 64 levels");
        const char c = Peek();
        switch (c) {
        case '{': return ReadObject(node, depth);
        case '[': return ReadArray(node, depth);
        case '"': {
            node = NewNode(Type::String);
            if (!ReadString()) return false;
            out_.nodes[node].last = LastToken();
            return true;
        }
        case 't': return ReadWord("true", node, Type::Bool, Tok::True);
        case 'f': return ReadWord("false", node, Type::Bool, Tok::False);
        case 'n': return ReadWord("null", node, Type::Null, Tok::Null);
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return ReadNumber(node);
            if (c == '\'') return Error("texts must use double quotes \"...\", not single quotes");
            if (c == '}' || c == ']') return Error("a value is missing here");
            if (AtEnd()) return Error("the file ends in the middle of a value");
            return Error("unexpected character; expected a value");
        }
    }

    auto ReadWord(const char* word, int& node, Type type, Tok kind) -> bool {
        const std::size_t length = std::strlen(word);
        if (text_.substr(pos_, length) != std::string_view(word, length)) {
            return Error("unexpected word; expected true, false, null, a number, a text, a list or an object");
        }
        node = NewNode(type);
        const std::size_t begin = pos_;
        const std::uint32_t line = line_;
        for (std::size_t i = 0; i < length; ++i) Advance();
        Emit(kind, begin, line);
        out_.nodes[node].first = out_.nodes[node].last = LastToken();
        return true;
    }

    auto IsDigit() const -> bool { return Peek() >= '0' && Peek() <= '9'; }

    auto ReadNumber(int& node) -> bool {
        const std::size_t begin = pos_;
        const std::uint32_t line = line_;
        if (Peek() == '-') Advance();
        if (!IsDigit()) return Error("a minus sign must be followed by a digit");
        while (IsDigit()) Advance();
        if (Peek() == '.') {
            Advance();
            if (!IsDigit()) return Error("a decimal point must be followed by a digit");
            while (IsDigit()) Advance();
        }
        if (Peek() == 'e' || Peek() == 'E') {
            Advance();
            if (Peek() == '+' || Peek() == '-') Advance();
            if (!IsDigit()) return Error("an exponent must be followed by a digit");
            while (IsDigit()) Advance();
        }
        node = NewNode(Type::Number);
        Emit(Tok::Number, begin, line);
        out_.nodes[node].first = out_.nodes[node].last = LastToken();
        return true;
    }

    auto ReadHex4(std::uint32_t& value) -> bool {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = Peek();
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else return Error("\\u must be followed by four hexadecimal digits");
            value = (value << 4) | digit;
            Advance();
        }
        return true;
    }

    // Checks a string as the plugin does and emits it as one token.
    auto ReadString() -> bool {
        const std::size_t begin = pos_;
        const std::uint32_t startLine = line_;
        const std::uint32_t startColumn = Column();
        Advance();
        while (true) {
            if (AtEnd()) {
                failure_ = "a text is never closed with a double quote";
                failureLine_ = startLine;
                failureColumn_ = startColumn;
                return false;
            }
            const char c = Peek();
            if (c == '"') {
                Advance();
                Emit(Tok::String, begin, startLine);
                return true;
            }
            if (c == '\n' || c == '\r') return Error("a text cannot span lines; write \\n for a line break");
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t') return Error("a text contains a control character");
            if (c != '\\') {
                Advance();
                continue;
            }
            Advance();
            switch (Peek()) {
            case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                Advance();
                break;
            case 'u': {
                Advance();
                std::uint32_t cp = 0;
                if (!ReadHex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (Peek() != '\\') return Error("a \\u surrogate pair is incomplete");
                    Advance();
                    if (Peek() != 'u') return Error("a \\u surrogate pair is incomplete");
                    Advance();
                    std::uint32_t low = 0;
                    if (!ReadHex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return Error("a \\u surrogate pair is invalid");
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return Error("a \\u surrogate pair is invalid");
                }
                break;
            }
            default:
                return Error("unknown escape; use \\\", \\\\, \\/, \\b, \\f, \\n, \\r, \\t or \\uXXXX");
            }
        }
    }

    auto ReadArray(int& node, int depth) -> bool {
        node = NewNode(Type::Array);
        Punct(Tok::LBracket);
        if (!SkipSpace()) return false;
        if (Peek() == ']') {
            Punct(Tok::RBracket);
            out_.nodes[node].last = LastToken();
            return true;
        }
        while (true) {
            Item item;
            if (!ReadValue(item.node, depth + 1)) return false;
            out_.nodes[node].items.push_back(item);
            if (!SkipSpace()) return false;
            if (Peek() == ',') {
                Punct(Tok::Comma);
                out_.nodes[node].items.back().comma = LastToken();
                if (!SkipSpace()) return false;
                if (Peek() == ']') break;
                continue;
            }
            if (Peek() == ']') break;
            if (AtEnd()) return Error("a list is never closed with ]");
            return Error("expected , or ] after a list element");
        }
        Punct(Tok::RBracket);
        out_.nodes[node].last = LastToken();
        return true;
    }

    auto ReadObject(int& node, int depth) -> bool {
        node = NewNode(Type::Object);
        Punct(Tok::LBrace);
        if (!SkipSpace()) return false;
        if (Peek() == '}') {
            Punct(Tok::RBrace);
            out_.nodes[node].last = LastToken();
            return true;
        }
        while (true) {
            if (Peek() != '"') {
                if (AtEnd()) return Error("an object is never closed with }");
                return Error("expected a property name in double quotes");
            }
            Item item;
            if (!ReadString()) return false;
            item.key = LastToken();
            if (!SkipSpace()) return false;
            if (Peek() != ':') return Error("expected : after a property name");
            Punct(Tok::Colon);
            item.colon = LastToken();
            if (!SkipSpace()) return false;
            if (!ReadValue(item.node, depth + 1)) return false;
            out_.nodes[node].items.push_back(item);
            if (!SkipSpace()) return false;
            if (Peek() == ',') {
                Punct(Tok::Comma);
                out_.nodes[node].items.back().comma = LastToken();
                if (!SkipSpace()) return false;
                if (Peek() == '}') break;
                continue;
            }
            if (Peek() == '}') break;
            if (AtEnd()) return Error("an object is never closed with }");
            return Error("expected , or } after a property");
        }
        Punct(Tok::RBrace);
        out_.nodes[node].last = LastToken();
        return true;
    }
};

}  // namespace

auto ParseSyntax(std::string_view text, Syntax& out, Diag& error) -> bool {
    Parser parser(text, out);
    return parser.Run(error);
}

auto ToValue(const Syntax& syntax, std::string_view text, int node) -> Value {
    const Node& n = syntax.nodes[static_cast<std::size_t>(node)];
    const Token& t = syntax.tokens[static_cast<std::size_t>(n.first)];
    const std::string_view raw = text.substr(t.begin, t.end - t.begin);
    switch (n.type) {
    case Type::Null: return Value::Null();
    case Type::Bool: return Value::Bool(t.kind == Tok::True);
    case Type::Number: return Value::NumberText(std::string(raw));
    case Type::String: {
        std::string s;
        DecodeString(raw, s);
        return Value::String(std::move(s));
    }
    case Type::Array: {
        Value v = Value::Array();
        v.array.reserve(n.items.size());
        for (const Item& item : n.items) v.array.push_back(ToValue(syntax, text, item.node));
        return v;
    }
    case Type::Object: {
        Value v = Value::Object();
        v.object.reserve(n.items.size());
        for (const Item& item : n.items) {
            const Token& k = syntax.tokens[static_cast<std::size_t>(item.key)];
            Member m;
            DecodeString(text.substr(k.begin, k.end - k.begin), m.key);
            m.value = ToValue(syntax, text, item.node);
            v.object.push_back(std::move(m));
        }
        return v;
    }
    }
    return Value::Null();
}

}  // namespace document
