#include "document.h"

#include <algorithm>
#include <map>

namespace document {

namespace {

constexpr std::size_t npos = std::string::npos;

auto Trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// "R001" from "R001 | ...": R or U, digits, then the end or a non-alphanumeric.
auto RuleId(std::string_view title) -> std::string {
    title = Trim(title);
    if (title.size() < 2 || (title[0] != 'R' && title[0] != 'U')) return {};
    std::size_t i = 1;
    while (i < title.size() && title[i] >= '0' && title[i] <= '9') ++i;
    if (i == 1) return {};
    if (i < title.size()) {
        const char c = title[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return {};
    }
    return std::string(title.substr(0, i));
}

// One line of text: newlines become spaces.
auto OneLine(std::string_view s) -> std::string {
    std::string out(s);
    for (char& c : out) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return std::string(Trim(out));
}

auto Warn(std::vector<Diag>& out, std::uint32_t line, std::string message) {
    out.push_back(Diag{Diag::Severity::Warning, line, 0, std::move(message)});
}

}  // namespace

// ---- Reading

auto Document::Parse(std::string text, Document& out, std::vector<Diag>& diags) -> bool {
    out = Document{};
    return out.Rebuild(std::move(text), &diags);
}

auto Document::Rebuild(std::string text, std::vector<Diag>* diags) -> bool {
    Syntax syntax;
    Diag error;
    if (!ParseSyntax(text, syntax, error)) {
        if (diags) diags->push_back(error);
        if (text_.empty()) text_ = std::move(text);
        return false;
    }
    text_ = std::move(text);
    syntax_ = std::move(syntax);
    bom_ = (!syntax_.tokens.empty() && syntax_.tokens[0].kind == Tok::Bom) ? 3 : 0;
    const std::size_t lf = text_.find('\n');
    nl_ = (lf != npos && lf > 0 && text_[lf - 1] == '\r') ? "\r\n" : "\n";
    BuildModel();
    if (diags) diags->insert(diags->end(), warnings_.begin(), warnings_.end());
    return true;
}

auto Document::TokText(int i) const -> std::string_view {
    const Token& t = Tk(i);
    return std::string_view(text_).substr(t.begin, t.end - t.begin);
}

auto Document::NodeText(int node) const -> std::string_view {
    return std::string_view(text_).substr(NodeBegin(node), NodeEnd(node) - NodeBegin(node));
}

auto Document::ItemFirst(int container, int item) const -> int {
    const Item& it = Nd(container).items[static_cast<std::size_t>(item)];
    return it.key >= 0 ? it.key : Nd(it.node).first;
}

auto Document::ItemLast(int container, int item) const -> int {
    const Item& it = Nd(container).items[static_cast<std::size_t>(item)];
    return it.comma >= 0 ? it.comma : Nd(it.node).last;
}

auto Document::LineStart(std::size_t offset) const -> std::size_t {
    if (offset == 0) return 0;
    const std::size_t p = text_.rfind('\n', offset - 1);
    return p == npos ? 0 : p + 1;
}

auto Document::NextLine(std::size_t offset) const -> std::size_t {
    const std::size_t p = text_.find('\n', offset);
    return p == npos ? text_.size() : p + 1;
}

auto Document::PrevLine(std::size_t lineStart) const -> std::size_t {
    if (lineStart == 0) return npos;
    return LineStart(lineStart - 1);
}

auto Document::IsBlankLine(std::size_t lineStart) const -> bool {
    if (lineStart >= text_.size()) return false;
    for (std::size_t i = lineStart; i < text_.size() && text_[i] != '\n'; ++i) {
        if (text_[i] != ' ' && text_[i] != '\t' && text_[i] != '\r') return false;
    }
    return true;
}

auto Document::IndentAt(std::size_t lineStart) const -> std::string {
    std::size_t i = lineStart == 0 ? bom_ : lineStart;
    const std::size_t from = i;
    while (i < text_.size() && (text_[i] == ' ' || text_[i] == '\t')) ++i;
    return text_.substr(from, i - from);
}

auto Document::BeginsLine(std::size_t offset) const -> bool {
    std::size_t i = offset;
    while (i > 0 && (text_[i - 1] == ' ' || text_[i - 1] == '\t')) --i;
    return i == 0 || text_[i - 1] == '\n' || (bom_ > 0 && i == bom_);
}

auto Document::RestOfLineIsTrivia(int token) const -> bool {
    for (std::size_t j = static_cast<std::size_t>(token) + 1; j < syntax_.tokens.size(); ++j) {
        switch (syntax_.tokens[j].kind) {
        case Tok::Space:
        case Tok::LineComment:
        case Tok::BlockComment: continue;
        case Tok::Newline: return true;
        default: return false;
        }
    }
    return true;
}

auto Document::WholeLineComment(int token) const -> bool {
    return Tk(token).kind == Tok::LineComment && BeginsLine(Tk(token).begin);
}

auto Document::CommentText(int token) const -> std::string {
    std::string_view s = TokText(token);
    s.remove_prefix(2);
    if (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

auto Document::CommentLines(std::string_view indent, std::string_view text) const -> std::string {
    std::string out;
    if (text.empty()) return out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out += indent;
        out += "//";
        if (!line.empty()) {
            out += ' ';
            out += line;
        }
        out += nl_;
        start = end + 1;
    }
    return out;
}

auto Document::ItemOwnsLines(int container, int item) const -> bool {
    return BeginsLine(Tk(ItemFirst(container, item)).begin) && RestOfLineIsTrivia(ItemLast(container, item));
}

auto Document::ItemIndent(int container) const -> std::string {
    const Node& n = Nd(container);
    for (int i = 0; i < static_cast<int>(n.items.size()); ++i) {
        if (ItemOwnsLines(container, i)) return IndentAt(LineStart(Tk(ItemFirst(container, i)).begin));
    }
    for (int t = n.first + 1; t < n.last; ++t) {
        if (WholeLineComment(t)) return IndentAt(LineStart(Tk(t).begin));
    }
    const int edge = BeginsLine(Tk(n.last).begin) ? n.last : n.first;
    return IndentAt(LineStart(Tk(edge).begin)) + "  ";
}

auto Document::LeadingCommentBegin(int container, int item) const -> std::size_t {
    const int first = ItemFirst(container, item);
    const int floor = item == 0 ? Nd(container).first : ItemLast(container, item - 1);
    std::size_t begin = LineStart(Tk(first).begin);
    std::uint32_t line = Tk(first).line;
    for (int j = first - 1; j > floor; --j) {
        const Tok kind = Tk(j).kind;
        if (kind == Tok::Space || kind == Tok::Newline) continue;
        if (WholeLineComment(j) && Tk(j).line + 1 == line) {
            begin = LineStart(Tk(j).begin);
            line = Tk(j).line;
            continue;
        }
        break;
    }
    return begin;
}

void Document::BuildModel() {
    warnings_.clear();
    header_.clear();
    settings_ = Settings{};
    styles_.clear();
    rules_.clear();
    ruleSpans_.clear();
    headings_.clear();
    headingSpans_.clear();
    activeCount_ = 0;
    rulesNode_ = disabledNode_ = rulesMember_ = disabledMember_ = -1;

    const int root = syntax_.root;
    for (int t = 0; t < Nd(root).first; ++t) {
        if (!WholeLineComment(t)) continue;
        if (!header_.empty()) header_ += '\n';
        header_ += CommentText(t);
    }
    if (Nd(root).type != Type::Object) {
        Warn(warnings_, Tk(Nd(root).first).line, "the filter is not an object { ... }");
        return;
    }

    const Node& r = Nd(root);
    for (int i = 0; i < static_cast<int>(r.items.size()); ++i) {
        const Item& it = r.items[static_cast<std::size_t>(i)];
        std::string key;
        DecodeString(TokText(it.key), key);
        const Type type = Nd(it.node).type;
        auto value = [&] { return ToValue(syntax_, text_, it.node); };
        auto integer = [&](std::optional<std::int64_t>& out) {
            std::int64_t n = 0;
            if (value().AsInteger(n)) out = n;
            else out.reset();
        };
        if (key == "formatVersion") integer(settings_.formatVersion);
        else if (key == "defaultLevel") integer(settings_.defaultLevel);
        else if (key == "soundVolume") integer(settings_.soundVolume);
        else if (key == "title") settings_.title = value().Str();
        else if (key == "author") settings_.author = value().Str();
        else if (key == "version") settings_.version = value().Str();
        else if (key == "description") settings_.description = value().Str();
        else if (key == "updateUrl") settings_.updateUrl = value().Str();
        else if (key == "hideUnmatchedItems") {
            if (type == Type::Bool) settings_.hideUnmatchedItems = value().boolean;
        } else if (key == "filterLevels") {
            settings_.filterLevels.clear();
            for (const Value& level : value().array) {
                FilterLevel fl;
                if (const Value* id = level.Find("id")) id->AsInteger(fl.id);
                if (const Value* name = level.Find("name")) fl.name = *name;
                settings_.filterLevels.push_back(std::move(fl));
            }
        } else if (key == "styles") {
            styles_.clear();
            if (type != Type::Object) continue;
            for (const Item& s : Nd(it.node).items) {
                Style style;
                DecodeString(TokText(s.key), style.name);
                style.value = ToValue(syntax_, text_, s.node);
                style.line = Tk(s.key).line;
                styles_.push_back(std::move(style));
            }
        } else if (key == "rules") {
            rulesMember_ = i;
            rulesNode_ = type == Type::Array ? it.node : -1;
            if (type != Type::Array) Warn(warnings_, Tk(it.key).line, "\"rules\" is not a list [ ... ]");
        } else if (key == "disabledRules") {
            disabledMember_ = i;
            disabledNode_ = type == Type::Array ? it.node : -1;
            if (type != Type::Array) Warn(warnings_, Tk(it.key).line, "\"disabledRules\" is not a list [ ... ]");
        }
    }
    if (rulesMember_ < 0) Warn(warnings_, Tk(r.first).line, "the filter has no \"rules\"");

    BuildRules();
    BuildDisabled();

    std::map<std::string, int> seen;
    for (const Rule& rule : rules_) {
        if (rule.id.empty()) continue;
        if (++seen[rule.id] == 2) Warn(warnings_, rule.line, rule.id + " is used by more than one rule");
    }
}

namespace {

// "// ==== Name" or "// ---- Name": level and the name's bytes in the token.
auto ParseHeading(std::string_view comment, Level& level, std::size_t& nameBegin, std::size_t& nameEnd) -> bool {
    std::size_t i = 2;
    while (i < comment.size() && (comment[i] == ' ' || comment[i] == '\t')) ++i;
    if (comment.substr(i, 4) == "====") level = Level::Part;
    else if (comment.substr(i, 4) == "----") level = Level::Section;
    else return false;
    const char mark = comment[i];
    while (i < comment.size() && comment[i] == mark) ++i;
    while (i < comment.size() && (comment[i] == ' ' || comment[i] == '\t')) ++i;
    std::size_t end = comment.size();
    while (end > i && (comment[end - 1] == ' ' || comment[end - 1] == '\t' || comment[end - 1] == '\r')) --end;
    // a closing "====" run is decoration, not name
    std::size_t k = end;
    while (k > i && comment[k - 1] == mark) --k;
    if (k < end && end - k >= 2) {
        while (k > i && (comment[k - 1] == ' ' || comment[k - 1] == '\t')) --k;
        end = k;
    }
    nameBegin = i;
    nameEnd = end;
    return true;
}

}  // namespace

void Document::BuildRules() {
    if (rulesNode_ < 0) return;
    const Node& arr = Nd(rulesNode_);
    const int n = static_cast<int>(arr.items.size());
    int part = -1;
    int section = -1;

    for (int g = 0; g <= n; ++g) {
        const int from = (g == 0 ? arr.first : ItemLast(rulesNode_, g - 1)) + 1;
        const int to = g < n ? ItemFirst(rulesNode_, g) : arr.last;
        const bool itemStartsLine = g < n && BeginsLine(Tk(to).begin);
        const std::uint32_t itemLine = g < n ? Tk(to).line : 0;

        // whole-line comments of this gap, in runs of consecutive lines
        std::vector<std::vector<int>> runs;
        for (int t = from; t < to; ++t) {
            if (!WholeLineComment(t)) continue;
            if (runs.empty() || Tk(runs.back().back()).line + 1 != Tk(t).line) runs.emplace_back();
            runs.back().push_back(t);
        }

        std::size_t commentBegin = npos;
        std::string lineComment;
        for (std::size_t ri = 0; ri < runs.size(); ++ri) {
            const std::vector<int>& run = runs[ri];
            const bool touches = ri + 1 == runs.size() && itemStartsLine && Tk(run.back()).line + 1 == itemLine;
            int heading = -1;
            bool descTaken = false;
            std::vector<int> pending;
            auto addDescription = [&](int h, int tok) {
                Heading& hd = headings_[static_cast<std::size_t>(h)];
                if (!hd.description.empty()) hd.description += '\n';
                hd.description += CommentText(tok);
                headingSpans_[static_cast<std::size_t>(h)].blockEnd = NextLine(Tk(tok).end);
            };
            auto flushLoose = [&] {
                if (heading >= 0) {
                    for (int tok : pending) addDescription(heading, tok);
                }
                pending.clear();
            };
            for (int tok : run) {
                Level level{};
                std::size_t nb = 0;
                std::size_t ne = 0;
                const std::string_view raw = TokText(tok);
                if (ParseHeading(raw, level, nb, ne)) {
                    flushLoose();
                    Heading hd;
                    hd.level = level;
                    hd.name = std::string(raw.substr(nb, ne - nb));
                    hd.line = Tk(tok).line;
                    hd.firstRule = g;
                    HeadingSpan hs;
                    hs.begin = LineStart(Tk(tok).begin);
                    hs.lineEnd = hs.blockEnd = NextLine(Tk(tok).end);
                    hs.nameBegin = Tk(tok).begin + nb;
                    hs.nameEnd = Tk(tok).begin + ne;
                    hs.gap = g;
                    heading = static_cast<int>(headings_.size());
                    if (level == Level::Part) {
                        part = heading;
                        section = -1;
                    } else {
                        hd.part = part;
                        section = heading;
                    }
                    headings_.push_back(std::move(hd));
                    headingSpans_.push_back(hs);
                    descTaken = false;
                } else if (heading >= 0 && !descTaken && pending.empty()) {
                    addDescription(heading, tok);
                    descTaken = true;
                } else {
                    pending.push_back(tok);
                }
            }
            if (touches && !pending.empty()) {
                commentBegin = LineStart(Tk(pending.front()).begin);
                for (int tok : pending) {
                    if (!lineComment.empty()) lineComment += '\n';
                    lineComment += CommentText(tok);
                }
                pending.clear();
            }
            flushLoose();
        }

        if (g == n) break;
        const Item& it = arr.items[static_cast<std::size_t>(g)];
        Rule rule;
        rule.index = g;
        rule.part = part;
        rule.section = section;
        rule.lineComment = std::move(lineComment);
        rule.value = ToValue(syntax_, text_, it.node);
        rule.line = Tk(Nd(it.node).first).line;
        if (rule.value.IsObject()) {
            if (const Value* title = rule.value.Find("comment")) rule.title = title->Str();
            rule.id = RuleId(rule.title);
        } else {
            Warn(warnings_, rule.line, "rule " + std::to_string(g + 1) + " is not an object { ... }");
        }
        RuleSpan span;
        span.node = it.node;
        span.container = rulesNode_;
        span.item = g;
        span.gap = g;
        span.ownsLines = ItemOwnsLines(rulesNode_, g);
        if (span.ownsLines) {
            span.ownBegin = LineStart(Tk(to).begin);
            span.ownEnd = NextLine(Tk(ItemLast(rulesNode_, g)).end);
        } else {
            span.ownBegin = Tk(to).begin;
            span.ownEnd = Tk(ItemLast(rulesNode_, g)).end;
        }
        span.blockBegin = commentBegin != npos ? commentBegin : span.ownBegin;
        rules_.push_back(std::move(rule));
        ruleSpans_.push_back(span);
    }
    activeCount_ = n;

    // where each heading's rules end
    const int count = static_cast<int>(headings_.size());
    for (int h = 0; h < count; ++h) {
        Heading& hd = headings_[static_cast<std::size_t>(h)];
        hd.endRule = n;
        for (int k = h + 1; k < count; ++k) {
            if (hd.level == Level::Section || headings_[static_cast<std::size_t>(k)].level == Level::Part) {
                hd.endRule = headings_[static_cast<std::size_t>(k)].firstRule;
                break;
            }
        }
    }
}

void Document::BuildDisabled() {
    if (disabledNode_ < 0) return;
    const Node& arr = Nd(disabledNode_);
    for (int j = 0; j < static_cast<int>(arr.items.size()); ++j) {
        const int entry = arr.items[static_cast<std::size_t>(j)].node;
        const std::uint32_t line = Tk(Nd(entry).first).line;
        int ruleNode = -1;
        std::int64_t at = -1;
        if (Nd(entry).type == Type::Object) {
            for (const Item& m : Nd(entry).items) {
                const std::string_view key = TokText(m.key);
                if (key == "\"rule\"" && Nd(m.node).type == Type::Object) ruleNode = m.node;
                if (key == "\"at\"") {
                    if (!ToValue(syntax_, text_, m.node).AsInteger(at)) at = -1;
                }
            }
        }
        if (ruleNode < 0 || at < 0) {
            Warn(warnings_, line, "a disabledRules entry is not {\"at\": <number>, \"rule\": {...}}");
            continue;
        }
        RuleSpan span;
        span.node = ruleNode;
        span.container = disabledNode_;
        span.item = j;
        span.entry = entry;
        span.ownsLines = ItemOwnsLines(disabledNode_, j);
        const int first = ItemFirst(disabledNode_, j);
        span.ownBegin = span.ownsLines ? LineStart(Tk(first).begin) : Tk(first).begin;
        span.ownEnd = span.ownsLines ? NextLine(Tk(ItemLast(disabledNode_, j)).end) : Tk(ItemLast(disabledNode_, j)).end;
        span.blockBegin = span.ownsLines ? LeadingCommentBegin(disabledNode_, j) : span.ownBegin;

        Rule rule;
        rule.index = j;
        rule.disabled = true;
        rule.at = static_cast<int>(at);
        rule.value = ToValue(syntax_, text_, ruleNode);
        rule.line = Tk(Nd(ruleNode).first).line;
        if (const Value* title = rule.value.Find("comment")) rule.title = title->Str();
        rule.id = RuleId(rule.title);
        // comment lines above: an optional "from:" line, then the line comment
        bool firstLine = true;
        const int floor = j == 0 ? arr.first : ItemLast(disabledNode_, j - 1);
        for (int t = floor + 1; t < first; ++t) {
            if (Tk(t).begin < span.blockBegin || !WholeLineComment(t)) continue;
            std::string text = CommentText(t);
            if (firstLine && text.rfind("from:", 0) == 0) {
                span.hasMarker = true;
                span.marker = std::string(Trim(std::string_view(text).substr(5)));
            } else {
                if (!rule.lineComment.empty()) rule.lineComment += '\n';
                rule.lineComment += text;
            }
            firstLine = false;
        }
        rules_.push_back(std::move(rule));
        ruleSpans_.push_back(span);
    }
    for (std::size_t i = static_cast<std::size_t>(activeCount_); i < rules_.size(); ++i) {
        const RuleSpan& span = ruleSpans_[i];
        const Slot slot = EnableSlot(rules_[i].at, span.hasMarker ? &span.marker : nullptr);
        rules_[i].part = slot.part;
        rules_[i].section = slot.section;
    }
}

auto Document::FindRule(std::string_view id) const -> int {
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        if (rules_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

auto Document::FindHeading(std::string_view part, std::string_view section) const -> int {
    for (std::size_t p = 0; p < headings_.size(); ++p) {
        if (headings_[p].level != Level::Part || headings_[p].name != part) continue;
        if (section.empty()) return static_cast<int>(p);
        for (std::size_t s = p + 1; s < headings_.size(); ++s) {
            if (headings_[s].level == Level::Part) break;
            if (headings_[s].name == section) return static_cast<int>(s);
        }
    }
    return -1;
}

auto Document::TopLevel(std::string_view key) const -> std::optional<Value> {
    const Node& root = Nd(syntax_.root);
    if (root.type != Type::Object) return std::nullopt;
    for (auto it = root.items.rbegin(); it != root.items.rend(); ++it) {
        std::string k;
        DecodeString(TokText(it->key), k);
        if (k == key) return ToValue(syntax_, text_, it->node);
    }
    return std::nullopt;
}

auto Document::SectionLabel(int part, int section) const -> std::string {
    std::string label;
    if (part >= 0) label = headings_[static_cast<std::size_t>(part)].name;
    if (section >= 0) {
        if (!label.empty()) label += " > ";
        label += headings_[static_cast<std::size_t>(section)].name;
    }
    return label;
}

// ---- Applying edits

auto Document::Apply(std::vector<Splice> splices) -> Status {
    std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin < b.begin; });
    for (std::size_t i = 1; i < splices.size(); ++i) {
        if (splices[i].begin < splices[i - 1].end) return {"internal: overlapping edits"};
    }
    std::string text;
    text.reserve(text_.size() + 256);
    std::size_t at = 0;
    for (const Splice& s : splices) {
        text.append(text_, at, s.begin - at);
        text += s.text;
        at = s.end;
    }
    text.append(text_, at, npos);
    std::string old = text_;
    std::vector<Diag> diags;
    if (!Rebuild(std::move(text), &diags)) {
        Rebuild(std::move(old), nullptr);
        const Diag& d = diags.front();
        return {"the edit would break the file (line " + std::to_string(d.line) + ": " + d.message + ")"};
    }
    return {};
}

auto Document::RenderLike(const Value& value, int oldNode) const -> std::string {
    const Node& old = Nd(oldNode);
    const bool container = old.type == Type::Array || old.type == Type::Object;
    if (!container || value.type != old.type || old.items.empty() || Tk(old.first).line == Tk(old.last).line) {
        return Render(value);
    }
    std::vector<std::string> items;
    if (value.IsArray()) {
        for (const Value& v : value.array) items.push_back(Render(v));
    } else {
        for (const Member& m : value.object) items.push_back(EncodeString(m.key) + ": " + Render(m.value));
    }
    const char open = value.IsArray() ? '[' : '{';
    const char close = value.IsArray() ? ']' : '}';
    if (items.empty()) return std::string{open, close};

    const int n = static_cast<int>(old.items.size());
    const std::uint32_t firstLine = Tk(ItemFirst(oldNode, 0)).line;
    const bool firstOnOpen = firstLine == Tk(old.first).line;
    int firstCount = 0;
    for (int i = 0; i < n && Tk(ItemFirst(oldNode, i)).line == firstLine; ++i) ++firstCount;
    // the first row that starts its own line gives the indent and the count per row
    int contCount = firstCount;
    std::string indent = IndentAt(LineStart(Tk(old.first).begin)) + "  ";
    for (int i = 0; i < n; ++i) {
        const Token& t = Tk(ItemFirst(oldNode, i));
        if (!BeginsLine(t.begin)) continue;
        indent = IndentAt(LineStart(t.begin));
        contCount = 0;
        for (int k = i; k < n && Tk(ItemFirst(oldNode, k)).line == t.line; ++k) ++contCount;
        break;
    }
    if (!firstOnOpen) firstCount = contCount;
    firstCount = std::max(firstCount, 1);
    contCount = std::max(contCount, 1);

    std::string out(1, open);
    if (!firstOnOpen) out += nl_ + indent;
    std::size_t i = 0;
    int inRow = 0;
    int rowSize = firstCount;
    for (; i < items.size(); ++i) {
        if (inRow == rowSize) {
            out += "," + nl_ + indent;
            inRow = 0;
            rowSize = contCount;
        } else if (inRow > 0) {
            out += ", ";
        }
        out += items[i];
        ++inRow;
    }
    if (BeginsLine(Tk(old.last).begin)) out += nl_ + IndentAt(LineStart(Tk(old.last).begin));
    out += close;
    return out;
}

auto Document::InsertItemSplices(int container, int index, const std::string& item, std::string_view comment,
                                 std::vector<Splice>& out) const -> bool {
    const Node& node = Nd(container);
    const int n = static_cast<int>(node.items.size());
    bool lineMode = false;
    if (n == 0) lineMode = Tk(node.first).line != Tk(node.last).line && BeginsLine(Tk(node.last).begin);
    else lineMode = ItemOwnsLines(container, index < n ? index : n - 1);

    if (lineMode) {
        const std::string indent = ItemIndent(container);
        std::string text = CommentLines(indent, comment) + indent + item;
        if (n == 0) {
            out.push_back({LineStart(Tk(node.last).begin), LineStart(Tk(node.last).begin), text + nl_});
        } else if (index < n) {
            const std::size_t at = LeadingCommentBegin(container, index);
            out.push_back({at, at, text + "," + nl_});
        } else {
            const Item& last = node.items.back();
            const bool hasComma = last.comma >= 0;
            if (!hasComma) out.push_back({NodeEnd(last.node), NodeEnd(last.node), ","});
            const std::size_t at = NextLine(Tk(ItemLast(container, n - 1)).end);
            out.push_back({at, at, text + (hasComma ? "," : "") + nl_});
        }
        return true;
    }
    if (n == 0) {
        out.push_back({Tk(node.first).end, Tk(node.first).end, item});
    } else if (index < n) {
        const std::size_t at = Tk(ItemFirst(container, index)).begin;
        out.push_back({at, at, item + ", "});
    } else {
        const std::size_t at = NodeEnd(node.items.back().node);
        out.push_back({at, at, ", " + item});
    }
    return true;
}

auto Document::RemoveItemSplices(int container, int item, std::size_t blockBegin, std::vector<Splice>& out) const
    -> void {
    const Node& node = Nd(container);
    const int n = static_cast<int>(node.items.size());
    const Item& it = node.items[static_cast<std::size_t>(item)];
    if (ItemOwnsLines(container, item)) {
        const std::size_t b = blockBegin != npos ? blockBegin : LeadingCommentBegin(container, item);
        std::size_t e = NextLine(Tk(ItemLast(container, item)).end);
        const std::size_t prev = PrevLine(b);
        if (prev != npos && IsBlankLine(prev) && e < text_.size() && IsBlankLine(e)) e = NextLine(e);
        out.push_back({b, e, ""});
        if (item == n - 1 && item > 0 && it.comma < 0) {
            const int comma = node.items[static_cast<std::size_t>(item - 1)].comma;
            if (comma >= 0) out.push_back({Tk(comma).begin, Tk(comma).end, ""});
        }
        return;
    }
    if (item < n - 1) {
        out.push_back({Tk(ItemFirst(container, item)).begin, Tk(ItemFirst(container, item + 1)).begin, ""});
    } else if (item > 0) {
        out.push_back({NodeEnd(node.items[static_cast<std::size_t>(item - 1)].node), Tk(ItemLast(container, item)).end, ""});
    } else {
        out.push_back({Tk(ItemFirst(container, item)).begin, Tk(ItemLast(container, item)).end, ""});
    }
}

namespace {

auto FindMember(const Syntax& syntax, std::string_view text, int object, std::string_view key) -> int {
    const Node& n = syntax.nodes[static_cast<std::size_t>(object)];
    int found = -1;
    for (std::size_t i = 0; i < n.items.size(); ++i) {
        const Token& t = syntax.tokens[static_cast<std::size_t>(n.items[i].key)];
        std::string k;
        DecodeString(text.substr(t.begin, t.end - t.begin), k);
        if (k == key) found = static_cast<int>(i);
    }
    return found;
}

}  // namespace

auto Document::SetMember(int object, std::string_view key, const Value& value, std::string_view beforeKey) -> Status {
    if (Nd(object).type != Type::Object) return {"not an object"};
    std::vector<Splice> splices;
    const int found = FindMember(syntax_, text_, object, key);
    if (found >= 0) {
        const int valueNode = Nd(object).items[static_cast<std::size_t>(found)].node;
        splices.push_back({NodeBegin(valueNode), NodeEnd(valueNode), RenderLike(value, valueNode)});
    } else {
        int index = static_cast<int>(Nd(object).items.size());
        if (!beforeKey.empty()) {
            // the first member with that key
            const Node& n = Nd(object);
            for (std::size_t i = 0; i < n.items.size(); ++i) {
                std::string k;
                DecodeString(TokText(n.items[i].key), k);
                if (k == beforeKey) {
                    index = static_cast<int>(i);
                    break;
                }
            }
        }
        InsertItemSplices(object, index, EncodeString(key) + ": " + Render(value), {}, splices);
    }
    return Apply(std::move(splices));
}

auto Document::RemoveMember(int object, std::string_view key) -> Status {
    const int found = FindMember(syntax_, text_, object, key);
    if (found < 0) return {"no field \"" + std::string(key) + "\""};
    std::vector<Splice> splices;
    RemoveItemSplices(object, found, npos, splices);
    return Apply(std::move(splices));
}

// ---- Rule edits

namespace {

auto BadRule(int rule, int count) -> bool { return rule < 0 || rule >= count; }

}  // namespace

auto Document::SetField(int rule, std::string_view key, const Value& value, std::string_view beforeKey) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size()))) return {"no such rule"};
    return SetMember(ruleSpans_[static_cast<std::size_t>(rule)].node, key, value, beforeKey);
}

