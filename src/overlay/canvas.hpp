// Drawing for views and widgets. The Win32 host implements it over GDI/GDI+ (gdi_canvas.cpp) with the wiki overlay's
// drawing code; tests implement it with a recorder. Coordinates are client pixels; sizes written at 96 dpi go through
// S(). Text is UTF-8 and may carry ÿc colour codes, which draw in the game's colours (Theme::ColorOfCode).
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "color_text.hpp"
#include "geometry.hpp"
#include "theme.hpp"

namespace esrfe::overlay {

// The wiki overlay's fonts: body 10 pt, small 8 pt, bold 10 pt semibold, title 13 pt semibold, snippet 9 pt (never
// smaller than 9 pt at the display's scaling) and its bold. Sizes follow Theme.
enum class Font { Body, Small, Bold, Title, Snippet, SnippetBold };

// Text flags (combine with |). The defaults are left aligned, top, wrapped at words.
namespace text_flags {
inline constexpr unsigned kLeft = 0;
inline constexpr unsigned kCenter = 1u << 0;
inline constexpr unsigned kRight = 1u << 1;
inline constexpr unsigned kVCenter = 1u << 2;
inline constexpr unsigned kSingleLine = 1u << 3;  // no wrapping
inline constexpr unsigned kEllipsis = 1u << 4;    // single line, cut with "..." (text without colour codes or highlight)
inline constexpr unsigned kRaw = 1u << 5;         // ÿc is shown as typed, not as a colour (what a person types)
inline constexpr unsigned kNoHighlight = 1u << 6;  // the search highlight does not mark this text
}  // namespace text_flags

// Measuring, which input handling needs too (a click in a text box finds its caret position).
class Measurer {
public:
    virtual ~Measurer() = default;
    [[nodiscard]] virtual auto GetTheme() const -> const Theme& = 0;
    // A size written at 96 dpi, in pixels at the display's dpi (the wiki's S()).
    [[nodiscard]] virtual auto S(int value) const -> int = 0;
    virtual auto LineHeight(Font font) -> int = 0;
    virtual auto TextWidth(std::string_view utf8, Font font, unsigned flags = 0) -> int = 0;  // one line
    virtual auto TextWidth16(std::u16string_view text, Font font) -> int = 0;               // raw, one line
    virtual auto TextHeight(std::string_view utf8, int width, Font font, unsigned flags = 0) -> int = 0;  // wrapped
};

class Canvas : public Measurer {
public:
    virtual void Fill(const Box& box, Rgb color) = 0;
    virtual void FillAlpha(const Box& box, Rgb color, std::uint8_t alpha) = 0;  // see-through (the wiki's FillPanel)
    virtual void Frame(const Box& box, Rgb color) = 0;                         // a one-pixel frame
    virtual void CornerShadow(const Box& box, int px) = 0;  // a shadow toward the bottom right, outside the box
    virtual void DropShadow(const Box& box, int px) = 0;    // a dark band under the box
    virtual void Text(std::string_view utf8, const Box& box, Rgb color, Font font, unsigned flags) = 0;
    virtual void Text16(std::u16string_view text, const Box& box, Rgb color, Font font, unsigned flags) = 0;  // raw
    // <dataDir>\images\<key>.png, fitted into the box keeping its proportions. False when there is no such picture.
    virtual auto Image(std::string_view key, const Box& box) -> bool = 0;
    // A picture made of pixels (BGRA, 8 bits each, not premultiplied), drawn fitted into the box keeping its proportions.
    // Kept by `key` after the first draw: draw a changed picture under a new key.
    virtual void Pixels(std::string_view key, int width, int height, const std::vector<std::uint8_t>& bgra, const Box& box) = 0;
    // Drawing is cut to the box until PopClip (nested clips intersect).
    virtual void PushClip(const Box& box) = 0;
    virtual void PopClip() = 0;
    // The search terms marked in text from now on (nullptr: none), as the wiki marks matches in its pages.
    virtual void SetHighlight(const Highlight* highlight) = 0;
};

}  // namespace esrfe::overlay
