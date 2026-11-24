#include "item_types.h"

#include <algorithm>

namespace gamedata {

auto ItemTypes::Load(const Table& t, std::vector<std::string>& problems) -> void {
    types_.clear();
    byCode_.clear();
    const int cName = t.NeedCol("ItemType", problems);
    const int cCode = t.NeedCol("Code", problems);
    const int cEq1 = t.NeedCol("Equiv1", problems);
    const int cEq2 = t.NeedCol("Equiv2", problems);
    const int cMagic = t.Col("Magic");
    const int cRare = t.Col("Rare");
    const int cNormal = t.Col("Normal");
    const int cS1 = t.Col("MaxSockets1"), cS2 = t.Col("MaxSockets2"), cS3 = t.Col("MaxSockets3");
    const int cL1 = t.Col("MaxSocketsLevelThreshold1"), cL2 = t.Col("MaxSocketsLevelThreshold2");
    const int cClass = t.Col("Class");
    const int cStore = t.Col("StorePage");
    const int cUi = t.Col("UICategory");

    types_.resize(static_cast<std::size_t>(t.Rows()));
    for (int r = 0; r < t.Rows(); ++r) {
        auto& ty = types_[static_cast<std::size_t>(r)];
        ty.row = r;
        ty.code = t.Str(r, cCode);
        ty.name = t.Str(r, cName);
        ty.alwaysMagic = t.Int(r, cMagic) == 1;
        ty.canBeRare = t.Int(r, cRare) == 1;
        ty.alwaysNormal = t.Int(r, cNormal) == 1;
        ty.maxSockets[0] = t.Int(r, cS1);
        ty.maxSockets[1] = t.Int(r, cS2);
        ty.maxSockets[2] = t.Int(r, cS3);
        ty.socketLevels[0] = t.Int(r, cL1, 25);
        ty.socketLevels[1] = t.Int(r, cL2, 40);
        ty.classCode = t.Str(r, cClass);
        ty.storePage = t.Str(r, cStore);
        ty.uiCategory = t.Str(r, cUi);
        if (ty.code.empty()) continue;
        if (!byCode_.emplace(ty.code, r).second)
            problems.push_back("itemtypes.txt: code '" + ty.code + "' twice, first row kept");
    }
    for (int r = 0; r < t.Rows(); ++r) {
        for (const int col : {cEq1, cEq2}) {
            const std::string_view code = t.Cell(r, col);
            if (code.empty()) continue;
            const int parent = Row(code);
            if (parent < 0) {
                problems.push_back("itemtypes.txt: row " + std::to_string(r) + " has unknown parent '" + std::string(code) + "'");
                continue;
            }
            auto& ty = types_[static_cast<std::size_t>(r)];
            if (parent == r || std::find(ty.parents.begin(), ty.parents.end(), parent) != ty.parents.end()) continue;
            ty.parents.push_back(parent);
            types_[static_cast<std::size_t>(parent)].children.push_back(r);
        }
    }

    // Ancestors breadth first; also guards against loops.
    const std::size_t n = types_.size();
    ancestors_.assign(n, {});
    isA_.assign(n, std::vector<char>(n, 0));
    for (std::size_t r = 0; r < n; ++r) {
        auto& list = ancestors_[r];
        auto& mark = isA_[r];
        list.push_back(static_cast<int>(r));
        mark[r] = 1;
        for (std::size_t i = 0; i < list.size(); ++i) {
            for (const int p : types_[static_cast<std::size_t>(list[i])].parents) {
                if (mark[static_cast<std::size_t>(p)]) continue;
                mark[static_cast<std::size_t>(p)] = 1;
                list.push_back(p);
            }
        }
    }
}

auto ItemTypes::Find(std::string_view code) const -> const ItemType* {
    const int r = Row(code);
    return r < 0 ? nullptr : &types_[static_cast<std::size_t>(r)];
}

auto ItemTypes::At(int row) const -> const ItemType* {
    return row >= 0 && row < static_cast<int>(types_.size()) ? &types_[static_cast<std::size_t>(row)] : nullptr;
}

auto ItemTypes::Row(std::string_view code) const -> int {
    if (code.empty()) return -1;
    const auto it = byCode_.find(std::string(code));
    return it == byCode_.end() ? -1 : it->second;
}

auto ItemTypes::IsA(int row, int ancestorRow) const -> bool {
    const int n = static_cast<int>(types_.size());
    if (row < 0 || row >= n || ancestorRow < 0 || ancestorRow >= n) return false;
    return isA_[static_cast<std::size_t>(row)][static_cast<std::size_t>(ancestorRow)] != 0;
}

auto ItemTypes::IsA(std::string_view code, std::string_view ancestorCode) const -> bool {
    return IsA(Row(code), Row(ancestorCode));
}

auto ItemTypes::Ancestors(int row) const -> const std::vector<int>& {
    static const std::vector<int> none;
    return row >= 0 && row < static_cast<int>(ancestors_.size()) ? ancestors_[static_cast<std::size_t>(row)] : none;
}

auto ItemTypes::Descendants(int row) const -> std::vector<int> {
    std::vector<int> out;
    for (std::size_t r = 0; r < types_.size(); ++r)
        if (static_cast<int>(r) != row && IsA(static_cast<int>(r), row)) out.push_back(static_cast<int>(r));
    return out;
}

auto ItemTypes::DisplayName(int row, const Strings& strings, Language language) const -> std::string {
    const ItemType* ty = At(row);
    if (ty == nullptr) return {};
    if (const std::string* text = strings.Find(ty->name, language)) return Strings::Plain(*text);
    return ty->name;
}

}  // namespace gamedata