auto Document::RemoveField(int rule, std::string_view key) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size()))) return {"no such rule"};
    Status status = RemoveMember(ruleSpans_[static_cast<std::size_t>(rule)].node, key);
    if (!status) return status;
    // a duplicate key would show its earlier value: remove those too
    while (FindMember(syntax_, text_, ruleSpans_[static_cast<std::size_t>(rule)].node, key) >= 0) {
        status = RemoveMember(ruleSpans_[static_cast<std::size_t>(rule)].node, key);
        if (!status) return status;
    }
    return {};
}

auto Document::SetTitle(int rule, std::string_view title) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size()))) return {"no such rule"};
    const int node = ruleSpans_[static_cast<std::size_t>(rule)].node;
    std::string first;
    if (!Nd(node).items.empty()) DecodeString(TokText(Nd(node).items[0].key), first);
    return SetMember(node, "comment", Value::String(std::string(title)), first);
}

auto Document::SetLineComment(int rule, std::string_view text) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size()))) return {"no such rule"};
    const RuleSpan& span = ruleSpans_[static_cast<std::size_t>(rule)];
    if (!span.ownsLines) return {"the rule shares its line with other text"};
    std::size_t begin = span.blockBegin;
    if (span.hasMarker) begin = NextLine(begin);
    std::string lines = CommentLines(IndentAt(span.ownBegin), text);
    if (!lines.empty() && begin == span.ownBegin) {
        // right under a heading without a description, it would become one
        for (const HeadingSpan& hs : headingSpans_) {
            if (hs.blockEnd == begin && hs.lineEnd == hs.blockEnd) lines = nl_ + lines;
        }
    }
    if (lines.empty() && begin == span.ownBegin) return {};
    return Apply({{begin, span.ownBegin, lines}});
}

