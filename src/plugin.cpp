// Loot Filter Editor, milestone 1.
//
// One key reads the ground item under the cursor (or the controller's target)
// through the loot filter's editor bridge and prints what the filter does with
// it. No window yet. The plugin hooks nothing in the game.
//
// The bridge is a service the loot filter plugin offers. This file only uses
// the contract in editor_bridge_api.h.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>
#include <D2RLPlugin/plugin_communication.h>
#include <D2RLPlugin/shared_events.h>

#include <editor_bridge_api.h>

#include "document.h"
#include "gamedata.h"
#include "overlay.hpp"
#include "debug_log.hpp"
#include "browse_view.hpp"
#include "rule_draft.h"
#include "demo_view.hpp"
#include "source.h"
#include "session.h"
#include "game_thread.hpp"
#include "input_block.hpp"
#include "ui_text.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <sstream>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

namespace Bridge = LootFilterBridge;
using BridgeLease = D2RL::PluginCommunication::ServiceLease<Bridge::ServiceTable>;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
const D2RL::PluginContext*              Context{};
const D2RL::InputService*               InputSvc{};
const D2RL::PluginCommunicationService* CommSvc{};
BridgeLease                             Lease;  // the bridge, once we have it

// The last failed Acquire result we told the player about. Each distinct
// failure is told once, so a held key does not fill the console.
constexpr uint32_t NoFailureYet = 0xFFFFFFFFu;
uint32_t           ReportedFailure = NoFailureYet;

constexpr size_t   MaxTitleBytes    = 512;  // longest rule title we ask for
constexpr size_t   MaxChatTitle     = 48;   // characters of a title in chat
constexpr unsigned MaxRulesInChat   = 3;

// "ÿ" in UTF-8. Written as bytes so the source file stays plain ASCII.
// Always split the literal after it: "\xC3\xBF" "c4", never "\xC3\xBFc4".
const std::string Yc = "\xC3\xBF" "c";
const std::string Dot       = " - ";
const std::string Ellipsis  = "...";

// ---------------------------------------------------------------------------
//  Logging and console (small local helpers over the SDK context)
// ---------------------------------------------------------------------------
void LogInfo(const std::string& text) noexcept {
    if (Context != nullptr) Context->LogInfo(text.c_str());
}
void LogWarn(const std::string& text) noexcept {
    if (Context != nullptr) Context->LogWarn(text.c_str());
}
void LogError(const std::string& text) noexcept {
    if (Context != nullptr) Context->LogError(text.c_str());
}
void Console(const std::string& text) noexcept {
    if (Context != nullptr) Context->WriteConsoleMessage(text.c_str());
}

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
// Copies a zero-ended char array into a string, never reading past the array.
template <size_t N>
auto Text(const char (&field)[N]) -> std::string {
    const void* end = std::memchr(field, 0, N);
    const size_t length = end != nullptr ? static_cast<size_t>(static_cast<const char*>(end) - field) : N;
    return std::string(field, length);
}

auto Number(long long value) -> std::string {
    return std::to_string(value);
}

// Cuts UTF-8 text to at most `limit` characters (code points). When it is cut,
// the last character becomes "...". Never cuts inside a multi-byte character.
auto Shorten(const std::string& text, size_t limit) -> std::string {
    size_t characters = 0;
    size_t cutAt = text.size();  // byte offset after the first limit-1 characters
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;  // continuation byte
        if (characters == limit - 1) cutAt = i;
        ++characters;
    }
    if (characters <= limit) return text;
    return text.substr(0, cutAt) + Ellipsis;
}

auto QualityName(int quality) -> std::string {
    switch (quality) {
    case 1: return "Inferior";
    case 2: return "Normal";
    case 3: return "Superior";
    case 4: return "Magic";
    case 5: return "Set";
    case 6: return "Rare";
    case 7: return "Unique";
    case 8: return "Crafted";
    case 9: return "Tempered";
    default: return "Quality " + Number(quality);
    }
}

auto TierName(int tier) -> std::string {
    switch (tier) {
    case 0: return "Normal";
    case 1: return "Exceptional";
    case 2: return "Elite";
    default: return "Tier " + Number(tier);
    }
}

