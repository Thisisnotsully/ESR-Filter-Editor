// What the editor implements: one screen of the overlay window (a list of rules, a rule card, a step of adding a rule).
// The overlay draws the frame (background picture, title, close button, help) and hands the content box to the view.
// A view owns its widgets; the Form routes input to them, keeps the focus, and draws the focus frame, the on-screen
// keyboard, open choice lists, dialogs and tooltips. Everything runs on the overlay's UI thread.
#pragma once

#include <string>
#include <vector>

#include "canvas.hpp"
#include "input.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace esrfe::overlay {

struct LayoutContext {
    Box client;   // the whole window
    Box content;  // inside the frame, below the header, above the help line
    Measurer& measure;
};

class View {
public:
    virtual ~View() = default;

    // Shown in the header (UTF-8; translate it yourself, it may be data).
    [[nodiscard]] virtual auto Title() const -> std::string = 0;
    // Sets the boxes of the widgets. Called before every paint (cheap work only) and when the window size changes.
    virtual void Layout(const LayoutContext& context) = 0;
    // Every widget, in Tab order (also the paint order). Called often; the pointers must stay valid until the next call.
    virtual void FocusList(std::vector<Widget*>& out) = 0;

    // Drawing under the widgets (backgrounds, separators) and over them (below open lists, dialogs and tooltips).
    virtual void Paint(Canvas& canvas) { (void)canvas; }
    virtual void PaintOver(Canvas& canvas) { (void)canvas; }

    // Help rows for the current focus, after the focused widget's own rows (LB/RB for tabs, View for a shortcut ...).
    virtual void HelpRows(const Widget* focused, std::vector<HelpRow>& out) const { (void)focused; (void)out; }

    // A key the focused widget did not take (global shortcuts: F3, Ctrl+F ...). True when handled.
    virtual auto Key(const KeyEvent& event, Ui& ui) -> bool { (void)event; (void)ui; return false; }
    // A controller press the focused widget did not take (LB/RB, View, L3/R3 ...). True when handled.
    virtual auto Pad(const PadEvent& event, Ui& ui) -> bool { (void)event; (void)ui; return false; }
    // Escape or B with nothing open to close. True when handled; false: the overlay goes back a view, or closes.
    virtual auto Back(Ui& ui) -> bool { (void)ui; return false; }

    // The widget focused when the view shows (nullptr: none until the keyboard or the controller is used).
    virtual auto InitialFocus() -> Widget* { return nullptr; }
    // Y with nothing else to do: focus this text box and open the on-screen keyboard (the wiki's "Y: Search").
    virtual auto SearchBox() -> TextBox* { return nullptr; }

    virtual void Shown(Ui& ui) { (void)ui; }
    virtual void Hidden(Ui& ui) { (void)ui; }
};

}  // namespace esrfe::overlay
