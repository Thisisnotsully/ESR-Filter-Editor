// Tests for src/document: round trips, the read model, every edit kind on the
// real filter, and edge cases.
//
//   document_test [path to Sullys-QoL-Filter.json]
//
// Older versions of the filter are read with git from the file's folder.

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "document.h"

using namespace document;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_OK(status)                                                     \
    do {                                                                     \
        ++checks;                                                            \
        const Status s_ = (status);                                          \
        if (!s_) {                                                           \
            ++failures;                                                      \
            std::printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #status, s_.error.c_str()); \
        }                                                                    \
    } while (0)

auto ReadFile(const std::string& path) -> std::string {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

auto RunCommand(const std::string& command) -> std::string {
    std::string out;
    FILE* p = popen(command.c_str(), "r");
    if (!p) return out;
    std::array<char, 65536> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0) out.append(buf.data(), n);
    pclose(p);
    return out;
}

auto Lines(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// A line diff: removed old lines and added new lines (1-based line numbers).
struct Diff {
    std::vector<int> removed;                          // old line numbers
    std::vector<std::pair<int, std::string>> added;    // new line numbers and text
};

auto LineDiff(const std::string& a, const std::string& b) -> Diff {
    const auto x = Lines(a);
    const auto y = Lines(b);
    std::size_t pre = 0;
    while (pre < x.size() && pre < y.size() && x[pre] == y[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < x.size() - pre && suf < y.size() - pre && x[x.size() - 1 - suf] == y[y.size() - 1 - suf]) ++suf;
    const std::size_t n = x.size() - pre - suf;
    const std::size_t m = y.size() - pre - suf;
    // LCS on the middle
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            dp[i][j] = x[pre + i] == y[pre + j] ? dp[i + 1][j + 1] + 1 : std::max(dp[i + 1][j], dp[i][j + 1]);
        }
    }
    Diff d;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && x[pre + i] == y[pre + j]) {
            ++i;
            ++j;
        } else if (j < m && (i == n || dp[i][j + 1] >= dp[i + 1][j])) {
            d.added.push_back({static_cast<int>(pre + j + 1), y[pre + j]});
            ++j;
        } else {
            d.removed.push_back(static_cast<int>(pre + i + 1));
            ++i;
        }
    }
    return d;
}

auto MustParse(const std::string& text) -> Document {
    Document d;
    std::vector<Diag> diags;
    const bool ok = Document::Parse(text, d, diags);
    CHECK(ok);
    if (!ok) {
        for (const Diag& g : diags) std::printf("  line %u col %u: %s\n", g.line, g.column, g.message.c_str());
    }
    return d;
}

// After an edit: the text parses again into the same model.
void CheckReparses(const Document& d) {
    Document again;
    std::vector<Diag> diags;
    CHECK(Document::Parse(d.Text(), again, diags));
    CHECK(again.Text() == d.Text());
    CHECK(again.Rules().size() == d.Rules().size());
}

// Every rule of before, other than the ones in changed, is in after unchanged.
void CheckOthersSame(const Document& before, const Document& after, const std::set<std::string>& changed) {
    std::map<std::string, const Rule*> byId;
    for (const Rule& r : after.Rules()) byId[r.id] = &r;
    int compared = 0;
    for (const Rule& r : before.Rules()) {
        if (changed.count(r.id)) continue;
        auto it = byId.find(r.id);
        CHECK(it != byId.end());
        if (it == byId.end()) continue;
        if (!(it->second->value == r.value) || it->second->lineComment != r.lineComment) {
            std::printf("  rule %s changed\n", r.id.c_str());
            CHECK(false);
        }
        ++compared;
    }
    CHECK(compared == static_cast<int>(before.Rules().size() - changed.size()) || !changed.empty());
}

auto Contains(const std::string& s, const std::string& part) -> bool { return s.find(part) != std::string::npos; }

auto Str(const char* s) -> Value { return Value::String(s); }

// ---- strings and numbers

void TestStrings() {
    std::string out;
    CHECK(DecodeString("\"\xC3\xBF" "c1Gold\"", out) && out == "\xC3\xBF" "c1Gold");
    CHECK(DecodeString(R"("a\"b\\c\nd\/eÿ")", out) && out == "a\"b\\c\nd/e\xC3\xBF");
    CHECK(DecodeString(R"("😀")", out) && out == "\xF0\x9F\x98\x80");
    CHECK(!DecodeString(R"("\ud83d")", out));
    CHECK(!DecodeString(R"("\ude00")", out));
    CHECK(!DecodeString(R"("\q")", out));
    CHECK(EncodeString("\xC3\xBF" "c4Name") == "\"\xC3\xBF" "c4Name\"");
    CHECK(EncodeString("a\"b\\c\nd\te/") == R"("a\"b\\c\nd\te/")");
    CHECK(EncodeString(std::string("\x01", 1)) == R"("\u0001")");
    CHECK(EncodeString("\xF0\x9F\x98\x80") == "\"\xF0\x9F\x98\x80\"");

    CHECK(Value::Number(3.0).text == "3");
    CHECK(Value::Number(-12.0).text == "-12");
    CHECK(Value::Number(2.5).text == "2.5");
    CHECK(Value::Number(0.1).text == "0.1");
    CHECK(Value::NumberText("1.50") == Value::Number(1.5));
    std::int64_t n = 0;
    CHECK(Value::NumberText("1e2").AsInteger(n) && n == 100);
    CHECK(!Value::NumberText("1.5").AsInteger(n));

    Value obj = Value::Object();
    obj.Set("a", Value::Integer(1));
    obj.Set("b", Value::Array({Str("x"), Value::Bool(true), Value::Null()}));
    CHECK(Render(obj) == R"({"a": 1, "b": ["x", true, null]})");

    // raw text of untouched values is kept; the model decodes it
    Document d = MustParse("{\"rules\": [{\"comment\": \"R001 | caf\\u00e9 \\\"x\\\" | hide\", \"n\": 1.50}]}");
    CHECK(d.Rules()[0].title == "R001 | caf\xC3\xA9 \"x\" | hide");
    CHECK(d.Rules()[0].id == "R001");
    CHECK(d.Rules()[0].value.Find("n")->text == "1.50");
    CHECK_OK(d.SetTitle(0, "R001 | \xC3\xBF" "c1 \"q\" | show"));
    CHECK(Contains(d.Text(), "\"comment\": \"R001 | \xC3\xBF" "c1 \\\"q\\\" | show\", \"n\": 1.50"));
    CHECK_OK(d.SetField(0, "n", Value::Number(7.0)));
    CHECK(Contains(d.Text(), "\"n\": 7}"));
}

