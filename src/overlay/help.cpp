// The button names, sprite names and row texts are the wiki overlay's (HelpRowsFor in overlay.cpp).
#include "help.hpp"

#include <set>

namespace esrfe::overlay {

auto PadButtonName(Glyph glyph, pad::Kind kind) -> std::string {
    const bool sony = kind == pad::Kind::PlayStation;
    switch (glyph) {
    case Glyph::None: return "";
    case Glyph::DPad: return "D-pad";
    case Glyph::DPadUpDown: return "D-pad up / down";
    case Glyph::DPadLeftRight: return "D-pad left / right";
    case Glyph::DPadDown: return "D-pad down";
    case Glyph::A: return sony ? "Cross" : "A";
    case Glyph::B: return sony ? "Circle" : "B";
    case Glyph::X: return sony ? "Square" : "X";
    case Glyph::Y: return sony ? "Triangle" : "Y";
    case Glyph::LB: return sony ? "L1" : "LB";
    case Glyph::RB: return sony ? "R1" : "RB";
    case Glyph::LBRB: return sony ? "L1 / R1" : "LB / RB";
    case Glyph::LT: return sony ? "L2" : "LT";
    case Glyph::RT: return sony ? "R2" : "RT";
    case Glyph::LTRT: return sony ? "L2 / R2" : "LT / RT";
    case Glyph::LeftStick: return "Left stick";
    case Glyph::RightStick: return "Right stick";
    case Glyph::L3: return "L3";
    case Glyph::R3: return "R3";
    case Glyph::Start: return sony ? "Options" : "Menu";
    case Glyph::View: return sony ? "Share / Create" : "View";
    }
    return "";
}

auto PadIcons(Glyph glyph) -> std::pair<const char*, const char*> {
    switch (glyph) {
    case Glyph::None: return {nullptr, nullptr};
    case Glyph::DPad: return {"dpad", nullptr};
    case Glyph::DPadUpDown: return {"dpadup", "dpaddown"};
    case Glyph::DPadLeftRight: return {"dpadleft", "dpadright"};
    case Glyph::DPadDown: return {"dpaddown", nullptr};
    case Glyph::A: return {"buttonbottom", nullptr};
    case Glyph::B: return {"buttonright", nullptr};
    case Glyph::X: return {"buttonleft", nullptr};
    case Glyph::Y: return {"buttontop", nullptr};
    case Glyph::LB: return {"bumperleft", nullptr};
    case Glyph::RB: return {"bumperright", nullptr};
    case Glyph::LBRB: return {"bumperleft", "bumperright"};
    case Glyph::LT: return {"triggerleft", nullptr};
    case Glyph::RT: return {"triggerright", nullptr};
    case Glyph::LTRT: return {"triggerleft", "triggerright"};
    case Glyph::LeftStick: return {"stickleft", nullptr};
    case Glyph::RightStick: return {"stickright", nullptr};
    case Glyph::L3: return {"stickleftbutton", nullptr};
    case Glyph::R3: return {"stickrightbutton", nullptr};
    case Glyph::Start: return {"buttonstart", nullptr};
    case Glyph::View: return {"buttonselect", nullptr};
    }
    return {nullptr, nullptr};
}

auto PadIconFolder(pad::Kind kind) -> std::string { return kind == pad::Kind::PlayStation ? "ps4" : "xbox"; }

auto CommonPadRows() -> std::vector<HelpRow> {
    return {{Glyph::LTRT, "", "Scroll up / down fast"},
            {Glyph::RightStick, "", "Scroll"},
            {Glyph::LeftStick, "", "Move cursor (A clicks)"},
            {Glyph::Start, "", "Close"}};
}

auto KeyboardPadRows(bool exitDown) -> std::vector<HelpRow> {
    std::vector<HelpRow> rows{{Glyph::DPad, "", "Highlight a key"}};
    if (exitDown) rows.push_back({Glyph::DPadDown, "", "From the bottom row: leave the keyboard"});
    rows.insert(rows.end(), {{Glyph::A, "", "Type the key"},
                             {Glyph::B, "", "Close keyboard"},
                             {Glyph::X, "", "Delete a character"},
                             {Glyph::Y, "", "Close keyboard"},
                             {Glyph::LeftStick, "", "Move cursor (A clicks)"},
                             {Glyph::Start, "", "Close"}});
    return rows;
}

auto PopupHelpRows() -> std::vector<HelpRow> {
    return {{Glyph::DPadUpDown, "Up / Down", "Pick a choice"},
            {Glyph::A, "Enter", "Choose"},
            {Glyph::B, "Esc", "Cancel"},
            {Glyph::LBRB, "PgUp / PgDn", "Page through the choices"}};
}

auto DialogHelpRows() -> std::vector<HelpRow> {
    return {{Glyph::DPadLeftRight, "Tab", "Highlight a button"},
            {Glyph::A, "Enter", "Press the button"},
            {Glyph::B, "Esc", "Cancel"}};
}

auto PadRowsOnly(const std::vector<HelpRow>& rows) -> std::vector<HelpRow> {
    std::vector<HelpRow> out;
    std::set<Glyph> seen;
    for (const HelpRow& row : rows) {
        if (row.pad == Glyph::None || row.action.empty() || !seen.insert(row.pad).second) continue;
        out.push_back(row);
    }
    return out;
}

auto KeyRowsOnly(const std::vector<HelpRow>& rows) -> std::vector<HelpRow> {
    std::vector<HelpRow> out;
    std::set<std::string> seen;
    for (const HelpRow& row : rows) {
        if (row.keys.empty() || row.action.empty() || !seen.insert(row.keys).second) continue;
        out.push_back(row);
    }
    return out;
}

}  // namespace esrfe::overlay