auto ResultName(Bridge::Result result) -> const char* {
    switch (result) {
    case Bridge::Result::Success: return "Success";
    case Bridge::Result::NotActive: return "NotActive";
    case Bridge::Result::NoItem: return "NoItem";
    case Bridge::Result::NoFilter: return "NoFilter";
    case Bridge::Result::NotFound: return "NotFound";
    case Bridge::Result::BufferTooSmall: return "BufferTooSmall";
    case Bridge::Result::InvalidArgument: return "InvalidArgument";
    case Bridge::Result::Failed: return "Failed";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
//  The bridge
// ---------------------------------------------------------------------------
// Takes the bridge when we do not have it yet. Plugins load in no set order, so
// this runs on first use, not at load. Returns the failure result in `failure`.
// With `announce` set, a failure is told once per distinct result.
auto AcquireBridge(bool announce, D2RL::PluginCommunication::Result* failure = nullptr) noexcept -> bool {
    if (Lease) return true;
    if (Context == nullptr || CommSvc == nullptr) return false;
    const auto result = D2RL::PluginCommunication::Acquire(Context, CommSvc, Bridge::ProviderPluginId, Bridge::ServiceName,
        Bridge::ServiceVersion, Bridge::ServiceTableRequiredSize, &Lease);
    if (result == D2RL::PluginCommunication::Result::Success && Lease) {
        ReportedFailure = NoFailureYet;  // told again if it is ever lost and fails again
        try {
            LogInfo("Loot Filter Editor: the editor bridge is acquired (version " + Number(Lease.Version()) + ", table " + Number(Lease.Size()) + " bytes).");
        } catch (...) {
        }
        return true;
    }
    if (failure != nullptr) *failure = result;
    const auto code = static_cast<uint32_t>(result);
    if (announce && code != ReportedFailure) {
        ReportedFailure = code;
        try {
            const std::string line = "Loot Filter Editor: the loot filter's editor bridge is not available (needs the loot filter build with the editor bridge). Result " + Number(code) + ".";
            Console(line);
            LogWarn(line);
        } catch (...) {
        }
    }
    return false;
}

// The title (comment) of a rule, "" when it has none or cannot be read.
// Two calls: the first learns the size, the second reads it.
auto RuleTitle(int ruleNumber, bool* tooLong = nullptr) -> std::string {
    if (!Lease || Lease->getRuleTitle == nullptr) return {};
    uint32_t required = 0;
    auto result = Lease->getRuleTitle(ruleNumber, nullptr, 0, &required);
    if (result == Bridge::Result::Success) return {};  // nothing to read
    if (result != Bridge::Result::BufferTooSmall || required <= 1) return {};
    if (required > MaxTitleBytes) {
        if (tooLong != nullptr) *tooLong = true;
        return {};
    }
    std::string buffer(required, '\0');
    result = Lease->getRuleTitle(ruleNumber, buffer.data(), required, &required);
    if (result != Bridge::Result::Success) return {};
    const size_t length = buffer.find('\0');
    if (length != std::string::npos) buffer.resize(length);
    return buffer;
}

// A title that starts with the rule's own id ("R274 | ...", "U003 | ...") names the rule by itself.
auto TitleHasId(const std::string& title) -> bool {
    if (title.size() < 2 || (title[0] != 'R' && title[0] != 'U')) return false;
    std::size_t i = 1;
    while (i < title.size() && title[i] >= '0' && title[i] <= '9') ++i;
    return i > 1 && (i == title.size() || title[i] == ' ' || title[i] == '|');
}

// A rule for people: its title when that starts with its id ("R274 | ..."), otherwise its place in the file
// ("rule #306 <title>"). The bridge's rule numbers are places in the file, not the filter's own R### ids.
auto RuleName(int ruleNumber, const std::string& title, std::size_t maxLength = std::string::npos) -> std::string {
    const std::string text = maxLength == std::string::npos ? title : Shorten(title, maxLength);
    if (TitleHasId(title)) return text;
    return "rule #" + Number(ruleNumber) + (title.empty() ? "" : " " + text);
}

auto RuleLabel(int ruleNumber, const std::string& title) -> std::string { return RuleName(ruleNumber, title, MaxChatTitle); }

// ---------------------------------------------------------------------------
//  The key
// ---------------------------------------------------------------------------
// The four chat lines about a hovered item (lfeditor-hoverchat); the log always gets its line.
std::atomic<bool> HoverChat{false};

// Logs the hovered item (and prints it in chat when HoverChat is on); returns its stats when it is identified.
auto PrintItem(const Bridge::HoveredItem& item, bool chat) -> std::vector<esrfe::ui::ItemStat> {
    const std::string name = Text(item.name);
    const std::string code = Text(item.code);

    // Titles of the matched rules (only as many as the list really holds).
    const uint32_t listed = std::min<uint32_t>(item.matchedCount, Bridge::MaxMatchedRules);
    std::string fullRules;       // for the log: every rule, no cutting
    std::string chatRules;       // for the chat: the first few, shortened
    for (uint32_t i = 0; i < listed; ++i) {
        const int number = item.matchedRules[i];
        bool tooLong = false;
        const std::string title = RuleTitle(number, &tooLong);
        if (i > 0) fullRules += "; ";
        fullRules += RuleName(number, title);
        if (tooLong) fullRules += " (title over 512 bytes, not read)";
        if (i < MaxRulesInChat) {
            if (i > 0) chatRules += "; ";
            chatRules += RuleLabel(number, title);
        }
    }
    const uint32_t more = item.matchedCount > MaxRulesInChat ? item.matchedCount - MaxRulesInChat : 0;

    // Line 1: the item.
    std::string line1 = Yc + "4Filter editor: " + Yc + "0" + name + " (" + code + ")" + Dot + QualityName(item.quality);
    if (item.baseTier >= 0) line1 += Dot + TierName(item.baseTier);
    line1 += Dot + "ilvl " + Number(item.itemLevel);
    if (item.sockets > 0) line1 += Dot + Number(item.sockets) + (item.sockets == 1 ? " socket" : " sockets");
    if (item.ethereal != 0) line1 += Dot + "ethereal";
    if (item.chronicle == 1) line1 += Dot + "not in Chronicle";
    else if (item.chronicle == 2) line1 += Dot + "in Chronicle";
    if (item.identified == 0) line1 += Dot + "unidentified";

    // The item's stats: a count in chat, every entry in the log.
    std::vector<Bridge::ItemStat> stats(128);
    uint32_t statCount = 0;
    Bridge::Result statsResult = Bridge::Result::Failed;
    if (Lease && Lease->getItemStats != nullptr) {
        statsResult = Lease->getItemStats(item.unitId, stats.data(), static_cast<uint32_t>(stats.size()), &statCount);
        if (statsResult == Bridge::Result::BufferTooSmall) {
            stats.resize(statCount);
            statsResult = Lease->getItemStats(item.unitId, stats.data(), static_cast<uint32_t>(stats.size()), &statCount);
        }
    }
    if (statsResult != Bridge::Result::Success) statCount = 0;
    stats.resize(std::min<std::size_t>(statCount, stats.size()));
    std::string statText;
    for (const Bridge::ItemStat& stat : stats) {
        if (!statText.empty()) statText += "; ";
        statText += Number(static_cast<uint32_t>(stat.stat)) + "," + Number(stat.param) + "=" + std::to_string(stat.value)
            + " (stored " + std::to_string(stat.stored) + ")";
    }

    // Line 2: shown or hidden.
    std::string line2;
    std::string hiddenText;  // for the log
    if (item.hiddenBy > 0) {
        const std::string title = RuleTitle(item.hiddenBy);
        line2 = Yc + "1Hidden by " + RuleName(item.hiddenBy, title, MaxChatTitle);
        hiddenText = "hidden by " + RuleName(item.hiddenBy, title);
    } else if (item.hiddenBy == -1) {
        line2 = Yc + "1Hidden: no rule matched and the filter hides unmatched items";
        hiddenText = "hidden: no rule matched and the filter hides unmatched items";
    } else {
        line2 = Yc + "2Shown";
        hiddenText = "shown";
    }

    // Line 3: the rules that match.
    std::string line3;
    if (item.matchedCount == 0) {
        line3 = Yc + "0No rule matches this item.";
    } else {
        line3 = Yc + "0Rules: " + chatRules;
        if (more > 0) line3 += " +" + Number(more) + " more";
    }

    if (chat && Lease && Lease->printChat != nullptr) {
        (void)Lease->printChat(line1.c_str());
        (void)Lease->printChat(line2.c_str());
        (void)Lease->printChat(line3.c_str());
        const std::string line4 = Yc + "0Stats: " + (statsResult != Bridge::Result::Success ? std::string("not read (") + ResultName(statsResult) + ")"
            : item.identified == 0 ? std::string("none, unidentified") : Number(statCount) + " (listed in the log)");
        (void)Lease->printChat(line4.c_str());
    }

    // One full line in the log, nothing cut.
    std::string log = "Loot Filter Editor: hovered item: name=\"" + name + "\" code=" + code + " unitId=" + Number(item.unitId)
        + " quality=" + Number(item.quality) + " baseTier=" + Number(item.baseTier) + " ilvl=" + Number(item.itemLevel)
        + " sockets=" + Number(item.sockets) + "/" + Number(item.maxSockets) + " uniqueOrSetId=" + Number(item.uniqueOrSetId)
        + " ethereal=" + Number(item.ethereal) + " identified=" + Number(item.identified) + " runeword=" + Number(item.runeword)
        + " gold=" + Number(item.gold) + " chronicle=" + Number(item.chronicle) + " hidden=" + Number(item.hidden) + " nameChanged=" + Number(item.nameChanged)
        + " hiddenBy=" + Number(item.hiddenBy) + " (" + hiddenText + ") matched=" + Number(item.matchedCount)
        + " revision=" + std::to_string(item.filterRevision) + " rules=[" + fullRules + "]";
    if (item.matchedCount > listed) log += " (list holds " + Number(listed) + " of " + Number(item.matchedCount) + ")";
    LogInfo(log);
    LogInfo("Loot Filter Editor: hovered item stats (stat,param=tooltip value): " + std::string(ResultName(statsResult)) + " count="
        + Number(statCount) + " [" + statText + "]");
    std::vector<esrfe::ui::ItemStat> out;
    if (item.identified != 0)
        for (const Bridge::ItemStat& stat : stats)
            out.push_back(esrfe::ui::ItemStat{static_cast<int>(stat.stat), static_cast<int>(stat.param), static_cast<int>(stat.value)});
    return out;
}

// The editor, on a new rule for the item: its base, tier and rarity chosen, its stats ticked (defined with the window).
void OpenRuleForItem(const std::string& code, int quality, std::vector<esrfe::ui::ItemStat> stats);

auto __cdecl OnEditItem(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
    if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize)
        || event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }
    try {
        if (!AcquireBridge(true)) return D2RL::Input::ActionResult::Handled;
        if (Lease->getHoveredItem == nullptr) return D2RL::Input::ActionResult::Handled;

        Bridge::HoveredItem item{};
        item.structSize = sizeof(item);
        const Bridge::Result result = Lease->getHoveredItem(&item);
        if (result == Bridge::Result::NoItem || result == Bridge::Result::NotActive) return D2RL::Input::ActionResult::Handled;
        if (result != Bridge::Result::Success) {
            LogWarn(std::string("Loot Filter Editor: getHoveredItem answered ") + ResultName(result) + " (" + Number(static_cast<uint32_t>(result)) + ").");
            return D2RL::Input::ActionResult::Handled;
        }
        std::vector<esrfe::ui::ItemStat> stats = PrintItem(item, HoverChat.load());
        OpenRuleForItem(Text(item.code), static_cast<int>(item.quality), std::move(stats));
    } catch (...) {
    }
    return D2RL::Input::ActionResult::Handled;
}

