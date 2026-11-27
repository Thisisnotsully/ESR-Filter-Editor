// The window host: the general part of the ESR Wiki overlay's overlay.cpp with the wiki taken out. Kept from it, nearly
// line for line: the UI thread and its message loop (UiThread), the drawn cursor on its own thread (CursorThread,
// CursorProc, RenderCursorImage, CursorPixels, RememberGameCursor, the 1 ms timer and the left stick gliding the
// pointer), the input blocker over the game (BlockerProc, PlaceBlocker, ShowBlocker), following the game window and
// its focus (FindGameWindow, FitToGame, SyncVisibility, Show, Hide), the window-size scale of fonts and cursor, the
// double-buffered paint with the cached background, the header (title, close button), the controller poll (PadSync,
// PadTick, PadMouseUsed, PadClickAtCursor) and the controller help window (MeasureHelp, PositionHelp, RenderHelp,
// SyncHelp). What the wiki did per zone (tabs, list, cards ...) is now the Form's (form.cpp).
//
// Left out: the wiki's pages and data, its EDIT search box (text boxes are drawn widgets now), the hand-off between
// two panels (gHandoff*, BeginSwitch: the editor has one window and switches views inside it), the help picture
// files (controller_xbox.png: the help is always the list, since the rows change with the view).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <imm.h>
// GDI+ needs the COM types (IStream, PROPID) that objidl.h declares. Include it first.
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cursor_art.hpp"
#include "debug_log.hpp"
#include "demo_view.hpp"
#include "form.hpp"
#include "gamepad.hpp"
#include "gdi_canvas.hpp"
#include "help.hpp"
#include "list_math.hpp"
#include "overlay.hpp"
#include "pad_logic.hpp"
#include "ui_text.hpp"
#include "utf.hpp"

