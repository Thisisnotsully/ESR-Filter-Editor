#include "pickers.hpp"

#include <algorithm>
#include <cmath>

#include "ui_text.hpp"

namespace esrfe::overlay {
namespace {

using namespace text_flags;

auto Tr(const std::string& text, bool translate) -> std::string { return translate ? UiText(text) : text; }

auto Lerp(int a, int b, int t, int range) -> int { return range <= 0 ? a : a + (b - a) * t / range; }

}  // namespace

// ---- Slider -------------------------------------------------------------------------------------------------------

void Slider::SetValue(int v) { value = std::clamp(v, minimum, maximum); }

auto Slider::Track() const -> Box {
    const int pad = std::max(6, box.Height() / 2);
    return Box{box.left + pad, box.CentreY() - 2, box.right - pad, box.CentreY() + 2};
}

void Slider::Set(int v, Ui& ui, bool commit) {
    const int before = value;
    SetValue(v);
    if (value != before && onChange) onChange(value, ui);
    if (value != before && commit && onCommit) onCommit(value, ui);
    ui.Repaint();
}

void Slider::FromX(int x, Ui& ui) {
    const Box t = Track();
    const int width = std::max(1, t.Width());
    const int at = std::clamp(x - t.left, 0, width);
    Set(minimum + static_cast<int>(std::lround(static_cast<double>(at) * (maximum - minimum) / width)), ui, false);
}

void Slider::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& th = canvas.GetTheme();
    const Box t = Track();
    canvas.Fill(t, th.field);
    const int range = std::max(1, maximum - minimum);
    const int x = t.left + (value - minimum) * t.Width() / range;
    canvas.Fill(Box{t.left, t.top, x, t.bottom}, enabled ? th.gold : th.dim);
    const int knob = std::max(canvas.S(6), box.Height() / 3);
    const Box k{x - knob, box.CentreY() - knob, x + knob, box.CentreY() + knob};
    canvas.Fill(k, enabled ? (state.hovered || state.focused || dragging_ ? th.headerGold : th.gold) : th.dim);
    canvas.Frame(k, th.border);
}

auto Slider::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (!enabled) return false;
    switch (event.kind) {
        case MouseKind::Down:
            dragging_ = true;
            startValue_ = value;
            ui.Capture(this);
            FromX(event.x, ui);
            return true;
        case MouseKind::Move:
            if (dragging_) FromX(event.x, ui);
            return true;
        case MouseKind::Up:
            if (dragging_) {
                dragging_ = false;
                ui.Capture(nullptr);
                if (value != startValue_ && onCommit) onCommit(value, ui);
                ui.Repaint();
            }
            return true;
        default: return false;
    }
}

auto Slider::Wheel(int steps, int, int, Ui& ui) -> bool {
    if (!enabled) return false;
    Set(value + steps * step, ui);
    return true;
}

auto Slider::Key(const KeyEvent& event, Ui& ui) -> bool {
    switch (event.key) {
        case Key::Left: Set(value - step, ui); return true;
        case Key::Right: Set(value + step, ui); return true;
        case Key::Home: Set(minimum, ui); return true;
        case Key::End: Set(maximum, ui); return true;
        default: return false;
    }
}

auto Slider::Pad(const PadEvent& event, Ui& ui) -> bool {
    if (event.button == pad::Button::Left) {
        Set(value - step, ui);
        return true;
    }
    if (event.button == pad::Button::Right) {
        Set(value + step, ui);
        return true;
    }
    return false;
}

void Slider::HelpRows(std::vector<HelpRow>& out) const { out.push_back({Glyph::DPadLeftRight, "Left / Right", "Less / more"}); }

// ---- Swatch -------------------------------------------------------------------------------------------------------

void Swatch::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(box, canvas.S(2));
    canvas.Fill(box, color);
    // The name in black or white, whichever reads on the colour.
    const int light = color.r * 299 + color.g * 587 + color.b * 114;
    const Rgb ink = light > 140000 ? Rgb{0, 0, 0} : Rgb{255, 255, 255};
    canvas.Frame(box, selected ? t.headerGold : (state.hovered && enabled ? t.gold : t.border));
    if (selected) canvas.Frame(box.Inflated(-1), t.headerGold);
    canvas.Text(Tr(label, translate), box, ink, Font::Small, kCenter | kVCenter | kSingleLine);
}

// ---- ColorPicker --------------------------------------------------------------------------------------------------

auto ToRgb(const Hsv& c) -> Rgb {
    const double h = std::fmod(std::max(0, c.h), 360) / 60.0;
    const double s = std::clamp(c.s, 0, 100) / 100.0;
    const double v = std::clamp(c.v, 0, 100) / 100.0;
    const double chroma = v * s;
    const double x = chroma * (1 - std::fabs(std::fmod(h, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(h)) {
        case 0: r = chroma, g = x; break;
        case 1: r = x, g = chroma; break;
        case 2: g = chroma, b = x; break;
        case 3: g = x, b = chroma; break;
        case 4: r = x, b = chroma; break;
        default: r = chroma, b = x; break;
    }
    const double m = v - chroma;
    const auto byte = [](double d) { return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(d * 255)), 0, 255)); };
    return Rgb{byte(r + m), byte(g + m), byte(b + m)};
}

auto ToHsv(Rgb c) -> Hsv {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    const double d = high - low;
    double h = 0;
    if (d > 0) {
        if (high == r) h = 60 * std::fmod((g - b) / d, 6.0);
        else if (high == g) h = 60 * ((b - r) / d + 2);
        else h = 60 * ((r - g) / d + 4);
    }
    if (h < 0) h += 360;
    return Hsv{static_cast<int>(std::lround(h)) % 360, high > 0 ? static_cast<int>(std::lround(d / high * 100)) : 0,
               static_cast<int>(std::lround(high * 100))};
}