// ---- Places in "rules"

auto Document::HeadingSlot(int heading) const -> Slot {
    const Heading& hd = headings_[static_cast<std::size_t>(heading)];
    Slot slot;
    slot.index = headingSpans_[static_cast<std::size_t>(heading)].gap;
    slot.offset = headingSpans_[static_cast<std::size_t>(heading)].blockEnd;
    slot.blankAround = true;
    slot.part = hd.level == Level::Part ? heading : hd.part;
    slot.section = hd.level == Level::Part ? -1 : heading;
    return slot;
}

auto Document::ResolveSlot(Position where, Slot& slot, std::string& error) const -> bool {
    if (rulesNode_ < 0) {
        error = "the filter has no \"rules\" list";
        return false;
    }
    const Node& arr = Nd(rulesNode_);
    const int n = activeCount_;
    const int headingCount = static_cast<int>(headings_.size());
    using K = Position::Kind;
    if ((where.kind == K::Before || where.kind == K::After) && BadRule(where.index, n)) {
        error = "no such active rule";
        return false;
    }
    if ((where.kind == K::StartOf || where.kind == K::EndOf) && BadRule(where.index, headingCount)) {
        error = "no such heading";
        return false;
    }
    const bool lineMode = Tk(arr.first).line != Tk(arr.last).line;
    auto ruleSlot = [&](int k, bool after) {
        Slot s;
        const RuleSpan& span = ruleSpans_[static_cast<std::size_t>(k)];
        s.index = after ? k + 1 : k;
        s.part = rules_[static_cast<std::size_t>(k)].part;
        s.section = rules_[static_cast<std::size_t>(k)].section;
        if (lineMode && span.ownsLines) {
            s.offset = after ? span.ownEnd : span.blockBegin;
        } else {
            s.lineMode = false;
        }
        return s;
    };
    // rules directly under heading h (before the next heading of any level)
    auto directEnd = [&](int h) {
        return h + 1 < headingCount ? headingSpans_[static_cast<std::size_t>(h + 1)].gap : n;
    };

    switch (where.kind) {
    case K::Before: slot = ruleSlot(where.index, false); return true;
    case K::After: slot = ruleSlot(where.index, true); return true;
    case K::StartOf: {
        const int h = where.index;
        if (directEnd(h) > headings_[static_cast<std::size_t>(h)].firstRule) {
            slot = ruleSlot(headings_[static_cast<std::size_t>(h)].firstRule, false);
        } else {
            slot = HeadingSlot(h);
        }
        return true;
    }
    case K::EndOf: {
        int h = where.index;
        if (headings_[static_cast<std::size_t>(h)].level == Level::Part) {
            for (int s = h + 1; s < headingCount && headings_[static_cast<std::size_t>(s)].level == Level::Section; ++s) {
                h = s;
            }
        }
        if (directEnd(h) > headings_[static_cast<std::size_t>(h)].firstRule) {
            slot = ruleSlot(directEnd(h) - 1, true);
        } else {
            slot = HeadingSlot(h);
        }
        return true;
    }
    case K::Begin:
        if (n > 0 && (headingCount == 0 || headingSpans_[0].gap > 0)) {
            slot = ruleSlot(0, false);
        } else {
            slot = Slot{};
            slot.index = 0;
            slot.lineMode = lineMode;
            slot.offset = NextLine(Tk(arr.first).end);
            slot.blankAround = true;
        }
        return true;
    case K::End:
        if (n > 0 && (headingCount == 0 || headingSpans_.back().gap < n)) {
            slot = ruleSlot(n - 1, true);
        } else {
            slot = Slot{};
            slot.index = n;
            slot.lineMode = lineMode && BeginsLine(Tk(arr.last).begin);
            slot.offset = LineStart(Tk(arr.last).begin);
            slot.blankAround = true;
            if (headingCount > 0) {
                const Slot last = HeadingSlot(headingCount - 1);
                slot.part = last.part;
                slot.section = last.section;
            }
        }
        return true;
    }
    return false;
}

