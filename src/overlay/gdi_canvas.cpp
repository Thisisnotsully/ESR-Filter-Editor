// From the ESR Wiki overlay's overlay.cpp: FillPanel, FillColor, FrameColor, CornerShadow, DropShadow, Text,
// LayoutPieces (its layout now in color_text.cpp's WrapPieces), MeasureHeight with its cache, MeasureWidth, LineHeight,
// MakeFont / ApplyFonts, FindImage, DrawImageKey, DrawBackground and EnsureBackgroundCache.
//
// Clipping: the wiki found that once GDI+ has drawn into a DC, later AlphaBlend fills there ignore the DC's clip, so it
// drew the scrolling pane onto a copy of the frame and copied only the visible band back. PushClip does the same: the
// band is copied into a layer, drawn there (also clipped with IntersectClipRect), and copied back on PopClip.
#include "gdi_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "utf.hpp"

namespace esrfe::overlay {

static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows: wchar_t is UTF-16");

namespace {

using namespace text_flags;

auto W(const std::u16string& text) -> const wchar_t* { return reinterpret_cast<const wchar_t*>(text.c_str()); }
auto W(std::u16string_view text) -> const wchar_t* { return reinterpret_cast<const wchar_t*>(text.data()); }

auto DrawFlags(unsigned flags) -> UINT {
    UINT format = DT_NOPREFIX;
    if (flags & kCenter) format |= DT_CENTER;
    else if (flags & kRight) format |= DT_RIGHT;
    else format |= DT_LEFT;
    if (flags & (kSingleLine | kEllipsis)) {
        format |= DT_SINGLELINE;
        if (flags & kVCenter) format |= DT_VCENTER;
        if (flags & kEllipsis) format |= DT_END_ELLIPSIS;
    } else {
        format |= DT_WORDBREAK;
    }
    return format;
}

constexpr auto Index(Font font) -> int { return static_cast<int>(font); }

}  // namespace

auto ToColorRef(Rgb c) -> COLORREF { return RGB(c.r, c.g, c.b); }
auto ToRect(const Box& box) -> RECT { return RECT{box.left, box.top, box.right, box.bottom}; }
auto ToBox(const RECT& rect) -> Box {
    return Box{static_cast<int>(rect.left), static_cast<int>(rect.top), static_cast<int>(rect.right), static_cast<int>(rect.bottom)};
}

GdiCanvas::GdiCanvas(const Theme& theme, std::filesystem::path dataDir) : theme_(theme), dataDir_(std::move(dataDir)) {}

GdiCanvas::~GdiCanvas() {
    End();
    for (Layer& layer : spareLayers_) {
        SelectObject(layer.dc, layer.previous);
        DeleteObject(layer.bitmap);
        DeleteDC(layer.dc);
    }
    spareLayers_.clear();
    ReleaseBackground();
    ReleaseImages();
    DeleteFonts();
    if (pixelDc_ != nullptr) {
        SelectObject(pixelDc_, pixelPrevious_);
        DeleteObject(pixelBitmap_);
        DeleteDC(pixelDc_);
    }
    if (measureDc_ != nullptr) DeleteDC(measureDc_);
}

// ---- fonts ---------------------------------------------------------------------------------------------------------

void GdiCanvas::DeleteFonts() {
    for (HFONT& font : fonts_) {
        if (font != nullptr) DeleteObject(font);
        font = nullptr;
    }
}

void GdiCanvas::SetScale(int dpi, double uiScale) {
    const bool first = fonts_[0] == nullptr;
    if (!first && dpi == dpi_ && std::fabs(uiScale - uiScale_) <= 0.02) return;
    dpi_ = dpi;
    uiScale_ = uiScale;
    ApplyFonts();
}

// Rebuilds the fonts for the current scale and re-measures their line heights. `minScale` keeps a font from shrinking
// below its size at that scale when the window is small.
void GdiCanvas::ApplyFonts() {
    DeleteFonts();
    measureCache_.clear();
    const std::wstring face = Utf8ToWide(theme_.fontFace);
    const auto make = [&](int points, int weight, double minScale) {
        const int pixelHeight = -static_cast<int>(std::lround(points * std::max(uiScale_, minScale) * dpi_ / 72.0));
        return CreateFontW(pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face.c_str());
    };
    fonts_[Index(Font::Body)] = make(theme_.bodyPoints, FW_NORMAL, 0.0);
    fonts_[Index(Font::Small)] = make(theme_.smallPoints, FW_NORMAL, 0.0);
    fonts_[Index(Font::Bold)] = make(theme_.boldPoints, FW_SEMIBOLD, 0.0);
    fonts_[Index(Font::Title)] = make(theme_.titlePoints, FW_SEMIBOLD, 0.0);
    fonts_[Index(Font::Snippet)] = make(theme_.snippetPoints, FW_NORMAL, 1.0);  // never below 9 pt at the display's scaling
    fonts_[Index(Font::SnippetBold)] = make(theme_.snippetPoints, FW_BOLD, 1.0);
    const HDC screen = GetDC(nullptr);
    for (int i = 0; i < 6; ++i) {
        TEXTMETRICW metrics{};
        const HGDIOBJ previous = SelectObject(screen, fonts_[i]);
        GetTextMetricsW(screen, &metrics);
        SelectObject(screen, previous);
        lineHeights_[i] = std::max(1, static_cast<int>(metrics.tmHeight));
    }
    ReleaseDC(nullptr, screen);
}

auto GdiCanvas::FontOf(Font font) const -> HFONT { return fonts_[Index(font)]; }

auto GdiCanvas::LineHeight(Font font) -> int {
    if (fonts_[0] == nullptr) ApplyFonts();
    return lineHeights_[Index(font)];
}

// ---- frame and layers ----------------------------------------------------------------------------------------------

void GdiCanvas::Begin(HDC dc, int width, int height) {
    if (fonts_[0] == nullptr) ApplyFonts();
    if (width != width_ || height != height_) {
        for (Layer& layer : spareLayers_) {
            SelectObject(layer.dc, layer.previous);
            DeleteObject(layer.bitmap);
            DeleteDC(layer.dc);
        }
        spareLayers_.clear();
    }
    dc_ = dc;
    width_ = width;
    height_ = height;
    highlight_ = nullptr;
    if (pixelDc_ == nullptr) {
        pixelDc_ = CreateCompatibleDC(dc);
        pixelBitmap_ = CreateCompatibleBitmap(dc, 1, 1);
        pixelPrevious_ = SelectObject(pixelDc_, pixelBitmap_);
    }
}

void GdiCanvas::End() {
    while (!layers_.empty()) PopClip();
    dc_ = nullptr;
    highlight_ = nullptr;
}

auto GdiCanvas::MeasureDc() -> HDC {
    if (dc_ != nullptr) return Current();
    if (measureDc_ == nullptr) measureDc_ = CreateCompatibleDC(nullptr);
    return measureDc_;
}

void GdiCanvas::PushClip(const Box& box) {
    if (dc_ == nullptr) return;
    const Box parent = layers_.empty() ? Box{0, 0, width_, height_} : layers_.back().band;
    Box band = box.Intersect(parent);
    if (band.Empty()) band = Box{band.left, band.top, band.left, band.top};
    Layer layer;
    if (!spareLayers_.empty()) {
        layer = spareLayers_.back();
        spareLayers_.pop_back();
    } else {
        layer.dc = CreateCompatibleDC(dc_);
        layer.bitmap = CreateCompatibleBitmap(dc_, std::max(1, width_), std::max(1, height_));
        layer.previous = SelectObject(layer.dc, layer.bitmap);
    }
    layer.band = band;
    if (!band.Empty()) BitBlt(layer.dc, band.left, band.top, band.Width(), band.Height(), Current(), band.left, band.top, SRCCOPY);
    layer.saved = SaveDC(layer.dc);
    IntersectClipRect(layer.dc, band.left, band.top, band.right, band.bottom);
    layers_.push_back(layer);
}

void GdiCanvas::PopClip() {
    if (layers_.empty()) return;
    Layer layer = layers_.back();
    layers_.pop_back();
    RestoreDC(layer.dc, layer.saved);
    const Box& band = layer.band;
    if (!band.Empty()) BitBlt(Current(), band.left, band.top, band.Width(), band.Height(), layer.dc, band.left, band.top, SRCCOPY);
    spareLayers_.push_back(layer);
}

// ---- fills ---------------------------------------------------------------------------------------------------------

void GdiCanvas::Fill(const Box& box, Rgb color) {
    if (dc_ == nullptr || box.Empty()) return;
    const HBRUSH brush = CreateSolidBrush(ToColorRef(color));
    const RECT rect = ToRect(box);
    FillRect(Current(), &rect, brush);
    DeleteObject(brush);
}

// Translucent fill so the background art shows through the panels (the wiki's FillPanel).
void GdiCanvas::FillAlpha(const Box& box, Rgb color, std::uint8_t alpha) {
    if (dc_ == nullptr || box.Empty()) return;
    SetPixel(pixelDc_, 0, 0, ToColorRef(color));
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(Current(), box.left, box.top, box.Width(), box.Height(), pixelDc_, 0, 0, 1, 1, blend);
}

void GdiCanvas::Frame(const Box& box, Rgb color) {
    if (dc_ == nullptr || box.Empty()) return;
    const HBRUSH brush = CreateSolidBrush(ToColorRef(color));
    const RECT rect = ToRect(box);
    FrameRect(Current(), &rect, brush);
    DeleteObject(brush);
}

// A shadow cast toward the bottom right: L-shaped bands just outside the rect, so the panel itself stays clear.
void GdiCanvas::CornerShadow(const Box& box, int px) {
    static const std::uint8_t alphas[] = {150, 100, 60, 30};
    for (int k = 0; k < 4; ++k) {
        const int right = box.right + k * px;  // this band's inner edge
        const int bottom = box.bottom + k * px;
        FillAlpha(Box{right, box.top + (k + 1) * px, right + px, bottom + px}, Rgb{0, 0, 0}, alphas[k]);
        FillAlpha(Box{box.left + (k + 1) * px, bottom, right + px, bottom + px}, Rgb{0, 0, 0}, alphas[k]);
    }
}

// A dark band under a panel, fading out.
void GdiCanvas::DropShadow(const Box& box, int px) {
    static const std::uint8_t alphas[] = {150, 100, 60, 30};
    for (int k = 0; k < 4; ++k)
        FillAlpha(Box{box.left + 2 * px, box.bottom + k * px, box.right - 2 * px, box.bottom + (k + 1) * px}, Rgb{0, 0, 0}, alphas[k]);
}

// ---- text ----------------------------------------------------------------------------------------------------------

// Lays the pieces out wrapped to the box's width (color_text.cpp's WrapPieces, measured with this DC's font) and draws
// them when `draw` is set. Returns the height used.
auto GdiCanvas::LayoutPieces(const std::vector<Piece>& pieces, const Box& box, Font font, unsigned flags, bool draw) -> int {
    const HDC dc = draw ? Current() : MeasureDc();
    const HGDIOBJ previous = SelectObject(dc, FontOf(font));
    const int lineHeight = LineHeight(font);
    const bool wrap = (flags & (kSingleLine | kEllipsis)) == 0;
    const Wrapped wrapped = WrapPieces(pieces, box.Width(), wrap, [dc](std::u16string_view token) {
        SIZE size{};
        GetTextExtentPoint32W(dc, W(token), static_cast<int>(token.size()), &size);
        return static_cast<int>(size.cx);
    });
    const int height = wrapped.lines * lineHeight;
    if (draw && !wrapped.placed.empty()) {
        int top = box.top;
        if (flags & kVCenter) top += (box.Height() - height) / 2;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, box.left, box.top, box.right, box.bottom);
        SetBkMode(dc, TRANSPARENT);
        const HBRUSH highlightBrush = CreateSolidBrush(ToColorRef(theme_.highlight));
        const int maxWidth = std::max(1, box.Width());
        for (const Placed& item : wrapped.placed) {
            int offset = 0;
            const int used = wrapped.lineWidths[static_cast<std::size_t>(item.line)];
            if (flags & kCenter) offset = (maxWidth - used) / 2;
            else if (flags & kRight) offset = maxWidth - used;
            if (item.highlight) {
                const RECT fill{box.left + offset + item.x, top + item.line * lineHeight, box.left + offset + item.x + item.width,
                                top + (item.line + 1) * lineHeight};
                FillRect(dc, &fill, highlightBrush);
            }
            SetTextColor(dc, ToColorRef(item.color));
            TextOutW(dc, box.left + offset + item.x, top + item.line * lineHeight, W(item.text), static_cast<int>(item.text.size()));
        }
        DeleteObject(highlightBrush);
        RestoreDC(dc, saved);
    }
    SelectObject(dc, previous);
    return height;
}

