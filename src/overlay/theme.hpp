// The look of the overlay. The defaults are the ESR Wiki overlay's: the palette taken from the Eastern Sun Resurrected
// launcher (near-black warm brown panels, thin bronze borders, gold headings, cream text, a muted tan for labels), the
// framed background picture, Segoe UI at 10/8/10/13/9 pt. The editor may change any of it before Start.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace esrfe::overlay {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    constexpr auto operator==(const Rgb&) const -> bool = default;
};

struct Theme {
    // ---- colours (the wiki's kBackground ... kBody) ----
    Rgb background{12, 8, 5};
    Rgb panel{24, 17, 11};
    Rgb tile{20, 14, 9};
    Rgb field{38, 28, 18};        // text boxes (the wiki's search box fill)
    Rgb selected{64, 46, 22};
    Rgb border{122, 92, 48};
    Rgb gold{224, 182, 96};       // accents, selected frames
    Rgb headerGold{208, 166, 82};  // titles and headings
    Rgb text{244, 236, 220};
    Rgb dim{168, 152, 124};
    Rgb snippet{206, 192, 166};
    Rgb body{228, 218, 198};      // default text colour, and colour code text with no known colour
    Rgb highlight{104, 74, 20};   // fill under search matches (kHighlightFill)
    Rgb focus{255, 240, 180};     // the controller's focus frame
    Rgb tooltipFill{10, 7, 4};
    Rgb placeholder{128, 114, 92};  // the grey hint in an empty text box
    Rgb primaryFill{196, 152, 66};  // the gold "PLAY" button (the wiki's switch buttons)
    Rgb primaryLight{250, 226, 160};
    Rgb primaryDark{122, 88, 28};
    Rgb primaryText{58, 40, 14};
    Rgb danger{200, 70, 60};

    // ---- see-through fills (0..255) ----
    std::uint8_t panelAlpha = 185;    // panels and list rows
    std::uint8_t controlAlpha = 200;  // buttons, chips, tabs
    std::uint8_t keyAlpha = 235;      // on-screen keyboard keys, selected header button
    std::uint8_t tooltipAlpha = 214;
    std::uint8_t popupAlpha = 238;    // on-screen keyboard box, drop-down lists, dialogs
    std::uint8_t dimAlpha = 150;      // the darkening behind a dialog

    // ---- fonts, in points at 96 dpi before the window-size scale ----
    std::string fontFace = "Segoe UI";
    int bodyPoints = 10;
    int smallPoints = 8;
    int boldPoints = 10;   // semibold
    int titlePoints = 13;  // semibold
    int snippetPoints = 9;  // never below 9 pt at the display's scaling

    // ---- the background picture: <dataDir>\images\<backgroundImage>.png, drawn as a frame (nine pieces) ----
    std::string backgroundImage = "background";  // empty: a plain background colour
    int frameSlice = 22;    // the picture's frame on the left, right and bottom, in its own pixels
    int headerSlice = 128;  // its header band on top, whose last row is a light line
    std::uint8_t backgroundTint = 150;  // a dark tint over the picture keeps the text readable

    // ---- window size against the game's client area (the wiki's FitToGame) ----
    int widthPercent = 60;
    int heightPercent = 72;
    int minWidth = 960, minHeight = 620;    // 96-dpi units
    int maxWidth = 1500, maxHeight = 1000;  // 96-dpi units

    // A line of keyboard help at the bottom of the window (Enter, Esc, Tab ...) while no controller is in use.
    bool keyboardHelp = true;

    // Colour codes: the colour for each X of ÿcX ('0'..'V'), as the game shows them. `known` is false for codes with
    // no colour here: they draw in `body`. The defaults are the D2 palette as the wiki draws it (approximations).
    struct CodeColor {
        Rgb color{};
        bool known = false;
    };
    std::array<CodeColor, 128> codeColors = DefaultCodeColors();

    [[nodiscard]] auto ColorOfCode(char code) const -> Rgb {
        const auto index = static_cast<unsigned char>(code);
        if (index < codeColors.size() && codeColors[index].known) return codeColors[index].color;
        return body;
    }

    static constexpr auto DefaultCodeColors() -> std::array<CodeColor, 128> {
        std::array<CodeColor, 128> c{};
        const auto set = [&c](char code, Rgb color) { c[static_cast<unsigned char>(code)] = CodeColor{color, true}; };
        set('0', Rgb{235, 235, 235});  // white
        set('1', Rgb{255, 90, 90});    // red
        set('2', Rgb{90, 220, 90});    // green, set items
        set('3', Rgb{110, 140, 255});  // blue, magic
        set('4', Rgb{201, 185, 150});  // gold/beige, uniques
        set('5', Rgb{150, 150, 150});  // gray
        set('6', Rgb{130, 130, 130});  // black in D2, lifted for a dark theme
        set('7', Rgb{190, 170, 120});  // tan
        set('8', Rgb{255, 150, 60});   // orange, crafted
        set('9', Rgb{240, 240, 110});  // yellow, rares
        set(':', Rgb{110, 210, 110});  // dark green
        set(';', Rgb{190, 120, 235});  // purple
        return c;
    }
};

}  // namespace esrfe::overlay
