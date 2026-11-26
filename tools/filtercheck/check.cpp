// Checks a loot filter with the loot filter plugin's own reader and engine,
// against the mod's tables, then runs a list of test items through it.
//
//   check <filter.json> <data dir> <filter dir> [scenario file]
//
// Scenario lines: name | code | quality | tier | level | stats (stat=value,...) | expect
//   expect: "shown", "hidden", or "match:R123" / "nomatch:R123" (several, space separated)
#include "filter_engine.h"
#include "filter_program.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace lootfilter;

namespace {

// Windows file names ignore letter case; so does this lookup.
auto FindFile(const std::filesystem::path& dir, const std::string& name, std::filesystem::path& found) -> bool {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string want = lower(name);
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (lower(e.path().filename().string()) == want) { found = e.path(); return true; }
    }
    return false;
}

auto Split(const std::string& s, char c) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == c) { out.push_back(cur); cur.clear(); } else if (ch != '\r') cur += ch;
    }
    out.push_back(cur);
    return out;
}
auto Trim(std::string s) -> std::string {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

struct Table {
    std::vector<std::string>              header;
    std::vector<std::vector<std::string>> rows;
    auto Col(const std::string& name) const -> int {
        for (std::size_t i = 0; i < header.size(); ++i) if (header[i] == name) return static_cast<int>(i);
        return -1;
    }
};
auto Load(const std::string& path) -> Table {
    Table t;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        auto cells = Split(line, '\t');
        if (first) { t.header = cells; first = false; continue; }
        if (!cells.empty() && cells[0] == "Expansion") continue;
        t.rows.push_back(cells);
    }
    return t;
}
auto Cell(const std::vector<std::string>& row, int col) -> std::string { return col >= 0 && col < static_cast<int>(row.size()) ? row[col] : ""; }

struct Data {
    std::map<std::string, int>              typeRow;      // itemtypes code -> row
    std::vector<std::vector<std::string>>   typeParents;  // row -> parent codes
    std::map<std::string, int>              statId;
    std::set<int>                           packsSkill;
    std::map<std::string, int>              soundRow;
    std::map<std::string, int>              skillRow;
    std::map<std::string, std::vector<std::string>> itemTypes;  // item code -> its types
    std::filesystem::path                   filterDir;
} D;

void LoadData(const std::string& dir) {
    Table types = Load(dir + "/itemtypes.txt");
    const int code = types.Col("Code"), e1 = types.Col("Equiv1"), e2 = types.Col("Equiv2");
    for (std::size_t i = 0; i < types.rows.size(); ++i) {
        D.typeRow[Cell(types.rows[i], code)] = static_cast<int>(i);
        D.typeParents.push_back({ Cell(types.rows[i], e1), Cell(types.rows[i], e2) });
    }
    Table stats = Load(dir + "/itemstatcost.txt");
    const int name = stats.Col("Stat"), id = stats.Col("*ID"), enc = stats.Col("Encode");
    for (const auto& r : stats.rows) {
        const int n = std::atoi(Cell(r, id).c_str());
        D.statId[Cell(r, name)] = n;
        const std::string e = Cell(r, enc);
        if (e == "2" || e == "3") D.packsSkill.insert(n);
    }
    Table sounds = Load(dir + "/sounds.txt");
    for (std::size_t i = 0; i < sounds.rows.size(); ++i) D.soundRow[Cell(sounds.rows[i], 0)] = static_cast<int>(i);
    Table skills = Load(dir + "/skills.txt");
    const int sk = skills.Col("skill"), skid = skills.Col("*Id");
    for (const auto& r : skills.rows) D.skillRow[Cell(r, sk)] = std::atoi(Cell(r, skid).c_str());
    for (const char* f : { "weapons.txt", "armor.txt", "misc.txt" }) {
        Table t = Load(dir + "/" + f);
        const int c = t.Col("code"), t1 = t.Col("type"), t2 = t.Col("type2");
        for (const auto& r : t.rows) D.itemTypes[Cell(r, c)] = { Cell(r, t1), Cell(r, t2) };
    }
}

