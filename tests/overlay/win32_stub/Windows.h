// A STUB of the Windows SDK, for a rough syntax check of the overlay's Win32 files on Linux (see src/overlay/README.md, "Checks").
// NOT the real API and never shipped: declarations only, with the real names and roughly the real types.
#pragma once
#include <cstddef>
#include <cstdint>
#define WINAPI
#define CALLBACK
#define APIENTRY
#ifndef __cdecl
#define __cdecl
#endif
#define TRUE 1
#define FALSE 0
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD; typedef unsigned long DWORD; typedef long LONG;
typedef unsigned int UINT; typedef std::uintptr_t UINT_PTR; typedef std::intptr_t LONG_PTR; typedef std::uintptr_t ULONG_PTR;
typedef unsigned long long ULONGLONG; typedef long long LONGLONG; typedef UINT_PTR WPARAM; typedef LONG_PTR LPARAM; typedef LONG_PTR LRESULT;
typedef DWORD COLORREF; typedef void* LPVOID; typedef const void* LPCVOID; typedef wchar_t WCHAR; typedef const wchar_t* LPCWSTR; typedef wchar_t* LPWSTR;
typedef unsigned short USHORT; typedef unsigned long ULONG; typedef short SHORT; typedef long HRESULT;
#define DECLARE_HANDLE(n) struct n##__ { int unused; }; typedef struct n##__* n
DECLARE_HANDLE(HWND); DECLARE_HANDLE(HDC); DECLARE_HANDLE(HBITMAP); DECLARE_HANDLE(HFONT); DECLARE_HANDLE(HBRUSH); DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HINSTANCE); DECLARE_HANDLE(HMENU); DECLARE_HANDLE(HRAWINPUT);
typedef void* HGDIOBJ; typedef HICON HCURSOR; typedef HINSTANCE HMODULE; typedef void* HANDLE; typedef HANDLE HGLOBAL;
typedef LONG_PTR (WINAPI *FARPROC)();
typedef LRESULT (CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (CALLBACK* WNDENUMPROC)(HWND, LPARAM);
typedef DWORD (WINAPI* LPTHREAD_START_ROUTINE)(LPVOID);
typedef void (CALLBACK* TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
struct POINT { LONG x, y; }; struct RECT { LONG left, top, right, bottom; }; struct SIZE { LONG cx, cy; };
typedef RECT* LPRECT;
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore; BOOL fIncUpdate; BYTE rgbReserved[32]; };
struct WNDCLASSEXW { UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm; };
struct CREATESTRUCTW { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy, cx, y, x; LONG style; LPCWSTR lpszName; LPCWSTR lpszClass; DWORD dwExStyle; };
struct TEXTMETRICW { LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY; WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar; BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet; };
struct BLENDFUNCTION { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; };
struct BITMAPINFOHEADER { DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount; DWORD biCompression; DWORD biSizeImage; LONG biXPelsPerMeter; LONG biYPelsPerMeter; DWORD biClrUsed; DWORD biClrImportant; };
struct RGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; };
struct BITMAP { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; LPVOID bmBits; };
struct ICONINFO { BOOL fIcon; DWORD xHotspot; DWORD yHotspot; HBITMAP hbmMask; HBITMAP hbmColor; };
struct CURSORINFO { DWORD cbSize; DWORD flags; HCURSOR hCursor; POINT ptScreenPos; };
struct TRACKMOUSEEVENT { DWORD cbSize; DWORD dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct SYSTEM_INFO { WORD wProcessorArchitecture; WORD wReserved; DWORD dwPageSize; LPVOID lpMinimumApplicationAddress; LPVOID lpMaximumApplicationAddress; ULONG_PTR dwActiveProcessorMask; DWORD dwNumberOfProcessors; DWORD dwProcessorType; DWORD dwAllocationGranularity; WORD wProcessorLevel; WORD wProcessorRevision; };
struct OVERLAPPED { ULONG_PTR Internal; ULONG_PTR InternalHigh; DWORD Offset; DWORD OffsetHigh; HANDLE hEvent; };
typedef OVERLAPPED* LPOVERLAPPED;
struct SECURITY_ATTRIBUTES; typedef SECURITY_ATTRIBUTES* LPSECURITY_ATTRIBUTES;
struct RAWINPUTDEVICELIST { HANDLE hDevice; DWORD dwType; };
struct RID_DEVICE_INFO_HID { DWORD dwVendorId; DWORD dwProductId; DWORD dwVersionNumber; USHORT usUsagePage; USHORT usUsage; };
struct RID_DEVICE_INFO { DWORD cbSize; DWORD dwType; RID_DEVICE_INFO_HID hid; };
struct IMAGE_DOS_HEADER { WORD e_magic; };
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
typedef ULONG_PTR DWORD_PTR;
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define WM_APP 0x8000
enum : UINT { WM_NCCREATE = 0x81, WM_DESTROY = 2, WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_PAINT = 0xF, WM_ERASEBKGND = 0x14, WM_SETCURSOR = 0x20, WM_MOUSEACTIVATE = 0x21,
  WM_NCHITTEST = 0x84, WM_KEYDOWN = 0x100, WM_CHAR = 0x102, WM_SYSKEYDOWN = 0x104, WM_TIMER = 0x113, WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201,
  WM_LBUTTONUP = 0x202, WM_MOUSEWHEEL = 0x20A, WM_CAPTURECHANGED = 0x215, WM_MOUSELEAVE = 0x2A3, WM_ACTIVATE = 6, WM_IME_STARTCOMPOSITION = 0x10D, WM_IME_CHAR = 0x286 };
enum : int { VK_BACK = 8, VK_TAB = 9, VK_RETURN = 0xD, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21,
  VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E, VK_F2 = 0x71, VK_F3 = 0x72 };
enum : DWORD { WS_POPUP = 0x80000000ul, WS_EX_TOPMOST = 8, WS_EX_TRANSPARENT = 0x20, WS_EX_TOOLWINDOW = 0x80, WS_EX_LAYERED = 0x80000, WS_EX_NOACTIVATE = 0x8000000 };
enum : UINT { CS_VREDRAW = 1, CS_HREDRAW = 2, SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_NOZORDER = 4, SWP_NOACTIVATE = 0x10, SWP_SHOWWINDOW = 0x40,
  DT_LEFT = 0, DT_CENTER = 1, DT_RIGHT = 2, DT_VCENTER = 4, DT_WORDBREAK = 0x10, DT_SINGLELINE = 0x20, DT_CALCRECT = 0x400, DT_NOPREFIX = 0x800, DT_END_ELLIPSIS = 0x8000,
  PM_REMOVE = 1, QS_ALLINPUT = 0x4FF, MWMO_INPUTAVAILABLE = 4, TME_LEAVE = 2, GMEM_MOVEABLE = 2, CF_UNICODETEXT = 13 };
enum : int { SW_HIDE = 0, SW_SHOWNOACTIVATE = 4, GA_ROOT = 2, GW_OWNER = 4, GWLP_USERDATA = -21, GWLP_HWNDPARENT = -8, SM_CXSCREEN = 0, SM_CYSCREEN = 1,
  SM_XVIRTUALSCREEN = 76, SM_YVIRTUALSCREEN = 77, SM_CXVIRTUALSCREEN = 78, SM_CYVIRTUALSCREEN = 79, LOGPIXELSY = 90, TRANSPARENT = 1, BLACK_BRUSH = 4,
  HTTRANSPARENT = -1, MA_NOACTIVATE = 3, WA_INACTIVE = 0, FW_NORMAL = 400, FW_SEMIBOLD = 600, FW_BOLD = 700, DEFAULT_CHARSET = 1, OUT_DEFAULT_PRECIS = 0,
  CLIP_DEFAULT_PRECIS = 0, CLEARTYPE_QUALITY = 5, DEFAULT_PITCH = 0, FF_DONTCARE = 0, BI_RGB = 0, DIB_RGB_COLORS = 0 };
enum : DWORD { SRCCOPY = 0xCC0020, LWA_ALPHA = 2, ULW_ALPHA = 2, CURSOR_SHOWING = 1, INFINITE = 0xFFFFFFFF, WAIT_OBJECT_0 = 0, WAIT_FAILED = 0xFFFFFFFF,
  ERROR_SUCCESS = 0, ERROR_IO_PENDING = 997, GENERIC_READ = 0x80000000ul, FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, OPEN_EXISTING = 3,
  FILE_FLAG_OVERLAPPED = 0x40000000, MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, PAGE_EXECUTE_READWRITE = 0x40, TIMER_ALL_ACCESS = 0x1F0003,
  RIM_TYPEHID = 2, RIDI_DEVICENAME = 0x20000007, RIDI_DEVICEINFO = 0x2000000b };
enum : BYTE { AC_SRC_OVER = 0, AC_SRC_ALPHA = 1 };
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define GET_WHEEL_DELTA_WPARAM(w) ((short)((w) >> 16))
#define WHEEL_DELTA 120
extern "C" {
int MulDiv(int, int, int);
HDC CreateCompatibleDC(HDC); HBITMAP CreateCompatibleBitmap(HDC, int, int); HGDIOBJ SelectObject(HDC, HGDIOBJ); BOOL DeleteObject(HGDIOBJ); BOOL DeleteDC(HDC);
COLORREF SetPixel(HDC, int, int, COLORREF); BOOL AlphaBlend(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
HBRUSH CreateSolidBrush(COLORREF); int FillRect(HDC, const RECT*, HBRUSH); int FrameRect(HDC, const RECT*, HBRUSH);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD); int SaveDC(HDC); BOOL RestoreDC(HDC, int); int IntersectClipRect(HDC, int, int, int, int);
HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR); BOOL GetTextMetricsW(HDC, TEXTMETRICW*);
HDC GetDC(HWND); int ReleaseDC(HWND, HDC); int GetDeviceCaps(HDC, int); int SetBkMode(HDC, int); COLORREF SetTextColor(HDC, COLORREF);
BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE*); BOOL TextOutW(HDC, int, int, LPCWSTR, int); int DrawTextW(HDC, LPCWSTR, int, LPRECT, UINT);
HGDIOBJ GetStockObject(int); HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD); int GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, BITMAPINFO*, UINT);
int GetObjectW(HANDLE, int, LPVOID); BOOL GetIconInfo(HICON, ICONINFO*);
BOOL UpdateLayeredWindow(HWND, HDC, POINT*, SIZE*, HDC, POINT*, COLORREF, BLENDFUNCTION*, DWORD); BOOL SetLayeredWindowAttributes(HWND, COLORREF, BYTE, DWORD);
BOOL IsWindowVisible(HWND); BOOL IsWindow(HWND); BOOL IsIconic(HWND); HWND GetAncestor(HWND, UINT); DWORD GetWindowThreadProcessId(HWND, DWORD*); DWORD GetCurrentProcessId();
HANDLE GetCurrentProcess(); BOOL GetClientRect(HWND, LPRECT); BOOL GetWindowRect(HWND, LPRECT); BOOL ClientToScreen(HWND, POINT*); BOOL ScreenToClient(HWND, POINT*);
BOOL EnumWindows(WNDENUMPROC, LPARAM); HWND GetForegroundWindow(); BOOL SetForegroundWindow(HWND); HWND SetFocus(HWND); HWND GetWindow(HWND, UINT);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT); BOOL ShowWindow(HWND, int); BOOL UpdateWindow(HWND); BOOL InvalidateRect(HWND, const RECT*, BOOL);
LONG_PTR GetWindowLongPtrW(HWND, int); LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR); int GetSystemMetrics(int); UINT GetDpiForWindow(HWND);
HCURSOR SetCursor(HCURSOR); HCURSOR LoadCursorW(HINSTANCE, LPCWSTR); BOOL GetCursorInfo(CURSORINFO*); BOOL GetCursorPos(POINT*); BOOL SetCursorPos(int, int);
HWND WindowFromPoint(POINT); LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM); BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM); BOOL PostThreadMessageW(DWORD, UINT, WPARAM, LPARAM);
void PostQuitMessage(int); BOOL GetMessageW(MSG*, HWND, UINT, UINT); BOOL PeekMessageW(MSG*, HWND, UINT, UINT, UINT); BOOL TranslateMessage(const MSG*); LRESULT DispatchMessageW(const MSG*);
HDC BeginPaint(HWND, PAINTSTRUCT*); BOOL EndPaint(HWND, const PAINTSTRUCT*); UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC); BOOL KillTimer(HWND, UINT_PTR);
WORD RegisterClassExW(const WNDCLASSEXW*); BOOL UnregisterClassW(LPCWSTR, HINSTANCE);
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID); BOOL DestroyWindow(HWND);
HWND SetCapture(HWND); BOOL ReleaseCapture(); BOOL TrackMouseEvent(TRACKMOUSEEVENT*); SHORT GetKeyState(int); UINT GetDoubleClickTime();
BOOL OpenClipboard(HWND); BOOL CloseClipboard(); BOOL EmptyClipboard(); HANDLE GetClipboardData(UINT); HANDLE SetClipboardData(UINT, HANDLE);
HGLOBAL GlobalAlloc(UINT, std::size_t); HGLOBAL GlobalFree(HGLOBAL); LPVOID GlobalLock(HGLOBAL); BOOL GlobalUnlock(HGLOBAL);
HMODULE GetModuleHandleW(LPCWSTR); HMODULE LoadLibraryW(LPCWSTR); BOOL FreeLibrary(HMODULE); FARPROC GetProcAddress(HMODULE, const char*);
DWORD GetLastError(); ULONGLONG GetTickCount64(); HANDLE CreateThread(LPSECURITY_ATTRIBUTES, std::size_t, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD*);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR); BOOL SetEvent(HANDLE); BOOL ResetEvent(HANDLE); BOOL CloseHandle(HANDLE);
DWORD WaitForSingleObject(HANDLE, DWORD); DWORD WaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD);
DWORD MsgWaitForMultipleObjectsEx(DWORD, const HANDLE*, DWORD, DWORD, DWORD);
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD); BOOL SetWaitableTimer(HANDLE, const LARGE_INTEGER*, LONG, void*, LPVOID, BOOL); BOOL CancelWaitableTimer(HANDLE);
BOOL QueryPerformanceCounter(LARGE_INTEGER*); BOOL QueryPerformanceFrequency(LARGE_INTEGER*);
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE); BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD*, LPOVERLAPPED);
BOOL GetOverlappedResult(HANDLE, LPOVERLAPPED, DWORD*, BOOL); BOOL CancelIoEx(HANDLE, LPOVERLAPPED);
UINT GetRawInputDeviceList(RAWINPUTDEVICELIST*, UINT*, UINT); UINT GetRawInputDeviceInfoW(HANDLE, UINT, LPVOID, UINT*);
void GetSystemInfo(SYSTEM_INFO*); LPVOID VirtualAlloc(LPVOID, std::size_t, DWORD, DWORD); BOOL VirtualProtect(LPVOID, std::size_t, DWORD, DWORD*);
BOOL FlushInstructionCache(HANDLE, LPCVOID, std::size_t);
}
enum : UINT { MAPVK_VK_TO_CHAR = 2 };
extern "C" UINT MapVirtualKeyW(UINT, UINT);
