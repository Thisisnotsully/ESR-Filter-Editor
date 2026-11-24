// Loot Filter Editor, milestone 1.
//
// One key reads the ground item under the cursor (or the controller's target)
// through the loot filter's editor bridge and prints what the filter does with
// it. No window yet. The plugin hooks nothing in the game.
//
// The bridge is a service the loot filter plugin offers. This file only uses
// the contract in editor_bridge_api.h.

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/plugin_communication.h>
#include <D2RLPlugin/shared_events.h>

#include <editor_bridge_api.h>

#include <algorithm>
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

// "R<n>" or "R<n> <title>" with the title shortened for chat.
auto RuleLabel(int ruleNumber, const std::string& title) -> std::string {
    std::string label = "R" + Number(ruleNumber);
    if (!title.empty()) label += " " + Shorten(title, MaxChatTitle);
    return label;
}

// ---------------------------------------------------------------------------
//  The key
// ---------------------------------------------------------------------------
void PrintItem(const Bridge::HoveredItem& item) {
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
        fullRules += "R" + Number(number);
        if (!title.empty()) fullRules += " " + title;
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
        line2 = Yc + "1Hidden by R" + Number(item.hiddenBy) + (title.empty() ? "" : ": " + Shorten(title, MaxChatTitle));
        hiddenText = "hidden by R" + Number(item.hiddenBy) + (title.empty() ? "" : ": " + title);
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

    if (Lease && Lease->printChat != nullptr) {
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
}

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
        PrintItem(item);
    } catch (...) {
    }
    return D2RL::Input::ActionResult::Handled;
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

auto RegisterAction(const char* logicalId, const char* displayName, D2RL::Input::ActionCallback callback) -> bool {
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
    return true;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "sully.loot-filter-editor",
    .name        = "Loot Filter Editor",
    .version     = "0.1.0",
    .author      = "Sully",
    .description = "In-game loot filter editor (milestone 1: hovered item test).",
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
        // Without both services we stay inert but let the game run.
        if (!QueryServices()) return true;
        if (!RegisterAction("loot-filter-editor-edit-item", "Edit Hovered Item (Filter Editor)", &OnEditItem)) {
            LogWarn("Loot Filter Editor: the Controls key is unavailable.");
            return true;
        }
        LogInfo("Loot Filter Editor 0.1.0: ready. Bind \"Edit Hovered Item (Filter Editor)\" in Controls.");
    } catch (...) {
        LogError("Loot Filter Editor: loading failed with an exception.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (EventsSvc != nullptr && UiListener != D2RL::SharedEvents::ListenerHandle{}) {
        (void)EventsSvc->unregisterUiMessageListener(Context, UiListener);
    }
    EventsSvc = nullptr;
    // Give the bridge back so the loot filter can unload.
    (void)Lease.Reset();
    InputSvc = nullptr;
    CommSvc  = nullptr;
}
