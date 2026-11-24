// itemtypes.txt: the item type tree.
#pragma once

#include "strings.h"
#include "table.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

struct ItemType {
    int row = -1;             // the game's row number, as the loot filter uses it
    std::string code;         // "axe", "rune"; empty on unused rows
    std::string name;         // the ItemType column ("Axe")
    std::vector<int> parents;  // rows of Equiv1/Equiv2
    std::vector<int> children;
    bool alwaysMagic = false;   // Magic column: never plain
    bool canBeRare = false;     // Rare column
    bool alwaysNormal = false;  // Normal column: never magic
    int maxSockets[3] = {0, 0, 0};       // MaxSockets1..3
    int socketLevels[2] = {25, 40};      // MaxSocketsLevelThreshold1..2
    std::string classCode;    // Class column: "ama"... for class items
    std::string storePage;    // weap, armo, misc
    std::string uiCategory;   // a strings key such as "helms", "amazo"
};

class ItemTypes {
public:
    auto Load(const Table& table, std::vector<std::string>& problems) -> void;

    auto All() const -> const std::vector<ItemType>& { return types_; }
    auto Find(std::string_view code) const -> const ItemType*;
    auto At(int row) const -> const ItemType*;

    // Row of the code, -1 if unknown.
    auto Row(std::string_view code) const -> int;

    // The type is the ancestor or one of its descendants (Equiv1/Equiv2,
    // any depth). A type is itself.
    auto IsA(int row, int ancestorRow) const -> bool;
    auto IsA(std::string_view code, std::string_view ancestorCode) const -> bool;

    // The type and all its ancestors, nearest first (breadth first).
    auto Ancestors(int row) const -> const std::vector<int>&;

    // Every type below it, any depth, not itself.
    auto Descendants(int row) const -> std::vector<int>;

    // From the strings where a key equal to the ItemType column exists
    // ("Armor", "Claw", "Rune"), else the ItemType column.
    auto DisplayName(int row, const Strings& strings, Language language) const -> std::string;

private:
    std::vector<ItemType> types_;
    std::unordered_map<std::string, int> byCode_;
    std::vector<std::vector<int>> ancestors_;
    std::vector<std::vector<char>> isA_;
};

}  // namespace gamedata
