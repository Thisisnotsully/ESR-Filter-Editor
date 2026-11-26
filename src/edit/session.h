// The editor's hold on the filter the game uses, for editing it (standard C++ and std::filesystem only; tested on
// Linux with real folders).
//
// The author's filter is never written. A player can have several versions of it, each named by them. A version's
// changes go to
//     <filters folder>\esr-filter-editor\<version name>.changes.json
// and the copy the game loads to
//     <filters folder>\<version name>.json       (the loot filter's id "user:<version name>.json")
// A changes file names its author's filter ("base") and its copy ("merged"): the session finds a version by its copy.
// Versions made before names (0.7 and older) are "<file> (my version)" with changes in "<file>.changes.json".
// The loot filter finds a filter's sounds and icons only in folders next to the filter, so the author's "sounds" and
// "icons" folders are copied next to the copy as well (the files the editor copied are listed in
// esr-filter-editor\assets.txt; a file it did not copy is never overwritten). The first save switches the game to the
// copy in selection.json, with the level the player had.
//
// <filters folder> is the loot filter's own filters folder (Saved Games\Diablo II Resurrected\mods\<mod>\LootFilters):
// a live filter is "repo:<repository>/<file>" in a folder of it, a player's filter "user:<file>" in it, a filter that
// ships with the mod "shipped:<file>" in the mod's LootFilters folder.
#pragma once

#include <string>
#include <vector>

#include "changes.h"

namespace esrfe::edit {

struct Folders {
    std::string user;     // the loot filter's filters folder (UTF-8)
    std::string shipped;  // <mod>\LootFilters
    std::string starter;  // the new-filter wizard's starter (<plugins>\esr-filter-editor\new-filter)
};

// "repo:QoL-Filter/Sullys-QoL-Filter.json" -> the file; "" for an id the loot filter would not take.
auto PathOfId(const std::string& id, const Folders& folders) -> std::string;
// "Sullys-QoL-Filter" from any id; "Sullys-QoL-Filter" also from "user:Sullys-QoL-Filter (my version).json".
auto StemOfId(const std::string& id) -> std::string;

// A filter the loot filter can load: in <mod>\LootFilters ("shipped:"), the player's folder ("user:"), or a
// repository folder in it, one with the launcher's .esr-repo.json ("repo:<folder>/").
struct FilterChoice {
    std::string id;     // "repo:QoL-Filter/Sullys-QoL-Filter.json"
    std::string path;   // the file (UTF-8)
    std::string title;  // its "title", or the file name without .json
    bool mine = false;  // one of the player's versions of a filter (a changes file makes it)
};
// Every one there, by title. Files that are not filters (no "rules") are left out.
auto ListFilters(const Folders& folders) -> std::vector<FilterChoice>;
// Makes `id` the loot filter's active filter (selection.json); the loot filter loads it a moment later.
auto SelectFilter(const Folders& folders, const std::string& id, std::string& error) -> bool;

// The player's versions of a filter (their copies with changes on top), by the author's filter's id.
struct Version {
    std::string id;     // "user:Sully's QoL Filter (rune farming).json"
    std::string title;  // the name the player gave it
};
auto VersionsOf(const Folders& folders, const std::string& baseId) -> std::vector<Version>;
// Whether `name` can name a new version: a file name the loot filter takes, not used yet. `error` says why not.
auto CheckVersionName(const Folders& folders, const std::string& name, std::string& error) -> bool;
// "Sully's QoL Filter (my version)", or "(my version 2)" and so on when taken.
auto SuggestVersionName(const Folders& folders, const std::string& baseTitle) -> std::string;

auto ReadText(const std::string& path, std::string& out) -> bool;
// Writes through a temporary file and a rename, so the loot filter never reads half a file.
auto WriteText(const std::string& path, const std::string& text) -> bool;

class Session {
public:
    // The filter the game uses (the loot filter's id and path). Works out whether it is the player's copy (the
    // changes are read, the author's filter is the base) or another filter (it is the base; its changes, when the
    // player made some before and went back to it, are read as well).
    auto Open(const std::string& activeId, const std::string& activePath, const Folders& folders, std::string& error) -> bool;

