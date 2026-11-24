// HighlightRanges, HighlightPieces and WrapPieces are the ESR Wiki overlay's HighlightRanges, HighlightPieces and
// LayoutPieces (overlay.cpp), with the global highlight made a parameter and GDI measuring made a callback. The wiki's
// kanji rune name colouring is left out (it is wiki data).
#include "color_text.hpp"

#include <algorithm>

#include "utf.hpp"

namespace esrfe::overlay {

auto HasColorCodes(std::string_view utf8) -> bool { return utf8.find(kColorMarker) != std::string_view::npos; }

auto SplitColorCodes(std::string_view utf8) -> std::vector<ColorRun> {
    std::vector<ColorRun> runs;
    ColorRun current;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const std::size_t at = utf8.find(kColorMarker, i);
        if (at == std::string_view::npos) {
            current.text.append(utf8.substr(i));
            break;
        }
        const std::size_t codeAt = at + kColorMarker.size();
        if (codeAt >= utf8.size() || !IsColorCode(utf8[codeAt])) {
            // not a colour code: keep the marker as text
            current.text.append(utf8.substr(i, codeAt - i));
            i = codeAt;
            continue;
        }
        current.text.append(utf8.substr(i, at - i));
        if (!current.text.empty()) runs.push_back(std::move(current));
        current = ColorRun{};
        current.code = utf8[codeAt];
        i = codeAt + 1;
    }
    if (!current.text.empty()) runs.push_back(std::move(current));
    return runs;
}

auto StripColorCodes(std::string_view utf8) -> std::string {
    std::string out;
    for (const ColorRun& run : SplitColorCodes(utf8)) out += run.text;
    return out;
}

auto FirstColorCode(std::string_view utf8) -> char {
    const std::vector<ColorRun> runs = SplitColorCodes(utf8);
    return runs.empty() ? '\0' : runs.front().code;
}

auto ColorPieces(std::string_view utf8, Rgb base, const Theme& theme) -> std::vector<Piece> {
    std::vector<Piece> pieces;
    for (const ColorRun& run : SplitColorCodes(utf8)) {
        Piece piece;
        piece.text = Utf8ToUtf16(run.text);
        piece.color = run.code != '\0' ? theme.ColorOfCode(run.code) : base;
        pieces.push_back(std::move(piece));
    }
    return pieces;
}

auto MakeHighlight(const std::string& query) -> Highlight {
    Highlight highlight;
    text::ParsedQuery parsed = text::ParseQuery(query);
    if (!parsed.HasRequired()) return highlight;
    parsed.terms.erase(std::remove_if(parsed.terms.begin(), parsed.terms.end(),
                                      [](const text::QueryTerm& term) { return term.excluded; }),
                       parsed.terms.end());
    highlight.terms = std::move(parsed);
    highlight.key = query;
    return highlight;
}

auto HighlightRanges(const Highlight& highlight, std::u16string_view line) -> std::vector<std::pair<std::size_t, std::size_t>> {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (highlight.Empty() || line.empty()) return ranges;
    std::u32string points;
    std::vector<std::size_t> unitOf;  // the UTF-16 position of each code point, then line.size()
    points.reserve(line.size());
    unitOf.reserve(line.size() + 1);
    for (std::size_t i = 0; i < line.size();) {
        unitOf.push_back(i);
        const auto unit = static_cast<unsigned int>(line[i]);
        const unsigned int low = i + 1 < line.size() ? static_cast<unsigned int>(line[i + 1]) : 0u;
        if (unit >= 0xD800u && unit <= 0xDBFFu && low >= 0xDC00u && low <= 0xDFFFu) {
            points.push_back(static_cast<char32_t>(0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u)));
            i += 2;
        } else {
            points.push_back(static_cast<char32_t>(unit));
            ++i;
        }
    }
    unitOf.push_back(line.size());
    const std::u32string searchable = text::SearchText(points);
    for (const text::QueryTerm& term : highlight.terms.terms) {
        std::size_t from = 0;
        while (from < searchable.size()) {
            std::size_t length = 0;
            const std::size_t at = text::FindTerm(searchable, term, from, &length);
            if (at == std::u32string::npos || at >= points.size() || length == 0) break;
            const std::size_t end = std::min(at + length, points.size());
            ranges.emplace_back(unitOf[at], unitOf[end]);
            from = end;
        }
    }
    if (ranges.size() > 1) {
        std::sort(ranges.begin(), ranges.end());
        std::vector<std::pair<std::size_t, std::size_t>> merged;
        for (const auto& range : ranges) {
            if (!merged.empty() && range.first <= merged.back().second) merged.back().second = std::max(merged.back().second, range.second);
            else merged.push_back(range);
        }
        ranges.swap(merged);
    }
    return ranges;
}