// ---- the plugin's reader rules

void TestSyntax() {
    Document d;
    std::vector<Diag> diags;
    // trailing commas, comments, tabs in strings are fine
    CHECK(Document::Parse("{\"a\": [1, 2,], /* x */ \"b\": \"t\tt\", // c\n}", d, diags));
    // the plugin's messages and places
    diags.clear();
    CHECK(!Document::Parse("{\n  \"a\": \"x\n\"}", d, diags));
    CHECK(diags.size() == 1 && diags[0].line == 2 && diags[0].message == "a text cannot span lines; write \\n for a line break");
    diags.clear();
    CHECK(!Document::Parse("{\"a\": 1 \"b\": 2}", d, diags));
    CHECK(diags.size() == 1 && diags[0].line == 1 && diags[0].column == 9 && diags[0].message == "expected , or } after a property");
    diags.clear();
    CHECK(!Document::Parse("{} /* open", d, diags));
    CHECK(diags.size() == 1 && diags[0].message == "a /* comment is never closed with */" && diags[0].column == 4);
    diags.clear();
    CHECK(!Document::Parse("   ", d, diags));
    CHECK(diags.size() == 1 && diags[0].message == "the file is empty");
    diags.clear();
    CHECK(!Document::Parse("{'a': 1}", d, diags));
    diags.clear();
    CHECK(!Document::Parse("[1.]", d, diags));
    CHECK(diags.size() == 1 && diags[0].message == "a decimal point must be followed by a digit");
    diags.clear();
    std::string deep(65, '[');
    deep += std::string(65, ']');
    CHECK(Document::Parse(deep, d, diags));
    deep = std::string(66, '[') + std::string(66, ']');
    diags.clear();
    CHECK(!Document::Parse(deep, d, diags));
    // every byte is in exactly one token, in order
    const std::string text = "\xEF\xBB\xBF// h\r\n{ \"a\" : [ 1 ,\t2 ] /*b*/ }\r\n  ";
    CHECK(Document::Parse(text, d, diags));
    std::string joined;
    std::size_t at = 0;
    for (const Token& t : d.Tree().tokens) {
        CHECK(t.begin == at);
        joined += text.substr(t.begin, t.end - t.begin);
        at = t.end;
    }
    CHECK(joined == text);
    CHECK(d.Tree().tokens[0].kind == Tok::Bom);
    CHECK(d.Tree().tokens[1].kind == Tok::LineComment && d.Tree().tokens[2].kind == Tok::Newline);
    CHECK(d.HeaderComment() == "h");
}

// ---- the real filter

struct Real {
    std::string text;
    Document doc;
};

void TestRoundTrip(const std::string& path) {
    const std::string text = ReadFile(path);
    CHECK(!text.empty());
    Document d = MustParse(text);
    CHECK(d.Text() == text);
    std::printf("  round trip %s: %zu bytes, %zu tokens\n", path.c_str(), text.size(), d.Tree().tokens.size());

    const std::string dir = path.substr(0, path.rfind('/'));
    for (const char* commit : {"0ea6e6a", "f8f74da"}) {
        const std::string old = RunCommand("git -C '" + dir + "' show " + commit + ":Sullys-QoL-Filter.json 2>/dev/null");
        CHECK(old.size() > 100000);
        Document o = MustParse(old);
        CHECK(o.Text() == old);
        CHECK(o.ActiveCount() > 690);
        std::printf("  round trip %s: %zu bytes, %d rules, %zu headings\n", commit, old.size(), o.ActiveCount(),
                    o.Headings().size());
    }
}

