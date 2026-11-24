#include "items.h"

#include <algorithm>

namespace gamedata {

namespace {

auto TextOr(const Strings& strings, std::string_view key, Language language, const std::string& fallback)
    -> std::string {
    if (const std::string* text = strings.Find(key, language)) return Strings::Plain(*text);
    return fallback;
}

}  // namespace

auto Bases::Load(const Table& weapons, const Table& armor, const Table& misc, const ItemTypes& types,
                 std::vector<std::string>& problems) -> void {
    bases_.clear();
    byCode_.clear();
    const std::pair<const Table*, BaseKind> files[] = {
        {&weapons, BaseKind::Weapon}, {&armor, BaseKind::Armor}, {&misc, BaseKind::Misc}};
    for (const auto& [t, kind] : files) {
        const int cCode = t->NeedCol("code", problems);
        const int cName = t->Col("name");
        const int cNameStr = t->Col("namestr");
        const int cType = t->NeedCol("type", problems);
        const int cType2 = t->Col("type2");
        const int cNorm = t->Col("normcode"), cUber = t->Col("ubercode"), cUltra = t->Col("ultracode");
        const int cLevel = t->Col("level");
        const int cReq = t->Col("levelreq");
        const int cSock = t->Col("gemsockets");
        const int cSpawn = t->Col("spawnable");
        const int cQuest = t->Col("quest");
        const int cAuto = t->Col("auto prefix");
        for (int r = 0; r < t->Rows(); ++r) {
            Base b;
            b.code = t->Str(r, cCode);
            if (b.code.empty()) continue;
            b.kind = kind;
            b.fileRow = r;
            b.name = t->Str(r, cName);
            b.nameKey = t->Str(r, cNameStr);
            if (b.nameKey.empty()) b.nameKey = b.code;
            b.type = t->Str(r, cType);
            b.type2 = t->Str(r, cType2);
            b.typeRow = types.Row(b.type);
            b.type2Row = types.Row(b.type2);
            if (b.typeRow < 0) problems.push_back(t->Name() + ": " + b.code + " has unknown type '" + b.type + "'");
            b.normCode = t->Str(r, cNorm);
            b.uberCode = t->Str(r, cUber);
            b.ultraCode = t->Str(r, cUltra);
            b.level = t->Int(r, cLevel);
            b.levelReq = t->Int(r, cReq);
            b.gemSockets = t->Int(r, cSock);
            b.spawnable = t->Int(r, cSpawn) == 1;
            b.quest = t->Int(r, cQuest) > 0;
            b.autoPrefix = t->Int(r, cAuto);
            const bool ownCodes = b.code == b.normCode && b.code == b.uberCode && b.code == b.ultraCode;
            if (kind != BaseKind::Misc) {
                // Elite first: a Mythical base is its own three codes and
                // counts as elite, as the loot filter reports it.
                if (b.code == b.ultraCode) b.tier = Tier::Elite;
                else if (b.code == b.uberCode) b.tier = Tier::Exceptional;
                else if (b.code == b.normCode) b.tier = Tier::Normal;
                b.mythical = ownCodes && b.level == 96;
            } else {
                // misc.txt fills all three codes with the item's own code, so
                // only the level tells: 100 for mam/mrn/mjw.
                const bool jewellery = types.IsA(b.typeRow, types.Row("ring")) ||
                                       types.IsA(b.typeRow, types.Row("amul")) ||
                                       types.IsA(b.typeRow, types.Row("jewl"));
                b.mythicalMisc = ownCodes && b.level == 100 && jewellery;
            }
            if (b.typeRow >= 0) {
                const ItemType* ty = types.At(b.typeRow);
                b.maxSockets = std::min(b.gemSockets, ty->maxSockets[2]);
            }
            if (!byCode_.emplace(b.code, static_cast<int>(bases_.size())).second) {
                problems.push_back(t->Name() + ": code '" + b.code + "' again, first kept");
                continue;
            }
            bases_.push_back(std::move(b));
        }
    }
}

auto Bases::Find(std::string_view code) const -> const Base* {
    const auto it = byCode_.find(std::string(code));
    return it == byCode_.end() ? nullptr : &bases_[static_cast<std::size_t>(it->second)];
}

auto Bases::Name(const Base& base, const Strings& strings, Language language) const -> std::string {
    return TextOr(strings, base.nameKey, language, base.name);
}

auto Bases::MaxSocketsAt(const Base& base, int itemLevel, const ItemTypes& types) const -> int {
    const ItemType* ty = types.At(base.typeRow);
    if (ty == nullptr) return 0;
    const int cap = itemLevel <= ty->socketLevels[0] ? ty->maxSockets[0]
                  : itemLevel <= ty->socketLevels[1] ? ty->maxSockets[1]
                                                     : ty->maxSockets[2];
    return std::min(base.gemSockets, cap);
}

auto Uniques::Load(const Table& t, const Bases& bases, std::vector<std::string>& problems) -> void {
    items_.clear();
    const int cIndex = t.NeedCol("index", problems);
    const int cCode = t.NeedCol("code", problems);
    const int cLevel = t.Col("lvl");
    const int cReq = t.Col("lvl req");
    const int cSpawn = t.Col("spawnable");
    const int cDisabled = t.Col("disabled");
    for (int r = 0; r < t.Rows(); ++r) {
        Unique u;
        u.row = r;
        u.key = t.Str(r, cIndex);
        u.code = t.Str(r, cCode);
        u.level = t.Int(r, cLevel);
        u.levelReq = t.Int(r, cReq);
        u.spawnable = t.Int(r, cSpawn) == 1;
        u.disabled = t.Int(r, cDisabled) == 1;
        const Base* b = bases.Find(u.code);
        if (b == nullptr && !u.code.empty())
            problems.push_back("uniqueitems.txt: row " + std::to_string(r) + " '" + u.key + "' has unknown base '" + u.code + "'");
        u.onMythical = b != nullptr && (b->mythical || b->mythicalMisc);
        items_.push_back(std::move(u));
    }
}

auto Uniques::At(int row) const -> const Unique* {
    return row >= 0 && row < static_cast<int>(items_.size()) ? &items_[static_cast<std::size_t>(row)] : nullptr;
}

auto Uniques::Name(const Unique& u, const Strings& strings, Language language) const -> std::string {
    return TextOr(strings, u.key, language, u.key);
}

auto Uniques::ForBase(std::string_view code) const -> std::vector<int> {
    std::vector<int> rows;
    for (const auto& u : items_)
        if (u.code == code) rows.push_back(u.row);
    return rows;
}

auto Sets::Load(const Table& t, const Table& sets, const Bases& bases, std::vector<std::string>& problems) -> void {
    items_.clear();
    sets_.clear();
    const int sIndex = sets.NeedCol("index", problems);
    const int sName = sets.Col("name");
    for (int r = 0; r < sets.Rows(); ++r) {
        Set s;
        s.row = r;
        s.key = sets.Str(r, sIndex);
        s.nameKey = sets.Str(r, sName);
        if (s.nameKey.empty()) s.nameKey = s.key;
        sets_.push_back(std::move(s));
    }
    const int cIndex = t.NeedCol("index", problems);
    const int cSet = t.NeedCol("set", problems);
    const int cItem = t.NeedCol("item", problems);
    const int cLevel = t.Col("lvl");
    const int cReq = t.Col("lvl req");
    const int cSpawn = t.Col("spawnable");
    const int cDisabled = t.Col("disabled");
    for (int r = 0; r < t.Rows(); ++r) {
        SetItem s;
        s.row = r;
        s.key = t.Str(r, cIndex);
        s.setKey = t.Str(r, cSet);
        s.code = t.Str(r, cItem);
        s.level = t.Int(r, cLevel);
        s.levelReq = t.Int(r, cReq);
        s.spawnable = t.Int(r, cSpawn) == 1;
        s.disabled = t.Int(r, cDisabled) == 1;
        for (auto& set : sets_) {
            if (set.key == s.setKey) {
                s.setRow = set.row;
                set.items.push_back(r);
                break;
            }
        }
        if (s.setRow < 0 && !s.setKey.empty())
            problems.push_back("setitems.txt: '" + s.key + "' has unknown set '" + s.setKey + "'");
        const Base* b = bases.Find(s.code);
        if (b == nullptr && !s.code.empty())
            problems.push_back("setitems.txt: '" + s.key + "' has unknown base '" + s.code + "'");
        s.onMythical = b != nullptr && (b->mythical || b->mythicalMisc);
        items_.push_back(std::move(s));
    }
}

auto Sets::Item(int row) const -> const SetItem* {
    return row >= 0 && row < static_cast<int>(items_.size()) ? &items_[static_cast<std::size_t>(row)] : nullptr;
}

auto Sets::SetOf(const SetItem& item) const -> const Set* {
    return item.setRow >= 0 ? &sets_[static_cast<std::size_t>(item.setRow)] : nullptr;
}

auto Sets::Name(const SetItem& item, const Strings& strings, Language language) const -> std::string {
    return TextOr(strings, item.key, language, item.key);
}

auto Sets::SetName(const Set& set, const Strings& strings, Language language) const -> std::string {
    return TextOr(strings, set.nameKey, language, set.key);
}

auto Sets::ForBase(std::string_view code) const -> std::vector<int> {
    std::vector<int> rows;
    for (const auto& s : items_)
        if (s.code == code) rows.push_back(s.row);
    return rows;
}

}  // namespace gamedata