auto __cdecl HoverChatCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const bool on = !HoverChat.load();
    HoverChat.store(on);
    Console(std::string("lfeditor-hoverchat: the edit-item key ") + (on ? "also prints the item in chat." : "no longer prints the item in chat."));
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
//  Console command: lfeditor
// ---------------------------------------------------------------------------
auto __cdecl EditorCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    try {
        if (InputSvc == nullptr || CommSvc == nullptr) {
            Console("lfeditor: inactive, a D2RLoader service is missing (see the plugin log).");
            return D2RL::ConsoleCommandResult::Handled;
        }
        D2RL::PluginCommunication::Result failure = D2RL::PluginCommunication::Result::Success;
        if (!AcquireBridge(false, &failure)) {
            Console("lfeditor: bridge not acquired. Result " + Number(static_cast<uint32_t>(failure)) + ".");
            Console("It needs the loot filter build with the editor bridge.");
            return D2RL::ConsoleCommandResult::Handled;
        }
        Console("lfeditor: bridge acquired, version " + Number(Lease.Version()) + ", table " + Number(Lease.Size()) + " bytes.");

        if (Lease->getFilterState == nullptr) return D2RL::ConsoleCommandResult::Handled;
        Bridge::FilterState state{};
        state.structSize = sizeof(state);
        const Bridge::Result result = Lease->getFilterState(&state);
        if (result != Bridge::Result::Success) {
            Console(std::string("getFilterState answered ") + ResultName(result) + ".");
            return D2RL::ConsoleCommandResult::Handled;
        }

        Console(std::string("Filter loaded: ") + (state.loaded != 0 ? "yes" : "no") + ", shipped: " + (state.shipped != 0 ? "yes" : "no") + ".");
        if (state.loaded == 0) {
            const std::string failureText = Text(state.failure);
            if (!failureText.empty()) Console("Failure: " + failureText);
        }
        Console("Title: " + Text(state.title));
        Console("Filter id: " + Text(state.filterId));

        std::string levelName = "none";
        const uint32_t levels = std::min<uint32_t>(state.levelCount, Bridge::MaxLevels);
        for (uint32_t i = 0; i < levels; ++i) {
            if (state.levelIds[i] == state.level) levelName = Text(state.levelNames[i]);
        }
        Console("Level: " + Number(state.level) + " (" + levelName + "), " + Number(state.levelCount) + " levels.");
        Console(std::string("Show hidden items: ") + (state.showHidden != 0 ? "on" : "off")
            + ", hover available: " + (state.hoverAvailable != 0 ? "yes" : "no") + ".");
        Console("Rules: " + Number(state.ruleCount) + ", errors: " + Number(state.errors) + ", warnings: " + Number(state.warnings) + ".");
    } catch (...) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
