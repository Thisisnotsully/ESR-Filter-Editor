#include "session.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace esrfe::edit {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kCopySuffix = " (my version)";
constexpr const char* kEditorFolder = "esr-filter-editor";

auto P(const std::string& utf8) -> fs::path { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
auto U(const fs::path& path) -> std::string {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

auto PlainName(const std::string& name) -> bool {
    return !name.empty() && name.find_first_of("\\/:") == std::string::npos && name.find("..") == std::string::npos;
}

auto CopyId(const std::string& stem) -> std::string { return "user:" + stem + std::string(kCopySuffix) + ".json"; }

auto SameFile(const fs::path& a, const fs::path& b) -> bool {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::string x, y;
    return ReadText(U(a), x) && ReadText(U(b), y) && x == y;
}

// The author's sounds and icons next to the player's copy. Only files the editor put there are ever replaced.
void SyncAssets(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::equivalent(from, to, ec)) return;
    const fs::path manifestPath = to / kEditorFolder / "assets.txt";
    std::set<std::string> ours;
    {
        std::string text;
        if (ReadText(U(manifestPath), text)) {
            std::istringstream lines(text);
            for (std::string line; std::getline(lines, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) ours.insert(line);
            }
        }
    }
    const std::size_t before = ours.size();
    bool changed = false;
    for (const char* folder : {"sounds", "icons"}) {
        const fs::path source = from / folder;
        if (!fs::is_directory(source, ec)) continue;
        for (const fs::directory_entry& entry : fs::directory_iterator(source, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string relative = std::string(folder) + "/" + U(entry.path().filename());
            const fs::path target = to / folder / entry.path().filename();
            const bool exists = fs::exists(target, ec);
            if (exists && (ours.count(relative) == 0 || SameFile(entry.path(), target))) continue;
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                ours.insert(relative);
                changed = true;
            }
        }
    }
    if (changed || ours.size() != before) {
        std::string text;
        for (const std::string& line : ours) text += line + "\n";
        WriteText(U(manifestPath), text);
    }
}

}  // namespace

auto PathOfId(const std::string& id, const Folders& folders) -> std::string {
    const std::size_t colon = id.find(':');
    if (colon == std::string::npos) return {};
    const std::string where = id.substr(0, colon);
    std::string file = id.substr(colon + 1);
    fs::path folder;
    if (where == "shipped") {
        folder = P(folders.shipped);
    } else if (where == "user") {
        folder = P(folders.user);
    } else if (where == "repo") {
        const std::size_t slash = file.find('/');
        if (slash == std::string::npos) return {};
        const std::string repository = file.substr(0, slash);
        file = file.substr(slash + 1);
        if (!PlainName(repository)) return {};
        folder = P(folders.user) / P(repository);
    } else {
        return {};
    }
    if (folder.empty() || !PlainName(file)) return {};
    return U(folder / P(file));
}

auto StemOfId(const std::string& id) -> std::string {
    std::string file = id.substr(id.find(':') == std::string::npos ? 0 : id.find(':') + 1);
    if (const std::size_t slash = file.find_last_of('/'); slash != std::string::npos) file = file.substr(slash + 1);
    if (file.size() > 5 && file.ends_with(".json")) file.resize(file.size() - 5);
    if (file.ends_with(kCopySuffix)) file.resize(file.size() - kCopySuffix.size());
    return file;
}

auto ReadText(const std::string& path, std::string& out) -> bool {
    std::ifstream file(P(path), std::ios::binary);
    if (!file) return false;
    std::stringstream text;
    text << file.rdbuf();
    out = text.str();
    return !file.bad();
}

auto WriteText(const std::string& path, const std::string& text) -> bool {
    const fs::path target = P(path);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << text;
        file.flush();
        if (!file) return false;
    }
    fs::rename(temporary, target, ec);  // replaces the file
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

// ---- the session ----