auto Document::EnableSlot(int ruleAt, const std::string* marker) const -> Slot {
    const int n = activeCount_;
    const int at = std::clamp(ruleAt, 0, n);
    std::string error;
    std::vector<Slot> candidates;
    Slot s;
    // between two sections: the end of the one above is a candidate too
    bool headingHere = false;
    for (const HeadingSpan& hs : headingSpans_) headingHere = headingHere || hs.gap == at;
    if (at > 0 && (headingHere || at == n) && ResolveSlot(Position::After(at - 1), s, error)) candidates.push_back(s);
    for (int h = 0; h < static_cast<int>(headings_.size()); ++h) {
        if (headingSpans_[static_cast<std::size_t>(h)].gap != at) continue;
        const bool lastHere = h + 1 == static_cast<int>(headings_.size()) ||
                              headingSpans_[static_cast<std::size_t>(h + 1)].gap != at;
        if (lastHere && at < n && ResolveSlot(Position::Before(at), s, error)) candidates.push_back(s);
        else candidates.push_back(HeadingSlot(h));
    }
    if (candidates.empty()) {
        if (ResolveSlot(at < n ? Position::Before(at) : Position::End(), s, error)) candidates.push_back(s);
        else return Slot{};
    }
    if (marker) {
        for (const Slot& c : candidates) {
            if (SectionLabel(c.part, c.section) == *marker) return c;
        }
    }
    return candidates.back();
}

