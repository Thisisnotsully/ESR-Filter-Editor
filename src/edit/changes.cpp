#include "changes.h"

#include <algorithm>
#include <cstdio>

#include "syntax.h"

namespace esrfe::edit {

using document::Member;
using document::Type;

// ---- the model ----

auto RuleChange::Field(std::string_view key) -> FieldEdit* {
    for (FieldEdit& f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

auto RuleChange::Field(std::string_view key) const -> const FieldEdit* {
    for (const FieldEdit& f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

auto RuleChange::RemovedFrom(std::string_view key) const -> const std::vector<Value>* {
    for (const Removed& r : removedItems)
        if (r.key == key) return &r.values;
    return nullptr;
}

auto ChangeSet::Find(std::string_view id) const -> const RuleChange* {
    for (const RuleChange& r : rules)
        if (r.id == id) return &r;
    return nullptr;
}

auto ChangeSet::Find(std::string_view id) -> RuleChange* {
    for (RuleChange& r : rules)
        if (r.id == id) return &r;
    return nullptr;
}

auto ChangeSet::Get(std::string_view id) -> RuleChange& {
    if (RuleChange* r = Find(id)) return *r;
    rules.push_back(RuleChange{});
    rules.back().id = std::string(id);
    return rules.back();
}

void ChangeSet::Drop(std::string_view id) {
    rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const RuleChange& r) { return r.id == id; }), rules.end());
}

void ChangeSet::DropEmpty() {
    rules.erase(std::remove_if(rules.begin(), rules.end(), [](const RuleChange& r) { return r.Empty(); }), rules.end());
}

auto ChangeSet::NextOwnId() const -> std::string {
    int highest = 0;
    for (const OwnRule& r : own) {
        if (r.id.size() > 1 && r.id[0] == 'U') highest = std::max(highest, std::atoi(r.id.c_str() + 1));
    }
    char text[16];
    std::snprintf(text, sizeof text, "U%03d", highest + 1);
    return text;
}

auto Hash(std::string_view text) -> std::uint64_t {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// ---- the file ----

namespace {

auto Str(const Value* v) -> std::string { return v != nullptr && v->IsString() ? v->Str() : std::string{}; }

auto HexHash(std::uint64_t h) -> std::string {
    char text[24];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
    return text;
}

auto ParseHex(const std::string& text) -> std::uint64_t {
    std::uint64_t h = 0;
    for (char c : text) {
        h <<= 4;
        if (c >= '0' && c <= '9') h |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') h |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') h |= static_cast<std::uint64_t>(c - 'A' + 10);
        else return 0;
    }
    return h;
}

auto ReadJson(std::string_view text, Value& out, std::string& error) -> bool {
    document::Syntax syntax;
    document::Diag diag;
    if (!document::ParseSyntax(text, syntax, diag)) {
        error = "line " + std::to_string(diag.line) + ": " + diag.message;
        return false;
    }
    out = document::ToValue(syntax, text, syntax.root);
    return true;
}

}  // namespace

auto Parse(std::string_view text, ChangeSet& out, std::string& error) -> bool {
    out = ChangeSet{};
    Value root;
    if (!ReadJson(text, root, error)) return false;
    if (!root.IsObject()) {
        error = "not a JSON object";
        return false;
    }
    if (const Value* v = root.Find("formatVersion")) v->AsInteger(out.formatVersion);
    out.base = Str(root.Find("base"));
    out.merged = Str(root.Find("merged"));
    out.baseVersion = Str(root.Find("baseVersion"));
    out.baseHash = ParseHex(Str(root.Find("baseHash")));
    if (const Value* rules = root.Find("rules"); rules != nullptr && rules->IsArray()) {
        for (const Value& r : rules->array) {
            if (!r.IsObject()) continue;
            RuleChange change;
            change.id = Str(r.Find("id"));
            if (change.id.empty()) continue;
            if (const Value* off = r.Find("off")) change.off = off->IsBool() && off->boolean;
            if (const Value* fields = r.Find("fields"); fields != nullptr && fields->IsArray()) {
                for (const Value& f : fields->array) {
                    FieldEdit edit;
                    edit.key = Str(f.Find("key"));
                    if (edit.key.empty()) continue;
                    if (const Value* removed = f.Find("removed")) edit.removed = removed->IsBool() && removed->boolean;
                    if (const Value* value = f.Find("value")) edit.value = *value;
                    if (const Value* author = f.Find("author")) {
                        edit.authorHad = true;
                        edit.author = *author;
                    }
                    change.fields.push_back(std::move(edit));
                }
            }
            if (const Value* items = r.Find("removedItems"); items != nullptr && items->IsArray()) {
                for (const Value& i : items->array) {
                    RuleChange::Removed removed;
                    removed.key = Str(i.Find("key"));
                    if (const Value* values = i.Find("values"); values != nullptr && values->IsArray()) removed.values = values->array;
                    if (!removed.key.empty() && !removed.values.empty()) change.removedItems.push_back(std::move(removed));
                }
            }
            if (const Value* snapshot = r.Find("snapshot")) change.snapshot = *snapshot;
            out.rules.push_back(std::move(change));
        }
    }
    if (const Value* own = root.Find("own"); own != nullptr && own->IsArray()) {
        for (const Value& o : own->array) {
            OwnRule rule;
            rule.id = Str(o.Find("id"));
            rule.after = Str(o.Find("after"));
            rule.section = Str(o.Find("section"));
            if (const Value* r = o.Find("rule")) rule.rule = *r;
            if (!rule.id.empty() && rule.rule.IsObject()) out.own.push_back(std::move(rule));
        }
    }
    return true;
}

auto Render(const ChangeSet& c) -> std::string {
    using document::EncodeString;
    std::string out = "{\n";
    out += "  \"formatVersion\": " + std::to_string(c.formatVersion) + ",\n";
    out += "  \"about\": \"Your changes to a loot filter, made with the ESR Filter Editor in game. The editor applies them to the author's filter (base) and writes your copy (merged), which the game loads.\",\n";
    out += "  \"base\": " + EncodeString(c.base) + ",\n";
    out += "  \"merged\": " + EncodeString(c.merged) + ",\n";
    out += "  \"baseVersion\": " + EncodeString(c.baseVersion) + ",\n";
    out += "  \"baseHash\": " + EncodeString(HexHash(c.baseHash)) + ",\n";
    out += "  \"rules\": [";
    for (std::size_t i = 0; i < c.rules.size(); ++i) {
        const RuleChange& r = c.rules[i];
        Value entry = Value::Object();
        entry.Set("id", Value::String(r.id));
        if (r.off) entry.Set("off", Value::Bool(true));
        if (!r.fields.empty()) {
            Value fields = Value::Array();
            for (const FieldEdit& f : r.fields) {
                Value field = Value::Object();
                field.Set("key", Value::String(f.key));
                if (f.removed) field.Set("removed", Value::Bool(true));
                else field.Set("value", f.value);
                if (f.authorHad) field.Set("author", f.author);
                fields.array.push_back(std::move(field));
            }
            entry.Set("fields", std::move(fields));
        }
        if (!r.removedItems.empty()) {
            Value items = Value::Array();
            for (const RuleChange::Removed& removed : r.removedItems) {
                Value item = Value::Object();
                item.Set("key", Value::String(removed.key));
                item.Set("values", Value::Array(removed.values));
                items.array.push_back(std::move(item));
            }
            entry.Set("removedItems", std::move(items));
        }
        if (!r.snapshot.IsNull()) entry.Set("snapshot", r.snapshot);
        out += (i == 0 ? "\n    " : ",\n    ") + document::Render(entry);
    }
    out += c.rules.empty() ? "],\n" : "\n  ],\n";
    out += "  \"own\": [";
    for (std::size_t i = 0; i < c.own.size(); ++i) {
        const OwnRule& o = c.own[i];
        Value entry = Value::Object();
        entry.Set("id", Value::String(o.id));
        entry.Set("after", Value::String(o.after));
        entry.Set("section", Value::String(o.section));
        entry.Set("rule", o.rule);
        out += (i == 0 ? "\n    " : ",\n    ") + document::Render(entry);
    }
    out += c.own.empty() ? "]\n" : "\n  ]\n";
    out += "}\n";
    return out;
}

// ---- editing ----

namespace {


void Touch(RuleChange& change, const Value& author) {
    if (change.snapshot.IsNull()) change.snapshot = author;
}

}  // namespace

auto Effective(const Value& author, const RuleChange& change) -> Value {
    Value rule = author;
    for (const FieldEdit& f : change.fields) {
        if (f.removed) rule.Erase(f.key);
        else rule.Set(f.key, f.value);
    }
    for (const RuleChange::Removed& removed : change.removedItems) {
        Value* list = rule.Find(removed.key);
        if (list == nullptr || !list->IsArray()) continue;
        std::vector<Value> left;
        for (const Value& entry : list->array) {
            const bool gone = std::any_of(removed.values.begin(), removed.values.end(), [&](const Value& v) { return v == entry; });
            if (!gone) left.push_back(entry);
        }
        if (!left.empty()) list->array = std::move(left);  // an empty list is refused: the editor never empties one
    }
    return rule;
}

void SetField(RuleChange& change, const Value& author, std::string_view key, const Value* value) {
    Touch(change, author);
    const Value* authorValue = author.Find(key);
    const bool sameAsAuthor = value == nullptr ? authorValue == nullptr : authorValue != nullptr && *authorValue == *value;
    FieldEdit* edit = change.Field(key);
    if (sameAsAuthor) {
        if (edit != nullptr) change.fields.erase(change.fields.begin() + (edit - change.fields.data()));
        return;
    }
    if (edit == nullptr) {
        change.fields.push_back(FieldEdit{});
        edit = &change.fields.back();
        edit->key = std::string(key);
        edit->authorHad = authorValue != nullptr;
        if (authorValue != nullptr) edit->author = *authorValue;
    }
    edit->removed = value == nullptr;
    edit->value = value == nullptr ? Value::Null() : *value;
}

void SetItem(RuleChange& change, const Value& author, std::string_view key, const Value& entry, bool removed) {
    Touch(change, author);
    RuleChange::Removed* list = nullptr;
    for (RuleChange::Removed& r : change.removedItems)
        if (r.key == key) list = &r;
    if (removed) {
        if (list == nullptr) {
            change.removedItems.push_back(RuleChange::Removed{std::string(key), {}});
            list = &change.removedItems.back();
        }
        if (std::none_of(list->values.begin(), list->values.end(), [&](const Value& v) { return v == entry; })) list->values.push_back(entry);
        return;
    }
    if (list == nullptr) return;
    list->values.erase(std::remove_if(list->values.begin(), list->values.end(), [&](const Value& v) { return v == entry; }), list->values.end());
    change.removedItems.erase(std::remove_if(change.removedItems.begin(), change.removedItems.end(),
                                             [](const RuleChange::Removed& r) { return r.values.empty(); }),
                              change.removedItems.end());
}

// ---- merging ----

namespace {

auto SplitPlace(const std::string& place, std::string& part, std::string& section) -> void {
    const std::size_t at = place.find(" > ");
    part = at == std::string::npos ? place : place.substr(0, at);
    section = at == std::string::npos ? std::string{} : place.substr(at + 3);
}

}  // namespace

auto Merge(std::string_view authorText, const ChangeSet& changes, const MergeOptions& options) -> MergeResult {
    MergeResult result;
    document::Document doc;
    std::vector<document::Diag> diags;
    if (!document::Document::Parse(std::string(authorText), doc, diags)) {
        result.error = diags.empty() ? "the author's filter could not be read" : "the author's filter could not be read: " + diags.front().message;
        return result;
    }
    const bool authorChanged = options.findConflicts && changes.baseHash != 0 && Hash(authorText) != changes.baseHash;
    const auto fail = [&result](const std::string& what, const document::Status& status) {
        result.error = what + ": " + status.error;
        return result;
    };

    // Changes to the author's rules.
    std::vector<std::string> off;
    for (const RuleChange& change : changes.rules) {
        const int index = doc.FindRule(change.id);
        if (index < 0 || index >= doc.ActiveCount()) {
            Conflict c;
            c.kind = Conflict::Kind::Deleted;
            c.id = change.id;
            result.conflicts.push_back(std::move(c));
            continue;
        }
        const Value author = doc.Rules()[static_cast<std::size_t>(index)].value;
        if (authorChanged) {
            for (const FieldEdit& f : change.fields) {
                const Value* now = author.Find(f.key);
                const bool same = (now != nullptr) == f.authorHad && (now == nullptr || *now == f.author);
                const bool agrees = f.removed ? now == nullptr : now != nullptr && *now == f.value;  // both made it the same
                if (same || agrees) continue;
                Conflict c;
                c.kind = Conflict::Kind::Field;
                c.id = change.id;
                c.key = f.key;
                c.mine = f.value;
                c.mineRemoved = f.removed;
                c.was = f.author;
                c.wasAbsent = !f.authorHad;
                c.nowAbsent = now == nullptr;
                if (now != nullptr) c.now = *now;
                result.conflicts.push_back(std::move(c));
            }
        }
        if (change.off && !options.keepOff) {
            off.push_back(change.id);
            continue;
        }
        const Value rule = Effective(author, change);
        std::vector<std::string> keys;
        for (const FieldEdit& f : change.fields) keys.push_back(f.key);
        for (const RuleChange::Removed& r : change.removedItems) keys.push_back(r.key);
        for (const std::string& key : keys) {
            const Value* value = rule.Find(key);
            const document::Status status = value != nullptr ? doc.SetField(index, key, *value) : doc.RemoveField(index, key);
            if (!status) return fail("rule " + change.id, status);
        }
    }

    // The player's own rules, where they belong (before off rules go, so a rule can follow one).
    for (const OwnRule& own : changes.own) {
        document::Position where = document::Position::End();
        bool placed = false;
        if (!own.after.empty()) {
            const int after = doc.FindRule(own.after);
            if (after >= 0 && after < doc.ActiveCount()) {
                where = document::Position::After(after);
                placed = true;
            }
        }
        if (!placed && !own.section.empty()) {
            std::string part, section;
            SplitPlace(own.section, part, section);
            const int heading = doc.FindHeading(part, section);
            if (heading >= 0) {
                where = own.after.empty() ? document::Position::StartOf(heading) : document::Position::EndOf(heading);
                placed = true;
            }
        }
        if (!placed) {
            int heading = doc.FindHeading(options.ownPart);
            if (heading < 0) {
                const document::Status status = doc.AddHeading(document::Level::Part, document::Position::End(), options.ownPart,
                                                               "Your own rules. The editor puts them here when the place they had is gone.");
                if (!status) return fail("the " + options.ownPart + " part", status);
                heading = doc.FindHeading(options.ownPart);
            }
            where = document::Position::EndOf(heading);
            if (!own.section.empty() || !own.after.empty())
                result.notes.push_back(own.id + ": its place (" + (own.after.empty() ? own.section : "after " + own.after) +
                                       ") is gone; it is in " + options.ownPart + " now");
        }
        const document::Status status = doc.InsertRule(where, own.rule);
        if (!status) return fail("rule " + own.id, status);
    }

    // Rules switched off are left out of the copy the game loads.
    for (const std::string& id : off) {
        const int index = doc.FindRule(id);
        if (index < 0) continue;
        const document::Status status = doc.DeleteRule(index);
        if (!status) return fail("rule " + id, status);
    }

    // The copy's own name; no update address (the launcher keeps the author's filter up to date, not this copy).
    const std::string title = doc.GetSettings().title;
    if (!options.titleSuffix.empty() && (title.size() < options.titleSuffix.size() || !title.ends_with(options.titleSuffix))) {
        const document::Status status = doc.SetSetting("title", Value::String(title.empty() ? "Loot filter" + options.titleSuffix : title + options.titleSuffix));
        if (!status) return fail("the title", status);
    }
    if (const document::Status status = doc.RemoveSetting("updateUrl"); !status) return fail("updateUrl", status);

    result.text = doc.Text();
    result.ok = true;
    return result;
}

void Resolve(ChangeSet& changes, const Conflict& conflict, bool keepMine) {
    RuleChange* change = changes.Find(conflict.id);
    if (change == nullptr) return;
    if (conflict.kind == Conflict::Kind::Field) {
        FieldEdit* edit = change->Field(conflict.key);
        if (edit == nullptr) return;
        if (keepMine) {
            edit->authorHad = !conflict.nowAbsent;
            edit->author = conflict.nowAbsent ? Value::Null() : conflict.now;
            const bool same = edit->removed ? conflict.nowAbsent : !conflict.nowAbsent && edit->value == conflict.now;
            if (same) change->fields.erase(change->fields.begin() + (edit - change->fields.data()));
        } else {
            change->fields.erase(change->fields.begin() + (edit - change->fields.data()));
        }
        changes.DropEmpty();
        return;
    }
    // The author deleted the rule.
    if (keepMine && !change->off && change->snapshot.IsObject()) {
        OwnRule own;
        own.id = changes.NextOwnId();
        own.rule = Effective(change->snapshot, *change);
        // Its title takes the new id: "R123 | ..." becomes "U004 | ...".
        if (Value* title = own.rule.Find("comment"); title != nullptr && title->IsString()) {
            std::string text = title->Str();
            if (text.rfind(conflict.id, 0) == 0) text = own.id + text.substr(conflict.id.size());
            else text = own.id + " | " + text;
            *title = Value::String(text);
        } else {
            own.rule.Set("comment", Value::String(own.id + " | kept from " + conflict.id + " | "));
        }
        changes.own.push_back(std::move(own));
    }
    changes.Drop(conflict.id);
}

// ---- the game's selection ----

auto SwitchSelection(std::string_view text, std::string_view from, std::string_view to, std::string& out) -> bool {
    Value root;
    std::string error;
    if (text.empty()) {
        root = Value::Object();
        root.Set("formatVersion", Value::Integer(1));
    } else if (!ReadJson(text, root, error) || !root.IsObject()) {
        return false;
    }
    root.Set("activeFilter", Value::String(std::string(to)));
    if (Value* levels = root.Find("levels"); levels != nullptr && levels->IsObject()) {
        if (const Value* level = levels->Find(from); level != nullptr && levels->Find(to) == nullptr) {
            const Value copy = *level;
            levels->Set(to, copy);
        }
    }
    // The loot filter's own layout: a member per line.
    out = "{\n";
    for (std::size_t i = 0; i < root.object.size(); ++i) {
        const Member& m = root.object[i];
        out += "  " + document::EncodeString(m.key) + ": " + document::Render(m.value) + (i + 1 < root.object.size() ? ",\n" : "\n");
    }
    out += "}\n";
    return true;
}

}  // namespace esrfe::edit