auto Session::Open(const std::string& activeId, const std::string& activePath, const Folders& folders, std::string& error) -> bool {
    *this = Session{};
    folders_ = folders;
    activeId_ = activeId;
    const std::string stem = StemOfId(activeId);
    if (stem.empty() || folders.user.empty()) {
        error = "the loot filter's filters folder is not known";
        return false;
    }
    changesPath_ = U(P(folders.user) / kEditorFolder / P(stem + ".changes.json"));
    const std::string copyId = CopyId(stem);
    mergedPath_ = PathOfId(copyId, folders);

    std::string saved;
    bool haveChanges = false;
    if (ReadText(changesPath_, saved)) {
        std::string why;
        if (!edit::Parse(saved, changes_, why)) {
            error = "your changes file could not be read (" + why + "): " + changesPath_;
            return false;
        }
        haveChanges = true;
    }
    if (activeId == copyId) {
        if (!haveChanges) {
            error = "this copy was made by the editor, but its changes file is missing: " + changesPath_;
            return false;
        }
        isCopy_ = true;
        baseId_ = changes_.base;
        basePath_ = PathOfId(baseId_, folders);
    } else {
        if (haveChanges && changes_.base != activeId) {
            error = "your changes in " + changesPath_ + " belong to another filter (" + changes_.base + ")";
            return false;
        }
        baseId_ = activeId;
        basePath_ = activePath.empty() ? PathOfId(activeId, folders) : activePath;
    }
    if (basePath_.empty() || !ReadText(basePath_, baseText_)) {
        error = "the author's filter could not be read: " + (basePath_.empty() ? baseId_ : basePath_);
        return false;
    }
    std::vector<document::Diag> diags;
    if (!document::Document::Parse(baseText_, base_, diags)) {
        error = "the author's filter could not be read: " + (diags.empty() ? basePath_ : diags.front().message);
        return false;
    }
    if (!haveChanges) {
        changes_.base = baseId_;
        changes_.baseHash = Hash(baseText_);
    }
    changes_.merged = copyId;
    changes_.baseVersion = base_.GetSettings().version;
    if (changes_.baseHash != Hash(baseText_)) {
        // Edits the author has now made too are no longer the player's own.
        for (RuleChange& change : changes_.rules) {
            const Value* author = AuthorRule(change.id);
            if (author == nullptr) continue;
            change.fields.erase(std::remove_if(change.fields.begin(), change.fields.end(),
                                               [author](const FieldEdit& f) {
                                                   const Value* now = author->Find(f.key);
                                                   return f.removed ? now == nullptr : now != nullptr && *now == f.value;
                                               }),
                                change.fields.end());
        }
        changes_.DropEmpty();
        const MergeResult check = Merge(baseText_, changes_);
        conflicts_ = check.conflicts;
        if (conflicts_.empty()) changes_.baseHash = Hash(baseText_);  // nothing to ask: taken in at the next save
    }
    Rebuild();
    return true;
}

void Session::Rebuild() {
    MergeOptions options;
    options.keepOff = true;
    options.findConflicts = false;
    options.titleSuffix.clear();
    options.companions = false;
    const MergeResult view = Merge(baseText_, changes_, options);
    viewText_ = view.ok ? view.text : baseText_;
    notes_ = view.notes;
}

auto Session::AuthorRule(const std::string& id) const -> const Value* {
    const int index = base_.FindRule(id);
    if (index < 0 || index >= base_.ActiveCount()) return nullptr;
    return &base_.Rules()[static_cast<std::size_t>(index)].value;
}

auto Session::Effective(const std::string& id) const -> Value {
    if (const Value* author = AuthorRule(id)) {
        const RuleChange* change = changes_.Find(id);
        return change == nullptr ? *author : edit::Effective(*author, *change);
    }
    for (const OwnRule& own : changes_.own)
        if (own.id == id) return own.rule;
    return Value::Null();
}

auto Session::IsOff(const std::string& id) const -> bool {
    if (const OwnRule* own = Own(id)) return own->off;
    const RuleChange* change = changes_.Find(id);
    return change != nullptr && change->off;
}

auto Session::Own(const std::string& id) const -> const OwnRule* {
    for (const OwnRule& own : changes_.own)
        if (own.id == id) return &own;
    return nullptr;
}

namespace {

// "U004 | <what the title said without an id> | <action>"
void TitleWithId(OwnRule& own) {
    Value* title = own.rule.Find("comment");
    std::string text = title != nullptr && title->IsString() ? title->Str() : std::string{};
    if (text.size() > 1 && (text[0] == 'U' || text[0] == 'R')) {  // an id already there: replaced
        std::size_t i = 1;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (i > 1 && text.compare(i, 3, " | ") == 0) text = text.substr(i + 3);
    }
    own.rule.Set("comment", Value::String(own.id + " | " + (text.empty() ? std::string("my rule | ") : text)));
}

}  // namespace

auto Session::AddOwn(OwnRule rule) -> std::string {
    rule.id = changes_.NextOwnId();
    TitleWithId(rule);
    changes_.own.push_back(std::move(rule));
    Rebuild();
    return changes_.own.back().id;
}

void Session::ReplaceOwn(const std::string& id, OwnRule rule) {
    for (OwnRule& own : changes_.own) {
        if (own.id != id) continue;
        rule.id = id;
        rule.off = own.off;
        TitleWithId(rule);
        own = std::move(rule);
        Rebuild();
        return;
    }
}

void Session::RemoveOwn(const std::string& id) {
    changes_.own.erase(std::remove_if(changes_.own.begin(), changes_.own.end(), [&](const OwnRule& o) { return o.id == id; }),
                       changes_.own.end());
    Rebuild();
}

void Session::SetOwnOff(const std::string& id, bool off) {
    for (OwnRule& own : changes_.own)
        if (own.id == id) own.off = off;
    Rebuild();
}

void Session::SetOff(const std::string& id, bool off) {
    const Value* author = AuthorRule(id);
    if (author == nullptr) return;
    RuleChange& change = changes_.Get(id);
    if (change.snapshot.IsNull()) change.snapshot = *author;
    change.off = off;
    changes_.DropEmpty();
    Rebuild();
}