void ColorPicker::SetColor(Rgb color) { hsv_ = ToHsv(color); }

auto ColorPicker::Strip() const -> Box {
    const int h = std::max(10, box.Height() / 6);
    return Box{box.left, box.bottom - h, box.right, box.bottom};
}

auto ColorPicker::Square() const -> Box {
    const Box s = Strip();
    return Box{box.left, box.top, box.right, s.top - std::max(4, box.Height() / 20)};
}

void ColorPicker::Paint(Canvas& canvas, const PaintState&) {
    const Theme& t = canvas.GetTheme();
    // Painted in blocks: a GDI fill per pixel would be slow.
    const Box sq = Square();
    const int block = std::max(4, canvas.S(5));
    for (int x = sq.left; x < sq.right; x += block) {
        const int s = Lerp(0, 100, x - sq.left, std::max(1, sq.Width() - 1));
        for (int y = sq.top; y < sq.bottom; y += block) {
            const int v = Lerp(100, 0, y - sq.top, std::max(1, sq.Height() - 1));
            canvas.Fill(Box{x, y, std::min(x + block, sq.right), std::min(y + block, sq.bottom)}, ToRgb(Hsv{hsv_.h, s, v}));
        }
    }
    canvas.Frame(sq, t.border);
    const Box st = Strip();
    for (int x = st.left; x < st.right; x += block) {
        const int h = Lerp(0, 359, x - st.left, std::max(1, st.Width() - 1));
        canvas.Fill(Box{x, st.top, std::min(x + block, st.right), st.bottom}, ToRgb(Hsv{h, 100, 100}));
    }
    canvas.Frame(st, t.border);
    // The markers.
    const int mx = sq.left + hsv_.s * (sq.Width() - 1) / 100;
    const int my = sq.top + (100 - hsv_.v) * (sq.Height() - 1) / 100;
    const int r = canvas.S(5);
    canvas.Frame(Box{mx - r, my - r, mx + r, my + r}, Rgb{255, 255, 255});
    canvas.Frame(Box{mx - r - 1, my - r - 1, mx + r + 1, my + r + 1}, Rgb{0, 0, 0});
    const int hx = st.left + hsv_.h * (st.Width() - 1) / 359;
    canvas.Frame(Box{hx - 2, st.top - 2, hx + 3, st.bottom + 2}, Rgb{255, 255, 255});
}

void ColorPicker::Changed(Ui& ui) {
    if (onChange) onChange(Color(), ui);
    ui.Repaint();
}

void ColorPicker::At(int x, int y, Ui& ui) {
    if (drag_ == Drag::Square) {
        const Box sq = Square();
        hsv_.s = std::clamp((x - sq.left) * 100 / std::max(1, sq.Width() - 1), 0, 100);
        hsv_.v = std::clamp(100 - (y - sq.top) * 100 / std::max(1, sq.Height() - 1), 0, 100);
    } else if (drag_ == Drag::Strip) {
        const Box st = Strip();
        hsv_.h = std::clamp((x - st.left) * 359 / std::max(1, st.Width() - 1), 0, 359);
    }
    Changed(ui);
}

auto ColorPicker::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (!enabled) return false;
    switch (event.kind) {
        case MouseKind::Down:
            drag_ = Strip().Contains(event.x, event.y) ? Drag::Strip : Drag::Square;
            ui.Capture(this);
            At(event.x, event.y, ui);
            return true;
        case MouseKind::Move:
            if (drag_ != Drag::None) At(event.x, event.y, ui);
            return true;
        case MouseKind::Up:
            if (drag_ != Drag::None) {
                drag_ = Drag::None;
                ui.Capture(nullptr);
            }
            return true;
        default: return false;
    }
}

auto ColorPicker::Key(const KeyEvent& event, Ui& ui) -> bool {
    switch (event.key) {
        case Key::Left: hsv_.h = (hsv_.h + 350) % 360; break;
        case Key::Right: hsv_.h = (hsv_.h + 10) % 360; break;
        case Key::Up: hsv_.v = std::min(100, hsv_.v + 10); break;
        case Key::Down: hsv_.v = std::max(0, hsv_.v - 10); break;
        case Key::PageUp: hsv_.s = std::min(100, hsv_.s + 10); break;
        case Key::PageDown: hsv_.s = std::max(0, hsv_.s - 10); break;
        default: return false;
    }
    Changed(ui);
    return true;
}

auto ColorPicker::Pad(const PadEvent& event, Ui& ui) -> bool {
    switch (event.button) {
        case pad::Button::Left: hsv_.h = (hsv_.h + 350) % 360; break;
        case pad::Button::Right: hsv_.h = (hsv_.h + 10) % 360; break;
        case pad::Button::X: hsv_.s = hsv_.s >= 100 ? 0 : std::min(100, hsv_.s + 25); break;  // paler, round again
        default: return false;  // up and down move on
    }
    Changed(ui);
    return true;
}

void ColorPicker::HelpRows(std::vector<HelpRow>& out) const {
    out.push_back({Glyph::DPadLeftRight, "Left / Right", "Colour"});
    out.push_back({Glyph::None, "Up / Down", "Brighter / darker"});
    out.push_back({Glyph::X, "Page Up / Down", "Paler / stronger"});
}

}  // namespace esrfe::overlay