void TestReadModel(const Document& d) {
    CHECK(d.ActiveCount() == 729);
    CHECK(d.Rules().size() == 729);
    CHECK(d.Warnings().empty());
    for (int i = 1; i <= 729; ++i) {
        char id[8];
        std::snprintf(id, sizeof id, "R%03d", i);
        int found = 0;
        for (const Rule& r : d.Rules()) found += r.id == id;
        CHECK(found == 1);
    }
    for (int i = 0; i < 729; ++i) CHECK(d.Rules()[static_cast<std::size_t>(i)].index == i);

    const Settings& s = d.GetSettings();
    CHECK(s.title == "Sully's QoL Filter");
    CHECK(s.author == "Sully");
    CHECK(s.version == "0.9.4");
    CHECK(s.formatVersion == 1);
    CHECK(s.defaultLevel == 3);
    CHECK(s.soundVolume == 25);
    CHECK(Contains(s.updateUrl, "Sullys-QoL-Filter.json"));
    CHECK(s.filterLevels.size() == 4);
    CHECK(s.filterLevels[0].id == 1 && s.filterLevels[0].name.Find("enUS")->Str() == "Leveling");
    CHECK(s.filterLevels[3].name.Find("deDE")->Str() == "Extrem streng - Karten+");
    CHECK(!d.Styles().empty() && d.Styles()[0].name == "EtherealMythical");
    CHECK(d.Styles().back().name == "TemperedLabel");
    CHECK(Contains(d.Styles()[0].value.Find("chatMessage")->Find("enUS")->Str(), "\xC3\xBF" "c1"));
    CHECK(Contains(d.HeaderComment(), "Sully's QoL Filter 0.9.4"));

    const std::vector<std::string> parts = {"Protection", "Drop Announcements", "Superior Base Stat Tags",
                                            "Hide by Strictness", "Highlights and Labels", "Name Shortening"};
    const std::vector<int> sectionCounts = {4, 5, 6, 10, 16, 5};
    std::vector<std::string> gotParts;
    std::vector<int> gotCounts;
    int sections = 0;
    for (std::size_t h = 0; h < d.Headings().size(); ++h) {
        const Heading& hd = d.Headings()[h];
        if (hd.level == Level::Part) {
            gotParts.push_back(hd.name);
            gotCounts.push_back(0);
            CHECK(!hd.description.empty());
        } else {
            ++sections;
            CHECK(hd.part >= 0 && d.Headings()[static_cast<std::size_t>(hd.part)].name == gotParts.back());
            ++gotCounts.back();
            CHECK(!hd.description.empty());
        }
    }
    CHECK(gotParts == parts);
    CHECK(gotCounts == sectionCounts);
    CHECK(sections == 46);
    // every rule is in a part and a section, and the sections hold all rules once
    int covered = 0;
    for (const Heading& hd : d.Headings()) {
        if (hd.level == Level::Section) covered += hd.endRule - hd.firstRule;
    }
    CHECK(covered == 729);
    for (const Rule& r : d.Rules()) CHECK(r.part >= 0 && r.section >= 0);

    const Rule& r697 = d.Rules()[static_cast<std::size_t>(d.FindRule("R697"))];
    CHECK(d.Headings()[static_cast<std::size_t>(r697.section)].name == "Missile weapons, crossbows and helms");
    CHECK(d.Headings()[static_cast<std::size_t>(r697.part)].name == "Superior Base Stat Tags");
    CHECK(Contains(r697.lineComment, "Superior missile weapons with the maximum projectile speed roll"));

    const Rule& r001 = d.Rules()[0];
    CHECK(r001.id == "R001");
    CHECK(r001.lineComment.empty());  // a blank line separates it from the section description
    CHECK(d.Headings()[static_cast<std::size_t>(r001.section)].name == "Mythicals");
    CHECK(d.Headings()[static_cast<std::size_t>(r001.section)].description == "Mythical uniques are never hidden at any level.");
    CHECK(r001.line == 73);
    const Value* codes = r001.value.Find("itemCodes");
    CHECK(codes && codes->IsArray() && codes->array.size() == 64 && codes->array.back().Str() == "mjw");
    CHECK(codes && codes->array[0].Str() == "dd1" && codes->array[1].Str() == "m01");
    CHECK(r001.value.Find("show")->boolean);

    const Rule& r705 = d.Rules()[1];
    CHECK(r705.id == "R705");
    CHECK(r705.lineComment.rfind("The Lich King Essence amulets (au9)", 0) == 0);
    CHECK(r705.value.Find("uniqueOrSetIds")->array.size() == 6);

    const Rule& r018 = d.Rules()[static_cast<std::size_t>(d.FindRule("R018"))];
    const Value* exclude = r018.value.Find("excludeItemCodes");
    CHECK(exclude && exclude->array.size() == 76 && exclude->array.back().Str() == "qf2");

    // the one section whose description sits right above its first rule
    const int organs = d.FindHeading("Name Shortening", "Organs");
    CHECK(organs >= 0);
    CHECK(d.Headings()[static_cast<std::size_t>(organs)].description.rfind("Organs and the Jawbone", 0) == 0);
    CHECK(d.FindHeading("Highlights and Labels", "Uniques and sets") > d.FindHeading("Hide by Strictness", "Uniques and sets"));
}

auto RuleLine(const Document& d, const char* id) -> int {
    return static_cast<int>(d.Rules()[static_cast<std::size_t>(d.FindRule(id))].line);
}