auto TypeIs(const std::string& code, int wanted, int depth = 0) -> bool {
    if (code.empty() || depth > 20) return false;
    auto it = D.typeRow.find(code);
    if (it == D.typeRow.end()) return false;
    if (it->second == wanted) return true;
    for (const auto& p : D.typeParents[it->second]) if (TypeIs(p, wanted, depth + 1)) return true;
    return false;
}

class Resolver final : public NameResolver {
public:
    auto ItemTypeRow(std::string_view code) -> int override { auto it = D.typeRow.find(std::string(code)); return it == D.typeRow.end() ? -1 : it->second; }
    auto StatId(std::string_view name) -> int override {
        auto it = D.statId.find(std::string(name));
        if (it != D.statId.end()) return it->second;
        const std::string s(name);
        if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos) return std::atoi(s.c_str());
        return -1;
    }
    auto SoundRow(std::string_view name) -> int override {
        auto it = D.soundRow.find(std::string(name));
        if (it != D.soundRow.end()) return it->second;
        const std::string s(name);
        if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos) return std::atoi(s.c_str());
        return -1;
    }
    auto SoundFile(std::string_view fileName, std::wstring& fullPath) -> bool override {
        std::filesystem::path p;
        if (!FindFile(D.filterDir / "sounds", std::string(fileName), p)) return false;
        fullPath = p.wstring();
        return true;
    }
    auto IconFile(IconSpec& icon) -> bool override {
        const std::string& n = icon.name;
        std::filesystem::path p;
        if (!FindFile(D.filterDir / "icons", n + ".sprite", p)) return false;
        icon.spriteName = n;
        icon.width = icon.height = 32;
        return true;
    }
    auto SkillRow(std::string_view name) -> int override { auto it = D.skillRow.find(std::string(name)); return it == D.skillRow.end() ? -1 : it->second; }
    auto StatPacksSkillLevel(int stat) -> bool override { return D.packsSkill.count(stat) != 0; }
};

struct Item final : ItemSubject {
    std::string code;
    int quality{ 2 }, tier{ -1 }, ilvl{ 85 };
    bool identified{ true };
    int uniqueId{ -1 };
    ChronicleStatus chronicle{ ChronicleStatus::Unknown };
    std::map<int, int> stats;
    auto Chronicle() const -> ChronicleStatus override { return chronicle; }
    auto Code() const -> std::uint32_t override { return PackItemCode(code); }
    auto Quality() const -> int override { return quality; }
    auto BaseTier() const -> int override { return tier; }
    auto ItemLevel() const -> int override { return ilvl; }
    auto Sockets() const -> int override { return 0; }
    auto MaxSockets() const -> int override { return 0; }
    auto Ethereal() const -> bool override { return false; }
    auto Identified() const -> bool override { return identified; }
    auto Runeword() const -> bool override { return false; }
    auto UniqueOrSetId() const -> int override { return uniqueId; }
    auto Where() const -> Location override { return Location::Ground; }
    auto Area() const -> int override { return 2; }  // Blood Moor: outside town
    auto IsType(int row) const -> bool override {
        auto it = D.itemTypes.find(code);
        if (it == D.itemTypes.end()) return false;
        for (const auto& t : it->second) if (TypeIs(t, row)) return true;
        return false;
    }
    auto Stat(int stat, int) const -> int override { auto it = stats.find(stat); return it == stats.end() ? 0 : it->second; }
    auto StatMax(int stat) const -> int override { return Stat(stat, 0); }
    auto StatSum(int stat) const -> std::int64_t override { return Stat(stat, 0); }
    auto StatForSkill(int stat, int) const -> int override { return Stat(stat, 0); }
    auto IsGold() const -> bool override { return code == "gld"; }
    auto DisplayName() const -> std::string override { return code; }
    auto NameColor() const -> char override { return '0'; }
};

struct Player final : PlayerSubject {
    auto Stat(int stat, int) const -> int override { return stat == D.statId["level"] ? 90 : 0; }
    auto StatForSkill(int, int) const -> int override { return 0; }
    auto Difficulty() const -> int override { return 2; }
};

