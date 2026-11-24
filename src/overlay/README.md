# src/overlay: the editor's in-game window

The window of the ESR Filter Editor, taken from the author's own ESR Wiki overlay. It knows nothing about filters:
it hosts views (`view.hpp`) drawn with a small toolkit (`widgets.hpp`). Namespace `esrfe::overlay`.

## Pieces

| File | What | Windows? |
|---|---|---|
| `overlay.hpp` | **The hosting API** (see below). | no headers |
| `view.hpp` | The `View` interface the editor implements. | no |
| `widgets.hpp/.cpp` | Label, Panel, Button, Chip, Toggle, Checkbox, TextBox, NumberBox, Dropdown, ListView (virtualised). | no |
| `form.hpp/.cpp` | Runs one view: one focus for mouse, keys and controller; focus frame, on-screen keyboard, choice lists, dialogs, tooltips. | no |
| `ui.hpp` | What views and widgets may ask for (`Ui`), and what the host provides (`Platform`). | no |
| `canvas.hpp` | Drawing and measuring, as an interface (fonts, see-through fills, shadows, ÿc colour text, highlight, images, clipping). | no |
| `theme.hpp` | The look: the wiki's palette, fonts, background picture, window size, colour code palette. | no |
| `color_text.hpp/.cpp` | ÿc colour runs, search highlight ranges, word wrapping of text drawn in pieces. | no |
| `focus_nav.hpp/.cpp` | Where the D-pad / arrows / Tab move the focus. | no |
| `list_math.hpp` | Virtual list rows, scroll bar thumb and drag, touchpad wheel. | no |
| `text_edit.hpp/.cpp` | One line of editable UTF-16 text (caret, selection, surrogate pairs, words). | no |
| `osk.hpp/.cpp` | The on-screen keyboard's state and placement. | no |
| `help.hpp/.cpp` | Controller button names, sprite names, shared help rows. | no |
| `ui_text.hpp/.cpp` | `locales\<code>\ui-strings.txt`, `UiText`, `UiFormat`, the loader's language. | no |
| `utf.hpp/.cpp` | UTF-8 / UTF-16. | no |
| `demo_view.hpp/.cpp` | `DemoView`: every control on one screen (milestone 2). | no |
| `pad_logic.hpp`, `text_fold.hpp/.cpp`, `debug_log.hpp`, `cursor_art.hpp` | From the wiki (see NOTICE). | no |
| `overlay_host.cpp` | The Win32 host: window, threads, cursor, input blocker, help window, controller poll, message loop. | yes |
| `gdi_canvas.hpp/.cpp` | `Canvas` over GDI/GDI+ with the wiki's drawing code. | yes |
| `gamepad.hpp/.cpp` | XInput and PlayStation (HID) controllers. | yes |
| `input_block.hpp/.cpp` | Keeps the game from acting on the controller while the window shows. | yes |
| `game_thread.hpp` | For plugin.cpp: queue work on D2RLoader's UI / game thread (header only, uses the SDK). | SDK |

`CMakeLists.txt` builds `overlay_core` (everything without Windows; builds anywhere) and, on Windows, `overlay`
(links `overlay_core user32 gdi32 gdiplus msimg32 hid`, like the wiki). Tests: `tests/overlay/`.

## Hosting (plugin.cpp)

```cpp
#include "overlay/overlay.hpp"
#include "overlay/game_thread.hpp"
#include "overlay/input_block.hpp"
#include "overlay/ui_text.hpp"

namespace ov = esrfe::overlay;
std::unique_ptr<ov::Overlay> window;

// load:
ov::DebugSink() = [](const std::string& m) { Context->LogInfo(m.c_str()); };  // overlay/debug_log.hpp
const std::string locale = ov::ReadLoaderLocale(gameDir, pluginDir);            // "" = English
ov::LoadUiStrings(ov::UiStringsPath(dataDir, locale));                          // before Start
ov::OverlayConfig config;
config.dataDir = dataDir;                 // images\background.png, images\controller\..., locales\...
config.keyboardLocale = locale;
config.isCloseKey = matchesOpenKey;       // as the wiki: the bound key closes it again
config.queueGameUi = ov::GameThreadQueue(Context, threads, ov::GameThread::Ui);
config.queueGameLogic = ov::GameThreadQueue(Context, threads, ov::GameThread::Game);
window = std::make_unique<ov::Overlay>();
if (!window->Start(config, [] { return std::make_shared<ov::DemoView>(); })) { /* log */ }
ov::inputblock::Install([] { return window != nullptr && window->IsOpen(); });

// key / console command:  window->Toggle();
// unload:                 ov::inputblock::Remove(); window->Stop(); window.reset();
```

