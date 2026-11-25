// Canvas over GDI and GDI+, with the wiki overlay's drawing code: FillPanel, FrameColor, CornerShadow, DropShadow,
// Text / LayoutPieces, the measure cache, the image cache and the framed background picture. UI thread only.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// GDI+ needs the COM types (IStream, PROPID) that objidl.h declares. Include it first.
#include <objidl.h>
#include <gdiplus.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "canvas.hpp"

namespace esrfe::overlay {

class GdiCanvas final : public Canvas {
public:
    GdiCanvas(const Theme& theme, std::filesystem::path dataDir);
    ~GdiCanvas() override;
    GdiCanvas(const GdiCanvas&) = delete;
    auto operator=(const GdiCanvas&) -> GdiCanvas& = delete;

    // The display's dpi (LOGPIXELSY) and the window-size scale on top of it (fonts only, as the wiki's uiScale).
    void SetScale(int dpi, double uiScale);
    [[nodiscard]] auto Dpi() const -> int { return dpi_; }
    [[nodiscard]] auto UiScale() const -> double { return uiScale_; }
    [[nodiscard]] auto FontOf(Font font) const -> HFONT;

    // The frame being drawn: everything goes into `dc` (a memory DC the size of the client area).
    void Begin(HDC dc, int width, int height);
    void End();

    // <dataDir>\images\<key>.png, loaded once (nullptr when missing).
    auto FindImage(const std::string& key) -> Gdiplus::Bitmap*;
    void ReleaseImages();
    // The background picture (Theme::backgroundImage) drawn as a frame, composed once per window size and header line.
    void DrawBackground(HDC target, int width, int height, int headerLineY);
    void ReleaseBackground();

    // ---- Measurer ----
    [[nodiscard]] auto GetTheme() const -> const Theme& override { return theme_; }
    [[nodiscard]] auto S(int value) const -> int override { return MulDiv(value, dpi_, 96); }
    auto LineHeight(Font font) -> int override;
    auto TextWidth(std::string_view utf8, Font font, unsigned flags = 0) -> int override;
    auto TextWidth16(std::u16string_view text, Font font) -> int override;
    auto TextHeight(std::string_view utf8, int width, Font font, unsigned flags = 0) -> int override;

    // ---- Canvas ----
    void Fill(const Box& box, Rgb color) override;
    void FillAlpha(const Box& box, Rgb color, std::uint8_t alpha) override;
    void Frame(const Box& box, Rgb color) override;
    void CornerShadow(const Box& box, int px) override;
    void DropShadow(const Box& box, int px) override;
    void Text(std::string_view utf8, const Box& box, Rgb color, Font font, unsigned flags) override;
    void Text16(std::u16string_view text, const Box& box, Rgb color, Font font, unsigned flags) override;
    auto Image(std::string_view key, const Box& box) -> bool override;
    void Pixels(std::string_view key, int width, int height, const std::vector<std::uint8_t>& bgra, const Box& box) override;
    void PushClip(const Box& box) override;
    void PopClip() override;
    void SetHighlight(const Highlight* highlight) override { highlight_ = highlight; }

private:
    struct Layer {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        Box band{};
        int saved = 0;  // SaveDC of the layer
    };
    void ApplyFonts();
    void DeleteFonts();
    auto MeasureDc() -> HDC;  // the frame's DC, or a memory DC for measuring between frames
    void DrawWide(const std::u16string& text, const Box& box, Rgb color, Font font, unsigned flags);
    auto LayoutPieces(const std::vector<Piece>& pieces, const Box& box, Font font, unsigned flags, bool draw) -> int;
    [[nodiscard]] auto Current() const -> HDC { return layers_.empty() ? dc_ : layers_.back().dc; }

    Theme theme_;
    std::filesystem::path dataDir_;
    int dpi_ = 96;
    double uiScale_ = 1.0;
    HFONT fonts_[6] = {};
    int lineHeights_[6] = {};
    HDC dc_ = nullptr;
    int width_ = 0, height_ = 0;
    HDC measureDc_ = nullptr;
    HDC pixelDc_ = nullptr;  // the one-pixel source of FillAlpha
    HBITMAP pixelBitmap_ = nullptr;
    HGDIOBJ pixelPrevious_ = nullptr;
    std::vector<Layer> layers_;
    std::vector<Layer> spareLayers_;  // kept for the next frame while the size stays
    const Highlight* highlight_ = nullptr;
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> images_;
    std::map<std::string, std::pair<std::vector<std::uint8_t>, std::unique_ptr<Gdiplus::Bitmap>>> pixels_;  // the bitmap uses the bytes
    struct MeasureKey {
        std::size_t textHash;
        int width;
        int font;
        bool operator==(const MeasureKey& other) const { return textHash == other.textHash && width == other.width && font == other.font; }
    };
    struct MeasureKeyHash {
        std::size_t operator()(const MeasureKey& key) const {
            return key.textHash ^ (static_cast<std::size_t>(key.width) * 1000003u) ^ (static_cast<std::size_t>(key.font) * 7919u);
        }
    };
    std::unordered_map<MeasureKey, int, MeasureKeyHash> measureCache_;
    HDC backgroundDc_ = nullptr;
    HBITMAP backgroundBitmap_ = nullptr;
    HGDIOBJ backgroundPrevious_ = nullptr;
    int backgroundWidth_ = 0, backgroundHeight_ = 0, backgroundSplit_ = 0;
};

auto ToColorRef(Rgb c) -> COLORREF;
auto ToRect(const Box& box) -> RECT;
auto ToBox(const RECT& rect) -> Box;

}  // namespace esrfe::overlay
