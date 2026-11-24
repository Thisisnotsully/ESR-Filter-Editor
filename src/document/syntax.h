#pragma once
// The concrete syntax tree of a filter file: every byte belongs to exactly one
// token (whitespace, newlines and comments included), and the tokens in order
// are the file. Values are nodes over those tokens.
//
// The reader accepts what the loot filter plugin's own reader accepts: a UTF-8
// byte order mark, // and /* */ comments, a trailing comma before ] or }, tabs
// inside strings, duplicate keys, at most 64 levels. Its error messages, lines
// and columns are the plugin's.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "json.h"

namespace document {

enum class Tok : std::uint8_t {
    Bom,           // EF BB BF at the very start
    Space,         // spaces, tabs, a lone CR
    Newline,       // LF or CR LF
    LineComment,   // from // up to the end of the line (not its CR LF)
    BlockComment,  // /* ... */
    LBrace, RBrace, LBracket, RBracket, Colon, Comma,
    String, Number, True, False, Null,
};

struct Token {
    Tok kind = Tok::Space;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::uint32_t line = 1;  // line of the first byte, from 1
};

// One entry of an array or object. Token indices; -1 when absent.
struct Item {
    int key = -1;    // object members: the key's String token
    int colon = -1;
    int node = -1;   // the value's node
    int comma = -1;  // the comma after the value
};

struct Node {
    Type type = Type::Null;
    int first = -1;  // first token ({, [ or the scalar)
    int last = -1;   // last token (}, ] or the scalar)
    std::vector<Item> items;
};

struct Diag {
    enum class Severity : std::uint8_t { Error, Warning };
    Severity severity = Severity::Error;
    std::uint32_t line = 0;
    std::uint32_t column = 0;  // in characters, from 1; 0 when not known
    std::string message;
};

struct Syntax {
    std::vector<Token> tokens;
    std::vector<Node> nodes;
    int root = -1;
};

// Reads text into out. False and error filled when the plugin would refuse it.
auto ParseSyntax(std::string_view text, Syntax& out, Diag& error) -> bool;

// The meaning of a node.
auto ToValue(const Syntax& syntax, std::string_view text, int node) -> Value;

}  // namespace document