void GdiCanvas::Text(std::string_view utf8, const Box& box, Rgb color, Font font, unsigned flags) {
    if (dc_ == nullptr || utf8.empty()) return;
    const bool raw = (flags & kRaw) != 0;
    std::vector<Piece> pieces;
    if (!raw && HasColorCodes(utf8)) pieces = ColorPieces(utf8, color, theme_);
    else pieces.push_back(Piece{Utf8ToUtf16(utf8), color, false, false});
    if (highlight_ != nullptr && !highlight_->Empty() && (flags & kNoHighlight) == 0) pieces = HighlightPieces(*highlight_, pieces);
    if (pieces.size() == 1 && !pieces.front().highlight) {
        DrawWide(pieces.front().text, box, pieces.front().color, font, flags);
        return;
    }
    LayoutPieces(pieces, box, font, flags, true);
}

void GdiCanvas::Text16(std::u16string_view text, const Box& box, Rgb color, Font font, unsigned flags) {
    if (dc_ == nullptr || text.empty()) return;
    DrawWide(std::u16string(text), box, color, font, flags);
}

// The plain path: one colour, DrawText.
void GdiCanvas::DrawWide(const std::u16string& text, const Box& box, Rgb color, Font font, unsigned flags) {
    const HDC dc = Current();
    const HGDIOBJ previous = SelectObject(dc, FontOf(font));
    SetTextColor(dc, ToColorRef(color));
    SetBkMode(dc, TRANSPARENT);
    RECT rect = ToRect(box);
    const UINT format = DrawFlags(flags);
    if ((flags & kVCenter) && !(format & DT_SINGLELINE)) {  // DT_VCENTER works for one line only: centre wrapped text here
        RECT measured{0, 0, box.Width(), 0};
        DrawTextW(dc, W(text), static_cast<int>(text.size()), &measured, format | DT_CALCRECT);
        rect.top += std::max(0, (box.Height() - static_cast<int>(measured.bottom - measured.top)) / 2);
    }
    DrawTextW(dc, W(text), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, previous);
}

