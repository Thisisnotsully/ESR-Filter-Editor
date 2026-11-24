// What the help shows: the controller's button names (Xbox or PlayStation wording), the game's own controller sprites
// for them (images\controller\xbox|ps4\<name>.png, as the wiki overlay uses them), and the rows every view shares.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "input.hpp"
#include "pad_logic.hpp"

namespace esrfe::overlay {

// The button's name as the controller prints it (English; translated when drawn).
auto PadButtonName(Glyph glyph, pad::Kind kind) -> std::string;
// Up to two sprite names for the glyph (nullptr: none).
auto PadIcons(Glyph glyph) -> std::pair<const char*, const char*>;
// The sprite folder for a controller kind: "ps4" or "xbox".
auto PadIconFolder(pad::Kind kind) -> std::string;

// Rows the host adds after the view's own: the sticks, the triggers and closing (the wiki's page list ends so).
auto CommonPadRows() -> std::vector<HelpRow>;
// The on-screen keyboard's rows (the wiki's keyboard list).
auto KeyboardPadRows(bool exitDown) -> std::vector<HelpRow>;
// The help of an open drop-down list, and of a dialog.
auto PopupHelpRows() -> std::vector<HelpRow>;
auto DialogHelpRows() -> std::vector<HelpRow>;

// Rows for the controller help: only those with a pad glyph, first of each glyph wins. Rows for the keyboard line:
// only those with keys, first of each key text wins.
auto PadRowsOnly(const std::vector<HelpRow>& rows) -> std::vector<HelpRow>;
auto KeyRowsOnly(const std::vector<HelpRow>& rows) -> std::vector<HelpRow>;

}  // namespace esrfe::overlay