void TestFieldEdits(const Document& base) {
    const std::string original = base.Text();
    const int r002 = base.FindRule("R002");

    {  // replace a field in a one-line rule
        Document d = base;
        CHECK_OK(d.SetField(r002, "activeInLevels", Value::Array({Value::Integer(1), Value::Integer(2)})));
        CheckReparses(d);
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed == std::vector<int>{RuleLine(base, "R002")});
        CHECK(diff.added.size() == 1 && diff.added[0].first == RuleLine(base, "R002"));
        CHECK(Contains(diff.added[0].second, "\"activeInLevels\": [1, 2], \"show\": true},"));
        Value expect = base.Rules()[static_cast<std::size_t>(r002)].value;
        expect.Set("activeInLevels", Value::Array({Value::Integer(1), Value::Integer(2)}));
        CHECK(d.Rules()[static_cast<std::size_t>(r002)].value == expect);
        CheckOthersSame(base, d, {"R002"});
    }
    {  // a new field in a one-line rule: at the end, or before a key
        Document d = base;
        CHECK_OK(d.SetField(r002, "notify", Value::Bool(true)));
        CHECK_OK(d.SetField(r002, "baseTierMax", Value::Integer(2), "isEthereal"));
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 1);
        CHECK(Contains(diff.added[0].second, "\"baseTier\": 0, \"baseTierMax\": 2, \"isEthereal\": true"));
        CHECK(Contains(diff.added[0].second, "\"show\": true, \"notify\": true},"));
        CheckOthersSame(base, d, {"R002"});
    }
    {  // a new field in a one-field-per-line rule: its own line, same indent
        Document d = base;
        const int r006 = d.FindRule("R006");
        CHECK_OK(d.SetField(r006, "notify", Value::Object({{"enUS", Str("\xC3\xBF" "c4Skills")}})));
        CheckReparses(d);
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 2);
        CHECK(diff.added.size() == 2 && diff.added[0].second == "      \"show\": true,");
        CHECK(diff.added.size() == 2 && diff.added[1].second == "      \"notify\": {\"enUS\": \"\xC3\xBF" "c4Skills\"}");
        CheckOthersSame(base, d, {"R006"});
        // replace a value in the middle; remove a middle field and the last field
        CHECK_OK(d.SetField(r006, "quality", Str("superior")));
        CHECK(Contains(d.Text(), "      \"quality\": \"superior\",\n      \"itemStats\""));
        CHECK_OK(d.RemoveField(r006, "itemStats"));
        CHECK_OK(d.RemoveField(r006, "notify"));
        const Diff diff2 = LineDiff(original, d.Text());
        CHECK(diff2.removed.size() == 2 && diff2.added.size() == 1);
        CHECK(diff2.added.size() == 1 && diff2.added[0].second == "      \"quality\": \"superior\",");
        Value expect = base.Rules()[static_cast<std::size_t>(r006)].value;
        expect.Set("quality", Str("superior"));
        expect.Erase("itemStats");
        CHECK(d.Rules()[static_cast<std::size_t>(r006)].value == expect);
        CHECK_OK(d.RemoveField(r006, "show"));  // the last: the comma above goes too
        CHECK(Contains(d.Text(), "\"armo\", \"weap\"],\n      \"quality\": \"superior\"\n    },"));
        CheckReparses(d);
    }
    {  // a wrapped list stays wrapped: same indent, same values per line
        Document d = base;
        const int r018 = d.FindRule("R018");
        Value codes = *d.Rules()[static_cast<std::size_t>(r018)].value.Find("excludeItemCodes");
        codes.array.push_back(Str("qf3"));
        CHECK_OK(d.SetField(r018, "excludeItemCodes", codes));
        CheckReparses(d);
        const Diff diff = LineDiff(original, d.Text());
        // only the last row of the list changes
        CHECK(diff.removed.size() == 1 && diff.added.size() == 1);
        CHECK(diff.added.size() == 1 && diff.added[0].second == "        \"qf2\", \"qf3\"");
        // a shorter list is wrapped the same way
        codes.array.erase(codes.array.begin(), codes.array.begin() + 20);
        CHECK_OK(d.SetField(r018, "excludeItemCodes", codes));
        const auto lines = Lines(d.Text());
        const int at = RuleLine(base, "R018");
        // lines[] counts from 0: lines[at - 1] is the rule's "{"
        CHECK(lines[static_cast<std::size_t>(at + 2)] == "      \"excludeItemCodes\": [");
        CHECK(lines[static_cast<std::size_t>(at + 3)].rfind("        \"m21\", \"m22\",", 0) == 0);
        int perLine = 0;
        for (char c : lines[static_cast<std::size_t>(at + 3)]) perLine += c == ',';
        CHECK(perLine == 15);
        CHECK(d.Rules()[static_cast<std::size_t>(r018)].value.Find("excludeItemCodes")->array == codes.array);
        CheckOthersSame(base, d, {"R018"});
    }
    {  // title and line comments
        Document d = base;
        const int r004 = d.FindRule("R004");
        CHECK_OK(d.SetTitle(r004, "R004 | Ethereal elite weapons, 0 or 2+ sockets | never hide at L1-4"));
        CHECK(d.Rules()[static_cast<std::size_t>(r004)].title == "R004 | Ethereal elite weapons, 0 or 2+ sockets | never hide at L1-4");
        Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 1);

        d = base;
        CHECK_OK(d.SetLineComment(r002, "Ethereal normal weapons."));
        diff = LineDiff(original, d.Text());
        CHECK(diff.removed.empty() && diff.added.size() == 1);
        CHECK(diff.added.size() == 1 && diff.added[0].second == "    // Ethereal normal weapons." &&
              diff.added[0].first == RuleLine(base, "R002"));
        CHECK(d.Rules()[static_cast<std::size_t>(r002)].lineComment == "Ethereal normal weapons.");
        CheckOthersSame(base, d, {"R002"});

        d = base;
        CHECK_OK(d.SetLineComment(1, "Lich King amulets.\nNever hidden."));
        diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 2);
        CHECK(d.Rules()[1].lineComment == "Lich King amulets.\nNever hidden.");
        CHECK_OK(d.SetLineComment(1, ""));
        diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.empty());
        CHECK(d.Rules()[1].lineComment.empty());
        CheckOthersSame(base, d, {"R705"});
    }
}