auto HighlightPieces(const Highlight& highlight, const std::vector<Piece>& input) -> std::vector<Piece> {
    if (highlight.Empty()) return input;
    std::vector<Piece> out;
    for (const Piece& piece : input) {
        // A part that starts right after a letter continues that word, so it cannot start a new line.
        auto add = [&](std::size_t start, std::size_t length, bool mark) {
            const bool continues = start == 0 ? piece.continues : piece.text[start - 1] != u' ';
            out.push_back(Piece{piece.text.substr(start, length), piece.color, mark || piece.highlight, continues});
        };
        std::size_t pos = 0;
        for (const auto& range : HighlightRanges(highlight, piece.text)) {
            if (range.first > pos) add(pos, range.first - pos, false);
            add(range.first, range.second - range.first, true);
            pos = range.second;
        }
        if (pos < piece.text.size()) add(pos, piece.text.size() - pos, false);
    }
    return out;
}

auto WrapPieces(const std::vector<Piece>& pieces, int maxWidthIn, bool wrap,
                const std::function<int(std::u16string_view)>& measure) -> Wrapped {
    Wrapped result;
    const int maxWidth = std::max(1, maxWidthIn);
    int x = 0;
    int line = 0;
    bool wordStart = true;
    std::vector<Placed> word;  // the current word; pieces that continue a word join it
    bool wordIsSpace = false;
    // Places the buffered word, moving the whole word to a new line first when it does not fit.
    auto flush = [&]() {
        if (word.empty()) return;
        int total = 0;
        for (const Placed& p : word) total += p.width;
        if (wrap && !wordIsSpace && x > 0 && x + total > maxWidth) {
            result.lineWidths.push_back(x);
            x = 0;
            ++line;
        }
        for (Placed& p : word) {
            p.x = x;
            p.line = line;
            x += p.width;
            result.placed.push_back(std::move(p));
        }
        word.clear();
    };
    for (const Piece& piece : pieces) {
        // A continuation joins the word before it. Unlike the wiki (which allowed a break at every piece), a new colour
        // run that starts right after a letter also joins it: the game breaks lines at spaces only.
        if (piece.continues) wordStart = false;
        std::size_t start = 0;
        while (start < piece.text.size()) {
            std::size_t end = start + 1;
            if (piece.text[start] != u'\n') {
                const bool space = piece.text[start] == u' ';
                while (end < piece.text.size() && piece.text[end] != u'\n' && (piece.text[end] == u' ') == space) ++end;
            }
            const std::u16string token = piece.text.substr(start, end - start);
            start = end;
            if (token == u"\n") {
                flush();
                result.lineWidths.push_back(x);
                x = 0;
                ++line;
                wordStart = true;
                continue;
            }
            const bool isSpace = token[0] == u' ';
            const int width = measure(token);
            if (isSpace) {
                flush();
                if (x == 0) {  // no leading spaces on a wrapped line
                    wordStart = true;
                    continue;
                }
                wordIsSpace = true;
                word.push_back(Placed{0, line, width, token, piece.color, piece.highlight});
                flush();
            } else {
                if (wordStart) {
                    flush();
                    wordIsSpace = false;
                }
                word.push_back(Placed{0, line, width, token, piece.color, piece.highlight});
            }
            wordStart = isSpace;
        }
    }
    flush();
    result.lineWidths.push_back(x);
    result.lines = result.placed.empty() ? 0 : line + 1;
    return result;
}

}  // namespace esrfe::overlay