auto RuleId(const FilterProgram& p, int number) -> std::string {
    for (const Rule& r : p.rules) {
        if (r.number != number) continue;
        const std::string& c = r.comment;
        const std::size_t bar = c.find(" |");
        return bar == std::string::npos ? "#" + std::to_string(number) : c.substr(0, bar);
    }
    return "#" + std::to_string(number);
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 4) { std::fprintf(stderr, "usage: check <filter.json> <data dir> <filter dir> [scenarios]\n"); return 2; }
    LoadData(argv[2]);
    D.filterDir = argv[3];
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    FilterProgram program;
    std::vector<Problem> problems;
    const bool ok = CompileFilter(text, program, problems);
    Resolver resolver;
    ResolveNames(program, resolver, problems);
    int errors = 0, warnings = 0, active = 0;
    for (const Problem& p : problems) {
        (p.severity == Problem::Severity::Error ? errors : warnings)++;
        std::printf("%s: %s\n", p.severity == Problem::Severity::Error ? "ERROR" : "warning", p.ToString().c_str());
    }
    for (const Rule& r : program.rules) active += r.enabled ? 1 : 0;
    std::printf("compiled: %s, %d of %zu rules active, %d errors, %d warnings\n", ok ? "yes" : "NO", active, program.rules.size(), errors, warnings);

    int failures = 0;
    if (argc >= 5) {
        std::ifstream sc(argv[4]);
        std::string line;
        Player player;
        while (std::getline(sc, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto f = Split(line, '|');
            for (auto& x : f) x = Trim(x);
            if (f.size() < 7) { std::printf("bad scenario: %s\n", line.c_str()); ++failures; continue; }
            Item item;
            item.code = f[1];
            item.quality = std::atoi(f[2].c_str());
            item.tier = std::atoi(f[3].c_str());
            const int level = std::atoi(f[4].c_str());
            if (!f[5].empty() && f[5] != "-") {
                for (auto& kv : Split(f[5], ',')) {
                    auto p = Split(Trim(kv), '=');
                    if (p.size() != 2) continue;
                    const std::string key = Trim(p[0]);
                    if (key == "unid") { item.identified = false; continue; }
                    if (key == "uid") { item.uniqueId = std::atoi(p[1].c_str()); continue; }
                    if (key == "chronicle") {  // missing, completed (otherwise unknown: no Chronicle Tracker)
                        item.chronicle = p[1] == "missing" ? ChronicleStatus::Missing : p[1] == "completed" ? ChronicleStatus::Completed : ChronicleStatus::Unknown;
                        continue;
                    }
                    item.stats[D.statId.count(key) ? D.statId[key] : std::atoi(key.c_str())] = std::atoi(p[1].c_str());
                }
            }
            Outcome out;
            EvalSettings settings{ .level = level };
            Evaluate(program, item, &player, settings, out);
            std::string matched;
            std::set<std::string> ids;
            for (int n : out.matchedRules) { const std::string id = RuleId(program, n); ids.insert(id); matched += id + " "; }
            bool pass = true;
            for (const auto& e : Split(f[6], ' ')) {
                if (e.empty()) continue;
                if (e == "shown") pass &= !out.hidden;
                else if (e == "hidden") pass &= out.hidden;
                else if (e.rfind("match:", 0) == 0) pass &= ids.count(e.substr(6)) != 0;
                else if (e.rfind("nomatch:", 0) == 0) pass &= ids.count(e.substr(8)) == 0;
            }
            failures += pass ? 0 : 1;
            std::printf("%s %-44s L%d %s%s matched: %s| chat %zu\n", pass ? "PASS" : "FAIL", f[0].c_str(), level,
                        out.hidden ? "hidden by " : "shown", out.hidden ? RuleId(program, out.hiddenBy).c_str() : "", matched.c_str(),
                        out.chatMessages.size());
            if (std::getenv("CHECK_NAMES") != nullptr && out.nameChanged)  // the label's name, for checking renames
                std::printf("     name: %s\n", out.nameTemplate.c_str());
        }
    }
    return (ok && errors == 0 && failures == 0) ? 0 : 1;
}