// Width of `text` on one line in `font` (the wiki's MeasureWidth). Colour codes take no room.
auto GdiCanvas::TextWidth(std::string_view utf8, Font font, unsigned flags) -> int {
    if (utf8.empty()) return 0;
    const std::string plain = (flags & kRaw) == 0 && HasColorCodes(utf8) ? StripColorCodes(utf8) : std::string(utf8);
    return TextWidth16(Utf8ToUtf16(plain), font);
}

auto GdiCanvas::TextWidth16(std::u16string_view text, Font font) -> int {
    if (text.empty()) return 0;
    const HDC dc = MeasureDc();
    RECT measured{0, 0, 0, 0};
    const HGDIOBJ previous = SelectObject(dc, FontOf(font));
    DrawTextW(dc, W(text), static_cast<int>(text.size()), &measured, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, previous);
    return measured.right - measured.left;
}

// Height that `text` needs when wrapped to `width` in `font`, remembered for repeat measures (the wiki's MeasureHeight:
// measuring runs GDI and the same text is measured on every repaint). Text drawn in pieces (colour codes, a search
// match) wraps a little differently from DrawText, so it is measured the same way it is drawn.
auto GdiCanvas::TextHeight(std::string_view utf8, int width, Font font, unsigned flags) -> int {
    if (utf8.empty()) return 0;
    const bool marked = highlight_ != nullptr && !highlight_->Empty() && (flags & kNoHighlight) == 0;
    const std::size_t textHash = std::hash<std::string_view>{}(utf8) ^ (marked ? std::hash<std::string>{}(highlight_->key) * 0x9E3779B1u : 0u) ^
                                 (static_cast<std::size_t>(flags & (kRaw | kSingleLine)) << 7);
    const MeasureKey key{textHash, width, Index(font)};
    const auto cached = measureCache_.find(key);
    if (cached != measureCache_.end()) return cached->second;
    const bool raw = (flags & kRaw) != 0;
    std::vector<Piece> pieces;
    if (!raw && HasColorCodes(utf8)) pieces = ColorPieces(utf8, theme_.body, theme_);
    else pieces.push_back(Piece{Utf8ToUtf16(utf8), theme_.body, false, false});
    if (marked) pieces = HighlightPieces(*highlight_, pieces);
    int height = 0;
    if (pieces.size() == 1 && !pieces.front().highlight) {
        const HDC dc = MeasureDc();
        RECT measured{0, 0, width, 0};
        const HGDIOBJ previous = SelectObject(dc, FontOf(font));
        const UINT single = (flags & (kSingleLine | kEllipsis)) ? DT_SINGLELINE : DT_WORDBREAK;
        DrawTextW(dc, W(pieces.front().text), static_cast<int>(pieces.front().text.size()), &measured, DT_LEFT | single | DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, previous);
        height = measured.bottom - measured.top;
    } else {
        height = LayoutPieces(pieces, Box{0, 0, width, 0}, font, flags, false);
    }
    if (measureCache_.size() > 50000) measureCache_.clear();
    measureCache_.emplace(key, height);
    return height;
}