void TestRuleEdits(const Document& base) {
    const std::string original = base.Text();
    const Value rule = Value::Object({{"comment", Str("U001 | Test rule | hide")},
                                      {"itemCodes", Value::Array({Str("gld")})},
                                      {"hide", Value::Bool(true)}});
    const std::string ruleText = R"({"comment": "U001 | Test rule | hide", "itemCodes": ["gld"], "hide": true})";
    const int r002 = base.FindRule("R002");
    const int ethereal = base.FindHeading("Protection", "Ethereal weapons");

    {  // after a rule
        Document d = base;
        CHECK_OK(d.InsertRule(Position::After(r002), rule, "A test."));
        CheckReparses(d);
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.empty() && diff.added.size() == 2);
        CHECK(diff.added.size() == 2 && diff.added[0].second == "    // A test." && diff.added[1].second == "    " + ruleText + ",");
        CHECK(d.ActiveCount() == 730);
        const Rule& u = d.Rules()[static_cast<std::size_t>(r002 + 1)];
        CHECK(u.id == "U001" && u.value == rule && u.lineComment == "A test.");
        CHECK(d.Headings()[static_cast<std::size_t>(u.section)].name == "Ethereal weapons");
        CHECK(d.Rules()[static_cast<std::size_t>(r002 + 2)].id == "R003");
        CheckOthersSame(base, d, {});
    }
    {  // start and end of a section, start of a part
        Document d = base;
        CHECK_OK(d.InsertRule(Position::StartOf(ethereal), rule));
        CHECK(d.Rules()[static_cast<std::size_t>(r002)].id == "U001");
        Diff diff = LineDiff(original, d.Text());
        CHECK(diff.added.size() == 1 && diff.added[0].first == RuleLine(base, "R002"));

        d = base;
        CHECK_OK(d.InsertRule(Position::EndOf(ethereal), rule));
        const int r005 = d.FindRule("R005");
        CHECK(d.Rules()[static_cast<std::size_t>(r005 + 1)].id == "U001");
        CHECK(d.Headings()[static_cast<std::size_t>(d.Rules()[static_cast<std::size_t>(r005 + 1)].section)].name == "Ethereal weapons");
        diff = LineDiff(original, d.Text());
        CHECK(diff.added.size() == 1 && diff.added[0].first == RuleLine(base, "R005") + 1 && diff.removed.empty());

        d = base;
        CHECK_OK(d.InsertRule(Position::Begin(), rule));
        CHECK(d.Rules()[0].id == "U001" && d.Rules()[1].id == "R001");
    }
    {  // at the end of the array: the last rule gets a comma
        Document d = base;
        CHECK_OK(d.InsertRule(Position::End(), rule));
        CheckReparses(d);
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 2);
        CHECK(diff.added.size() == 2 && diff.added[0].second.back() == ',' && Contains(diff.added[0].second, "R696"));
        CHECK(diff.added.size() == 2 && diff.added[1].second == "    " + ruleText);
        CHECK(d.Rules()[729].id == "U001");
        CHECK(d.Headings()[static_cast<std::size_t>(d.Rules()[729].section)].name == "Decals");
        // and deleting it takes the comma away again
        CHECK_OK(d.DeleteRule(729));
        CHECK(d.Text() == original);
    }
    {  // delete
        Document d = base;
        CHECK_OK(d.DeleteRule(d.FindRule("R003")));
        CheckReparses(d);
        Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed == std::vector<int>{RuleLine(base, "R003")} && diff.added.empty());
        CHECK(d.ActiveCount() == 728 && d.FindRule("R003") < 0);
        CheckOthersSame(base, d, {"R003"});
        // a rule with a line comment and several lines
        d = base;
        CHECK_OK(d.DeleteRule(d.FindRule("R018")));
        diff = LineDiff(original, d.Text());
        CHECK(diff.added.empty() && static_cast<int>(diff.removed.size()) == 17);
        CHECK(diff.removed.front() == RuleLine(base, "R018") - 1);
        // the last rule: the comma on the rule above goes
        d = base;
        CHECK_OK(d.DeleteRule(728));
        diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 2 && diff.added.size() == 1);
        CHECK(diff.added.size() == 1 && Contains(diff.added[0].second, "R695") && diff.added[0].second.back() == '}');
    }
    {  // move
        Document d = base;
        CHECK_OK(d.MoveRule(r002, Position::After(d.FindRule("R004"))));
        CheckReparses(d);
        CHECK(d.Rules()[static_cast<std::size_t>(r002)].id == "R003");
        CHECK(d.Rules()[static_cast<std::size_t>(r002 + 2)].id == "R002");
        const Diff diff = LineDiff(original, d.Text());
        CHECK(diff.removed.size() == 1 && diff.added.size() == 1);
        CheckOthersSame(base, d, {});
        // a multi-line rule with its line comment, to another part
        d = base;
        const int r018 = d.FindRule("R018");
        CHECK_OK(d.MoveRule(r018, Position::EndOf(d.FindHeading("Name Shortening"))));
        CHECK(d.Rules()[728].id == "R018");
        CHECK(d.Rules()[728].lineComment == base.Rules()[static_cast<std::size_t>(r018)].lineComment);
        CHECK(d.Headings()[static_cast<std::size_t>(d.Rules()[728].section)].name == "Decals");
        CHECK(Contains(d.Text(), "\"ptBR\": \"\xC3\xBF" "c4Um\"}},\n    // Chat announcement for normal-tier uniques"));
        CHECK(Contains(d.Text(), "      \"excludeItemCodes\": [\n        \"dd1\""));
        CheckOthersSame(base, d, {});
        // and back
        CHECK_OK(d.MoveRule(728, Position::Before(r018)));
        CHECK(d.Text() == original);
    }
}

void TestDisable(const Document& base) {
    const std::string original = base.Text();
    {
        Document d = base;
        const int r003 = d.FindRule("R003");
        CHECK_OK(d.DisableRule(r003));
        CheckReparses(d);
        CHECK(d.ActiveCount() == 728 && d.Rules().size() == 729);
        const Rule& off = d.Rules().back();
        CHECK(off.disabled && off.id == "R003" && off.at == r003 && off.index == 0);
        CHECK(off.value == base.Rules()[static_cast<std::size_t>(r003)].value);
        CHECK(d.Headings()[static_cast<std::size_t>(off.section)].name == "Ethereal weapons");
        const std::optional<Value> list = d.TopLevel("disabledRules");
        CHECK(list && list->array.size() == 1 && list->array[0].Find("at")->text == std::to_string(r003));
        CHECK(Contains(d.Text(), "  ],\n  \"disabledRules\": [\n    // from: Protection > Ethereal weapons\n    {\"at\": "));
        CHECK(d.Text().size() > original.size() && d.Text().substr(d.Text().size() - 9) == "}}\n  ]\n}\n");
        CheckOthersSame(base, d, {});
        // edits work on disabled rules too
        const int idx = static_cast<int>(d.Rules().size()) - 1;
        CHECK_OK(d.SetField(idx, "activeInLevels", Value::Array({Value::Integer(1)})));
        CHECK(d.Rules().back().value.Find("activeInLevels")->array.size() == 1);
        CHECK_OK(d.SetField(idx, "activeInLevels", Value::Array({Value::Integer(1), Value::Integer(2)})));
        // a second one, multi-line, first of its section, with a line comment
        CHECK_OK(d.DisableRule(d.FindRule("R018")));
        CHECK(d.ActiveCount() == 727);
        CHECK(d.Rules().back().lineComment == base.Rules()[static_cast<std::size_t>(base.FindRule("R018"))].lineComment);
        CHECK(d.Headings()[static_cast<std::size_t>(d.Rules().back().section)].name == "Uniques and sets");
        CheckReparses(d);
        // enable both again, newest first: the file is as before
        CHECK_OK(d.EnableRule(d.FindRule("R018")));
        CHECK_OK(d.EnableRule(d.FindRule("R003")));
        CHECK(d.TopLevel("disabledRules") == std::nullopt);
        CHECK(d.Text() == original);
    }
    {  // every kind of position: first of a section, last of a section, first and last rule
        for (const char* id : {"R001", "R705", "R002", "R005", "R009", "R010", "R696", "R278"}) {
            Document d = base;
            const int r = d.FindRule(id);
            CHECK_OK(d.DisableRule(r));
            CHECK_OK(d.EnableRule(d.FindRule(id)));
            if (d.Text() != original) std::printf("  disable+enable %s changed the file\n", id);
            CHECK(d.Text() == original);
        }
    }
    {  // a sweep over the filter: the rules come back in order with the same meaning
        int exact = 0;
        int tried = 0;
        for (int i = 0; i < base.ActiveCount(); i += 9) {
            Document d = base;
            const std::string id = base.Rules()[static_cast<std::size_t>(i)].id;
            CHECK_OK(d.DisableRule(i));
            CHECK(d.FindRule(id) == d.ActiveCount() && d.Rules().back().section == base.Rules()[static_cast<std::size_t>(i)].section);
            CHECK_OK(d.EnableRule(d.FindRule(id)));
            ++tried;
            exact += d.Text() == original;
            bool same = d.Rules().size() == base.Rules().size();
            for (std::size_t k = 0; same && k < base.Rules().size(); ++k) {
                const Rule& a = base.Rules()[k];
                const Rule& b = d.Rules()[k];
                same = a.id == b.id && a.value == b.value && a.lineComment == b.lineComment && a.section == b.section;
            }
            CHECK(same);
        }
        std::printf("  disable+enable sweep: %d rules, %d give the file back byte for byte\n", tried, exact);
        CHECK(exact >= tried - 2);
    }
    {  // entries written by the web editor (no "from:" line) go back to their at
        std::string text = original;
        const std::size_t end = text.rfind("  ]\n}");
        text.insert(end + 4, ",\n  \"disabledRules\": [{\"at\": 2, \"rule\": {\"comment\": \"U005 | Off | hide\", \"hide\": true}}]");
        Document d = MustParse(text);
        CHECK(d.ActiveCount() == 729 && d.Rules().size() == 730);
        const Rule& off = d.Rules().back();
        CHECK(off.disabled && off.id == "U005" && off.at == 2);
        CHECK(d.Headings()[static_cast<std::size_t>(off.section)].name == "Ethereal weapons");
        CHECK_OK(d.EnableRule(729));
        CHECK(d.Rules()[2].id == "U005" && d.Rules()[3].id == "R002");
        CHECK(d.TopLevel("disabledRules") == std::nullopt);
        CHECK(d.ActiveCount() == 730);
        CheckReparses(d);
    }
}

