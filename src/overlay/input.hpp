// Input as the toolkit sees it, without Windows: the Win32 host turns WM_* messages and the controller poll into these.
#pragma once

#include <cstdint>
#include <string>

#include "pad_logic.hpp"

namespace esrfe::overlay {

// The keys the toolkit acts on. Anything else arrives as Key::Other (with its virtual-key code in KeyEvent::vk).
enum class Key {
    Other,
    Escape, Tab, Enter, Space, Backspace, Delete, Insert,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    F2, F3,
    A, C, V, X, Z,
};

struct Mods {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

struct KeyEvent {
    Key key = Key::Other;
    Mods mods;
    bool repeat = false;   // auto-repeat of a held key
    unsigned vk = 0;       // the Windows virtual-key code
};

enum class MouseKind { Move, Down, Up, Wheel, Leave };

struct MouseEvent {
    MouseKind kind = MouseKind::Move;
    int x = 0, y = 0;    // client pixels
    int wheelSteps = 0;  // whole notches, up positive (MouseKind::Wheel; touchpad deltas already added up)
    int clicks = 1;      // 2 for the second press of a double click (MouseKind::Down)
    Mods mods;
};

// A controller button press after the repeat rules (pad::Repeater): D-pad presses repeat while held.
struct PadEvent {
    pad::Button button = pad::Button::A;
    bool repeat = false;  // an auto-repeat, not a fresh press (moves between panels only happen on fresh presses)
};

// One row of the help window (controller) and the help line (keyboard): the button or key and what it does. Text is
// English; it is translated (UiText) when drawn.
enum class Glyph {
    None,
    DPad, DPadUpDown, DPadLeftRight, DPadDown,
    A, B, X, Y,
    LB, RB, LBRB,
    LT, RT, LTRT,
    LeftStick, RightStick, L3, R3,
    Start, View,
};

struct HelpRow {
    Glyph pad = Glyph::None;  // None: not shown in the controller help
    std::string keys;          // keyboard keys, e.g. "Enter"; empty: not shown in the keyboard help
    std::string action;        // what it does, e.g. "Select"
};

}  // namespace esrfe::overlay
