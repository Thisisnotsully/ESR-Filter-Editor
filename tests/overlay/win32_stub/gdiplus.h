#pragma once
namespace Gdiplus {
typedef float REAL; typedef int INT;
enum Status { Ok = 0, GenericError = 1 };
enum Unit { UnitPixel = 2 };
enum FontStyle { FontStyleRegular = 0, FontStyleBold = 1 };
enum InterpolationMode { InterpolationModeHighQualityBicubic = 7 };
enum PixelOffsetMode { PixelOffsetModeHalf = 4 };
enum SmoothingMode { SmoothingModeAntiAlias = 4 };
enum TextRenderingHint { TextRenderingHintAntiAliasGridFit = 3 };
enum WrapMode { WrapModeTileFlipXY = 3 };
typedef int PixelFormat;
struct GdiplusStartupInput { GdiplusStartupInput(); };
Status GdiplusStartup(ULONG_PTR*, const GdiplusStartupInput*, void*); void GdiplusShutdown(ULONG_PTR);
struct Color { Color(BYTE a, BYTE r, BYTE g, BYTE b); };
struct Rect { Rect(INT x, INT y, INT w, INT h); };
struct RectF { REAL X, Y, Width, Height; };
struct PointF { PointF(REAL x, REAL y); };
class Image { public: unsigned GetWidth(); unsigned GetHeight(); Status GetLastStatus() const; };
class Bitmap : public Image { public: Bitmap(const WCHAR*); Bitmap(INT, INT, PixelFormat); Status GetHBITMAP(const Color&, HBITMAP*); };
class ImageAttributes { public: Status SetWrapMode(WrapMode); };
class Brush {};
class SolidBrush : public Brush { public: SolidBrush(const Color&); };
class Pen { public: Pen(const Color&, REAL); };
class FontFamily { public: FontFamily(const WCHAR*); };
class Font { public: Font(const FontFamily*, REAL, INT, Unit); };
class Graphics { public: Graphics(HDC); Graphics(Image*); Status SetInterpolationMode(InterpolationMode); Status SetPixelOffsetMode(PixelOffsetMode);
  Status SetSmoothingMode(SmoothingMode); Status SetTextRenderingHint(TextRenderingHint); Status Clear(const Color&);
  Status DrawImage(Image*, INT, INT, INT, INT); Status DrawImage(Image*, REAL, REAL, REAL, REAL);
  Status DrawImage(Image*, const Rect&, INT, INT, INT, INT, Unit, const ImageAttributes*);
  Status FillRectangle(const Brush*, INT, INT, INT, INT); Status DrawRectangle(const Pen*, INT, INT, INT, INT);
  Status DrawString(const WCHAR*, INT, const Font*, const PointF&, const Brush*); Status MeasureString(const WCHAR*, INT, const Font*, const PointF&, RectF*); };
}
#define PixelFormat32bppPARGB 0xE200B