void TestHeadings(const Document& base) {
    const std::string original = base.Text();
    Document d = base;
    const int r004 = d.FindRule("R004");
    CHECK_OK(d.AddHeading(Level::Section, Position::Before(r004), "Elite ethereals", "Elite and mythical ethereal weapons."));
    CheckReparses(d);
    Diff diff = LineDiff(original, d.Text());
    CHECK(diff.removed.empty() && diff.added.size() == 4);
    CHECK(diff.added.size() == 4 && diff.added[1].second == "    // ---- Elite ethereals" &&
          diff.added[2].second == "    // Elite and mythical ethereal weapons." && diff.added[0].second.empty() &&
          diff.added[3].second.empty());
    const int h = d.FindHeading("Protection", "Elite ethereals");
    CHECK(h >= 0);
    CHECK(d.Rules()[static_cast<std::size_t>(r004)].section == h);
    CHECK(d.Rules()[static_cast<std::size_t>(d.FindRule("R005"))].section == h);
    CHECK(d.Headings()[static_cast<std::size_t>(d.Rules()[static_cast<std::size_t>(d.FindRule("R003"))].section)].name == "Ethereal weapons");
    CHECK(d.Headings()[static_cast<std::size_t>(h)].description == "Elite and mythical ethereal weapons.");
    CHECK(d.Headings()[static_cast<std::size_t>(h)].part == d.FindHeading("Protection"));
    CheckOthersSame(base, d, {});

    CHECK_OK(d.RenameHeading(h, "Elite and mythical"));
    CHECK(d.Headings()[static_cast<std::size_t>(h)].name == "Elite and mythical");
    CHECK_OK(d.SetHeadingDescription(h, "Line one.\nLine two."));
    CHECK(d.Headings()[static_cast<std::size_t>(h)].description == "Line one.\nLine two.");
    CHECK_OK(d.SetHeadingDescription(h, ""));
    CHECK(d.Headings()[static_cast<std::size_t>(h)].description.empty());
    CHECK_OK(d.SetHeadingDescription(h, "Back."));
    CHECK_OK(d.RemoveHeading(h));
    CHECK(d.Text() == original);

    // a part with an empty section at the end, then a rule in it
    d = base;
    CHECK_OK(d.AddHeading(Level::Part, Position::End(), "My rules", "A player's own rules."));
    CHECK_OK(d.AddHeading(Level::Section, Position::End(), "Hides"));
    const int mine = d.FindHeading("My rules");
    const int hides = d.FindHeading("My rules", "Hides");
    CHECK(mine >= 0 && hides == mine + 1);
    CHECK(d.Headings()[static_cast<std::size_t>(hides)].firstRule == 729 && d.Headings()[static_cast<std::size_t>(hides)].endRule == 729);
    CHECK_OK(d.InsertRule(Position::StartOf(hides), Value::Object({{"comment", Str("U001 | Mine | hide")}, {"hide", Value::Bool(true)}})));
    CHECK_OK(d.InsertRule(Position::EndOf(mine), Value::Object({{"comment", Str("U002 | Mine | hide")}, {"hide", Value::Bool(true)}})));
    CheckReparses(d);
    CHECK(d.Rules()[729].id == "U001" && d.Rules()[729].section == hides && d.Rules()[729].part == mine);
    CHECK(d.Rules()[730].id == "U002" && d.Rules()[730].section == hides);
    CHECK(Contains(d.Text(), "c4Um\"}},\n\n    // ==== My rules\n    // A player's own rules.\n\n    // ---- Hides\n\n"
                             "    {\"comment\": \"U001 | Mine | hide\", \"hide\": true},\n"
                             "    {\"comment\": \"U002 | Mine | hide\", \"hide\": true}\n  ]"));
    // the description of the old last section is unchanged
    CHECK(d.Headings()[static_cast<std::size_t>(d.FindHeading("Name Shortening", "Decals"))].endRule == 729);
    CheckOthersSame(base, d, {});
}