void Session::SetField(const std::string& id, const std::string& key, const Value* value) {
    const Value* author = AuthorRule(id);
    if (author == nullptr) return;
    edit::SetField(changes_.Get(id), *author, key, value);
    changes_.DropEmpty();
    Rebuild();
}

void Session::SetItem(const std::string& id, const std::string& key, const Value& entry, bool removed) {
    const Value* author = AuthorRule(id);
    if (author == nullptr) return;
    edit::SetItem(changes_.Get(id), *author, key, entry, removed);
    changes_.DropEmpty();
    Rebuild();
}

void Session::Reset(const std::string& id) {
    changes_.Drop(id);
    Rebuild();
}

void Session::Resolve(const Conflict& conflict, bool keepMine) {
    edit::Resolve(changes_, conflict, keepMine);
    conflicts_.erase(std::remove_if(conflicts_.begin(), conflicts_.end(),
                                    [&](const Conflict& c) { return c.id == conflict.id && c.key == conflict.key && c.kind == conflict.kind; }),
                     conflicts_.end());
    Rebuild();
}

void Session::FinishConflicts() {
    conflicts_.clear();
    changes_.baseHash = Hash(baseText_);
    Rebuild();
}

auto Session::Save(std::string& error, bool& switched) -> bool {
    switched = false;
    if (conflicts_.empty()) changes_.baseHash = Hash(baseText_);
    changes_.baseVersion = base_.GetSettings().version;
    const MergeResult merged = Merge(baseText_, changes_, MergeOptions{});
    if (!merged.ok) {
        error = merged.error;
        return false;
    }
    if (!WriteText(changesPath_, Render(changes_))) {
        error = "could not write " + changesPath_;
        return false;
    }
    if (!WriteText(mergedPath_, merged.text)) {
        error = "could not write " + mergedPath_;
        return false;
    }
    SyncAssets(P(basePath_).parent_path(), P(folders_.user));
    if (activeId_ != changes_.merged) {
        const std::string selectionPath = U(P(folders_.user) / "selection.json");
        std::string selection, updated;
        ReadText(selectionPath, selection);  // none yet: one is made
        if (!SwitchSelection(selection, activeId_, changes_.merged, updated) || !WriteText(selectionPath, updated)) {
            error = "your copy is saved, but the game could not be switched to it (" + selectionPath +
                    "); choose it in the launcher's Loot Filter tab";
            return false;
        }
        activeId_ = changes_.merged;
        isCopy_ = true;
        switched = true;
    }
    return true;
}

auto UpdateCopies(const Folders& folders) -> std::vector<CopyUpdate> {
    std::vector<CopyUpdate> out;
    std::error_code ec;
    const fs::path folder = P(folders.user) / kEditorFolder;
    if (!fs::is_directory(folder, ec)) return out;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        const std::string name = U(entry.path().filename());
        if (!entry.is_regular_file(ec) || !name.ends_with(".changes.json")) continue;
        std::string text, why;
        ChangeSet changes;
        if (!ReadText(U(entry.path()), text) || !edit::Parse(text, changes, why) || changes.merged.empty()) continue;
        std::string author;
        if (!ReadText(PathOfId(changes.base, folders), author) || Hash(author) == changes.baseHash) continue;
        CopyUpdate update;
        update.copy = changes.merged;
        Session session;
        bool switched = false;
        if (!session.Open(changes.merged, PathOfId(changes.merged, folders), folders, update.error)) {
            out.push_back(std::move(update));
            continue;
        }
        update.conflicts = session.Conflicts().size();
        update.updated = session.Save(update.error, switched);
        out.push_back(std::move(update));
    }
    return out;
}

auto Session::PlayerSetting(const std::string& key) const -> const Value* {
    for (const document::Member& m : changes_.settings)
        if (m.key == key) return &m.value;
    return nullptr;
}

void Session::SetSetting(const std::string& key, const Value* value) {
    changes_.settings.erase(std::remove_if(changes_.settings.begin(), changes_.settings.end(), [&](const document::Member& m) { return m.key == key; }),
                            changes_.settings.end());
    if (value != nullptr) changes_.settings.push_back(document::Member{key, *value});
    Rebuild();
}

auto Session::SoundVolume() const -> int {
    std::int64_t volume = base_.GetSettings().soundVolume.value_or(100);
    if (const Value* v = PlayerSetting("soundVolume")) v->AsInteger(volume);
    return static_cast<int>(std::clamp<std::int64_t>(volume, 0, 100));
}

auto Session::SoundFiles() const -> std::vector<std::string> {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path folder = P(basePath_).parent_path() / "sounds";
    if (!fs::is_directory(folder, ec)) return out;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string name = U(entry.path().filename());
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (lower.ends_with(".wav") || lower.ends_with(".mp3") || lower.ends_with(".flac")) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

auto Session::SoundPath(const std::string& file) const -> std::string { return U(P(basePath_).parent_path() / "sounds" / P(file)); }

}  // namespace esrfe::edit
