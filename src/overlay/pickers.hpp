// Controls for choosing an amount and a colour: Slider (a volume), Swatch (a colour button drawn in its colour, with
// its name in a colour that reads on it), ColorPicker (any colour: a hue strip and a saturation / brightness square).
// Mouse (click and drag), keyboard (arrows, Home, End) and controller (D-pad) all work.
#pragma once

#include <functional>
#include <string>

#include "widgets.hpp"

namespace esrfe::overlay {

class Slider : public Widget {
public:
    int value = 0;
    int minimum = 0;
    int maximum = 100;
    int step = 5;  // arrows and D-pad
    std::function<void(int, Ui&)> onChange;
    std::function<void(int, Ui&)> onCommit;  // once it stops moving: the mouse let go, or after a key / D-pad / wheel step

    void SetValue(int v);  // clamped, no onChange
    [[nodiscard]] auto Dragging() const -> bool { return dragging_; }
    [[nodiscard]] auto Track() const -> Box;
    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Wheel(int steps, int x, int y, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;

private:
    void Set(int v, Ui& ui, bool commit = true);  // clamped; onChange when it changed, then onCommit unless dragging
    int startValue_ = 0;  // when the drag began
    void FromX(int x, Ui& ui);
    bool dragging_ = false;
};

class Swatch : public Button {
public:
    Rgb color{};
    bool selected = false;
    void Paint(Canvas& canvas, const PaintState& state) override;
};

// Hue 0-359, saturation and brightness 0-100.
struct Hsv {
    int h = 0, s = 100, v = 100;
};
auto ToRgb(const Hsv& hsv) -> Rgb;
auto ToHsv(Rgb rgb) -> Hsv;

class ColorPicker : public Widget {
public:
    std::function<void(Rgb, Ui&)> onChange;

    void SetColor(Rgb color);  // no onChange
    [[nodiscard]] auto Color() const -> Rgb { return ToRgb(hsv_); }
    [[nodiscard]] auto Square() const -> Box;  // saturation left to right, brightness bottom to top
    [[nodiscard]] auto Strip() const -> Box;   // hue left to right
    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;

private:
    void Changed(Ui& ui);
    void At(int x, int y, Ui& ui);
    enum class Drag { None, Square, Strip } drag_ = Drag::None;
    Hsv hsv_{};
};

}  // namespace esrfe::overlay