//  The editor window: the browse view over the loaded filter
// ---------------------------------------------------------------------------
namespace ov = esrfe::overlay;

const D2RL::ThreadService* ThreadsSvc{};
const D2RL::WidgetService* WidgetsSvc{};
D2RL::Input::ActionHandle  OpenAction{ D2RL::Input::InvalidHandle };
std::unique_ptr<ov::Overlay> Window;
std::filesystem::path      PluginDir;   // the folder with this DLL (<mod>\d2rloader\plugins)
std::filesystem::path      DataDir;     // PluginDir\esr-filter-editor: images, locales
std::string                Locale;      // the loader's language code, "" for English
// OpenLootFilter messages pass to the game until this time (GetTickCount64), after the editor sent one: the game
// may send it on again while its screen opens, and none of those should bring the editor back.
std::atomic<std::uint64_t> PassNativeUntil{};

auto ThisDllFolder() -> std::filesystem::path {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisDllFolder), &module);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

auto GameFolder() -> std::filesystem::path {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

// <mod>\<mod>.mpq\data: ESR ships the mod as a folder, so its tables are plain files.
auto ModDataFolder() -> std::filesystem::path {
    const std::filesystem::path mod = PluginDir.parent_path().parent_path();
    return mod / (mod.filename().string() + ".mpq") / "data";
}

// ---- the loot filter's folders (for the player's copy of a filter and its changes) ----

auto Utf8(const std::filesystem::path& path) -> std::string {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The loot filter's own default: Saved Games\Diablo II Resurrected\mods\<mod>\LootFilters. A player who set the loot
// filter's user_folder gets the right folder once the editor has seen a filter from it (KnownUserFolder).
auto DefaultUserFolder() -> std::string {
    PWSTR saved = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved)) && saved != nullptr) {
        const std::filesystem::path mod = PluginDir.parent_path().parent_path();
        folder = std::filesystem::path(saved) / L"Diablo II Resurrected" / L"mods" / mod.filename() / L"LootFilters";
    }
    if (saved != nullptr) CoTaskMemFree(saved);
    return folder.empty() ? std::string{} : Utf8(folder);
}

std::mutex  FolderLock;
std::string KnownUserFolder;  // from the filter the loot filter has (repo: and user: filters sit in it)

auto EditorFolders() -> esrfe::edit::Folders {
    esrfe::edit::Folders folders;
    {
        std::lock_guard<std::mutex> lock(FolderLock);
        folders.user = KnownUserFolder;
    }
    if (folders.user.empty()) folders.user = DefaultUserFolder();
    folders.shipped = Utf8(PluginDir.parent_path().parent_path() / L"LootFilters");
    return folders;
}