void TestSettings(const Document& base) {
    const std::string original = base.Text();
    Document d = base;
    CHECK_OK(d.SetSetting("soundVolume", Value::Integer(30)));
    CHECK(d.GetSettings().soundVolume == 30);
    Diff diff = LineDiff(original, d.Text());
    CHECK(diff.removed.size() == 1 && diff.added.size() == 1 && diff.added[0].second == "  \"soundVolume\": 30,");
    CHECK_OK(d.SetSetting("hideUnmatchedItems", Value::Bool(false)));
    CHECK(d.GetSettings().hideUnmatchedItems == false);
    diff = LineDiff(original, d.Text());
    CHECK(diff.added.size() == 2 && diff.added[1].second == "  \"hideUnmatchedItems\": false,");
    CHECK(Contains(d.Text(), "  \"hideUnmatchedItems\": false,\n  \"styles\": {"));
    // a multi-line list of objects keeps one object per line
    Value levels = *d.TopLevel("filterLevels");
    levels.array.pop_back();
    CHECK_OK(d.SetSetting("filterLevels", levels));
    CHECK(d.GetSettings().filterLevels.size() == 3);
    diff = LineDiff(original, d.Text());
    CHECK(diff.removed.size() == 3);
    CheckOthersSame(base, d, {});
}

// ---- edge cases on small files

