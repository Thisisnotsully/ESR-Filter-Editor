// The on-screen keyboard for typing with a controller, as the wiki overlay has it: the per-language layouts and the
// key moves of pad_logic.hpp (OskLayout, OskMove), the keys drawn as a box of keys under the text box. This part has
// no Windows: which key is highlighted, what a key press does, and where the box goes.
#pragma once

#include <string>
#include <vector>

#include "geometry.hpp"
#include "pad_logic.hpp"

namespace esrfe::overlay {

class OnScreenKeyboard {
public:
    enum class Action {
        None,
        Type,       // type `ch`
        Backspace,
        Clear,
        Done,       // close the keyboard
        Toggled,    // the letters page and the accents page swapped
    };
    struct Result {
        Action action = Action::None;
        char16_t ch = 0;
    };

    // `locale` is the loader's language code (deDE ...); it picks the layout.
    void Open(const std::string& locale);
    void Close() { open_ = false; }
    [[nodiscard]] auto IsOpen() const -> bool { return open_; }

    [[nodiscard]] auto Rows() const -> const pad::OskRows& { return rows_; }
    [[nodiscard]] auto Row() const -> int { return row_; }
    [[nodiscard]] auto Col() const -> int { return col_; }
    [[nodiscard]] auto AtBottomRow() const -> bool { return !rows_.empty() && row_ == static_cast<int>(rows_.size()) - 1; }
    void Highlight(int row, int col);
    void Move(int dx, int dy) { pad::OskMove(rows_, row_, col_, dx, dy); }

    // Presses a key (the highlighted one, or one clicked). The accents key swaps the page here.
    auto Press(int row, int col) -> Result;
    auto PressHighlighted() -> Result { return Press(row_, col_); }

private:
    bool open_ = false;
    bool accents_ = false;
    std::string locale_;
    pad::OskRows rows_;
    int row_ = 1;
    int col_ = 0;
};

// The keyboard's box and its keys, for `rows` under (or, without room, over) `anchor` (the text box) inside `client`.
struct KeyboardGeometry {
    struct Key {
        Box box;
        int row = 0;
        int col = 0;
    };
    Box box;
    std::vector<Key> keys;
    int keyHeight = 0;
};

struct KeyboardMetrics {
    int unit = 22;       // one half-key, in pixels (a normal key is two)
    int keyHeight = 34;
    int gap = 4;         // between keys
    int pad = 8;         // inside the box
    int edge = 8;        // kept clear at the window's edges
    int offset = 4;      // between the text box and the keyboard
};

auto LayoutKeyboard(const pad::OskRows& rows, const Box& anchor, const Box& client, KeyboardMetrics metrics) -> KeyboardGeometry;

}  // namespace esrfe::overlay