void LearnUserFolder(const std::string& filterId, const std::string& path) {
    const std::filesystem::path file(std::u8string(path.begin(), path.end()));
    std::filesystem::path folder;
    if (filterId.rfind("repo:", 0) == 0) folder = file.parent_path().parent_path();
    else if (filterId.rfind("user:", 0) == 0) folder = file.parent_path();
    if (folder.empty()) return;
    std::lock_guard<std::mutex> lock(FolderLock);
    KnownUserFolder = Utf8(folder);
}

void __cdecl DoOpenNativeLootFilter(const D2RL::PluginContext*, void*) noexcept;

// ---- what the editor's views ask of the plugin (src/ui/source.h) ----

// The mod's data, read once on a thread of its own when the window starts (about a second), then shared.
std::mutex                                     GameLock;
std::shared_ptr<const gamedata::GameData>      GameData;      // nullptr until read
std::string                                    GameProblem;   // why it could not be read
std::thread                                    GameLoader;
std::weak_ptr<esrfe::ui::BrowseView>           Browse;        // the root view, to refresh it when the data is in

auto EditorLanguage() -> gamedata::Language {
    gamedata::Language language = gamedata::Language::enUS;
    if (!Locale.empty()) gamedata::ParseLanguage(Locale, language);
    return language;
}

void LoadGameData() noexcept {
    // The players' copies first: the launcher updated the authors' filters before the game started.
    try {
        for (const esrfe::edit::CopyUpdate& update : esrfe::edit::UpdateCopies(EditorFolders())) {
            if (!update.error.empty()) LogWarn("Loot Filter Editor: " + update.copy + " could not be brought up to date: " + update.error);
            else LogInfo("Loot Filter Editor: " + update.copy + " is up to date with its author's new version"
                + (update.conflicts > 0 ? " (" + Number(static_cast<uint32_t>(update.conflicts)) + " changes to ask about when the editor opens)" : std::string{}) + ".");
        }
    } catch (...) {
        LogWarn("Loot Filter Editor: bringing the players' copies up to date failed.");
    }
    try {
        const std::filesystem::path data = ModDataFolder();
        auto game = std::make_shared<gamedata::GameData>();
        std::vector<std::string> problems;
        const bool ok = gamedata::GameData::Load((data / "global" / "excel").string(), (data / "local" / "lng" / "strings").string(),
                                                 EditorLanguage(), *game, problems);
        LogInfo("Loot Filter Editor: the mod's data " + std::string(ok ? "is read" : "could NOT be read") + " ("
            + Number(static_cast<uint32_t>(game->loadMilliseconds)) + " ms, " + Number(static_cast<uint32_t>(problems.size())) + " notes).");
        {
            std::lock_guard<std::mutex> lock(GameLock);
            if (ok) GameData = std::move(game);
            else GameProblem = problems.empty() ? "the tables are missing from " + data.string() : problems.front();
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(GameLock);
        GameProblem = "reading it failed";
    }
    if (Window) {
        Window->PostToUi([](ov::Ui& ui) {
            if (auto view = Browse.lock()) view->Refresh(ui);
        });
    }
}

void OpenRuleForItem(const std::string& code, int quality, std::vector<esrfe::ui::ItemStat> stats) {
    if (!Window) {
        LogWarn("Loot Filter Editor: the window is unavailable (see the lines above); the edit-item key does nothing.");
        return;
    }
    Window->Open();
    Window->PostToUi([code, quality, stats = std::move(stats)](ov::Ui& ui) {
        if (auto view = Browse.lock()) {
            view->StartRule(ui, [code, quality, stats](const gamedata::GameData& game) { return esrfe::ui::FromItem(code, quality, stats, game); });
        }
    });
}

class PluginSource final : public esrfe::ui::Source {
public:
    auto Game() -> std::shared_ptr<const gamedata::GameData> override {
        std::lock_guard<std::mutex> lock(GameLock);
        return GameData;
    }
    auto GameDataProblem() -> std::string override {
        std::lock_guard<std::mutex> lock(GameLock);
        return GameProblem;
    }
    auto Language() -> gamedata::Language override { return EditorLanguage(); }

    // On the game's UI thread, where the bridge lives.
    auto Filter() -> esrfe::ui::FilterInfo override {
        esrfe::ui::FilterInfo info;
        if (!AcquireBridge(false, nullptr) || Lease->getFilterState == nullptr) return info;
        Bridge::FilterState state{};
        state.structSize = sizeof(state);
        if (Lease->getFilterState(&state) != Bridge::Result::Success) return info;
        info.bridge = true;
        info.loaded = state.loaded != 0;
        info.shipped = state.shipped != 0;
        info.filterId = Text(state.filterId);
        info.path = Text(state.path);
        LearnUserFolder(info.filterId, info.path);
        info.title = Text(state.title);
        info.failure = Text(state.failure);
        info.level = state.level;
        info.textHash = state.textHash;
        const uint32_t levels = std::min<uint32_t>(state.levelCount, Bridge::MaxLevels);
        for (uint32_t i = 0; i < levels; ++i) info.levels.push_back({state.levelIds[i], Text(state.levelNames[i])});
        return info;
    }

    auto ReadFile(const std::string& path, std::string& out) -> bool override {
        std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        if (!file) return false;
        std::stringstream text;
        text << file.rdbuf();
        out = text.str();
        return !file.bad();
    }

    auto Folders() -> esrfe::edit::Folders override { return EditorFolders(); }

    void OpenGameSettings() override {
        if (Window) Window->Close();
        if (ThreadsSvc == nullptr || ThreadsSvc->runOnUiThread(Context, &DoOpenNativeLootFilter, nullptr) != D2RL::Threads::Result::Success)
            LogWarn("Loot Filter Editor: the game's loot filter screen could not be opened.");
    }
};

// The open key, as bound in Controls. It closes the window again while the window has the keyboard.
auto MatchesOpenKey(unsigned int virtualKey) -> bool {
    if (InputSvc == nullptr || OpenAction == D2RL::Input::InvalidHandle) return false;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    for (auto slot : { D2RL::Input::BindingSlot::Primary, D2RL::Input::BindingSlot::Secondary }) {
        D2RL::Input::Binding binding{};
        if (InputSvc->getBinding(Context, OpenAction, slot, &binding) != D2RL::Input::Result::Success) continue;
        if (static_cast<unsigned int>(binding.key) != virtualKey) continue;
        switch (binding.modifier) {
            case D2RL::Input::Modifier::None:    if (!shift && !control && !alt) return true; break;
            case D2RL::Input::Modifier::Shift:   if (shift && !control && !alt) return true; break;
            case D2RL::Input::Modifier::Control: if (control && !shift && !alt) return true; break;
            case D2RL::Input::Modifier::Alt:     if (alt && !shift && !control) return true; break;
            default: break;
        }
    }
    return false;
}

void StartWindow() {
    if (ThreadsSvc == nullptr) {
        LogWarn("Loot Filter Editor: no thread service; the window is unavailable.");
        return;
    }
    ov::DebugSink() = [](const std::string& line) { LogInfo("Loot Filter Editor: " + line); };
    Locale = ov::ReadLoaderLocale(GameFolder(), PluginDir);
    ov::LoadUiStrings(ov::UiStringsPath(DataDir, Locale));
    ov::OverlayConfig config;
    config.dataDir = DataDir;
    config.title = "Loot Filter Editor";
    config.keyboardLocale = Locale;
    config.theme.keyboardHelp = false;  // no key legend under the window
    config.isCloseKey = &MatchesOpenKey;
    config.queueGameUi = ov::GameThreadQueue(Context, ThreadsSvc, ov::GameThread::Ui);
    config.queueGameLogic = ov::GameThreadQueue(Context, ThreadsSvc, ov::GameThread::Game);
    Window = std::make_unique<ov::Overlay>();
    auto source = std::make_shared<PluginSource>();
    if (!Window->Start(config, [source] {
            auto view = std::make_shared<esrfe::ui::BrowseView>(source);
            Browse = view;
            return view;
        })) {
        LogError("Loot Filter Editor: the window could not be created (see the lines above).");
        Window.reset();
        return;
    }
    GameLoader = std::thread(&LoadGameData);
    if (ov::inputblock::Install([] { return Window != nullptr && Window->IsOpen(); })) {
        LogInfo("Loot Filter Editor: controller block installed.");
    } else {
        LogWarn("Loot Filter Editor: controller block not installed (this game build does not match, or another plugin has it).");
    }
}

void StopWindow() noexcept {
    try {
        ov::inputblock::Remove();
        if (GameLoader.joinable()) GameLoader.join();  // it posts to the window when it is done
        if (Window) Window->Stop();
        Window.reset();
    } catch (...) {
    }
}

auto __cdecl OnOpenEditor(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
    if (event == nullptr || !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize)
        || event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (Window) Window->Toggle();
    return D2RL::Input::ActionResult::Handled;
}

auto __cdecl OpenCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    if (!Window) {
        Console("lfeditor-open: the window is unavailable (see the plugin log).");
        return D2RL::ConsoleCommandResult::Handled;
    }
    Window->Toggle();
    return D2RL::ConsoleCommandResult::Handled;
}