void TestEdgeCases() {
    const Value rule = Value::Object({{"comment", Str("U001 | A | hide")}, {"hide", Value::Bool(true)}});

    {  // empty rules array, one line
        Document d = MustParse("{\"title\": \"x\", \"rules\": []}");
        CHECK(d.ActiveCount() == 0 && d.Headings().empty());
        CHECK_OK(d.InsertRule(Position::End(), rule));
        CHECK(d.Text() == "{\"title\": \"x\", \"rules\": [\n  {\"comment\": \"U001 | A | hide\", \"hide\": true}\n]}");
        CHECK_OK(d.InsertRule(Position::End(), rule));
        CHECK(d.ActiveCount() == 2);
        CHECK_OK(d.DeleteRule(0));
        CHECK_OK(d.DeleteRule(0));
        CHECK(d.Text() == "{\"title\": \"x\", \"rules\": [\n]}");
        CHECK_OK(d.AddHeading(Level::Part, Position::Begin(), "P"));
        CHECK(d.Headings().size() == 1);
        CheckReparses(d);
    }
    {  // empty multi-line rules array
        const std::string text = "{\n  \"rules\": [\n  ]\n}\n";
        Document d = MustParse(text);
        CHECK(d.Text() == text);
        CHECK_OK(d.InsertRule(Position::Begin(), rule, "Why."));
        CHECK(d.Text() == "{\n  \"rules\": [\n    // Why.\n    {\"comment\": \"U001 | A | hide\", \"hide\": true}\n  ]\n}\n");
        CHECK(d.Rules()[0].lineComment == "Why.");
        CHECK_OK(d.DisableRule(0));
        CHECK(d.Text() == "{\n  \"rules\": [\n  ],\n  \"disabledRules\": [\n    // Why.\n"
                          "    {\"at\": 0, \"rule\": {\"comment\": \"U001 | A | hide\", \"hide\": true}}\n  ]\n}\n");
        CHECK(d.Rules().size() == 1 && d.Rules()[0].disabled && d.Rules()[0].lineComment == "Why.");
        CHECK_OK(d.EnableRule(0));
        CHECK(d.Text() == "{\n  \"rules\": [\n    // Why.\n    {\"comment\": \"U001 | A | hide\", \"hide\": true}\n  ]\n}\n");
    }
    {  // no headings; rules on one line; the last rule
        const std::string text = "{\"rules\": [{\"comment\": \"R001 | a | hide\"}, {\"comment\": \"R002 | b | hide\"}]}";
        Document d = MustParse(text);
        CHECK(d.ActiveCount() == 2 && d.Rules()[1].part == -1 && d.Rules()[1].section == -1);
        CHECK_OK(d.InsertRule(Position::After(1), rule));
        CHECK(d.Text() == "{\"rules\": [{\"comment\": \"R001 | a | hide\"}, {\"comment\": \"R002 | b | hide\"}, " + Render(rule) + "]}");
        CHECK_OK(d.DeleteRule(2));
        CHECK(d.Text() == text);
        CHECK_OK(d.InsertRule(Position::Before(0), rule));
        CHECK_OK(d.DeleteRule(0));
        CHECK(d.Text() == text);
        CHECK_OK(d.DeleteRule(1));
        CHECK(d.Text() == "{\"rules\": [{\"comment\": \"R001 | a | hide\"}]}");
        CHECK_OK(d.DeleteRule(0));
        CHECK(d.Text() == "{\"rules\": []}");
    }
    {  // comments inside a rule object, block comments, trailing commas
        const std::string text =
            "{\n"
            "  \"rules\": [\n"
            "    // ==== Part\n"
            "    {\n"
            "      // the title\n"
            "      \"comment\": \"R001 | a | hide\", /* inline */\n"
            "      \"itemCodes\": [\"a\", /* b */ \"c\"],\n"
            "      // why hide\n"
            "      \"hide\": true,\n"
            "    },\n"
            "    {\"comment\": \"R002 | b | hide\", /* x */ \"hide\": true},\n"
            "  ]\n"
            "}\n";
        Document d = MustParse(text);
        CHECK(d.Text() == text);
        CHECK(d.ActiveCount() == 2 && d.Rules()[0].part == 0 && d.Headings()[0].description.empty());
        CHECK(d.Rules()[0].value.Find("itemCodes")->array.size() == 2);
        CHECK_OK(d.SetField(0, "notify", Value::Bool(true)));
        CHECK(Contains(d.Text(), "      \"hide\": true,\n      \"notify\": true,\n    },"));
        CHECK_OK(d.RemoveField(0, "hide"));
        CHECK(Contains(d.Text(), "/* b */ \"c\"],\n      \"notify\": true,\n    },"));  // its comment line went with it
        CHECK_OK(d.SetField(0, "itemCodes", Value::Array({Str("z")})));
        CHECK(Contains(d.Text(), "\"comment\": \"R001 | a | hide\", /* inline */\n      \"itemCodes\": [\"z\"],"));
        CHECK_OK(d.RemoveField(1, "hide"));
        CHECK(Contains(d.Text(), "{\"comment\": \"R002 | b | hide\"},\n  ]"));
        CHECK_OK(d.InsertRule(Position::End(), rule));
        CHECK(Contains(d.Text(), "{\"comment\": \"R002 | b | hide\"},\n    " + Render(rule) + ",\n  ]"));
        CheckReparses(d);
        CHECK_OK(d.DeleteRule(2));
        CHECK_OK(d.DeleteRule(1));
        CHECK(Contains(d.Text(), "      \"notify\": true,\n    },\n  ]"));
        CheckReparses(d);
    }
    {  // CRLF is kept, also in new lines
        std::string text =
            "// head\r\n{\r\n  \"rules\": [\r\n    // ---- S\r\n    // desc\r\n\r\n    {\r\n      \"comment\": \"R001 | a | hide\",\r\n"
            "      \"hide\": true\r\n    }\r\n  ]\r\n}\r\n";
        Document d = MustParse(text);
        CHECK(d.Text() == text);
        CHECK(d.HeaderComment() == "head");
        CHECK(d.Headings().size() == 1 && d.Headings()[0].description == "desc");
        CHECK_OK(d.SetField(0, "itemCodes", Value::Array({Str("x")})));
        CHECK_OK(d.InsertRule(Position::End(), rule, "c"));
        CHECK_OK(d.SetLineComment(0, "first"));
        CHECK_OK(d.AddHeading(Level::Section, Position::After(1), "T", "d"));
        CHECK_OK(d.DisableRule(1));
        const std::string& t = d.Text();
        bool loneLf = false;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '\n' && (i == 0 || t[i - 1] != '\r')) loneLf = true;
        }
        CHECK(!loneLf);
        CHECK(Contains(t, "      \"hide\": true,\r\n      \"itemCodes\": [\"x\"]\r\n"));
        CHECK(d.Rules()[0].lineComment == "first");
        CHECK_OK(d.EnableRule(1));
        CHECK(d.Rules()[1].lineComment == "c");
        CheckReparses(d);
    }
    {  // a byte order mark is kept; trailing whitespace is kept
        const std::string text = "\xEF\xBB\xBF{  \n  \"rules\": [   \n    {\"comment\": \"R001 | a | hide\"}  \n  ]  \n}  \n\n";
        Document d = MustParse(text);
        CHECK(d.Text() == text);
        CHECK_OK(d.SetField(0, "hide", Value::Bool(true)));
        CHECK(d.Text() == "\xEF\xBB\xBF{  \n  \"rules\": [   \n    {\"comment\": \"R001 | a | hide\", \"hide\": true}  \n  ]  \n}  \n\n");
        CHECK_OK(d.InsertRule(Position::After(0), rule));
        CHECK(Contains(d.Text(), "\"hide\": true},  \n    " + Render(rule) + "\n  ]  \n"));
        CHECK_OK(d.SetSetting("title", Str("T")));
        CHECK(d.Text().rfind("\xEF\xBB\xBF{  \n  \"title\": \"T\",\n  \"rules\"", 0) == 0);
        CheckReparses(d);
    }
    {  // a heading without a description stays without one when a line comment is added
        const std::string text = "{\n  \"rules\": [\n    // ---- S\n    {\"comment\": \"R001 | a | hide\"}\n  ]\n}\n";
        Document d = MustParse(text);
        CHECK(d.Headings()[0].description.empty());
        CHECK_OK(d.SetLineComment(0, "note"));
        CHECK(d.Headings()[0].description.empty() && d.Rules()[0].lineComment == "note");
        CHECK_OK(d.SetHeadingDescription(0, "desc"));
        CHECK(d.Headings()[0].description == "desc" && d.Rules()[0].lineComment == "note");
        CHECK_OK(d.SetHeadingDescription(0, ""));
        CHECK(d.Headings()[0].description.empty() && d.Rules()[0].lineComment == "note");
        CheckReparses(d);
    }
    {  // warnings about the structure
        Document d;
        std::vector<Diag> diags;
        CHECK(Document::Parse("{\"rules\": [{\"comment\": \"R001 | a\"}, 5, {\"comment\": \"R001 | b\"}],"
                              " \"disabledRules\": [{\"rule\": {}}]}", d, diags));
        CHECK(diags.size() == 3);
        CHECK(d.ActiveCount() == 3 && d.Rules().size() == 3);
        CHECK(d.Rules()[1].id.empty() && d.Rules()[1].value == Value::Integer(5));
        diags.clear();
        CHECK(Document::Parse("{\"title\": \"x\"}", d, diags));
        CHECK(diags.size() == 1 && diags[0].severity == Diag::Severity::Warning);
        CHECK(!d.InsertRule(Position::End(), rule));
    }
    {  // errors leave the document as it was
        Document d = MustParse("{\"rules\": [{\"comment\": \"R001 | a | hide\"}]}");
        const std::string before = d.Text();
        CHECK(!d.DeleteRule(5));
        CHECK(!d.EnableRule(0));
        CHECK(!d.RemoveField(0, "nope"));
        CHECK(!d.InsertRule(Position::End(), Value::Array()));
        CHECK(!d.AddHeading(Level::Part, Position::End(), "x"));  // the rules are on one line
        CHECK(d.Text() == before);
    }
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string path = argc > 1 ? argv[1] : "/home/claude/qol-filter/Sullys-QoL-Filter.json";
    std::printf("strings and numbers\n");
    TestStrings();
    std::printf("syntax\n");
    TestSyntax();
    std::printf("round trips\n");
    TestRoundTrip(path);
    // The model and edit tests were written against 0.9.4 (729 rules, its ids and layout): they read that version from
    // the filter's git history, so later filter changes do not move them. The round trips above take the file as it is.
    const std::string dir = path.substr(0, path.rfind('/'));
    const std::string pinned = RunCommand("git -C '" + dir + "' show da7c826:Sullys-QoL-Filter.json 2>/dev/null");
    CHECK(pinned.size() > 100000);
    const Document real = MustParse(pinned);
    std::printf("read model\n");
    TestReadModel(real);
    std::printf("field edits\n");
    TestFieldEdits(real);
    std::printf("rule edits\n");
    TestRuleEdits(real);
    std::printf("disable and enable\n");
    TestDisable(real);
    std::printf("headings\n");
    TestHeadings(real);
    std::printf("settings\n");
    TestSettings(real);
    std::printf("edge cases\n");
    TestEdgeCases();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
