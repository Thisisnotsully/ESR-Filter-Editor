// Runs one view: hands mouse, keyboard and controller input to its widgets, keeps the one focus that all three move,
// and draws what goes over the view: the focus frame, the on-screen keyboard, an open choice list, a dialog, the tooltip.
// No Windows: the host feeds it events and a Canvas; tests do the same with fakes.
//
// The controller's rules follow the wiki overlay: the D-pad moves the highlight (the focused widget first, then to the
// nearest widget in that direction), A selects, B goes back (a dialog, a list or the keyboard closes first), Y opens the
// keyboard for the text box, X clears, LB/RB page; with the keyboard open the D-pad picks keys, A types, X deletes,
// Y closes (as in the wiki's search).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "osk.hpp"
#include "ui.hpp"
#include "view.hpp"
#include "widgets.hpp"

namespace esrfe::overlay {

// An open dialog (outside Form so std::optional sees a complete type).
struct DialogState {
    DialogSpec spec;
    std::function<void(bool)> done;
    Button confirm;
    Button cancel;
    int focus = 0;  // 0 cancel, 1 confirm
    Box box;
};

class Form final : public Ui {
public:
    Form(Platform& platform, std::shared_ptr<View> view);
    ~Form() override = default;
    Form(const Form&) = delete;
    auto operator=(const Form&) -> Form& = delete;

    [[nodiscard]] auto GetView() -> View& { return *view_; }
    [[nodiscard]] auto ViewPtr() const -> const std::shared_ptr<View>& { return view_; }

    // ---- from the host ----
    void Layout(const Box& client, const Box& content);
    void Paint(Canvas& canvas);
    auto Mouse(const MouseEvent& event) -> bool;
    auto KeyDown(const KeyEvent& event) -> bool;
    auto Char(char16_t c) -> bool;
    auto PadPress(const PadEvent& event) -> bool;  // false: not used (B at the top: the host goes back or closes)
    auto Back() -> bool;                           // Escape / B: false when nothing took it
    auto ScrollBy(int pixels) -> bool;             // the right stick and the triggers
    void CollectHelp(std::vector<HelpRow>& out);
    // A text box has the keyboard focus (the IME is on); `caret` gets its caret's box.
    auto TextInputCaret(Box& caret) -> bool;
    // The left stick moved the cursor: the focus goes to what is under it (the wiki's PadFocusFromCursor).
    void FocusFromPoint(int x, int y);
    void Shown();
    void Hidden();
    void PadModeChanged(bool active);
    [[nodiscard]] auto DialogOpen() const -> bool { return dialog_.has_value(); }
    [[nodiscard]] auto ChoicesOpen() const -> bool { return choices_ != nullptr; }
    [[nodiscard]] auto KeyboardTarget() const -> TextBox* { return oskTarget_; }
    [[nodiscard]] auto Keyboard() const -> const OnScreenKeyboard& { return osk_; }
    [[nodiscard]] auto KeyboardBox() const -> Box { return oskGeometry_.box; }

    // ---- Ui ----
    auto GetMeasurer() -> Measurer& override { return platform_.GetMeasurer(); }
    void Repaint() override { platform_.Repaint(); }
    [[nodiscard]] auto Now() const -> std::uint64_t override { return platform_.Now(); }
    [[nodiscard]] auto PadActive() const -> bool override { return platform_.PadActive(); }
    [[nodiscard]] auto PadKind() const -> pad::Kind override { return platform_.PadKind(); }
    auto ClipboardText() -> std::string override { return platform_.ClipboardText(); }
    void SetClipboardText(const std::string& utf8) override { platform_.SetClipboardText(utf8); }
    void Focus(Widget* widget) override;
    [[nodiscard]] auto Focused() const -> Widget* override { return focus_; }
    void Capture(Widget* widget) override { capture_ = widget; }
    void OpenKeyboard(TextBox& target) override;
    void CloseKeyboard() override;
    [[nodiscard]] auto KeyboardOpen() const -> bool override { return oskTarget_ != nullptr && osk_.IsOpen(); }
    void OpenChoices(Dropdown& dropdown) override;
    void CloseChoices() override { choices_ = nullptr; }
    void ShowDialog(DialogSpec spec, std::function<void(bool confirmed)> done) override;
    void PostToUi(std::function<void(Ui&)> work) override { platform_.PostToUi(std::move(work)); }
    auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread thread) -> bool override {
        return platform_.RunOnGame(std::move(work), std::move(then), thread);
    }
    void Close() override { platform_.Close(); }
    void PushView(std::shared_ptr<View> view) override { platform_.PushView(std::move(view)); }
    void PopView() override { platform_.PopView(); }

private:
    void Refresh();  // the view's widget list again; drops pointers to widgets that are gone
    [[nodiscard]] auto Has(const Widget* widget) const -> bool;
    [[nodiscard]] auto WidgetAt(int x, int y) const -> Widget*;
    [[nodiscard]] auto FirstFocusable() const -> Widget*;
    auto MoveFocus(int dx, int dy) -> bool;
    auto TabFocus(int direction) -> bool;
    void FinishDialog(bool confirmed);
    auto DialogPad(const PadEvent& event) -> bool;
    auto DialogKey(const KeyEvent& event) -> bool;
    auto DialogMouse(const MouseEvent& event) -> bool;
    auto KeyboardPad(const PadEvent& event) -> bool;
    void ApplyKey(const OnScreenKeyboard::Result& result);
    void PaintKeyboard(Canvas& canvas);
    void PaintDialog(Canvas& canvas);
    void PaintTooltip(Canvas& canvas);
    void UpdateHover(int x, int y);

    Platform& platform_;
    std::shared_ptr<View> view_;
    std::vector<Widget*> widgets_;
    Widget* focus_ = nullptr;
    Widget* hover_ = nullptr;
    Widget* capture_ = nullptr;
    Widget* pressed_ = nullptr;
    Box client_{};
    Box content_{};
    bool keyboardNav_ = false;  // the focus was moved with keys: the focus frame shows without a controller
    bool mouseInside_ = false;
    int mouseX_ = 0, mouseY_ = 0;
    std::uint64_t lastDownAt_ = 0;
    int lastDownX_ = 0, lastDownY_ = 0;
    Widget* lastDownWidget_ = nullptr;
    bool lastWasDouble_ = false;

    OnScreenKeyboard osk_;
    TextBox* oskTarget_ = nullptr;
    KeyboardGeometry oskGeometry_;
    Dropdown* choices_ = nullptr;
    std::optional<DialogState> dialog_;
};

}  // namespace esrfe::overlay