// The milestone 2 demo of every control, pushed over the browse view (for checking the toolkit in game).
auto __cdecl DemoCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    if (!Window) {
        Console("lfeditor-demo: the window is unavailable (see the plugin log).");
        return D2RL::ConsoleCommandResult::Handled;
    }
    Window->PostToUi([](ov::Ui& ui) { ui.PushView(std::make_shared<ov::DemoView>()); });
    Window->Open();
    return D2RL::ConsoleCommandResult::Handled;
}

// Opens the game's own loot filter screen, which the pause menu's Loot Filter button now leaves to the editor.
void __cdecl DoOpenNativeLootFilter(const D2RL::PluginContext*, void*) noexcept {
    if (WidgetsSvc == nullptr) return;
    PassNativeUntil.store(GetTickCount64() + 1500);
    const D2RL::Widgets::UiAction action{ D2RL::Widgets::UiActionSize, 0, "PausePanelMessage", "OpenLootFilter", "" };
    const auto result = WidgetsSvc->dispatchUiAction(Context, &action);
    LogInfo("Loot Filter Editor: asked the game to open its loot filter screen (result " + Number(static_cast<uint32_t>(result)) + ").");
    if (result != D2RL::Widgets::Result::Success) {
        PassNativeUntil.store(0);
        LogWarn("Loot Filter Editor: the game's loot filter screen could not be opened.");
    }
}

