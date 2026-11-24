// Text as the game shows it: ÿc colour runs (ÿc followed by one of 0-9 : ; < = > ? @ A-V), search highlights, and the
// word wrapping the wiki overlay does for text drawn in pieces (LayoutPieces), with the measuring passed in, so all of
// it runs and is tested without Windows. The Win32 canvas measures with GetTextExtentPoint32W and draws the result.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "text_fold.hpp"
#include "theme.hpp"

namespace esrfe::overlay {

// A run of text and its colour code ('\0': no code, the caller's colour).
struct ColorRun {
    std::string text;
    char code = '\0';
};

// True for the X of ÿcX.
constexpr auto IsColorCode(char c) -> bool { return c >= '0' && c <= 'V'; }
// ÿ is U+00FF, "\xC3\xBF" in UTF-8.
inline constexpr std::string_view kColorMarker = "\xC3\xBF" "c";

auto HasColorCodes(std::string_view utf8) -> bool;
// Splits at each ÿcX. A ÿc not followed by a code is kept as text. Empty runs are dropped.
auto SplitColorCodes(std::string_view utf8) -> std::vector<ColorRun>;
// The text without its colour codes.
auto StripColorCodes(std::string_view utf8) -> std::string;
// The colour code in effect at the start of the text, or '\0'.
auto FirstColorCode(std::string_view utf8) -> char;

// A piece of text to lay out: UTF-16, one colour.
struct Piece {
    std::u16string text;
    Rgb color{};
    bool highlight = false;  // drawn on the highlight fill
    bool continues = false;  // continues the word before it (no line break between them)
};

// The text as pieces: each coded run in its code's colour (Theme::ColorOfCode), uncoded runs in `base`.
auto ColorPieces(std::string_view utf8, Rgb base, const Theme& theme) -> std::vector<Piece>;

// What to highlight: the required terms of a search query (text_fold's query language), never the -excluded ones.
struct Highlight {
    text::ParsedQuery terms;
    std::string key;  // the query as typed; empty: nothing highlighted
    [[nodiscard]] auto Empty() const -> bool { return key.empty(); }
};
auto MakeHighlight(const std::string& query) -> Highlight;

// Where the highlighted terms are in `line`, as [start, end) UTF-16 positions, sorted and not overlapping. The line is
// matched as the search matches it (text::SearchText: folded, separators as spaces), one code point for one.
auto HighlightRanges(const Highlight& highlight, std::u16string_view line) -> std::vector<std::pair<std::size_t, std::size_t>>;
// Splits pieces at each match so the matched part is drawn on the highlight fill.
auto HighlightPieces(const Highlight& highlight, const std::vector<Piece>& pieces) -> std::vector<Piece>;

// One placed token of the wrapped text.
struct Placed {
    int x = 0;     // from the left of the box
    int line = 0;  // 0-based
    int width = 0;
    std::u16string text;
    Rgb color{};
    bool highlight = false;
};

struct Wrapped {
    std::vector<Placed> placed;
    std::vector<int> lineWidths;  // one per line
    int lines = 0;                // 0 when there is nothing to draw
};

// Lays the pieces out wrapped to `maxWidth`, as DrawText does for words: a word that does not fit moves to a new line
// whole, no wrapped line starts with spaces, '\n' starts a line. With `wrap` false everything stays on one line (except
// at '\n'). `measure` gives the width of a run of text in the font being used.
auto WrapPieces(const std::vector<Piece>& pieces, int maxWidth, bool wrap,
                const std::function<int(std::u16string_view)>& measure) -> Wrapped;

}  // namespace esrfe::overlay
