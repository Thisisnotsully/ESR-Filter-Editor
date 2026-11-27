// A player's changes to a filter they did not write, kept apart from it so the author's updates still come through
// (see the project's filter-maintenance.md):
//   * changes to the author's rules, by their permanent R### id: only the fields the player changed, each with the
//     author's value at the time, and the items the player took out of a rule's lists;
//   * rules switched off (left out of the copy the game loads);
//   * the player's own rules, whole, with a U### id and the place they go (after a rule, or in a section).
// Merge applies them to the author's filter and gives the player's copy, "<title> (my version)", which is what the
// game loads. When the author changed a field the player also changed, or deleted a rule the player changed, Merge
// says so (a Conflict) and keeps the player's version until the player chooses (Resolve).
//
// Standard C++ only (tested on Linux). The changes file is JSON, written by Render.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "document.h"
#include "json.h"

namespace esrfe::edit {

using document::Value;

struct FieldEdit {
    std::string key;
    bool removed = false;  // the player took the field out
    Value value;           // the player's value (when not removed)
    bool authorHad = false;  // the author's rule had the field when the player changed it
    Value author;          // its value then
};

struct RuleChange {
    std::string id;  // R###
    bool off = false;
    std::vector<FieldEdit> fields;
    struct Removed {
        std::string key;            // itemCodes, itemTypes, uniqueOrSetIds
        std::vector<Value> values;  // the entries the player took out
    };
    std::vector<Removed> removedItems;
    Value snapshot;  // the author's whole rule when the player first changed it (to keep it if the author deletes it)

    [[nodiscard]] auto Empty() const -> bool { return !off && fields.empty() && removedItems.empty(); }
    auto Field(std::string_view key) -> FieldEdit*;
    [[nodiscard]] auto Field(std::string_view key) const -> const FieldEdit*;
    [[nodiscard]] auto RemovedFrom(std::string_view key) const -> const std::vector<Value>*;
};

struct OwnRule {
    std::string id;       // U###
    // Where it goes:
    //   "top": the "My Rules" part above every author rule, so the player's hide and always-show win (the first
    //          hide stops an item; a show protects it from every later hide);
    //   "end": the "My Highlights" part below every author rule, so the player's colours, beam, icon and name win
    //          (a later rule's replace an earlier one's); with `keepShown` a copy that only shows goes to the top
    //          as well, so no author rule hides what the player highlights;
    //   "":    after the rule `after`, else in `section` (a change kept from a rule the author deleted).
    std::string place;
    bool keepShown = false;
    std::string after;    // the rule it follows (an R### or U### id), "" for the start of its section
    std::string section;  // "Part > Section" or "Part" or "", where it goes when `after` is gone
    std::string replaces; // the author's rule this edited copy stands in for (switched off while the copy is there)
    Value rule;           // the whole rule, its title "U### | ... | ..."
    bool off = false;     // switched off: left out of the game's copy
};

struct ChangeSet {
    std::int64_t formatVersion = 1;
    std::string base;         // the author's filter: the loot filter's id ("repo:QoL-Filter/Sullys-QoL-Filter.json")
    std::string merged;       // the player's copy: "user:Sullys-QoL-Filter (my version).json"
    std::string title;        // the copy's title, the name the player gave it; "" (older copies): the author's + " (my version)"
    std::string baseVersion;  // the author's version at the last merge
    std::uint64_t baseHash = 0;  // Hash of the author's file at the last merge
    std::vector<RuleChange> rules;
    std::vector<OwnRule> own;
    std::vector<document::Member> settings;  // the player's top-level settings (soundVolume), over the author's

    [[nodiscard]] auto Find(std::string_view id) const -> const RuleChange*;
    auto Find(std::string_view id) -> RuleChange*;
    auto Get(std::string_view id) -> RuleChange&;  // made when missing
    void Drop(std::string_view id);                // the rule back to the author's version
    void DropEmpty();
    [[nodiscard]] auto NextOwnId() const -> std::string;  // "U001", "U002", ...
};

auto Parse(std::string_view text, ChangeSet& out, std::string& error) -> bool;
auto Render(const ChangeSet& changes) -> std::string;

// FNV-1a 64 of a file's bytes: tells whether the author's filter changed since the last merge.
auto Hash(std::string_view text) -> std::uint64_t;

// ---- editing: what the player does to an author's rule ----

// The rule as the player has it: the author's rule with the change applied (fields set or removed, items taken out).
// `author` is the author's rule now. A list the player emptied is kept with what is left (empty lists are refused by
// the loot filter, so SetItems keeps at least one item).
auto Effective(const Value& author, const RuleChange& change) -> Value;

// Records that the player wants `key` to be `value` (or gone, with nullptr) in the rule whose author version is
// `author`. Setting it back to the author's value drops the edit.
void SetField(RuleChange& change, const Value& author, std::string_view key, const Value* value);
// Takes an entry out of one of the rule's item lists (itemCodes, itemTypes, uniqueOrSetIds), or puts it back.
void SetItem(RuleChange& change, const Value& author, std::string_view key, const Value& entry, bool removed);

// ---- merging ----

struct Conflict {
    enum class Kind {
        Field,    // the author changed a field the player changed: keep mine / use the author's
        Deleted,  // the author's filter has no rule with this id any more: keep it as my own rule / drop my change
    };
    Kind kind = Kind::Field;
    std::string id;
    std::string key;       // Field
    Value mine;            // the player's value (Field; Null with mineRemoved)
    bool mineRemoved = false;
    Value was;             // the author's value when the player changed it
    bool wasAbsent = false;
    Value now;             // the author's value now
    bool nowAbsent = false;
};

struct MergeResult {
    bool ok = false;
    std::string error;
    std::string text;                 // the player's copy
    std::vector<Conflict> conflicts;  // to ask about (the copy has the player's version meanwhile)
    std::vector<std::string> notes;   // own rules whose place is gone and went to the end, etc.
};

struct MergeOptions {
    std::string titleSuffix = " (my version)";
    std::string ownPart = "My Rules";  // where own rules go when their place is gone, and "top" ones
    std::string highlightPart = "My Highlights";  // "end" own rules
    bool companions = true;  // the show-only copies of keepShown rules (the editor's own view leaves them out)
    bool keepOff = false;   // rules switched off stay (what the editor shows); the game's copy leaves them out
    bool findConflicts = true;
};

// Applies the changes to the author's filter text. Conflicts are looked for only when the author's file changed since
// the last merge (Hash differs from changes.baseHash).
auto Merge(std::string_view authorText, const ChangeSet& changes, const MergeOptions& options = {}) -> MergeResult;

// The player's answer to a conflict: true keeps the player's version (it now follows the author's new value), false
// takes the author's (the player's edit of that field, or of the deleted rule, is dropped). Keeping a change to a
// deleted rule makes it one of the player's own rules (its snapshot with the change, a new U### id).
void Resolve(ChangeSet& changes, const Conflict& conflict, bool keepMine);

// Copies that still have to be brought up to date are done by UpdateCopies (session.h).

// The rule that keeps an "end" own rule's items shown: its conditions, "show": true, at the top.
auto ShownCompanion(const OwnRule& own) -> Value;

// ---- the game's selection ----

// selection.json with activeFilter set to `to`, and the level the player had for `from` given to `to` as well.
// False when the text is not a JSON object.
auto SwitchSelection(std::string_view text, std::string_view from, std::string_view to, std::string& out) -> bool;

}  // namespace esrfe::edit