auto __cdecl GameFilterCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    if (WidgetsSvc == nullptr || ThreadsSvc == nullptr) {
        Console("lfeditor-gamefilter: unavailable (no widget or thread service).");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (ThreadsSvc->runOnUiThread(Context, &DoOpenNativeLootFilter, nullptr) != D2RL::Threads::Result::Success) {
        Console("lfeditor-gamefilter: could not be queued.");
    }
    return D2RL::ConsoleCommandResult::Handled;
}

// Milestone 2 check: reads the active filter and the mod's data from their real places, as the editor will.
auto __cdecl SelfTestCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    try {
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point from) { return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count()); };
        // The filter: its path from the loot filter, through the bridge.
        if (AcquireBridge(false, nullptr) && Lease->getFilterState != nullptr) {
            Bridge::FilterState state{};
            state.structSize = sizeof(state);
            if (Lease->getFilterState(&state) == Bridge::Result::Success && state.loaded != 0) {
                const std::string pathText = Text(state.path);
                std::filesystem::path path(std::u8string(pathText.begin(), pathText.end()));
                std::ifstream file(path, std::ios::binary);
                std::stringstream text;
                text << file.rdbuf();
                const std::string input = text.str();
                const auto start = Clock::now();
                document::Document doc;
                std::vector<document::Diag> diags;
                const bool ok = document::Document::Parse(input, doc, diags);
                const std::string took = ms(start);
                Console("Filter " + pathText + ": " + (ok ? "read" : "NOT read") + ", " + Number(static_cast<uint32_t>(doc.Rules().size()))
                    + " rules, " + Number(static_cast<uint32_t>(input.size())) + " bytes, " + took + " ms, saved back "
                    + (ok && doc.Text() == input ? "byte for byte" : "DIFFERENTLY") + ".");
                for (std::size_t i = 0; i < diags.size() && i < 3; ++i) Console("  " + diags[i].message);
            } else {
                Console("Filter: no filter is loaded.");
            }
        } else {
            Console("Filter: the loot filter's editor bridge is not available.");
        }
        // The mod's data.
        const std::filesystem::path data = ModDataFolder();
        gamedata::Language language = gamedata::Language::enUS;
        if (!Locale.empty()) gamedata::ParseLanguage(Locale, language);
        gamedata::GameData game;
        std::vector<std::string> problems;
        const auto start = Clock::now();
        const bool loaded = gamedata::GameData::Load((data / "global" / "excel").string(), (data / "local" / "lng" / "strings").string(),
                                                     language, game, problems);
        Console("Mod data " + data.string() + ": " + (loaded ? "read" : "NOT read") + " in " + ms(start) + " ms, "
            + Number(static_cast<uint32_t>(game.bases.All().size())) + " bases, " + Number(static_cast<uint32_t>(game.uniques.All().size()))
            + " uniques, language " + std::string(gamedata::LanguageCode(language)) + ".");
        if (const gamedata::Base* bow = game.bases.Find("m01")) {
            Console("  m01: " + game.BaseName("m01", language) + (bow->mythical ? ", Mythical" : ", NOT Mythical"));
        }
        LogInfo("Loot Filter Editor: self test: " + Number(static_cast<uint32_t>(problems.size())) + " data notes.");
        for (const std::string& note : problems) LogInfo("Loot Filter Editor:   data: " + note);
    } catch (...) {
        Console("lfeditor-selftest: failed with an exception.");
        return D2RL::ConsoleCommandResult::Failed;
    }
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
//  UI message log (lfeditor-uilog): finds the message a game button sends
// ---------------------------------------------------------------------------
// Off until the command turns it on. Every UI message the game sends is
// written to the log as target:command:text and always passed on unchanged.
const D2RL::SharedEventService* EventsSvc{};
D2RL::SharedEvents::ListenerHandle UiListener{};
bool                               UiLogOn{};