auto Document::SlotSplices(const Slot& slot, const std::string& item, std::string_view comment,
                           std::vector<Splice>& out) const -> void {
    if (!slot.lineMode) {
        InsertItemSplices(rulesNode_, slot.index, item, {}, out);
        return;
    }
    const Node& arr = Nd(rulesNode_);
    const int n = static_cast<int>(arr.items.size());
    const std::string indent = ItemIndent(rulesNode_);
    const bool lastHasComma = n > 0 && arr.items.back().comma >= 0;
    const bool comma = slot.index < n || lastHasComma;
    std::string lines = CommentLines(indent, comment) + indent + item + (comma ? "," : "") + nl_;
    if (slot.index == n && n > 0 && !lastHasComma) {
        const std::size_t end = NodeEnd(arr.items.back().node);
        out.push_back({end, end, ","});
    }
    const std::size_t openLine = LineStart(Tk(arr.first).begin);
    const std::size_t closeLine = LineStart(Tk(arr.last).begin);
    const std::size_t prev = PrevLine(slot.offset);
    if (slot.blankAround) {
        if (prev != npos && prev != openLine && !IsBlankLine(prev)) lines = nl_ + lines;
        if (slot.offset < text_.size() && slot.offset != closeLine && !IsBlankLine(slot.offset)) lines += nl_;
    } else if (!comment.empty()) {
        for (const HeadingSpan& hs : headingSpans_) {
            if (hs.blockEnd == slot.offset && hs.lineEnd == hs.blockEnd) lines = nl_ + lines;
        }
    }
    out.push_back({slot.offset, slot.offset, lines});
}

