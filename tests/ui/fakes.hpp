// The overlay test fakes (from tests/overlay/overlay_logic_test.cpp): a canvas that measures 7 px per UTF-16 unit, and a
// platform that records what the views ask for.
#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "color_text.hpp"
#include "form.hpp"
#include "utf.hpp"
namespace fakes {
using namespace esrfe::overlay;

// Every UTF-16 unit is 7 px wide, a line is 16 px high (20 for the title font); wrapping follows WrapPieces.
class FakeCanvas final : public Canvas {
public:
    Theme theme;
    int texts = 0;
    int clipDepth = 0;
    int maxClipDepth = 0;
    bool clipUnbalanced = false;
    std::vector<std::string> drawn;
    const Highlight* highlight = nullptr;
    int highlightedTexts = 0;

    [[nodiscard]] auto GetTheme() const -> const Theme& override { return theme; }
    [[nodiscard]] auto S(int value) const -> int override { return value; }
    auto LineHeight(Font font) -> int override { return font == Font::Title ? 20 : 16; }
    auto TextWidth(std::string_view utf8, Font, unsigned flags) -> int override {
        const std::string plain = (flags & text_flags::kRaw) ? std::string(utf8) : StripColorCodes(utf8);
        return static_cast<int>(Utf8ToUtf16(plain).size()) * 7;
    }
    auto TextWidth16(std::u16string_view text, Font) -> int override { return static_cast<int>(text.size()) * 7; }
    auto TextHeight(std::string_view utf8, int width, Font font, unsigned) -> int override {
        const auto pieces = ColorPieces(utf8, theme.body, theme);
        const Wrapped w = WrapPieces(pieces, width, true, [](std::u16string_view t) { return static_cast<int>(t.size()) * 7; });
        return w.lines * LineHeight(font);
    }
    void Fill(const Box&, Rgb) override {}
    void FillAlpha(const Box&, Rgb, std::uint8_t) override {}
    void Frame(const Box&, Rgb) override {}
    void CornerShadow(const Box&, int) override {}
    void DropShadow(const Box&, int) override {}
    void Text(std::string_view utf8, const Box&, Rgb, Font, unsigned flags) override {
        ++texts;
        drawn.emplace_back(utf8);
        if (highlight != nullptr && (flags & text_flags::kNoHighlight) == 0 &&
            !HighlightRanges(*highlight, Utf8ToUtf16(StripColorCodes(utf8))).empty())
            ++highlightedTexts;
    }
    void Text16(std::u16string_view text, const Box&, Rgb, Font, unsigned) override {
        ++texts;
        drawn.push_back(Utf16ToUtf8(text));
    }
    auto Image(std::string_view, const Box&) -> bool override { return false; }
    void PushClip(const Box&) override { maxClipDepth = std::max(maxClipDepth, ++clipDepth); }
    void PopClip() override {
        if (--clipDepth < 0) clipUnbalanced = true;
    }
    void SetHighlight(const Highlight* h) override { highlight = h; }
};

class FakePlatform final : public Platform {
public:
    FakeCanvas canvas;
    std::uint64_t now = 100000;
    bool pad = false;
    std::string clipboard;
    int repaints = 0;
    int closes = 0;
    std::vector<std::shared_ptr<View>> pushed;
    int pops = 0;
    std::vector<std::function<void(Ui&)>> posted;

    auto GetMeasurer() -> Measurer& override { return canvas; }
    void Repaint() override { ++repaints; }
    [[nodiscard]] auto Now() const -> std::uint64_t override { return now; }
    [[nodiscard]] auto PadActive() const -> bool override { return pad; }
    [[nodiscard]] auto PadKind() const -> pad::Kind override { return pad ? pad::Kind::Xbox : pad::Kind::None; }
    auto ClipboardText() -> std::string override { return clipboard; }
    void SetClipboardText(const std::string& utf8) override { clipboard = utf8; }
    void PostToUi(std::function<void(Ui&)> work) override { posted.push_back(std::move(work)); }
    auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread) -> bool override {
        work();
        posted.push_back(std::move(then));
        return true;
    }
    void Close() override { ++closes; }
    void PushView(std::shared_ptr<View> view) override { pushed.push_back(std::move(view)); }
    void PopView() override { ++pops; }
};

}  // namespace fakes