- `Start(config, factory)`: starts the UI thread (and the cursor thread), creates the hidden window; the factory runs on
  the UI thread and makes the first view. `Stop()`: ends both threads; views are destroyed on the UI thread.
- `Open()`, `Close()`, `Toggle()`, `IsOpen()`, `PostToUi(fn)`: any thread. `PostToUi` runs `fn(Ui&)` on the UI thread.
- From the UI thread, `ui.RunOnGame(work, then, GameThread::Ui | Game)` queues `work` on a game thread and runs
  `then(Ui&)` back on the UI thread. Capture results by value or `shared_ptr`; `work` can run after the window closed
  (and, if the loader keeps it, after Stop: then `then` is dropped).
- `ui.PushView(view)` / `ui.PopView()` switch views inside the window (they take effect after the current event).
  B / Escape with nothing to close goes back a view; at the first view it closes the window.

## Views

A view (see `view.hpp`) owns its widgets, sets their boxes in `Layout(context)` (called before every paint, with the
content box and a `Measurer`; sizes written at 96 dpi go through `S()`), lists them in `FocusList` (Tab order and paint
order), and may draw under and over them, add help rows, and take keys / pad buttons the focused widget left. Widgets
call back with a `Ui&`: focus, the on-screen keyboard, choice lists, `ShowDialog(spec, done)`, repaint, the clipboard,
posting, game work, views. Text is UTF-8 everywhere; widget labels are English and go through `UiText` when drawn
(`translate = false` for data). Text drawn through the canvas shows ÿc colour codes in the game's colours
(`Theme::codeColors`; only 0-9 : ; are filled in, as the wiki draws them; the rest draw in the body colour until the
editor fills them from the game's palette). `canvas.SetHighlight(&highlight)` marks search matches (the wiki's rule).

## Input

- Mouse: click (on the press, as the wiki), double click, drag (text selection, scroll bar thumb), wheel with touchpad
  steps added up, hover, tooltips beside the target clear of the drawn cursor.
- Keyboard: Tab / Shift+Tab, arrows (move the focus when the widget does not use them), Enter, Space, Escape (back),
  text editing (Ctrl+Left/Right, Home/End, Shift to select, Ctrl+A/C/X/V, Shift+Insert, Ctrl+Backspace/Delete), the
  IME (the window's own; on while a text box has the focus, with its composition window at the caret; committed text
  arrives as WM_CHAR UTF-16 units, surrogate pairs joined).