auto Document::ExpandEmptyRules() -> Status {
    if (rulesNode_ < 0) return {"the filter has no \"rules\" list"};
    const Node& arr = Nd(rulesNode_);
    if (Tk(arr.first).line != Tk(arr.last).line) return {};
    if (!arr.items.empty()) return {};  // rules on one line: edits go inline
    const Item& member = Nd(syntax_.root).items[static_cast<std::size_t>(rulesMember_)];
    const std::string indent = IndentAt(LineStart(Tk(member.key).begin));
    return Apply({{Tk(arr.first).begin, Tk(arr.last).end, "[" + nl_ + indent + "]"}});
}

auto Document::RuleBlockSplices(int rule, std::vector<Splice>& out) const -> void {
    const RuleSpan& span = ruleSpans_[static_cast<std::size_t>(rule)];
    if (!rules_[static_cast<std::size_t>(rule)].disabled) {
        RemoveItemSplices(rulesNode_, span.item, span.blockBegin, out);
        return;
    }
    if (Nd(disabledNode_).items.size() == 1) {
        RemoveItemSplices(syntax_.root, disabledMember_, npos, out);
    } else {
        RemoveItemSplices(disabledNode_, span.item, span.blockBegin, out);
    }
}

auto Document::InsertRule(Position where, const Value& rule, std::string_view lineComment) -> Status {
    if (!rule.IsObject()) return {"a rule must be an object { ... }"};
    const std::string old = text_;
    if (Status s = ExpandEmptyRules(); !s) return s;
    Slot slot;
    std::string error;
    if (!ResolveSlot(where, slot, error)) {
        Rebuild(old, nullptr);
        return {error};
    }
    std::vector<Splice> splices;
    SlotSplices(slot, Render(rule), lineComment, splices);
    Status s = Apply(std::move(splices));
    if (!s) Rebuild(old, nullptr);
    return s;
}