namespace esrfe::overlay {
namespace {

constexpr UINT kMsgToggle = WM_APP + 1;
constexpr UINT kMsgShutdown = WM_APP + 2;
constexpr UINT kMsgOpen = WM_APP + 3;
constexpr UINT kMsgClose = WM_APP + 4;
constexpr UINT kMsgRun = WM_APP + 5;  // posted work is waiting in the mailbox
constexpr UINT kMsgCursorShow = WM_APP + 10;  // to the cursor thread
constexpr UINT kMsgCursorHide = WM_APP + 11;
constexpr UINT_PTR kTimerFocus = 1;
constexpr UINT_PTR kTimerPad = 3;  // polls the controller while the window is showing
constexpr UINT kTimerInterval = 150;
constexpr UINT kPadInterval = 16;
constexpr BYTE kBlockerAlpha = 1;  // practically clear; keep above 0 so the layer still catches clicks meant for the game

auto Bounded(int value, int low, int high) -> int { return value < low ? low : (value > high ? high : value); }

auto Rect(int left, int top, int right, int bottom) -> RECT {
    RECT rect{left, top, right, bottom};
    return rect;
}

// ---- the game window -----------------------------------------------------------------------------------------------

// Largest visible top-level window of this process, which is the game window.
struct GameCandidate {
    HWND excluded = nullptr;
    HWND best = nullptr;
    long long area = 0;
};

BOOL CALLBACK FindGameCallback(HWND window, LPARAM value) {
    auto& candidate = *reinterpret_cast<GameCandidate*>(value);
    if (window == candidate.excluded || !IsWindowVisible(window) || GetAncestor(window, GA_ROOT) != window) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const long long area = static_cast<long long>(client.right - client.left) * (client.bottom - client.top);
    if (area > candidate.area) {
        candidate.area = area;
        candidate.best = window;
    }
    return TRUE;
}

auto FindGameWindow(HWND overlay) -> HWND {
    GameCandidate candidate{overlay};
    EnumWindows(FindGameCallback, reinterpret_cast<LPARAM>(&candidate));
    return candidate.best;
}

// Screen rectangle of the game's client area.
auto GameScreenRect(HWND game) -> RECT {
    RECT client{};
    GetClientRect(game, &client);
    POINT origin{0, 0};
    ClientToScreen(game, &origin);
    return Rect(origin.x, origin.y, origin.x + client.right, origin.y + client.bottom);
}

// ---- the drawn cursor ----------------------------------------------------------------------------------------------
// The game keeps hiding and showing the system cursor, which flickers. Over the overlay the system cursor is hidden
// instead, and a drawn copy of the game's cursor (CursorProc) follows the pointer.

// True while a window of this process (the game or the overlay) is in front. A screenshot tool such as Win+Shift+S
// puts its own window in front; then the system cursor must show and the drawn one must go.
auto OwnWindowInFront() -> bool {
    const HWND front = GetForegroundWindow();
    if (front == nullptr) return true;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    return process == GetCurrentProcessId();
}

// The game's own cursor, if the game has a system cursor to copy (null when it draws its own). It is remembered while
// the overlay is closed; its picture is drawn by the cursor window (RenderCursorImage). Showing the game's cursor
// handle itself does not work: the game keeps hiding it, so it flickers and vanishes while the pointer is still. Set
// to false to always draw the built-in gauntlet instead.
constexpr bool kUseGameCursor = true;
std::atomic<HCURSOR> gGameCursor{nullptr};
std::atomic<int> gCursorHotX{0};  // hot spot of the cursor image now drawn, in pixels
std::atomic<int> gCursorHotY{0};
std::atomic<int> gCursorDrawWidth{0};  // size of the cursor image now drawn, so tooltips can keep clear of it
std::atomic<int> gCursorDrawHeight{0};
// The screen area the overlay covers (the game window), for the drawn cursor: outside it the pointer belongs to other
// programs and shows their own cursor. Empty (all zero) means no limit.
std::atomic<int> gCoverLeft{0}, gCoverTop{0}, gCoverRight{0}, gCoverBottom{0};

auto PointInCover(const POINT& p) -> bool {
    const int r = gCoverRight.load(), b = gCoverBottom.load();
    if (r <= gCoverLeft.load() || b <= gCoverTop.load()) return true;
    return p.x >= gCoverLeft.load() && p.x < r && p.y >= gCoverTop.load() && p.y < b;
}

// Puts the input blocker exactly over the game window (its client area), so every click outside the game goes to the
// program under it as usual. Without a game window it covers all monitors.
void PlaceBlocker(HWND blocker, HWND game) {
    if (!IsWindow(game)) {
        gCoverLeft = gCoverTop = gCoverRight = gCoverBottom = 0;
        if (IsWindow(blocker))
            SetWindowPos(blocker, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                         GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    const RECT cover = GameScreenRect(game);
    gCoverLeft = cover.left;
    gCoverTop = cover.top;
    gCoverRight = cover.right;
    gCoverBottom = cover.bottom;
    if (IsWindow(blocker))
        SetWindowPos(blocker, nullptr, cover.left, cover.top, cover.right - cover.left, cover.bottom - cover.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Over the overlay the system cursor is hidden (the drawn one replaces it); with another program in front the normal
// arrow shows.
void HideSystemCursor() { SetCursor(OwnWindowInFront() ? nullptr : LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))); }  // IDC_ARROW

// While the overlay is closed: remembers the last cursor the game showed over its own window.
void RememberGameCursor() {
    if (!kUseGameCursor || !OwnWindowInFront()) return;
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || info.hCursor == nullptr) return;
    const HWND under = WindowFromPoint(info.ptScreenPos);
    DWORD process = 0;
    if (under != nullptr) GetWindowThreadProcessId(under, &process);
    if (process != GetCurrentProcessId()) return;  // the pointer is not over the game
    if (info.hCursor == LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))) return;  // the plain Windows arrow is not the game's
    gGameCursor = info.hCursor;
}

// The picture of a system cursor as premultiplied 0xAARRGGBB pixels, with its hot spot. False when it cannot be read
// (for example a black and white cursor).
auto CursorPixels(HCURSOR cursor, std::vector<std::uint32_t>& pixels, int& width, int& height, int& hotX, int& hotY) -> bool {
    ICONINFO icon{};
    if (!GetIconInfo(cursor, &icon)) return false;
    bool ok = false;
    BITMAP color{};
    if (icon.hbmColor != nullptr && GetObjectW(icon.hbmColor, sizeof(color), &color) != 0 && color.bmWidth > 0 && color.bmHeight > 0 &&
        color.bmWidth <= 512 && color.bmHeight <= 512) {
        width = color.bmWidth;
        height = color.bmHeight;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;  // top row first
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const HDC screen = GetDC(nullptr);
        pixels.assign(static_cast<std::size_t>(width) * height, 0);
        if (GetDIBits(screen, icon.hbmColor, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height) {
            bool hasAlpha = false;
            for (const std::uint32_t pixel : pixels) hasAlpha = hasAlpha || (pixel >> 24) != 0;
            if (hasAlpha) {
                for (std::uint32_t& pixel : pixels) {  // colours were not multiplied by alpha
                    const std::uint32_t a = pixel >> 24;
                    pixel = (a << 24) | ((((pixel >> 16) & 255) * a / 255) << 16) | ((((pixel >> 8) & 255) * a / 255) << 8) |
                            ((pixel & 255) * a / 255);
                }
                ok = true;
            } else if (icon.hbmMask != nullptr) {  // no alpha: the mask says which pixels are drawn
                std::vector<std::uint32_t> mask(pixels.size(), 0);
                if (GetDIBits(screen, icon.hbmMask, 0, height, mask.data(), &info, DIB_RGB_COLORS) == height) {
                    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = (mask[i] & 0xFFFFFF) == 0 ? (pixels[i] | 0xFF000000u) : 0u;
                    ok = true;
                }
            }
        }
        ReleaseDC(nullptr, screen);
        hotX = static_cast<int>(icon.xHotspot);
        hotY = static_cast<int>(icon.yHotspot);
    }
    if (icon.hbmColor != nullptr) DeleteObject(icon.hbmColor);
    if (icon.hbmMask != nullptr) DeleteObject(icon.hbmMask);
    return ok;
}

// Cursor size as a percent of kCursorWidth (before display scaling). Set from the game window's height, so the cursor
// grows and shrinks with the game's resolution.
std::atomic<int> gCursorScale{100};
int gRenderedCursorScale = 0;  // scale the cursor image was last drawn at (cursor thread only)

auto CursorWidth(int dpi) -> int { return std::max(1, MulDiv(MulDiv(kCursorWidth, gCursorScale.load(), 100), dpi, 96)); }
auto CursorHeight(int dpi) -> int { return std::max(1, MulDiv(MulDiv(kCursorHeight, gCursorScale.load(), 100), dpi, 96)); }

// Moves the cursor window to the pointer, but only when the pointer has moved.
void MoveCursorToPointer(HWND window) {
    POINT point{};
    if (!GetCursorPos(&point)) return;
    auto* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (last != nullptr) {
        if (last->x == point.x && last->y == point.y) return;
        *last = point;
    }
    // The window sits up and to the left of the pointer so the cursor's hot spot is on the pointer.
    SetWindowPos(window, HWND_TOPMOST, point.x - gCursorHotX.load(), point.y - gCursorHotY.load(), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

// Draws the cursor into the cursor window with per-pixel alpha: the game's own cursor picture when one was remembered
// (RememberGameCursor), at its own size, otherwise the built-in gauntlet (cursor_art.hpp), sized for the dpi and the
// game's height. Done each time the window is shown.
void RenderCursorImage(HWND window) {
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    gRenderedCursorScale = gCursorScale.load();
    std::vector<std::uint32_t> game;
    int sourceWidth = kCursorWidth, sourceHeight = kCursorHeight;
    const std::uint32_t* source = kCursorPixels;
    int hotX = MulDiv(MulDiv(kCursorHotX, gCursorScale.load(), 100), dpi, 96);
    int hotY = MulDiv(MulDiv(kCursorHotY, gCursorScale.load(), 100), dpi, 96);
    int width = CursorWidth(dpi);
    int height = CursorHeight(dpi);
    const HCURSOR remembered = kUseGameCursor ? gGameCursor.load() : nullptr;
    int gameHotX = 0, gameHotY = 0;
    if (remembered != nullptr && CursorPixels(remembered, game, sourceWidth, sourceHeight, gameHotX, gameHotY)) {
        source = game.data();
        width = sourceWidth;  // the game's cursor is drawn at the size the game uses
        height = sourceHeight;
        hotX = gameHotX;
        hotY = gameHotY;
    } else {
        sourceWidth = kCursorWidth;
        sourceHeight = kCursorHeight;
    }
    gCursorHotX = hotX;
    gCursorHotY = hotY;
    gCursorDrawWidth = width;
    gCursorDrawHeight = height;
    const HDC screen = GetDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top row first
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
        // Scale by averaging the source pixels under each target pixel (one to one for the game's cursor). The colours
        // are already multiplied by alpha, so averaging every channel is correct. GDI scaling is not used because it
        // can clear the alpha channel.
        auto* out = static_cast<std::uint32_t*>(bits);
        for (int y = 0; y < height; ++y) {
            const int y0 = y * sourceHeight / height;
            const int y1 = std::max(y0 + 1, std::min(sourceHeight, ((y + 1) * sourceHeight + height - 1) / height));
            for (int x = 0; x < width; ++x) {
                const int x0 = x * sourceWidth / width;
                const int x1 = std::max(x0 + 1, std::min(sourceWidth, ((x + 1) * sourceWidth + width - 1) / width));
                std::uint32_t sum[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy) {
                    for (int sx = x0; sx < x1; ++sx) {
                        const std::uint32_t pixel = source[sy * sourceWidth + sx];
                        sum[0] += pixel >> 24;
                        sum[1] += (pixel >> 16) & 255;
                        sum[2] += (pixel >> 8) & 255;
                        sum[3] += pixel & 255;
                    }
                }
                const auto count = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
                out[y * width + x] = ((sum[0] / count) << 24) | ((sum[1] / count) << 16) | ((sum[2] / count) << 8) | (sum[3] / count);
            }
        }
        const HDC memory = CreateCompatibleDC(screen);
        const HGDIOBJ old = SelectObject(memory, bitmap);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        POINT origin{0, 0};
        SIZE size{width, height};
        UpdateLayeredWindow(window, screen, nullptr, &size, memory, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    ReleaseDC(nullptr, screen);
}

// What the UI thread hands to the cursor thread for the left stick, plus the cursor thread's flags. The stick moves the
// real pointer on the cursor thread (see CursorThread), so the pointer and the drawn cursor that follows it glide.
struct CursorDrive {
    std::atomic<float> stickX{0.0f}, stickY{0.0f};  // left stick, -1..1, up is positive
    std::atomic<bool> driving{false};               // a controller is in use: the stick moves the pointer
    std::atomic<bool> hidden{false};                // D-pad use hides the drawn cursor until the stick or the mouse moves
    std::atomic<int> speed{1300};                   // pixels per second at a full push
    std::atomic<LONG> pointerX{-1}, pointerY{-1};   // where the stick last put the pointer, to tell it from the real mouse
    std::atomic<ULONGLONG> movedAt{0};              // when it did (GetTickCount64)
    void Stop() {
        driving = false;
        hidden = false;
        stickX = 0.0f;
        stickY = 0.0f;
    }
};
thread_local CursorDrive* tCursorDrive = nullptr;  // the cursor thread's own overlay (CursorProc runs on that thread)

// Click-through window that draws the game's cursor at the pointer. It lives on its own thread (CursorThread), so
// repaints of the overlay cannot delay it.
LRESULT CALLBACK CursorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    thread_local bool wanted = false;  // the overlay is open; only this thread reads or writes it
    switch (message) {
    case kMsgCursorShow:
        wanted = true;
        if (!OwnWindowInFront() || (tCursorDrive != nullptr && tCursorDrive->hidden.load())) return 0;  // the timer shows it later
        if (IsWindow(window)) {
            auto* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
        }
        RenderCursorImage(window);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        MoveCursorToPointer(window);
        return 0;
    case kMsgCursorHide:
        wanted = false;
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_TIMER: {
        thread_local int polls = 0;
        if (!wanted && ++polls % 5 == 0) RememberGameCursor();  // about every 40 ms while the overlay is closed
        POINT pointer{};
        const bool inside = !GetCursorPos(&pointer) || PointInCover(pointer);
        const bool show = wanted && OwnWindowInFront() && inside && !(tCursorDrive != nullptr && tCursorDrive->hidden.load());
        if (show && !IsWindowVisible(window)) {
            auto* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
            RenderCursorImage(window);
            ShowWindow(window, SW_SHOWNOACTIVATE);
        } else if (!show && IsWindowVisible(window)) {
            ShowWindow(window, SW_HIDE);
        }
        if (IsWindowVisible(window) && gRenderedCursorScale != gCursorScale.load()) {
            RenderCursorImage(window);  // the overlay's scale changed (window resized)
            auto* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
        }
        if (IsWindowVisible(window)) MoveCursorToPointer(window);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        HideSystemCursor();
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// The controller help: it only shows, never takes the mouse or the focus.
LRESULT CALLBACK HelpProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wParam, lParam);
}

// Clicks on the blocker must not activate it (that would move keyboard focus off the panel), and closing it is
// ignored so Alt+F4 cannot remove the input block.
LRESULT CALLBACK BlockerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SETCURSOR) {
        HideSystemCursor();
        return TRUE;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_CLOSE) return 0;
    return DefWindowProcW(window, message, wParam, lParam);
}

// ---- the IME, loaded at run time like XInput (imm32 is not linked) -------------------------------------------------
struct Imm {
    using GetContextFn = HIMC(WINAPI*)(HWND);
    using ReleaseContextFn = BOOL(WINAPI*)(HWND, HIMC);
    using SetCompositionWindowFn = BOOL(WINAPI*)(HIMC, LPCOMPOSITIONFORM);
    using SetCandidateWindowFn = BOOL(WINAPI*)(HIMC, LPCANDIDATEFORM);
    using AssociateContextExFn = BOOL(WINAPI*)(HWND, HIMC, DWORD);
    HMODULE lib = nullptr;
    bool tried = false;
    GetContextFn getContext = nullptr;
    ReleaseContextFn releaseContext = nullptr;
    SetCompositionWindowFn setCompositionWindow = nullptr;
    SetCandidateWindowFn setCandidateWindow = nullptr;
    AssociateContextExFn associateContextEx = nullptr;

    auto Load() -> bool {
        if (!tried) {
            tried = true;
            lib = LoadLibraryW(L"imm32.dll");
            if (lib != nullptr) {
                getContext = reinterpret_cast<GetContextFn>(reinterpret_cast<void*>(GetProcAddress(lib, "ImmGetContext")));
                releaseContext = reinterpret_cast<ReleaseContextFn>(reinterpret_cast<void*>(GetProcAddress(lib, "ImmReleaseContext")));
                setCompositionWindow = reinterpret_cast<SetCompositionWindowFn>(reinterpret_cast<void*>(GetProcAddress(lib, "ImmSetCompositionWindow")));
                setCandidateWindow = reinterpret_cast<SetCandidateWindowFn>(reinterpret_cast<void*>(GetProcAddress(lib, "ImmSetCandidateWindow")));
                associateContextEx = reinterpret_cast<AssociateContextExFn>(reinterpret_cast<void*>(GetProcAddress(lib, "ImmAssociateContextEx")));
            }
        }
        return getContext != nullptr && releaseContext != nullptr;
    }
    ~Imm() {
        if (lib != nullptr) FreeLibrary(lib);
    }
};

auto MapKey(WPARAM vk) -> Key {
    switch (vk) {
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB: return Key::Tab;
    case VK_RETURN: return Key::Enter;
    case VK_SPACE: return Key::Space;
    case VK_BACK: return Key::Backspace;
    case VK_DELETE: return Key::Delete;
    case VK_INSERT: return Key::Insert;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_F2: return Key::F2;
    case VK_F3: return Key::F3;
    case 'A': return Key::A;
    case 'C': return Key::C;
    case 'V': return Key::V;
    case 'X': return Key::X;
    case 'Z': return Key::Z;
    default: return Key::Other;
    }
}

auto CurrentMods() -> Mods {
    Mods mods;
    mods.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mods.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return mods;
}

auto LeaveEvent() -> MouseEvent {
    MouseEvent event;
    event.kind = MouseKind::Leave;
    return event;
}

// Nothing may leave a window procedure or a loader callback as an exception.
template <typename F>
void Guard(const char* where, F&& f) {
    try {
        f();
    } catch (const std::exception& error) {
        DebugLog(std::string("overlay: ") + where + " failed: " + error.what());
    } catch (...) {
        DebugLog(std::string("overlay: ") + where + " failed");
    }
}

}  // namespace

// Work posted to the UI thread. Shared with queued game work, which may finish after the overlay stopped: then
// `window` is null and the work is dropped.
struct Mailbox {
    std::mutex lock;
    std::deque<std::function<void(Ui&)>> queue;
    HWND window = nullptr;

    auto Post(std::function<void(Ui&)> work) -> bool {
        std::lock_guard<std::mutex> guard(lock);
        if (window == nullptr || !work) return false;
        queue.push_back(std::move(work));
        PostMessageW(window, kMsgRun, 0, 0);
        return true;
    }
};

struct OverlayHost final : Platform {
    OverlayConfig config;
    ViewFactory rootFactory;
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();

    HANDLE thread = nullptr;
    HANDLE ready = nullptr;
    HWND window = nullptr;
    HWND blocker = nullptr;
    HWND cursorWindow = nullptr;
    HWND helpWindow = nullptr;
    HANDLE cursorThread = nullptr;
    DWORD cursorThreadId = 0;
    POINT lastCursor{-1, -1};
    ULONG_PTR gdiplusToken = 0;
    std::unique_ptr<GdiCanvas> canvas;
    HBRUSH backgroundBrush = nullptr;
    std::wstring windowClassName, blockerClassName, cursorClassName, helpClassName;
    std::atomic<bool> started{false};
    bool open = false;          // the player wants it open; it only shows while the game is active
    bool firstPainted = false;  // false until the current show has painted once
    HWND previousForeground = nullptr;
    HWND gameWindow = nullptr;
    RECT fittedGameRect{};
    int dpi = 96;
    double uiScale = 1.0;  // font and text scale from the window size, on top of DPI
    int width = 0;
    int height = 0;
    int helpWidth = 0;
    bool trackingMouse = false;
    bool mouseDown = false;
    bool closeHot = false;
    WheelAccumulator wheel;

    // The header and the content box, from the last layout.
    Box titleBox{}, closeBox{}, contentBox{}, helpLineBox{};
    int separatorY = 0;

    // The views, the top one showing. Changes asked for during an event wait in `deferred` until it is over.
    std::vector<std::unique_ptr<Form>> forms;
    std::vector<std::function<void()>> deferred;

    // ---- controller (pad_logic.hpp has the rules, gamepad.hpp reads the devices) ----
    pad::Gamepad gamepad;
    pad::Repeater padRepeat;
    bool padCursorMoved = false;  // the left stick moved the cursor: the next D-pad press starts from what is under it
    bool padRunning = false;      // the poll timer is on and the controller devices are open (the window is showing)
    bool padActive = false;       // a controller was the last thing used; the mouse or keyboard switches it off
    pad::Kind padKind = pad::Kind::None;
    ULONGLONG padLast = 0;        // time of the previous poll
    ULONGLONG padMouseUntil = 0;  // the left stick moved the cursor: A clicks at the cursor until this time
    CursorDrive drive;            // the left stick and the cursor flags, shared with the cursor thread
    double padScrollCarry = 0;

    // ---- help window ----
    std::string helpDrawnKey;  // what the help window shows now (kind, size, rows)
    std::size_t helpMostRows = 0;  // the window keeps the size of the longest list since it opened
    float helpWidest = 0;

    // ---- IME ----
    Imm imm;
    int imeState = -1;  // -1 unknown, 0 off, 1 on

    [[nodiscard]] auto S(int value) const -> int { return MulDiv(value, dpi, 96); }
    auto Top() -> Form& { return *forms.back(); }

    // ---- Platform ----
    auto GetMeasurer() -> Measurer& override { return *canvas; }
    void Repaint() override {
        if (window != nullptr) InvalidateRect(window, nullptr, FALSE);
    }
    [[nodiscard]] auto Now() const -> std::uint64_t override { return GetTickCount64(); }
    [[nodiscard]] auto PadActive() const -> bool override { return padActive; }
    [[nodiscard]] auto PadKind() const -> pad::Kind override { return padKind; }
    auto ClipboardText() -> std::string override;
    void SetClipboardText(const std::string& utf8) override;
    void PostToUi(std::function<void(Ui&)> work) override { mailbox->Post(std::move(work)); }
    auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread which) -> bool override;
    void Close() override {
        deferred.push_back([this] { Hide(); });
    }
    void PushView(std::shared_ptr<View> view) override;
    void PopView() override;
    [[nodiscard]] auto CursorReach() const -> Box override {
        const int hotX = gCursorHotX.load(), hotY = gCursorHotY.load();
        return Box{hotX, hotY, std::max(gCursorDrawWidth.load() - hotX, 0), std::max(gCursorDrawHeight.load() - hotY, 0)};
    }
    [[nodiscard]] auto DoubleClickMs() const -> std::uint64_t override { return GetDoubleClickTime(); }
    [[nodiscard]] auto KeyboardLocaleCode() const -> std::string override { return config.keyboardLocale; }

    // ---- window ----
    void ComputeChrome();
    void FitToGame();
    void Show();
    void Hide();
    void GoBack();
    void Toggle() {
        if (open) Hide();
        else Show();
    }
    void SyncVisibility();
    void ShowBlocker(bool show);
    void ShowCursorWindow(bool show) {
        if (cursorWindow != nullptr) PostMessageW(cursorWindow, show ? kMsgCursorShow : kMsgCursorHide, 0, 0);
    }
    void Paint(HDC target);
    void PaintKeyboardHelp();
    void AfterInput();
    void ApplyDeferred();
    void RunMailbox();
    void SyncIme();
    void HandleMouse(MouseEvent event);

    // ---- controller ----
    void PadSync();
    void PadTick();
    void PadMouseUsed();
    void PadModeChanged();
    void PadClickAtCursor();

    // ---- help ----
    auto HelpRowsNow() -> std::vector<HelpRow>;
    auto HelpIconWidth(const std::string& folder, const char* name, float iconHeight) -> float;
    void PositionHelp();
    void RenderHelp(bool force);
    void SyncHelp();
};

// ---- Platform ------------------------------------------------------------------------------------------------------

auto OverlayHost::ClipboardText() -> std::string {
    std::string text;
    if (!OpenClipboard(window)) return text;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data != nullptr) {
        const auto* wide = static_cast<const wchar_t*>(GlobalLock(data));
        if (wide != nullptr) {
            const std::wstring_view view(wide);
            text = Utf16ToUtf8(std::u16string(view.begin(), view.end()));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

void OverlayHost::SetClipboardText(const std::string& utf8) {
    const std::u16string wide = Utf8ToUtf16(utf8);
    if (!OpenClipboard(window)) return;
    EmptyClipboard();
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(char16_t));
    if (memory != nullptr) {
        auto* out = static_cast<char16_t*>(GlobalLock(memory));
        if (out != nullptr) {
            std::copy(wide.begin(), wide.end(), out);
            out[wide.size()] = 0;
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

auto OverlayHost::RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread which) -> bool {
    const auto& queue = which == GameThread::Ui ? config.queueGameUi : config.queueGameLogic;
    if (!queue || !work) return false;
    std::weak_ptr<Mailbox> box = mailbox;
    return queue([work = std::move(work), then = std::move(then), box]() mutable {
        try {
            work();
        } catch (...) {
            DebugLog("overlay: game-thread work failed");
        }
        if (!then) return;
        if (const std::shared_ptr<Mailbox> target = box.lock()) target->Post(std::move(then));
    });
}

void OverlayHost::PushView(std::shared_ptr<View> view) {
    if (!view) return;
    deferred.push_back([this, view = std::move(view)]() mutable {
        if (!forms.empty()) Top().Hidden();
        forms.push_back(std::make_unique<Form>(*this, std::move(view)));
        ComputeChrome();
        Top().Layout(Box{0, 0, width, height}, contentBox);
        Top().Shown();
        Repaint();
    });
}

void OverlayHost::PopView() {
    deferred.push_back([this] {
        if (forms.size() <= 1) {
            Hide();
            return;
        }
        Top().Hidden();
        forms.pop_back();
        Top().Shown();
        Repaint();
    });
}

void OverlayHost::ApplyDeferred() {
    for (int guard = 0; guard < 16 && !deferred.empty(); ++guard) {
        std::vector<std::function<void()>> now;
        now.swap(deferred);
        for (auto& step : now) Guard("view change", step);
    }
}

void OverlayHost::RunMailbox() {
    std::deque<std::function<void(Ui&)>> work;
    {
        std::lock_guard<std::mutex> guard(mailbox->lock);
        work.swap(mailbox->queue);
    }
    for (auto& item : work) {
        if (forms.empty()) break;
        Guard("posted work", [&] { item(Top()); });
        ApplyDeferred();
    }
    AfterInput();
}

// After every event: view changes asked for during it, the IME's state, the help window.
void OverlayHost::AfterInput() {
    ApplyDeferred();
    SyncIme();
    if (padActive) SyncHelp();
}

// The IME is on while a text box has the keyboard focus, with its composition window at the caret; off otherwise, so
// keys typed elsewhere stay keys.
void OverlayHost::SyncIme() {
    if (window == nullptr || forms.empty() || !imm.Load()) return;
    Box caret{};
    const bool want = Top().TextInputCaret(caret);
    if (imm.associateContextEx != nullptr && imeState != (want ? 1 : 0)) {
        imm.associateContextEx(window, nullptr, want ? static_cast<DWORD>(IACE_DEFAULT) : 0u);
        imeState = want ? 1 : 0;
    }
    if (!want) return;
    const HIMC context = imm.getContext(window);
    if (context == nullptr) return;
    if (imm.setCompositionWindow != nullptr) {
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT;
        form.ptCurrentPos = POINT{caret.left, caret.top};
        imm.setCompositionWindow(context, &form);
    }
    if (imm.setCandidateWindow != nullptr) {
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = POINT{caret.left, caret.bottom};
        candidate.rcArea = RECT{caret.left, caret.top, caret.right + 1, caret.bottom};
        imm.setCandidateWindow(context, &candidate);
    }
    imm.releaseContext(window, context);
}

// ---- layout and fitting --------------------------------------------------------------------------------------------

// The header as the wiki lays it out (ComputeLayout): title and close button on one row near the top frame, the
// picture's header line under them, the content below, the keyboard help line at the bottom without a controller.
void OverlayHost::ComputeChrome() {
    const Theme& t = config.theme;
    const int margin = t.frameSlice + S(10);  // content sits inside the background picture's frame, with room to breathe
    const int gap = S(8);
    const int right = width - margin;
    const int headerLine = t.frameSlice + S(6);  // where the header would sit with the header line below it
    const int headerTop = headerLine - S(10);   // clear of the picture's top frame, which grows with the window
    const int headerHeight = S(28);
    const int rowTop = headerTop + S(3);  // the title and the close button share one row, centres lined up
    const int buttonTop = rowTop + S(1);
    closeBox = Box{right - S(32), buttonTop, right, buttonTop + headerHeight};
    titleBox = Box{margin, rowTop, closeBox.left - gap, rowTop + headerHeight};
    separatorY = headerLine + headerHeight + S(6);
    const int contentTop = headerLine + headerHeight + gap + S(2);
    int bottom = height - margin;
    helpLineBox = Box{};
    if (t.keyboardHelp && !padActive && canvas != nullptr) {
        const int line = canvas->LineHeight(Font::Small) + S(6);
        helpLineBox = Box{margin, bottom - line, right, bottom};
        bottom = helpLineBox.top - S(6);
    }
    contentBox = Box{margin, contentTop, right, std::max(contentTop, bottom)};
}

// Sizes the window to the game: by default 60% of the game's width and 72% of its height, clamped to a sensible range
// and centred over the game. Fonts scale with the resulting width. Without a game window it is centred on the screen.
void OverlayHost::FitToGame() {
    const Theme& t = config.theme;
    if (!IsWindow(gameWindow)) gameWindow = FindGameWindow(window);
    int left = 0;
    int top = 0;
    int w = S(t.minWidth);
    int h = S(t.minHeight);
    helpWidth = 0;
    if (IsWindow(gameWindow)) {
        const RECT game = GameScreenRect(gameWindow);
        const int cw = game.right - game.left;
        const int ch = game.bottom - game.top;
        w = std::min(Bounded(cw * t.widthPercent / 100, S(t.minWidth), S(t.maxWidth)), cw);
        h = std::min(Bounded(ch * t.heightPercent / 100, S(t.minHeight), S(t.maxHeight)), ch);
        left = game.left + (cw - w) / 2;
        top = game.top + (ch - h) / 2;
        // The controller help sits right of the window, where the window already is; the window does not move.
        if (padActive) helpWidth = std::clamp(cw * 17 / 100, S(280), S(440));
        fittedGameRect = game;
    } else {
        left = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        top = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        fittedGameRect = RECT{};
    }

    PlaceBlocker(blocker, gameWindow);

    const double scale = std::clamp(static_cast<double>(w) / S(960), 0.85, 1.5);
    if (std::fabs(scale - uiScale) > 0.02) uiScale = scale;
    canvas->SetScale(dpi, uiScale);
    // The cursor follows the game window's height, relative to the display scaling. Tuned on a 150% display: 150 px
    // wide in a 2160 px high window and 62 px wide at 1080p, which is a little more than linear. Display scaling does
    // not change it (the 1440 is a height in 96 dpi units).
    gCursorScale = std::clamp(static_cast<int>(std::lround(80.0 * std::pow(static_cast<double>(h) / S(1440), 1.27))), 20, 150);

    width = w;
    height = h;
    RECT current{};
    GetWindowRect(window, &current);
    if (current.left != left || current.top != top || current.right - current.left != w || current.bottom - current.top != h) {
        SetWindowPos(window, nullptr, left, top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ComputeChrome();
    PositionHelp();
}

// ---- show and hide -------------------------------------------------------------------------------------------------

void OverlayHost::Show() {
    const bool wasOpen = open;
    open = true;
    previousForeground = GetForegroundWindow();
    if (!wasOpen) {
        helpMostRows = 0;
        helpWidest = 0;
        if (!forms.empty()) Top().Shown();
        if (config.onOpenChanged) Guard("open callback", [&] { config.onOpenChanged(true); });
        ApplyDeferred();
    }
    FitToGame();
    SyncVisibility();
    if (IsWindowVisible(window)) {
        SetForegroundWindow(window);
        SetFocus(window);
    }
    InvalidateRect(window, nullptr, FALSE);
    if (IsWindowVisible(window)) UpdateWindow(window);
    PadSync();
    SyncIme();
}

void OverlayHost::Hide() {
    wheel.Reset();
    const bool wasOpen = open;
    open = false;
    firstPainted = false;
    if (mouseDown) {
        mouseDown = false;
        ReleaseCapture();
    }
    if (wasOpen && !forms.empty()) Top().Hidden();
    // Opened again, the editor starts at its first page.
    if (wasOpen)
        while (forms.size() > 1) forms.pop_back();
    ShowBlocker(false);
    ShowWindow(window, SW_HIDE);
    PadSync();
    if (previousForeground != nullptr && previousForeground != window && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    if (wasOpen && config.onOpenChanged) Guard("close callback", [&] { config.onOpenChanged(false); });
}

// B or Escape with nothing left to close: back a view, or closed at the first one.
void OverlayHost::GoBack() {
    if (forms.size() > 1) {
        PopView();
        ApplyDeferred();
    } else {
        Hide();
    }
}

void OverlayHost::ShowBlocker(bool show) {
    ShowCursorWindow(show);
    if (!IsWindow(blocker)) return;
    if (!show) {
        ShowWindow(blocker, SW_HIDE);
        return;
    }
    if (IsWindow(gameWindow) && GetWindow(blocker, GW_OWNER) != gameWindow)
        SetWindowLongPtrW(blocker, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
    PlaceBlocker(blocker, gameWindow);
    // Shown fully transparent and then faded to its strength, so the first frame is never opaque black.
    SetLayeredWindowAttributes(blocker, 0, 0, LWA_ALPHA);
    SetWindowPos(blocker, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(blocker, 0, kBlockerAlpha, LWA_ALPHA);
}

// Runs on a timer. The overlay is owned by the game window, so it stays above the game without being topmost. While it
// is open and the game is active, the blocker covers the game and the window holds keyboard focus. It is hidden
// whenever another app is in focus, and it follows the game when the game moves or resizes.
void OverlayHost::SyncVisibility() {
    if (!IsWindow(window)) return;
    if (!IsWindow(gameWindow)) gameWindow = FindGameWindow(window);
    bool gameActive = false;
    if (IsWindow(gameWindow)) {
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        gameActive = foregroundProcess == GetCurrentProcessId();
        if (GetWindow(window, GW_OWNER) != gameWindow) SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
    }
    // A minimised game has no client area to cover: the window waits until it is back.
    const bool gameMinimized = IsWindow(gameWindow) && IsIconic(gameWindow);
    const bool shouldShow = open && gameActive && !gameMinimized;
    const bool shown = IsWindowVisible(window) != FALSE;
    if (!shouldShow) {
        if (shown) {
            if (mouseDown) {
                mouseDown = false;
                ReleaseCapture();
            }
            if (!forms.empty()) Guard("leave", [&] { Top().Mouse(LeaveEvent()); });
            ShowBlocker(false);
            ShowWindow(window, SW_HIDE);
            firstPainted = false;
            PadSync();
        }
        return;
    }
    if (!shown) {
        FitToGame();
        ShowBlocker(true);
        // Shown fully transparent, painted once, then made opaque: no unpainted frame is ever visible.
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    } else if (IsWindow(gameWindow)) {
        const RECT game = GameScreenRect(gameWindow);
        if (game.left != fittedGameRect.left || game.top != fittedGameRect.top || game.right != fittedGameRect.right ||
            game.bottom != fittedGameRect.bottom) {
            FitToGame();
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    // Keep keyboard focus here, so the game does not receive keys while the overlay is open.
    const HWND foreground = GetForegroundWindow();
    if (foreground != window && foreground != blocker) SetForegroundWindow(window);
    PadSync();
}

// ---- painting ------------------------------------------------------------------------------------------------------

void OverlayHost::Paint(HDC target) {
    RECT client{};
    GetClientRect(window, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0 || forms.empty()) return;
    width = clientWidth;
    height = clientHeight;
    ComputeChrome();
    Guard("layout", [&] { Top().Layout(Box{0, 0, clientWidth, clientHeight}, contentBox); });

    const HDC dc = CreateCompatibleDC(target);
    const HBITMAP bitmap = CreateCompatibleBitmap(target, clientWidth, clientHeight);
    const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    canvas->DrawBackground(dc, clientWidth, clientHeight, separatorY);
    canvas->Begin(dc, clientWidth, clientHeight);
    const Theme& t = config.theme;
    using namespace text_flags;

    std::string title;
    Guard("title", [&] { title = Top().GetView().Title(); });
    if (title.empty()) title = config.title;
    // Stacked dark copies at growing offsets make a heavier shadow, which shows on the dark header.
    for (int offset : {S(1), S(2), S(3)})
        canvas->Text(title, titleBox.Offset(offset, offset), Rgb{0, 0, 0}, Font::Title, kVCenter | kSingleLine | kEllipsis | kNoHighlight);
    canvas->Text(title, titleBox, t.headerGold, Font::Title, kVCenter | kSingleLine | kEllipsis | kNoHighlight);
    canvas->DropShadow(closeBox, S(2));
    canvas->FillAlpha(closeBox, t.panel, t.controlAlpha);
    canvas->Frame(closeBox, closeHot && !padActive ? t.gold : t.border);
    canvas->Text("X", closeBox, t.text, Font::Bold, kCenter | kVCenter | kSingleLine | kRaw);
    PaintKeyboardHelp();

    Guard("paint", [&] { Top().Paint(*canvas); });
    canvas->End();
    BitBlt(target, 0, 0, clientWidth, clientHeight, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

// The keyboard's help line: "Enter Select   Esc Back ...", keys in gold, what they do in tan.
void OverlayHost::PaintKeyboardHelp() {
    if (helpLineBox.Empty()) return;
    const Theme& t = config.theme;
    using namespace text_flags;
    std::vector<HelpRow> rows;
    Guard("help", [&] { Top().CollectHelp(rows); });
    rows = KeyRowsOnly(rows);
    int x = helpLineBox.left;
    for (const HelpRow& row : rows) {
        const std::string key = UiText(row.keys);
        const std::string action = UiText(row.action);
        const int keyWidth = canvas->TextWidth(key, Font::Small, kRaw);
        const int actionWidth = canvas->TextWidth(action, Font::Small, kRaw);
        if (x + keyWidth + S(6) + actionWidth > helpLineBox.right) break;
        canvas->Text(key, Box{x, helpLineBox.top, x + keyWidth + 1, helpLineBox.bottom}, t.gold, Font::Small, kVCenter | kSingleLine | kRaw | kNoHighlight);
        x += keyWidth + S(6);
        canvas->Text(action, Box{x, helpLineBox.top, x + actionWidth + 1, helpLineBox.bottom}, t.dim, Font::Small, kVCenter | kSingleLine | kRaw | kNoHighlight);
        x += actionWidth + S(18);
    }
}

// ---- mouse ---------------------------------------------------------------------------------------------------------

void OverlayHost::HandleMouse(MouseEvent event) {
    if (forms.empty()) return;
    event.mods = CurrentMods();
    if (event.kind == MouseKind::Move || event.kind == MouseKind::Leave) {
        const bool hot = event.kind == MouseKind::Move && closeBox.Contains(event.x, event.y);
        if (hot != closeHot) {
            closeHot = hot;
            Repaint();
        }
    }
    // The close button, unless a dialog is up (it is modal over the whole window).
    if (event.kind == MouseKind::Down && closeBox.Contains(event.x, event.y) && !Top().DialogOpen()) {
        Hide();
        return;
    }
    Guard("mouse", [&] { Top().Mouse(event); });
    AfterInput();
}

// ---- controller ----------------------------------------------------------------------------------------------------
// While the window is showing, the controller is polled every 16 ms. D-pad: highlight; A: select; B: back; Y: the
// on-screen keyboard; X: clear; LB/RB: page; right stick: scroll; LT/RT: scroll faster; left stick: move the cursor
// (A then clicks it); Start: close. What each does where is the Form's and the widgets' (form.cpp).

void OverlayHost::PadSync() {
    const bool want = IsWindow(window) && IsWindowVisible(window);
    if (want == padRunning) return;
    padRunning = want;
    if (want) {
        // controller.txt in the data folder: "playstation" or "xbox" forces the help's controller kind (a PlayStation
        // controller used through Steam Input or DS4Windows looks like an Xbox one).
        pad::Kind forced = pad::Kind::None;
        std::ifstream file(config.dataDir / "controller.txt");
        std::string word;
        if (file >> word) {
            for (char& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (word.find("play") != std::string::npos || word.find("sony") != std::string::npos) forced = pad::Kind::PlayStation;
            else if (word.find("xbox") != std::string::npos) forced = pad::Kind::Xbox;
        }
        gamepad.SetKindOverride(forced);
        gamepad.Start();
        padRepeat.Reset();
        padLast = 0;
        SetTimer(window, kTimerPad, kPadInterval, nullptr);
    } else {
        KillTimer(window, kTimerPad);
        gamepad.Stop();
        const bool was = padActive;
        padActive = false;
        padMouseUntil = 0;
        drive.Stop();
        if (was && !forms.empty()) Top().PadModeChanged(false);
        SyncHelp();
    }
}

// A real mouse or keyboard was used: the controller help goes away until the controller is used again.
void OverlayHost::PadMouseUsed() {
    if (!padActive) return;
    padActive = false;
    drive.Stop();  // the stick lets go of the pointer and the cursor comes back
    if (!forms.empty()) Top().PadModeChanged(false);
    PadModeChanged();
}

// The layout reserves room for the help window while a controller is in use, so it is redone when that changes.
void OverlayHost::PadModeChanged() {
    FitToGame();
    SyncHelp();
    InvalidateRect(window, nullptr, FALSE);
}

void OverlayHost::PadClickAtCursor() {
    POINT point{};
    if (!GetCursorPos(&point)) return;
    ScreenToClient(window, &point);
    MouseEvent down;
    down.kind = MouseKind::Down;
    down.x = point.x;
    down.y = point.y;
    HandleMouse(down);
    if (!open) return;
    MouseEvent up = down;
    up.kind = MouseKind::Up;
    HandleMouse(up);
}

void OverlayHost::PadTick() {
    if (!IsWindowVisible(window) || forms.empty()) return;
    using pad::Button;
    const ULONGLONG now = GetTickCount64();
    pad::State s;
    if (!gamepad.Poll(s, now)) {
        padKind = pad::Kind::None;
        drive.Stop();
        if (padActive) {
            padActive = false;
            Top().PadModeChanged(false);
            PadModeChanged();
        }
        padLast = now;
        return;
    }
    bool pressed[pad::kButtonCount];
    bool released[pad::kButtonCount];
    padRepeat.Step(s, now, pressed, released);
    const float dt = padLast == 0 ? 0.016f : std::min(0.1f, static_cast<float>(now - padLast) / 1000.0f);
    padLast = now;
    if (!padActive) {
        if (!pad::Active(s)) return;
        padActive = true;
        padKind = s.kind;
        padMouseUntil = 0;
        POINT here{};
        if (GetCursorPos(&here)) {
            drive.pointerX.store(here.x);
            drive.pointerY.store(here.y);
        }
        drive.hidden.store(false);
        Top().PadModeChanged(true);
        PadModeChanged();
    } else if (s.kind != padKind) {
        padKind = s.kind;
        SyncHelp();
    }
    const auto press = [&](Button b) { return pressed[static_cast<int>(b)]; };
    // The directional buttons put the cursor away; the left stick or the mouse brings it back.
    if (press(Button::Left) || press(Button::Right) || press(Button::Up) || press(Button::Down)) drive.hidden.store(true);

    if (press(Button::Start)) {
        Hide();
        return;
    }
    static constexpr Button kOrder[] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::A,  Button::B, Button::X,
                                        Button::Y,  Button::LB,   Button::RB,   Button::L3,    Button::R3, Button::Back};
    for (Button b : kOrder) {
        if (!press(b) || !open || forms.empty()) continue;
        const bool direction = b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right;
        if (direction) {
            padMouseUntil = 0;
            if (padCursorMoved) {  // start from what is under the cursor
                padCursorMoved = false;
                POINT at{};
                if (GetCursorPos(&at) && ScreenToClient(window, &at)) Guard("pad focus", [&] { Top().FocusFromPoint(at.x, at.y); });
            }
        }
        if (b == Button::A && now < padMouseUntil) {  // the left stick moved the cursor: A is a click
            PadClickAtCursor();
            continue;
        }
        bool handled = false;
        Guard("pad", [&] { handled = Top().PadPress(PadEvent{b, padRepeat.IsRepeat(b)}); });
        ApplyDeferred();
        if (!handled && b == Button::B) GoBack();
    }
    if (!open || !IsWindowVisible(window) || forms.empty()) return;
    (void)released;

    // Left stick: the cursor. The cursor thread moves the pointer (see CursorThread), a millisecond at a time and eased,
    // so it glides; this poll only hands the stick over. Speed grows with the push, so small pushes are precise.
    drive.speed.store(S(1300));
    drive.stickX.store(s.lx);
    drive.stickY.store(s.ly);
    drive.driving.store(true);
    if (s.lx != 0.0f || s.ly != 0.0f) {
        drive.hidden.store(false);  // moving the cursor brings it back after the D-pad put it away
        padMouseUntil = now + 2500;
        padCursorMoved = true;
    }

    // Right stick scrolls; the triggers scroll much faster (RT down, LT up).
    const auto trigger = [](float value) { return value < 0.12f ? 0.0f : (value - 0.12f) / 0.88f; };
    const double scrollSpeed = -static_cast<double>(s.ry) * S(700) + (trigger(s.rt) - trigger(s.lt)) * S(2600);
    if (scrollSpeed != 0.0) {
        padScrollCarry += scrollSpeed * dt;
        const int whole = static_cast<int>(padScrollCarry);
        padScrollCarry -= whole;
        if (whole != 0) Guard("pad scroll", [&] { Top().ScrollBy(whole); });
    } else {
        padScrollCarry = 0;
    }
    AfterInput();
}

// ---- controller help window, right of the overlay ------------------------------------------------------------------

auto OverlayHost::HelpRowsNow() -> std::vector<HelpRow> {
    std::vector<HelpRow> rows;
    if (!forms.empty()) Guard("help", [&] { Top().CollectHelp(rows); });
    const std::vector<HelpRow> common = CommonPadRows();
    rows.insert(rows.end(), common.begin(), common.end());
    return PadRowsOnly(rows);
}

auto OverlayHost::HelpIconWidth(const std::string& folder, const char* name, float iconHeight) -> float {
    if (name == nullptr) return 0.0f;
    Gdiplus::Bitmap* img = canvas->FindImage("controller/" + folder + "/" + name);
    if (img == nullptr || img->GetHeight() == 0) return 0.0f;
    return iconHeight * static_cast<float>(img->GetWidth()) / static_cast<float>(img->GetHeight());
}

// The help window is as big as its list (the longest list since the window opened, so it does not jump as the focus
// moves), against the overlay's right edge and centred on its height.
void OverlayHost::PositionHelp() {
    if (!IsWindow(helpWindow) || !IsWindow(window) || helpWidth <= 0) return;
    RenderHelp(false);
}

void OverlayHost::RenderHelp(bool force) {
    if (!IsWindow(helpWindow) || !IsWindow(window) || helpWidth <= 0) return;
    const std::vector<HelpRow> rows = HelpRowsNow();
    const bool sony = padKind == pad::Kind::PlayStation;
    const std::string folder = PadIconFolder(padKind);
    const std::wstring heading = Utf8ToWide(UiText(sony ? "PlayStation controller" : "Xbox controller"));

    // Sizes: the wiki's MeasureHelp.
    const float inset = static_cast<float>(S(10));
    const auto size = static_cast<float>(std::max(12.0, S(15) * uiScale));
    const float line = size * 2.3f;
    const float iconHeight = size * 2.0f;
    const auto gap = static_cast<float>(S(6));
    float iconColumn = 0;
    for (const HelpRow& row : rows) {
        const auto icons = PadIcons(row.pad);
        const float group = HelpIconWidth(folder, icons.first, iconHeight) + (icons.second != nullptr ? gap + HelpIconWidth(folder, icons.second, iconHeight) : 0.0f);
        iconColumn = std::max(iconColumn, group);
    }
    const float textX = inset + iconColumn + gap;
    Gdiplus::FontFamily family(Utf8ToWide(config.theme.fontFace).c_str());
    Gdiplus::Font font(&family, size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font bold(&family, size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    std::vector<std::pair<std::wstring, std::wstring>> texts;
    float right = 0;
    {
        Gdiplus::Bitmap probe(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&probe);
        const auto measure = [&](const std::wstring& text, const Gdiplus::Font& f) {
            Gdiplus::RectF box;
            g.MeasureString(text.c_str(), -1, &f, Gdiplus::PointF(0, 0), &box);
            return box.Width;
        };
        right = inset + measure(heading, bold);
        for (const HelpRow& row : rows) {
            texts.emplace_back(Utf8ToWide(UiText(PadButtonName(row.pad, padKind))), Utf8ToWide(UiText(row.action)));
            right = std::max(right, textX + std::max(measure(texts.back().first, bold), measure(texts.back().second, font)));
        }
    }
    helpMostRows = std::max(helpMostRows, rows.size());
    helpWidest = std::max(helpWidest, right + inset);
    RECT area{};
    GetWindowRect(window, &area);
    const int available = area.bottom - area.top;
    const int w = static_cast<int>(std::ceil(helpWidest));
    const int h = std::min(static_cast<int>(std::ceil(inset + size * 2.0f + line * static_cast<float>(helpMostRows) + inset)), available);
    const int x = area.right + S(8);  // against the overlay's right edge
    const int y = area.top + (available - h) / 2;

    std::string key = std::to_string(static_cast<int>(padKind)) + "|" + std::to_string(x) + "," + std::to_string(y) + "," +
                      std::to_string(w) + "," + std::to_string(h);
    for (const HelpRow& row : rows) key += "|" + std::to_string(static_cast<int>(row.pad)) + row.action;
    if (!force && key == helpDrawnKey) return;
    helpDrawnKey = key;
    if (w <= 4 || h <= 4) return;

    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    const Theme& t = config.theme;
    Gdiplus::SolidBrush panel(Gdiplus::Color(255, 12, 9, 6));
    Gdiplus::Pen frame(Gdiplus::Color(255, t.border.r, t.border.g, t.border.b), 2.0f);
    g.FillRectangle(&panel, 1, 1, w - 2, h - 2);
    g.DrawRectangle(&frame, 1, 1, w - 3, h - 3);
    Gdiplus::SolidBrush gold(Gdiplus::Color(255, t.gold.r, t.gold.g, t.gold.b));
    Gdiplus::SolidBrush text(Gdiplus::Color(255, t.text.r, t.text.g, t.text.b));
    float top = inset;
    g.DrawString(heading.c_str(), -1, &bold, Gdiplus::PointF(inset, top), &gold);
    top += size * 2.0f;
    // Each sprite is drawn at the row's height; its width follows its own proportions.
    const auto drawIcon = [&](const char* name, float left, float at) -> float {
        const float iconWidth = HelpIconWidth(folder, name, iconHeight);
        if (iconWidth <= 0.0f) return 0.0f;
        Gdiplus::Bitmap* img = canvas->FindImage("controller/" + folder + "/" + name);
        g.DrawImage(img, left, at, iconWidth, iconHeight);
        return iconWidth;
    };
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float iconY = top + (line - iconHeight) / 2.0f;
        float left = inset;
        const auto icons = PadIcons(rows[i].pad);
        left += drawIcon(icons.first, left, iconY);
        if (icons.second != nullptr) left += gap + drawIcon(icons.second, left + gap, iconY);
        g.DrawString(texts[i].first.c_str(), -1, &bold, Gdiplus::PointF(textX, top), &gold);
        g.DrawString(texts[i].second.c_str(), -1, &font, Gdiplus::PointF(textX, top + size * 1.1f), &text);
        top += line;
    }

    HBITMAP bits = nullptr;
    if (bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &bits) != Gdiplus::Ok || bits == nullptr) return;
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HGDIOBJ old = SelectObject(memory, bits);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT origin{x, y};
    POINT source{0, 0};
    SIZE sizeOut{w, h};
    SetWindowPos(helpWindow, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    UpdateLayeredWindow(helpWindow, screen, &origin, &sizeOut, memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, old);
    DeleteDC(memory);
    DeleteObject(bits);
    ReleaseDC(nullptr, screen);
}

void OverlayHost::SyncHelp() {
    if (!IsWindow(helpWindow)) return;
    const bool show = padRunning && padActive && helpWidth > 0 && IsWindow(window) && IsWindowVisible(window);
    if (!show) {
        if (IsWindowVisible(helpWindow)) ShowWindow(helpWindow, SW_HIDE);
        helpDrawnKey.clear();
        return;
    }
    RenderHelp(false);
    if (!IsWindowVisible(helpWindow)) ShowWindow(helpWindow, SW_SHOWNOACTIVATE);
}

// ---- window procedure and threads ----------------------------------------------------------------------------------
namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    auto* self = reinterpret_cast<OverlayHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case kMsgToggle:
        self->Toggle();
        return 0;
    case kMsgOpen:
        self->Show();
        return 0;
    case kMsgClose:
        if (self->open) self->Hide();
        return 0;
    case kMsgRun:
        self->RunMailbox();
        return 0;
    case kMsgShutdown:
        if (IsWindow(self->blocker)) DestroyWindow(self->blocker);
        self->blocker = nullptr;
        DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        self->Hide();
        return 0;
    case WM_DESTROY:
        KillTimer(window, kTimerFocus);
        KillTimer(window, kTimerPad);
        PostQuitMessage(0);
        return 0;
    case WM_SETCURSOR:
        HideSystemCursor();
        return TRUE;
    case WM_ERASEBKGND:
        if (!self->firstPainted && self->backgroundBrush != nullptr) {
            RECT area{};
            GetClientRect(window, &area);
            FillRect(reinterpret_cast<HDC>(wParam), &area, self->backgroundBrush);
        }
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        self->Paint(dc);
        EndPaint(window, &paint);
        self->firstPainted = true;
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTimerFocus) self->SyncVisibility();
        else if (wParam == kTimerPad) self->PadTick();
        return 0;
    case WM_MOUSEMOVE: {
        if (!self->trackingMouse) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            self->trackingMouse = true;
        }
        // A move the left stick made is not the mouse; the stick moves the pointer on the cursor thread, so a move soon
        // after it is not judged.
        if (self->padActive && GetTickCount64() - self->drive.movedAt.load() > 150) {
            POINT now{};
            if (GetCursorPos(&now) && (std::abs(now.x - self->drive.pointerX.load()) > 3 || std::abs(now.y - self->drive.pointerY.load()) > 3)) {
                DebugLog("controller mode ended by a mouse move");
                self->PadMouseUsed();
            }
        }
        MouseEvent event;
        event.kind = MouseKind::Move;
        event.x = GET_X_LPARAM(lParam);
        event.y = GET_Y_LPARAM(lParam);
        self->HandleMouse(event);
        return 0;
    }
    case WM_MOUSELEAVE: {
        self->trackingMouse = false;
        if (!self->mouseDown) self->HandleMouse(LeaveEvent());
        return 0;
    }
    case WM_LBUTTONDOWN: {
        self->PadMouseUsed();
        self->mouseDown = true;
        SetCapture(window);
        MouseEvent event;
        event.kind = MouseKind::Down;
        event.x = GET_X_LPARAM(lParam);
        event.y = GET_Y_LPARAM(lParam);
        self->HandleMouse(event);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!self->mouseDown) return 0;
        self->mouseDown = false;
        MouseEvent event;
        event.kind = MouseKind::Up;
        event.x = GET_X_LPARAM(lParam);
        event.y = GET_Y_LPARAM(lParam);
        self->HandleMouse(event);
        ReleaseCapture();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (self->mouseDown) {  // capture taken away mid-drag: end the drag where the pointer is
            self->mouseDown = false;
            POINT at{};
            GetCursorPos(&at);
            ScreenToClient(window, &at);
            MouseEvent event;
            event.kind = MouseKind::Up;
            event.x = at.x;
            event.y = at.y;
            self->HandleMouse(event);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        // Precision touchpads send many small deltas: they add up to whole notches instead of each rounding to none.
        const int steps = self->wheel.Add(GET_WHEEL_DELTA_WPARAM(wParam));
        if (steps == 0) return 0;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        MouseEvent event;
        event.kind = MouseKind::Wheel;
        event.x = point.x;
        event.y = point.y;
        event.wheelSteps = steps;
        self->HandleMouse(event);
        return 0;
    }
    case WM_KEYDOWN: {
        self->PadMouseUsed();
        if (self->forms.empty()) return 0;
        KeyEvent event;
        event.key = MapKey(wParam);
        event.vk = static_cast<unsigned>(wParam);
        event.mods = CurrentMods();
        event.repeat = (lParam & (1 << 30)) != 0;
        bool handled = false;
        Guard("key", [&] { handled = self->forms.back()->KeyDown(event); });
        self->ApplyDeferred();
        // Escape with nothing of its own to close (a dialog, a list, the keyboard): the editor closes (the pad's B goes back).
        if (!handled && event.key == Key::Escape && !event.repeat) self->Hide();
        self->AfterInput();
        return 0;
    }
    case WM_SYSKEYDOWN:
        // Alt+Down / Alt+Up open a drop-down's list; other Alt keys are the system's (Alt+F4 closes: WM_CLOSE hides).
        if ((wParam == VK_DOWN || wParam == VK_UP) && !self->forms.empty()) {
            KeyEvent event;
            event.key = wParam == VK_DOWN ? Key::Down : Key::Up;
            event.vk = static_cast<unsigned>(wParam);
            event.mods = CurrentMods();
            event.mods.alt = true;
            event.repeat = (lParam & (1 << 30)) != 0;
            Guard("key", [&] { self->forms.back()->KeyDown(event); });
            self->AfterInput();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_CHAR: {
        if (self->forms.empty()) return 0;
        // UTF-16 units: a character outside the BMP comes as two WM_CHARs (TextEdit::TypeChar joins them); an IME's
        // committed text comes this way too (DefWindowProc turns WM_IME_CHAR into WM_CHAR for a Unicode window).
        Guard("char", [&] { self->forms.back()->Char(static_cast<char16_t>(wParam)); });
        self->AfterInput();
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
        self->SyncIme();  // the composition window goes to the caret
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && self->mouseDown) {
            self->mouseDown = false;
            ReleaseCapture();
        }
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

// Owns the drawn-cursor window and its message loop, separate from the overlay's UI thread.
DWORD WINAPI CursorThread(LPVOID parameter) {
    auto* self = static_cast<OverlayHost*>(parameter);
    tCursorDrive = &self->drive;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cursorClass{};
    cursorClass.cbSize = sizeof(cursorClass);
    cursorClass.lpfnWndProc = CursorProc;
    cursorClass.hInstance = instance;
    cursorClass.lpszClassName = self->cursorClassName.c_str();
    RegisterClassExW(&cursorClass);
    self->cursorWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                         self->cursorClassName.c_str(), L"", WS_POPUP, 0, 0, self->S(kCursorWidth), self->S(kCursorHeight),
                                         nullptr, nullptr, instance, nullptr);
    if (self->cursorWindow != nullptr) {
        SetWindowLongPtrW(self->cursorWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&self->lastCursor));
        SetTimer(self->cursorWindow, 1, 8, nullptr);  // about 120 updates a second while shown
    }

    // The 8 ms WM_TIMER above is rounded up to the system tick (about 15.6 ms), so on its own the drawn cursor follows
    // the pointer at only ~64 updates a second and trails it. While the cursor is shown, a 1 ms high-resolution
    // waitable timer moves the window to the pointer instead (the game's own cursor is drawn once per frame, so this
    // matches it). Without the timer (older Windows) the WM_TIMER path remains.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE fastTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    bool fastOn = false;
    const auto setFast = [&](bool on) {
        if (fastTimer == nullptr || on == fastOn) return;
        if (on) {
            LARGE_INTEGER due;
            due.QuadPart = -10000;  // 1 ms, then every 1 ms
            SetWaitableTimer(fastTimer, &due, 1, nullptr, nullptr, FALSE);
        } else {
            CancelWaitableTimer(fastTimer);
        }
        fastOn = on;
    };

    // The left stick moves the real pointer from here, on the same 1 ms timer that moves the drawn cursor, so it glides
    // at the display's pace instead of stepping at the controller poll's. The speed eases toward what the stick asks
    // for (pad::SmoothToward) and the pointer moves in whole pixels with the remainder carried over.
    LARGE_INTEGER ticksPerSecond{};
    QueryPerformanceFrequency(&ticksPerSecond);
    LARGE_INTEGER lastTick{};
    double velocityX = 0, velocityY = 0, carryX = 0, carryY = 0;
    const auto stepStick = [&]() {
        LARGE_INTEGER nowTick{};
        QueryPerformanceCounter(&nowTick);
        const double dt = (lastTick.QuadPart == 0 || ticksPerSecond.QuadPart == 0)
                              ? 0.001
                              : std::min(0.05, static_cast<double>(nowTick.QuadPart - lastTick.QuadPart) / static_cast<double>(ticksPerSecond.QuadPart));
        lastTick = nowTick;
        CursorDrive& d = self->drive;
        const bool driving = d.driving.load();
        const float sx = driving ? d.stickX.load() : 0.0f;
        const float sy = driving ? d.stickY.load() : 0.0f;
        const double push = std::min(1.0, std::sqrt(static_cast<double>(sx) * sx + static_cast<double>(sy) * sy));
        const double speed = d.speed.load();
        const auto targetX = static_cast<float>(sx * push * speed);
        const auto targetY = static_cast<float>(-sy * push * speed);
        velocityX = pad::SmoothToward(static_cast<float>(velocityX), targetX, static_cast<float>(dt));
        velocityY = pad::SmoothToward(static_cast<float>(velocityY), targetY, static_cast<float>(dt));
        if (targetX == 0.0f && targetY == 0.0f && std::fabs(velocityX) < 4.0 && std::fabs(velocityY) < 4.0) {
            velocityX = velocityY = carryX = carryY = 0.0;  // at rest
            return;
        }
        carryX += velocityX * dt;
        carryY += velocityY * dt;
        const int stepX = static_cast<int>(carryX);
        const int stepY = static_cast<int>(carryY);
        carryX -= stepX;
        carryY -= stepY;
        if (stepX == 0 && stepY == 0) return;
        POINT point{};
        RECT area{};
        if (self->window == nullptr || !GetCursorPos(&point) || !GetWindowRect(self->window, &area)) return;
        point.x = std::clamp<LONG>(point.x + static_cast<LONG>(stepX), area.left, area.right - 1);
        point.y = std::clamp<LONG>(point.y + static_cast<LONG>(stepY), area.top, area.bottom - 1);
        SetCursorPos(point.x, point.y);
        d.pointerX.store(point.x);
        d.pointerY.store(point.y);
        d.movedAt.store(GetTickCount64());
    };
    // The pointer is moved about 125 times a second at most (the sub-pixel remainder carries over), which is still
    // smoother than the display; moving it every millisecond made the window repaint for every move.
    LARGE_INTEGER lastMove{};
    const auto stepStickPaced = [&]() {
        LARGE_INTEGER nowTick{};
        QueryPerformanceCounter(&nowTick);
        if (lastMove.QuadPart != 0 && ticksPerSecond.QuadPart != 0 && (nowTick.QuadPart - lastMove.QuadPart) * 1000 < 8 * ticksPerSecond.QuadPart)
            return;
        lastMove = nowTick;
        stepStick();
    };

    for (bool quit = false; !quit;) {
        const DWORD handles = fastTimer != nullptr ? 1 : 0;
        const DWORD woke = MsgWaitForMultipleObjectsEx(handles, handles != 0 ? &fastTimer : nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (woke == WAIT_FAILED) break;
        if (handles != 0 && woke == WAIT_OBJECT_0) {
            stepStickPaced();
            if (self->cursorWindow != nullptr && IsWindowVisible(self->cursorWindow)) MoveCursorToPointer(self->cursorWindow);
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (handles == 0) stepStick();  // no high-resolution timer: the stick moves at the thread's slower wake-ups
        setFast((self->cursorWindow != nullptr && IsWindowVisible(self->cursorWindow)) || self->drive.driving.load());
    }
    if (fastTimer != nullptr) CloseHandle(fastTimer);
    if (IsWindow(self->cursorWindow)) DestroyWindow(self->cursorWindow);
    self->cursorWindow = nullptr;
    UnregisterClassW(self->cursorClassName.c_str(), instance);
    return 0;
}

DWORD WINAPI UiThread(LPVOID parameter) {
    auto* self = static_cast<OverlayHost*>(parameter);
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&self->gdiplusToken, &startup, nullptr) != Gdiplus::Ok) {
        DebugLog("overlay: GDI+ did not start");
        SetEvent(self->ready);
        return 1;
    }
    const Theme& theme = self->config.theme;
    const COLORREF background = RGB(theme.background.r, theme.background.g, theme.background.b);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    windowClass.hbrBackground = CreateSolidBrush(background);
    windowClass.lpszClassName = self->windowClassName.c_str();
    RegisterClassExW(&windowClass);

    WNDCLASSEXW blockerClass{};
    blockerClass.cbSize = sizeof(blockerClass);
    blockerClass.lpfnWndProc = BlockerProc;
    blockerClass.hInstance = windowClass.hInstance;
    blockerClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    blockerClass.lpszClassName = self->blockerClassName.c_str();
    RegisterClassExW(&blockerClass);

    WNDCLASSEXW helpClass{};
    helpClass.cbSize = sizeof(helpClass);
    helpClass.lpfnWndProc = HelpProc;
    helpClass.hInstance = windowClass.hInstance;
    helpClass.lpszClassName = self->helpClassName.c_str();
    RegisterClassExW(&helpClass);

    {
        const HDC screen = GetDC(nullptr);
        self->dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
    }
    self->canvas = std::make_unique<GdiCanvas>(self->config.theme, self->config.dataDir);
    self->canvas->SetScale(self->dpi, self->uiScale);
    self->backgroundBrush = CreateSolidBrush(background);

    const std::wstring title = Utf8ToWide(self->config.title);
    self->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, self->windowClassName.c_str(), title.c_str(), WS_POPUP, 0, 0,
                                   self->S(theme.minWidth), self->S(theme.minHeight), nullptr, nullptr, windowClass.hInstance, self);
    bool ok = self->window != nullptr;
    if (ok) {
        SetLayeredWindowAttributes(self->window, 0, 255, LWA_ALPHA);
        // Covers the game. Hidden until the overlay opens; a missing blocker only loses the input capture, the overlay
        // still works.
        self->blocker = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, self->blockerClassName.c_str(), L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                                        nullptr, windowClass.hInstance, nullptr);
        if (self->blocker != nullptr) {
            SetLayeredWindowAttributes(self->blocker, 0, kBlockerAlpha, LWA_ALPHA);
            SetWindowPos(self->blocker, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                         GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // The controller help: click-through, never active, shown only while a controller is in use.
        self->helpWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                           self->helpClassName.c_str(), L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, windowClass.hInstance,
                                           nullptr);
        SetTimer(self->window, kTimerFocus, kTimerInterval, nullptr);
        self->FitToGame();
        // The root view, made here: views belong to this thread.
        try {
            std::shared_ptr<View> root = self->rootFactory ? self->rootFactory() : nullptr;
            if (!root) root = std::make_shared<DemoView>();
            self->forms.push_back(std::make_unique<Form>(*self, std::move(root)));
        } catch (const std::exception& error) {
            DebugLog(std::string("overlay: the first view could not be made: ") + error.what());
            ok = false;
        } catch (...) {
            DebugLog("overlay: the first view could not be made");
            ok = false;
        }
    } else {
        DebugLog("overlay: the window could not be created (error " + std::to_string(GetLastError()) + ")");
    }
    if (!ok) {
        if (self->helpWindow != nullptr) DestroyWindow(self->helpWindow);
        if (self->blocker != nullptr) DestroyWindow(self->blocker);
        if (self->window != nullptr) DestroyWindow(self->window);
        self->helpWindow = self->blocker = self->window = nullptr;
        self->forms.clear();
        self->canvas.reset();
        UnregisterClassW(self->helpClassName.c_str(), helpClass.hInstance);
        UnregisterClassW(self->blockerClassName.c_str(), blockerClass.hInstance);
        UnregisterClassW(self->windowClassName.c_str(), windowClass.hInstance);
        Gdiplus::GdiplusShutdown(self->gdiplusToken);
        SetEvent(self->ready);
        return 1;
    }
    {
        std::lock_guard<std::mutex> guard(self->mailbox->lock);
        self->mailbox->window = self->window;
    }
    self->started = true;
    SetEvent(self->ready);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // The open key toggles: pressing it again closes the overlay. Alt combinations arrive as WM_SYSKEYDOWN.
        // continue skips TranslateMessage, so the key does not also type into a text box.
        // Unlike the wiki (whose one text box was a search box), a key that types a character does not close the window
        // while a text box has the focus: the open key may be a letter, and rule names are typed here.
        if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && IsWindowVisible(self->window) && self->config.isCloseKey) {
            bool close = false;
            Guard("close key", [&] { close = self->config.isCloseKey(static_cast<unsigned int>(message.wParam)); });
            if (close && message.message == WM_KEYDOWN && !self->forms.empty()) {
                Box caret{};
                const bool typing = self->forms.back()->TextInputCaret(caret);
                const bool character = MapVirtualKeyW(static_cast<UINT>(message.wParam), MAPVK_VK_TO_CHAR) != 0 &&
                                       (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_MENU) & 0x8000) == 0;
                if (typing && character) close = false;
            }
            if (close) {
                self->Hide();
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    {
        std::lock_guard<std::mutex> guard(self->mailbox->lock);
        self->mailbox->window = nullptr;
        self->mailbox->queue.clear();
    }
    // Views go first, on their own thread; then the drawing resources, then GDI+.
    for (auto it = self->forms.rbegin(); it != self->forms.rend(); ++it) Guard("view end", [&] { it->reset(); });
    self->forms.clear();
    self->canvas.reset();
    if (self->backgroundBrush) DeleteObject(self->backgroundBrush);
    self->backgroundBrush = nullptr;
    self->gamepad.Stop();
    if (self->helpWindow != nullptr) DestroyWindow(self->helpWindow);
    self->helpWindow = nullptr;
    UnregisterClassW(self->helpClassName.c_str(), helpClass.hInstance);
    UnregisterClassW(self->blockerClassName.c_str(), blockerClass.hInstance);
    UnregisterClassW(self->windowClassName.c_str(), windowClass.hInstance);
    DeleteObject(windowClass.hbrBackground);
    Gdiplus::GdiplusShutdown(self->gdiplusToken);
    return 0;
}

}  // namespace

// ---- Overlay -------------------------------------------------------------------------------------------------------

Overlay::Overlay() : impl_(std::make_unique<OverlayHost>()) {}

Overlay::~Overlay() { Stop(); }

auto Overlay::Start(OverlayConfig config, ViewFactory rootView) -> bool {
    if (impl_->started || impl_->thread != nullptr) return false;
    impl_->config = std::move(config);
    impl_->rootFactory = std::move(rootView);
    const std::wstring id = Utf8ToWide(impl_->config.instanceId);
    impl_->windowClassName = L"EsrFilterEditorWindow-" + id;
    impl_->blockerClassName = L"EsrFilterEditorInputBlocker-" + id;
    impl_->cursorClassName = L"EsrFilterEditorCursor-" + id;
    impl_->helpClassName = L"EsrFilterEditorPadHelp-" + id;
    impl_->cursorThread = CreateThread(nullptr, 0, CursorThread, impl_.get(), 0, &impl_->cursorThreadId);
    impl_->ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (impl_->ready == nullptr) {
        Stop();
        return false;
    }
    impl_->thread = CreateThread(nullptr, 0, UiThread, impl_.get(), 0, nullptr);
    if (impl_->thread == nullptr) {
        Stop();
        return false;
    }
    WaitForSingleObject(impl_->ready, 10000);
    if (!impl_->started) {
        Stop();
        return false;
    }
    return true;
}

void Overlay::Stop() {
    if (impl_->cursorThread != nullptr) {
        PostThreadMessageW(impl_->cursorThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(impl_->cursorThread, 2000);
        CloseHandle(impl_->cursorThread);
        impl_->cursorThread = nullptr;
        impl_->cursorThreadId = 0;
    }
    if (impl_->thread != nullptr) {
        if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgShutdown, 0, 0);
        WaitForSingleObject(impl_->thread, 5000);
        CloseHandle(impl_->thread);
        impl_->thread = nullptr;
    }
    if (impl_->ready != nullptr) CloseHandle(impl_->ready);
    impl_->ready = nullptr;
    impl_->window = nullptr;
    impl_->blocker = nullptr;
    impl_->started = false;
}

void Overlay::Open() {
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgOpen, 0, 0);
}

void Overlay::Close() {
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgClose, 0, 0);
}

void Overlay::Toggle() {
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgToggle, 0, 0);
}

auto Overlay::IsOpen() const -> bool {
    return impl_ != nullptr && impl_->window != nullptr && IsWindowVisible(impl_->window) != FALSE;
}

auto Overlay::PostToUi(std::function<void(Ui&)> work) -> bool { return impl_->mailbox->Post(std::move(work)); }

}  // namespace esrfe::overlay