// ---- images --------------------------------------------------------------------------------------------------------

auto GdiCanvas::FindImage(const std::string& key) -> Gdiplus::Bitmap* {
    const auto cached = images_.find(key);
    if (cached != images_.end()) return cached->second.get();
    const std::wstring file = (dataDir_ / L"images" / Utf8ToWide(key + ".png")).wstring();
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(file.c_str());
    if (bitmap->GetLastStatus() != Gdiplus::Ok) bitmap.reset();
    Gdiplus::Bitmap* raw = bitmap.get();
    images_.emplace(key, std::move(bitmap));
    return raw;
}

void GdiCanvas::ReleaseImages() { images_.clear(); }

auto GdiCanvas::Image(std::string_view key, const Box& box) -> bool {
    if (dc_ == nullptr || box.Empty()) return false;
    Gdiplus::Bitmap* bitmap = FindImage(std::string(key));
    if (bitmap == nullptr) return false;
    const double imageWidth = bitmap->GetWidth();
    const double imageHeight = bitmap->GetHeight();
    if (imageWidth <= 0 || imageHeight <= 0) return false;
    const double scale = std::min(box.Width() / imageWidth, box.Height() / imageHeight);
    const int drawWidth = static_cast<int>(imageWidth * scale);
    const int drawHeight = static_cast<int>(imageHeight * scale);
    Gdiplus::Graphics graphics(Current());
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(bitmap, box.left + (box.Width() - drawWidth) / 2, box.top + (box.Height() - drawHeight) / 2, drawWidth, drawHeight);
    return true;
}

