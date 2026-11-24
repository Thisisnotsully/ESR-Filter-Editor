// One of the mod's tab-separated excel tables (weapons.txt, itemtypes.txt...).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gamedata {

// Reads a whole file. False if it can't be opened.
auto ReadFile(const std::string& path, std::string& out) -> bool;

// True if the bytes are valid UTF-8.
auto IsUtf8(std::string_view text) -> bool;

// Latin-1 bytes to UTF-8.
auto Latin1ToUtf8(std::string_view text) -> std::string;

class Table {
public:
    // Reads the file. The first line is the header. Rows whose first cell is
    // "Expansion" are separators: the game does not count them, so they are
    // dropped and row numbers match the game's (itemtypes: Jewel = 58 as in
    // the game). Text that is not valid UTF-8 is read as Latin-1.
    auto Load(const std::string& path, std::vector<std::string>& problems) -> bool;

    auto Name() const -> const std::string& { return name_; }
    auto Rows() const -> int { return static_cast<int>(rows_.size()); }

    // Column by header name, -1 if missing.
    auto Col(std::string_view name) const -> int;

    // Column that must exist: -1 and a problem if missing.
    auto NeedCol(std::string_view name, std::vector<std::string>& problems) const -> int;

    // Empty for a missing column or a short row.
    auto Cell(int row, int col) const -> std::string_view;
    auto Str(int row, int col) const -> std::string { return std::string(Cell(row, col)); }

    // The cell as a number; fallback if empty or not a number.
    auto Int(int row, int col, int fallback = 0) const -> int;

private:
    std::string name_;
    std::string text_;
    std::vector<std::string_view> header_;
    std::vector<std::vector<std::string_view>> rows_;
};

// Text as an integer. False if it is not one.
auto ParseInt(std::string_view text, int& out) -> bool;

}  // namespace gamedata