auto Document::DeleteRule(int rule) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size()))) return {"no such rule"};
    std::vector<Splice> splices;
    RuleBlockSplices(rule, splices);
    return Apply(std::move(splices));
}

auto Document::MoveRule(int rule, Position where) -> Status {
    if (BadRule(rule, activeCount_)) return {"no such active rule"};
    using K = Position::Kind;
    if ((where.kind == K::Before || where.kind == K::After)) {
        if (BadRule(where.index, activeCount_)) return {"no such active rule"};
        if (where.index == rule) return {};
    }
    const std::string old = text_;
    const std::string ruleText(NodeText(ruleSpans_[static_cast<std::size_t>(rule)].node));
    const std::string comment = rules_[static_cast<std::size_t>(rule)].lineComment;
    if (Status s = DeleteRule(rule); !s) return s;
    if ((where.kind == K::Before || where.kind == K::After) && where.index > rule) --where.index;
    Slot slot;
    std::string error;
    Status s;
    if (Status e = ExpandEmptyRules(); !e) {
        s = e;
    } else if (!ResolveSlot(where, slot, error)) {
        s = {error};
    } else {
        std::vector<Splice> splices;
        SlotSplices(slot, ruleText, comment, splices);
        s = Apply(std::move(splices));
    }
    if (!s) Rebuild(old, nullptr);
    return s;
}

auto Document::DisableRule(int rule) -> Status {
    if (BadRule(rule, activeCount_)) return {"no such active rule"};
    const Rule& r = rules_[static_cast<std::size_t>(rule)];
    const std::string old = text_;
    const std::string ruleText(NodeText(ruleSpans_[static_cast<std::size_t>(rule)].node));
    const std::string label = SectionLabel(r.part, r.section);
    std::string comment = label.empty() ? std::string() : "from: " + label;
    if (!r.lineComment.empty()) comment += (comment.empty() ? "" : "\n") + r.lineComment;
    const std::string indent = ItemIndent(rulesNode_);
    const std::string entry = "{\"at\": " + std::to_string(rule) + ", \"rule\": " + ruleText + "}";
    if (disabledMember_ >= 0 && disabledNode_ < 0) return {"\"disabledRules\" is not a list"};

    if (Status s = DeleteRule(rule); !s) return s;
    std::vector<Splice> splices;
    if (disabledNode_ < 0) {
        const int root = syntax_.root;
        const std::string memberIndent = ItemIndent(root);
        const std::string member = "\"disabledRules\": [" + nl_ + CommentLines(indent, comment) + indent + entry +
                                   nl_ + memberIndent + "]";
        InsertItemSplices(root, static_cast<int>(Nd(root).items.size()), member, {}, splices);
    } else {
        InsertItemSplices(disabledNode_, static_cast<int>(Nd(disabledNode_).items.size()), entry, comment, splices);
    }
    Status s = Apply(std::move(splices));
    if (!s) Rebuild(old, nullptr);
    return s;
}