// ---- background ----------------------------------------------------------------------------------------------------

// Background art: the picture's frame is drawn at its own pixel size and only the middle is stretched, so the frame
// stays thin at any window size. The picture's header band is stretched so its bottom line lands at `headerLineY`.
// A dark tint keeps the text readable. Built once per window size, so repaints only copy it.
void GdiCanvas::DrawBackground(HDC target, int areaWidth, int areaHeight, int headerLineY) {
    if (backgroundBitmap_ == nullptr || areaWidth != backgroundWidth_ || areaHeight != backgroundHeight_ || headerLineY != backgroundSplit_) {
        ReleaseBackground();
        backgroundDc_ = CreateCompatibleDC(target);
        backgroundBitmap_ = CreateCompatibleBitmap(target, areaWidth, areaHeight);
        backgroundPrevious_ = SelectObject(backgroundDc_, backgroundBitmap_);
        const RECT rect{0, 0, areaWidth, areaHeight};
        const HBRUSH brush = CreateSolidBrush(ToColorRef(theme_.background));
        FillRect(backgroundDc_, &rect, brush);
        DeleteObject(brush);
        Gdiplus::Bitmap* bitmap = theme_.backgroundImage.empty() ? nullptr : FindImage(theme_.backgroundImage);
        if (bitmap != nullptr && areaWidth > 0 && areaHeight > 0) {
            Gdiplus::Graphics graphics(backgroundDc_);
            const int sourceWidth = static_cast<int>(bitmap->GetWidth());
            const int sourceHeight = static_cast<int>(bitmap->GetHeight());
            const int side = std::min(theme_.frameSlice, sourceWidth / 4);
            const int headerSource = std::min(theme_.headerSlice, sourceHeight / 2);
            const int footer = side;
            if (sourceWidth > 0 && sourceHeight > 0 && areaWidth >= 4 * side && areaHeight >= headerSource / 2 + 3 * side) {
                const int headerDest = std::clamp(headerLineY + 1, side, areaHeight - footer - side);
                const int sourceX[4] = {0, side, sourceWidth - side, sourceWidth};
                const int sourceY[4] = {0, headerSource, sourceHeight - footer, sourceHeight};
                const int destX[4] = {0, side, areaWidth - side, areaWidth};
                const int destY[4] = {0, headerDest, areaHeight - footer, areaHeight};
                Gdiplus::ImageAttributes attributes;
                attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);  // no seams where the pieces meet
                graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        graphics.DrawImage(bitmap,
                                           Gdiplus::Rect(destX[column], destY[row], destX[column + 1] - destX[column], destY[row + 1] - destY[row]),
                                           sourceX[column], sourceY[row], sourceX[column + 1] - sourceX[column],
                                           sourceY[row + 1] - sourceY[row], Gdiplus::UnitPixel, &attributes);
                    }
                }
                Gdiplus::SolidBrush tint(Gdiplus::Color(theme_.backgroundTint, 14, 14, 14));
                graphics.FillRectangle(&tint, 0, 0, areaWidth, areaHeight);
            }
        }
        backgroundWidth_ = areaWidth;
        backgroundHeight_ = areaHeight;
        backgroundSplit_ = headerLineY;
    }
    BitBlt(target, 0, 0, areaWidth, areaHeight, backgroundDc_, 0, 0, SRCCOPY);
}

void GdiCanvas::ReleaseBackground() {
    if (backgroundDc_ != nullptr) {
        SelectObject(backgroundDc_, backgroundPrevious_);
        DeleteDC(backgroundDc_);
        backgroundDc_ = nullptr;
    }
    if (backgroundBitmap_ != nullptr) {
        DeleteObject(backgroundBitmap_);
        backgroundBitmap_ = nullptr;
    }
    backgroundWidth_ = backgroundHeight_ = 0;
}

}  // namespace esrfe::overlay