auto __cdecl OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
    -> D2RL::SharedEvents::UiMessageAction {
    if (UiLogOn && event != nullptr) {
        try {
            LogInfo(std::string("Loot Filter Editor: UI message ") + (event->target != nullptr ? event->target : "")
                + ":" + (event->command != nullptr ? event->command : "") + ":" + (event->text != nullptr ? event->text : ""));
        } catch (...) {
        }
    }
    // The pause menu's Loot Filter button opens the editor instead of the game's screen.
    if (event != nullptr && event->target != nullptr && event->command != nullptr
        && std::strcmp(event->target, "PausePanelMessage") == 0 && std::strcmp(event->command, "OpenLootFilter") == 0) {
        if (GetTickCount64() < PassNativeUntil.load()) {
            LogInfo("Loot Filter Editor: OpenLootFilter passed to the game.");
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        if (Window) {
            LogInfo("Loot Filter Editor: OpenLootFilter opens the editor.");
            Window->Open();
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
    }
    return D2RL::SharedEvents::UiMessageAction::Continue;
}

void RegisterUiLog() {
    if (Context->QueryService(&EventsSvc) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceField(EventsSvc, D2RL::SharedEventServiceRequiredSize)) {
        EventsSvc = nullptr;
        LogWarn("Loot Filter Editor: no shared event service; lfeditor-uilog is unavailable.");
        return;
    }
    const D2RL::SharedEvents::UiMessageListener listener{ D2RL::SharedEvents::UiMessageListenerSize, 0, 0, 0, &OnUiMessage, nullptr };
    if (EventsSvc->registerUiMessageListener(Context, &listener, &UiListener) != D2RL::SharedEvents::Result::Success) {
        LogWarn("Loot Filter Editor: the UI message listener was refused; lfeditor-uilog is unavailable.");
    }
}

auto __cdecl UiLogCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr) return D2RL::ConsoleCommandResult::Failed;
    UiLogOn = !UiLogOn;
    Console(UiLogOn ? "lfeditor-uilog: on. Every UI message goes to the plugin log until you run it again."
                    : "lfeditor-uilog: off.");
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
//  Loading
// ---------------------------------------------------------------------------
auto QueryServices() -> bool {
    if (Context->QueryService(&InputSvc) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInputServiceField(InputSvc, D2RL::InputServiceRequiredSize)) {
        InputSvc = nullptr;
        LogError("Loot Filter Editor: the input service is missing; the plugin stays inactive.");
        return false;
    }
    if (Context->QueryService(&CommSvc) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasPluginCommunicationServiceField(CommSvc, D2RL::PluginCommunicationServiceRequiredSize)) {
        CommSvc = nullptr;
        LogError("Loot Filter Editor: the plugin communication service is missing; the plugin stays inactive.");
        return false;
    }
    return true;
}

auto RegisterAction(const char* logicalId, const char* displayName, D2RL::Input::ActionCallback callback,
                    D2RL::Input::ActionHandle* out = nullptr) -> bool {
    const D2RL::Input::ActionRegistration registration{
        .structSize       = D2RL::Input::ActionRegistrationSize,
        .flags            = 0,
        .logicalId        = logicalId,
        .displayName      = displayName,
        .category         = "Loot Filter",  // same group as the loot filter's own keys
        .defaultPrimary   = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .defaultSecondary = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .callback         = callback,
        .userData         = nullptr,
    };
    D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
    const D2RL::Input::Result result = InputSvc->registerAction(Context, &registration, &handle);
    if (result != D2RL::Input::Result::Success || handle == D2RL::Input::InvalidHandle) {
        LogError(std::string("Loot Filter Editor: the Controls entry \"") + displayName + "\" was refused (result " + Number(static_cast<uint32_t>(result)) + ").");
        return false;
    }
    if (out != nullptr) *out = handle;
    return true;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "sully.loot-filter-editor",
    .name        = "Loot Filter Editor",
    .version     = "0.5.0",
    .author      = "Sully",
    .description = "In-game loot filter editor (milestone 2: the window and its readers).",
    .flags       = D2RL::PluginFlags::Client,
    .reserved    = {},
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr) return false;
    try {
        if (!Context->RegisterConsoleCommand("lfeditor", &EditorCommand, "Loot filter editor: bridge and filter state.")) {
            LogWarn("Loot Filter Editor: the console command could not be registered.");
        }
        if (!Context->RegisterConsoleCommand("lfeditor-uilog", &UiLogCommand, "Loot filter editor: log every UI message (on/off).")) {
            LogWarn("Loot Filter Editor: the lfeditor-uilog command could not be registered.");
        }
        RegisterUiLog();
        (void)Context->RegisterConsoleCommand("lfeditor-open", &OpenCommand, "Loot filter editor: open or close the window.");
        (void)Context->RegisterConsoleCommand("lfeditor-gamefilter", &GameFilterCommand, "Loot filter editor: open the game's own loot filter screen.");
        (void)Context->RegisterConsoleCommand("lfeditor-demo", &DemoCommand, "Loot filter editor: open the controls demo.");
        (void)Context->RegisterConsoleCommand("lfeditor-hoverchat", &HoverChatCommand, "Loot filter editor: the edit-item key also prints the item in chat (on/off).");
        (void)Context->RegisterConsoleCommand("lfeditor-selftest", &SelfTestCommand, "Loot filter editor: read the active filter and the mod's data.");
        // Without both services we stay inert but let the game run.
        if (!QueryServices()) return true;
        if (!RegisterAction("loot-filter-editor-edit-item", "Edit Hovered Item (Filter Editor)", &OnEditItem)) {
            LogWarn("Loot Filter Editor: the Controls key is unavailable.");
            return true;
        }
        if (!RegisterAction("loot-filter-editor-open", "Open Filter Editor", &OnOpenEditor, &OpenAction)) {
            LogWarn("Loot Filter Editor: the open key is unavailable; use lfeditor-open.");
        }
        if (Context->QueryService(&ThreadsSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadsSvc, D2RL::ThreadServiceRequiredSize)) {
            ThreadsSvc = nullptr;
        }
        if (Context->QueryService(&WidgetsSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasWidgetServiceField(WidgetsSvc, D2RL::WidgetServiceRequiredSize)) {
            WidgetsSvc = nullptr;
        }
        PluginDir = ThisDllFolder();
        DataDir = PluginDir / "esr-filter-editor";
        StartWindow();
        LogInfo("Loot Filter Editor 0.2.0: ready. Bind \"Open Filter Editor\" and \"Edit Hovered Item (Filter Editor)\" in Controls.");
    } catch (...) {
        LogError("Loot Filter Editor: loading failed with an exception.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    StopWindow();
    if (EventsSvc != nullptr && UiListener != D2RL::SharedEvents::ListenerHandle{}) {
        (void)EventsSvc->unregisterUiMessageListener(Context, UiListener);
    }
    EventsSvc = nullptr;
    // Give the bridge back so the loot filter can unload.
    (void)Lease.Reset();
    InputSvc = nullptr;
    CommSvc  = nullptr;
}
