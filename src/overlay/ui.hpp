// What views and widgets can ask of the overlay while they handle input or run posted work, all on the overlay's UI
// thread. The Form (form.hpp) implements the focus, keyboard, choice list and dialog parts; the window host implements
// the rest through Platform.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "canvas.hpp"
#include "pad_logic.hpp"

namespace esrfe::overlay {

class Widget;
class TextBox;
class Dropdown;
class View;
class Ui;

// Which of the game's threads runs queued work: D2RLoader's UI thread (panels, the loot filter bridge's
// getHoveredItem) or its game thread (item access).
enum class GameThread { Ui, Game };

// A confirm / cancel dialog.
struct DialogSpec {
    std::string title;
    std::string message;           // may wrap; ÿc colour codes allowed
    std::string confirm = "OK";    // English, translated when drawn
    std::string cancel = "Cancel";  // empty: only the confirm button
    bool confirmIsDefault = false;  // the confirm button starts focused (false: Cancel does, safer for deleting)
    bool danger = false;           // the confirm button is drawn as a warning
};

// The window host's side, implemented by the Win32 host (and by tests).
class Platform {
public:
    virtual ~Platform() = default;
    virtual auto GetMeasurer() -> Measurer& = 0;
    virtual void Repaint() = 0;
    [[nodiscard]] virtual auto Now() const -> std::uint64_t = 0;  // milliseconds (GetTickCount64)
    [[nodiscard]] virtual auto PadActive() const -> bool = 0;     // a controller was the last thing used
    [[nodiscard]] virtual auto PadKind() const -> pad::Kind = 0;
    virtual auto ClipboardText() -> std::string = 0;
    virtual void SetClipboardText(const std::string& utf8) = 0;
    virtual void PostToUi(std::function<void(Ui&)> work) = 0;
    virtual auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread thread) -> bool = 0;
    virtual void Close() = 0;
    virtual void PushView(std::shared_ptr<View> view) = 0;
    virtual void PopView() = 0;
    // How far the drawn cursor reaches right and down from its hot spot, and left of it (tooltips keep clear of it).
    [[nodiscard]] virtual auto CursorReach() const -> Box { return Box{0, 0, 0, 0}; }
    [[nodiscard]] virtual auto DoubleClickMs() const -> std::uint64_t { return 500; }
    [[nodiscard]] virtual auto KeyboardLocaleCode() const -> std::string { return {}; }
};

class Ui {
public:
    virtual ~Ui() = default;

    // ---- the window ----
    virtual auto GetMeasurer() -> Measurer& = 0;
    virtual void Repaint() = 0;
    [[nodiscard]] virtual auto Now() const -> std::uint64_t = 0;
    [[nodiscard]] virtual auto PadActive() const -> bool = 0;
    [[nodiscard]] virtual auto PadKind() const -> pad::Kind = 0;
    virtual auto ClipboardText() -> std::string = 0;
    virtual void SetClipboardText(const std::string& utf8) = 0;

    // ---- focus and the layers over the view ----
    virtual void Focus(Widget* widget) = 0;  // nullptr: nothing focused
    [[nodiscard]] virtual auto Focused() const -> Widget* = 0;
    virtual void Capture(Widget* widget) = 0;  // mouse moves and the release go to it (a drag); nullptr releases
    virtual void OpenKeyboard(TextBox& target) = 0;  // the on-screen keyboard, typing into `target`
    virtual void CloseKeyboard() = 0;
    [[nodiscard]] virtual auto KeyboardOpen() const -> bool = 0;
    virtual void OpenChoices(Dropdown& dropdown) = 0;  // a drop-down's list, over everything
    virtual void CloseChoices() = 0;
    virtual void ShowDialog(DialogSpec spec, std::function<void(bool confirmed)> done) = 0;

    // ---- threads and views ----
    // Runs `work` on the overlay's UI thread later (from any thread: Overlay::PostToUi).
    virtual void PostToUi(std::function<void(Ui&)> work) = 0;
    // Runs `work` on a game thread, then `then` back on the UI thread. Results come back as copies the lambdas share.
    // False when the game refused the work (then neither runs).
    virtual auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread thread = GameThread::Ui) -> bool = 0;
    virtual void Close() = 0;  // hides the overlay
    virtual void PushView(std::shared_ptr<View> view) = 0;  // takes effect after the current event
    virtual void PopView() = 0;                             // the last view: closes the overlay instead
};

}  // namespace esrfe::overlay