    [[nodiscard]] auto IsCopy() const -> bool { return isCopy_; }  // the game uses the player's copy
    // Changes to an author's filter make a new version: it needs a name before the first Save.
    [[nodiscard]] auto NeedsName() const -> bool { return !isCopy_ && changes_.merged.empty(); }
    auto NameVersion(const std::string& name, std::string& error) -> bool;
    [[nodiscard]] auto BaseId() const -> const std::string& { return baseId_; }
    [[nodiscard]] auto GetFolders() const -> const Folders& { return folders_; }
    [[nodiscard]] auto HasChanges() const -> bool { return !changes_.rules.empty() || !changes_.own.empty(); }
    [[nodiscard]] auto Changes() const -> const ChangeSet& { return changes_; }
    [[nodiscard]] auto BaseText() const -> const std::string& { return baseText_; }
    [[nodiscard]] auto Base() const -> const document::Document& { return base_; }
    [[nodiscard]] auto MergedId() const -> const std::string& { return changes_.merged; }
    [[nodiscard]] auto MergedPath() const -> const std::string& { return mergedPath_; }
    [[nodiscard]] auto ChangesPath() const -> const std::string& { return changesPath_; }
    // What the player sees: the author's filter with the changes, rules that are off still in it.
    [[nodiscard]] auto ViewText() const -> const std::string& { return viewText_; }
    // Conflicts with an author's update, found when the session opened; ask, Resolve each, then FinishConflicts.
    [[nodiscard]] auto Conflicts() const -> const std::vector<Conflict>& { return conflicts_; }
    [[nodiscard]] auto Notes() const -> const std::vector<std::string>& { return notes_; }

    // The author's rule with this id, nullptr for the player's own rules.
    [[nodiscard]] auto AuthorRule(const std::string& id) const -> const Value*;
    [[nodiscard]] auto Change(const std::string& id) const -> const RuleChange* { return changes_.Find(id); }
    [[nodiscard]] auto Effective(const std::string& id) const -> Value;
    [[nodiscard]] auto IsOff(const std::string& id) const -> bool;

    // Edits (not saved until Save): the change of an author's rule.
    void SetOff(const std::string& id, bool off);
    void SetField(const std::string& id, const std::string& key, const Value* value);
    void SetItem(const std::string& id, const std::string& key, const Value& entry, bool removed);
    void Reset(const std::string& id);  // back to the author's version

    // The player's own rules. AddOwn gives the rule its U### id (and puts it in front of the title) and returns it.
    auto AddOwn(OwnRule rule) -> std::string;
    void ReplaceOwn(const std::string& id, OwnRule rule);  // keeps the id
    void RemoveOwn(const std::string& id);
    void SetOwnOff(const std::string& id, bool off);
    [[nodiscard]] auto Own(const std::string& id) const -> const OwnRule*;
    [[nodiscard]] auto OwnRules() const -> const std::vector<OwnRule>& { return changes_.own; }

    // A top-level setting the player set (nullptr: the author's applies).
    [[nodiscard]] auto PlayerSetting(const std::string& key) const -> const Value*;
    // The player's value (nullptr: back to the author's).
    void SetSetting(const std::string& key, const Value* value);
    // The filter's volume as the player has it: soundVolume, 100 when the filter has none.
    [[nodiscard]] auto SoundVolume() const -> int;
    // The author's sound files ("gong.mp3"), sorted, and where one is.
    [[nodiscard]] auto SoundFiles() const -> std::vector<std::string>;
    [[nodiscard]] auto SoundPath(const std::string& file) const -> std::string;
    // The filter's minimap icons: the names (without .sprite) of the files in its icons folder, sorted.
    [[nodiscard]] auto IconNames() const -> std::vector<std::string>;
    [[nodiscard]] auto IconPath(const std::string& name) const -> std::string;

    void Resolve(const Conflict& conflict, bool keepMine);
    // The update is taken in: later merges compare with this version of the author's filter.
    void FinishConflicts();

    // Writes the changes and the copy, copies the sounds and icons, and the first time switches the game to the copy.
    // `switched`: selection.json was changed.
    auto Save(std::string& error, bool& switched) -> bool;

private:
    void Rebuild();  // viewText_ from the base and the changes

    Folders folders_;
    bool isCopy_ = false;
    std::string activeId_;
    std::string baseId_;
    std::string basePath_;
    std::string baseText_;
    document::Document base_;
    ChangeSet changes_;
    std::string changesPath_;
    std::string mergedPath_;
    std::string viewText_;
    std::vector<Conflict> conflicts_;
    std::vector<std::string> notes_;
};

// Brings every player's copy in the filters folder up to date with its author's filter (when the game starts: the
// launcher updates the author's filters before). A copy whose update has conflicts gets the player's version for now
// and keeps them for the editor to ask about.
struct CopyUpdate {
    std::string copy;   // the copy's id
    bool updated = false;
    std::size_t conflicts = 0;
    std::string error;
};
auto UpdateCopies(const Folders& folders) -> std::vector<CopyUpdate>;

}  // namespace esrfe::edit