auto Document::EnableRule(int rule) -> Status {
    if (BadRule(rule, static_cast<int>(rules_.size())) || !rules_[static_cast<std::size_t>(rule)].disabled) {
        return {"no such disabled rule"};
    }
    const std::string old = text_;
    const RuleSpan span = ruleSpans_[static_cast<std::size_t>(rule)];
    const Rule r = rules_[static_cast<std::size_t>(rule)];
    const std::string ruleText(NodeText(span.node));
    if (Status s = DeleteRule(rule); !s) return s;
    Status s = ExpandEmptyRules();
    if (s) {
        // where it goes, from its "at" and "from:" line, in the text as it is now
        const Slot slot = EnableSlot(r.at, span.hasMarker ? &span.marker : nullptr);
        std::vector<Splice> splices;
        SlotSplices(slot, ruleText, r.lineComment, splices);
        s = Apply(std::move(splices));
    }
    if (!s) Rebuild(old, nullptr);
    return s;
}

// ---- Headings

auto Document::AddHeading(Level level, Position where, std::string_view name, std::string_view description)
    -> Status {
    const std::string old = text_;
    if (Status s = ExpandEmptyRules(); !s) return s;
    Slot slot;
    std::string error;
    Status s;
    if (!ResolveSlot(where, slot, error)) {
        s = {error};
    } else if (!slot.lineMode) {
        s = {"the rules list is written on one line"};
    } else {
        const Node& arr = Nd(rulesNode_);
        const std::string indent = ItemIndent(rulesNode_);
        std::string lines = indent + (level == Level::Part ? "// ==== " : "// ---- ") + OneLine(name) + nl_ +
                            CommentLines(indent, description);
        const std::size_t prev = PrevLine(slot.offset);
        if (prev != npos && prev != LineStart(Tk(arr.first).begin) && !IsBlankLine(prev)) lines = nl_ + lines;
        if (slot.offset < text_.size() && slot.offset != LineStart(Tk(arr.last).begin) && !IsBlankLine(slot.offset)) {
            lines += nl_;
        }
        s = Apply({{slot.offset, slot.offset, lines}});
    }
    if (!s) Rebuild(old, nullptr);
    return s;
}

auto Document::RenameHeading(int heading, std::string_view name) -> Status {
    if (BadRule(heading, static_cast<int>(headings_.size()))) return {"no such heading"};
    const HeadingSpan& hs = headingSpans_[static_cast<std::size_t>(heading)];
    std::string text = OneLine(name);
    if (hs.nameBegin == hs.nameEnd && hs.nameBegin > 0 && text_[hs.nameBegin - 1] != ' ') text = " " + text;
    return Apply({{hs.nameBegin, hs.nameEnd, text}});
}

auto Document::SetHeadingDescription(int heading, std::string_view text) -> Status {
    if (BadRule(heading, static_cast<int>(headings_.size()))) return {"no such heading"};
    const HeadingSpan& hs = headingSpans_[static_cast<std::size_t>(heading)];
    std::string lines = CommentLines(IndentAt(hs.begin), text);
    // a comment right below would become the description: keep a blank line
    if (lines.empty() && hs.blockEnd < text_.size() && !IsBlankLine(hs.blockEnd)) {
        const std::string_view rest = Trim(std::string_view(text_).substr(hs.blockEnd, NextLine(hs.blockEnd) - hs.blockEnd));
        if (rest.substr(0, 2) == "//") lines = nl_;
    }
    if (lines.empty() && hs.lineEnd == hs.blockEnd) return {};
    return Apply({{hs.lineEnd, hs.blockEnd, lines}});
}

auto Document::RemoveHeading(int heading) -> Status {
    if (BadRule(heading, static_cast<int>(headings_.size()))) return {"no such heading"};
    const HeadingSpan& hs = headingSpans_[static_cast<std::size_t>(heading)];
    std::size_t begin = hs.begin;
    std::size_t end = hs.blockEnd;
    const std::size_t openLine = LineStart(Tk(Nd(rulesNode_).first).begin);
    const std::size_t closeLine = LineStart(Tk(Nd(rulesNode_).last).begin);
    const std::size_t prev = PrevLine(begin);
    const bool prevBlank = prev == npos || IsBlankLine(prev) || prev == openLine;
    if (prevBlank && end < text_.size() && IsBlankLine(end)) end = NextLine(end);
    // blank lines separate headings, not rules: between two rules, drop the blank above too
    auto isRuleLine = [&](std::size_t line) {
        if (line == npos || line >= text_.size() || line == openLine || line == closeLine || IsBlankLine(line)) return false;
        return Trim(std::string_view(text_).substr(line, NextLine(line) - line)).substr(0, 2) != "//";
    };
    if (prev != npos && prev != openLine && IsBlankLine(prev) && isRuleLine(PrevLine(prev)) && isRuleLine(end)) begin = prev;
    return Apply({{begin, end, ""}});
}

auto Document::SetSetting(std::string_view key, const Value& value, std::string_view beforeKey) -> Status {
    const int root = syntax_.root;
    if (Nd(root).type != Type::Object) return {"the filter is not an object"};
    std::string before(beforeKey);
    if (before.empty()) {
        if (FindMember(syntax_, text_, root, "styles") >= 0) before = "styles";
        else if (FindMember(syntax_, text_, root, "rules") >= 0) before = "rules";
    }
    return SetMember(root, key, value, before);
}

auto Document::RemoveSetting(std::string_view key) -> Status {
    if (Nd(syntax_.root).type != Type::Object) return {"the filter is not an object"};
    while (FindMember(syntax_, text_, syntax_.root, key) >= 0) {
        Status status = RemoveMember(syntax_.root, key);
        if (!status) return status;
    }
    return {};
}

}  // namespace document
