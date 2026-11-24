// OnScreenKeyboard::Press is the wiki overlay's PressOskKey and LayoutKeyboard its DrawOsk geometry (overlay.cpp),
// without the search box: they work on any text box.
#include "osk.hpp"

#include <algorithm>

namespace esrfe::overlay {

void OnScreenKeyboard::Open(const std::string& locale) {
    if (rows_.empty() || locale != locale_) {
        locale_ = locale;
        accents_ = false;
        rows_ = pad::OskLayout(accents_, locale_);
    }
    open_ = true;
    row_ = 1;  // the first row of letters (the wiki starts there)
    col_ = 0;
}

void OnScreenKeyboard::Highlight(int row, int col) {
    if (rows_.empty()) return;
    row_ = std::clamp(row, 0, static_cast<int>(rows_.size()) - 1);
    col_ = std::clamp(col, 0, static_cast<int>(rows_[static_cast<std::size_t>(row_)].size()) - 1);
}

auto OnScreenKeyboard::Press(int row, int col) -> Result {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    const auto& keys = rows_[static_cast<std::size_t>(row)];
    if (col < 0 || col >= static_cast<int>(keys.size())) return {};
    const pad::OskKey& key = keys[static_cast<std::size_t>(col)];
    row_ = row;
    col_ = col;
    switch (key.kind) {
    case pad::KeyKind::Char:
    case pad::KeyKind::Space:
        return Result{Action::Type, static_cast<char16_t>(key.ch)};
    case pad::KeyKind::Backspace:
        return Result{Action::Backspace, 0};
    case pad::KeyKind::Clear:
        return Result{Action::Clear, 0};
    case pad::KeyKind::Done:
        open_ = false;
        return Result{Action::Done, 0};
    case pad::KeyKind::Accents:
        accents_ = !accents_;
        rows_ = pad::OskLayout(accents_, locale_);
        row_ = std::min(row_, static_cast<int>(rows_.size()) - 1);
        col_ = std::min(col_, static_cast<int>(rows_[static_cast<std::size_t>(row_)].size()) - 1);
        return Result{Action::Toggled, 0};
    }
    return {};
}

auto LayoutKeyboard(const pad::OskRows& rows, const Box& anchor, const Box& client, KeyboardMetrics m) -> KeyboardGeometry {
    KeyboardGeometry g;
    if (rows.empty()) return g;
    int units = 20;  // the widest row, in half-key units (20 unless the language's letters need more)
    for (const auto& row : rows) units = std::max(units, pad::RowSpan(row));
    // Narrow windows get narrower keys.
    const int widthRoom = client.Width() - 2 * m.edge - 2 * m.pad;
    if (units * m.unit > widthRoom) m.unit = std::max(8, widthRoom / units);
    const int rowCount = static_cast<int>(rows.size());
    const int innerWidth = units * m.unit;
    const int boxWidth = innerWidth + 2 * m.pad;
    int height = pad::KeyboardHeight(rowCount, m.keyHeight, m.gap, m.pad);
    // Shorter keys when the window is too short for the keyboard at all.
    while (height > client.Height() - 2 * m.edge && m.keyHeight > 12) {
        --m.keyHeight;
        height = pad::KeyboardHeight(rowCount, m.keyHeight, m.gap, m.pad);
    }
    int top = anchor.bottom + m.offset;
    if (top + height > client.bottom - m.edge) {
        const int above = anchor.top - m.offset - height;
        // Over the text box when there is room there; otherwise moved up from the bottom edge (the wiki's rule).
        top = above >= client.top + m.edge ? above : std::max(client.top + m.edge, client.bottom - m.edge - height);
    }
    int left = std::clamp(anchor.left, client.left + m.edge, std::max(client.left + m.edge, client.right - m.edge - boxWidth));
    g.box = Box{left, top, left + boxWidth, top + height};
    g.keyHeight = m.keyHeight;
    const int keysLeft = left + (boxWidth - innerWidth) / 2;
    for (int r = 0; r < rowCount; ++r) {
        const auto& keys = rows[static_cast<std::size_t>(r)];
        const int y = top + m.pad + r * (m.keyHeight + m.gap);
        const int rowLeft = keysLeft + (units - pad::RowSpan(keys)) * m.unit / 2;  // a shorter row is centred
        int cumulative = 0;
        for (int c = 0; c < static_cast<int>(keys.size()); ++c) {
            const pad::OskKey& key = keys[static_cast<std::size_t>(c)];
            const Box box{rowLeft + cumulative * m.unit, y, rowLeft + (cumulative + key.span) * m.unit - m.gap, y + m.keyHeight};
            cumulative += key.span;
            g.keys.push_back(KeyboardGeometry::Key{box, r, c});
        }
    }
    return g;
}

}  // namespace esrfe::overlay