- Controller (the wiki's scheme): D-pad highlight (the focused widget first: a list moves its row, a number box steps;
  then the nearest widget that way), A select, B back, Y keyboard on the text box (or the view's search box), X clear
  (search box) / 0 (number box) / delete a character (keyboard), LB/RB page (lists, choice lists), right stick scroll,
  LT/RT fast scroll, left stick moves the cursor (A then clicks there for 2.5 s), Start closes. The help window right
  of the overlay lists the buttons for the current focus; without a controller a line at the bottom lists the keys.

## Checks

The pure parts build and are tested on Linux with g++ and clang++ (`tests/overlay/`, also under ASan/UBSan). The
Win32 files cannot be compiled here; they were checked for syntax only, with a stub `windows.h` / `gdiplus.h` / `imm.h`
/ `hidsdi.h` / `Xinput.h` in `tests/overlay/win32_stub/` (declarations only, never shipped, `-fshort-wchar`). That
catches typos, wrong member names, missing includes of our own headers and type mistakes against the stubbed
declarations; it does not prove the real SDK's signatures, macros or MSVC's warnings.

## NOTICE

This folder is taken from the ESR Wiki overlay (same author, Sully; written with Claude). From it:

| Here | From the wiki | Changed |
|---|---|---|
| `pad_logic.hpp` | `src/pad_logic.hpp` | namespace only |
| `gamepad.hpp/.cpp` | `src/gamepad.hpp/.cpp` | namespace only |
| `input_block.hpp/.cpp` | `src/input_block.hpp/.cpp` | **namespace only** (kept exactly, by the author's decision) |
| `text_fold.hpp/.cpp` | `src/text_fold.hpp/.cpp` | namespace only |
| `debug_log.hpp` | `src/debug_log.hpp` | namespace only |
| `cursor_art.hpp` | `src/cursor_art.hpp` (generated from the game's ohand sprite) | wrapped in the namespace |
| `tests/overlay/pad_logic_test.cpp` | `tests/pad_logic_test.cpp` | namespace only |
| `overlay_host.cpp` | `src/overlay.cpp`: UiThread, CursorThread, CursorProc, RenderCursorImage, CursorPixels, RememberGameCursor, HideSystemCursor, BlockerProc, HelpProc, PlaceBlocker, FindGameWindow, GameScreenRect, FitToGame, Show, Hide, SyncVisibility, ShowBlocker, the header part of ComputeLayout and Paint, PadSync, PadTick, PadMouseUsed, PadModeChanged, PadClickAtCursor, MeasureHelp, PositionHelp, RenderHelp, SyncHelp, WindowProc, Overlay::Start/Stop/Toggle/Open/IsShown | the wiki's pages, search box (an EDIT control), panel hand-off and help pictures taken out; views, a mailbox for posted and game work, the IME, Alt+Down, the help line added; the help window's rows come from the view |
| `gdi_canvas.cpp` | `src/overlay.cpp`: FillPanel, FillColor, FrameColor, CornerShadow, DropShadow, Text, LayoutPieces, MeasureHeight (and its cache), MeasureWidth, LineHeight, MakeFont, ApplyFonts, FindImage, DrawImageKey, DrawBackground, EnsureBackgroundCache, and the wiki's band copy for clipping | behind the `Canvas` interface; UTF-8 in; kanji rune name colouring left out |
| `color_text.cpp` | `src/overlay.cpp`: HighlightRanges, HighlightPieces, LayoutPieces' wrapping | measuring as a callback; a colour change inside a word no longer allows a line break there |
| `ui_text.cpp` | `src/overlay.cpp`: LoadUiStrings, UiText, UiDataText, UiFormat; `src/plugin.cpp`: LoaderLocale | UTF-8, std::filesystem; a UTF-8 BOM is skipped |
| `widgets.cpp`, `form.cpp` | the look and rules of the wiki's buttons, chips, list rows, scroll bar, search box, tooltip, focus frame, on-screen keyboard (DrawOsk, PressOskKey, OskClick, PadTick's keyboard branch), PadDirection / PadConfirm / PadBack | general widgets and one focus model instead of the wiki's fixed zones; text box, number box, drop-down, check box, toggle and dialogs are new |
| `theme.hpp` | the wiki's colour constants, font sizes, frame slices, FitToGame sizes, CodeColor (0-9 : ;) | a struct the editor can change |
| `help.cpp` | `src/overlay.cpp`: HelpRowsFor (button names, sprite names, row texts) | rows per widget / view |

Images the window uses at run time, from the wiki's data folder (copy them into the editor's data folder):
`images\background.png` and `images\controller\xbox\*.png`, `images\controller\ps4\*.png` (the game's own controller
art). Without them the window draws a plain background and the help without button pictures.

The wiki's window and plugin code is partly based on the D2RR Item Database overlay by Falendar, used under the MIT
License, which is reproduced here:

```
MIT License

Copyright (c) 2026 Falendar

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

`game_thread.hpp` uses the D2RLoader Plugin SDK headers (MIT, D2RLoader contributors), which the repository already
carries. Diablo II: Resurrected and its artwork (the cursor in `cursor_art.hpp`, the controller sprites) are the
property of Blizzard Entertainment; Eastern Sun Resurrected artwork (the background picture) belongs to the Eastern Sun
Resurrected project.
