// From the ESR Wiki overlay (overlay.cpp: LoadUiStrings, UiText, UiDataText, UiFormat; plugin.cpp: LoaderLocale),
// moved to UTF-8 and std::filesystem so it builds and is tested without Windows.
#include "ui_text.hpp"

#include <fstream>
#include <set>
#include <sstream>

#include "debug_log.hpp"

namespace esrfe::overlay {
namespace {

std::map<std::string, std::string> gUiStrings;  // English text -> the loader language's text
std::set<std::string> gUiValues;  // the translations themselves (text already in the language is not 'missing')
std::string gKeyboardLocale;

auto ReadFile(const std::filesystem::path& path, std::string& out) -> bool {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();
    return true;
}

}  // namespace

auto ParseUiStrings(std::string_view content) -> std::map<std::string, std::string> {
    std::map<std::string, std::string> strings;
    if (content.size() >= 3 && content.compare(0, 3, "\xEF\xBB\xBF") == 0) content.remove_prefix(3);
    while (!content.empty()) {
        const std::size_t end = content.find('\n');
        std::string_view line = content.substr(0, end);
        content = end == std::string_view::npos ? std::string_view() : content.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::size_t tab = line.find('\t');
        if (tab == 0 || tab == std::string_view::npos) continue;
        strings[std::string(line.substr(0, tab))] = std::string(line.substr(tab + 1));
    }
    return strings;
}

void SetUiStrings(std::map<std::string, std::string> strings) {
    gUiStrings = std::move(strings);
    gUiValues.clear();
    for (const auto& [english, text] : gUiStrings) gUiValues.insert(text);
}

auto LoadUiStrings(const std::filesystem::path& path) -> std::size_t {
    SetUiStrings({});
    if (path.empty()) return 0;
    std::string content;
    if (!ReadFile(path, content)) return 0;
    SetUiStrings(ParseUiStrings(content));
    return gUiStrings.size();
}

auto UiText(std::string_view english) -> std::string {
    const auto found = gUiStrings.find(std::string(english));
    if (found != gUiStrings.end()) return found->second;
    if (!gUiStrings.empty()) {  // a language is loaded: say which labels it has no text for
        bool plainEnglish = true;
        for (const char c : english) {
            if (static_cast<unsigned char>(c) > 127) plainEnglish = false;
        }
        if (gUiValues.count(std::string(english)) != 0) plainEnglish = false;  // already a translation
        if (plainEnglish && !english.empty())
            DebugLogOnce("ui-missing:" + std::string(english), "no translation for UI text: " + std::string(english));
    }
    return std::string(english);
}

auto UiDataText(std::string_view text) -> std::string {
    const auto found = gUiStrings.find(std::string(text));
    return found != gUiStrings.end() ? found->second : std::string(text);
}

auto FormatArgs(std::string text, const std::vector<std::string>& args) -> std::string {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string token = "{" + std::to_string(i) + "}";
        for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + args[i].size())) {
            text.replace(at, token.size(), args[i]);
        }
    }
    return text;
}

auto UiFormat(std::string_view english, const std::vector<std::string>& args) -> std::string {
    return FormatArgs(UiText(english), args);
}

auto LocaleFromLoaderToml(std::string_view toml) -> std::string {
    static const char* const kLocales[] = {"deDE", "esES", "frFR", "itIT", "koKR", "plPL", "ruRU", "zhCN",
                                           "zhTW", "esMX", "jaJP", "ptBR"};
    // The key at the start of a line (the wiki looks for "\ntext_locale"; a first line counts too).
    std::size_t key = toml.rfind("text_locale", 0) == 0 ? 0 : toml.find("\ntext_locale");
    if (key == std::string_view::npos) return "";
    const std::size_t open = toml.find('"', key);
    const std::size_t lineEnd = toml.find('\n', key + 1);
    if (open == std::string_view::npos || (lineEnd != std::string_view::npos && open > lineEnd)) return "";
    const std::size_t close = toml.find('"', open + 1);
    if (close == std::string_view::npos) return "";
    const std::string_view code = toml.substr(open + 1, close - open - 1);
    for (const char* known : kLocales) {
        if (code == known) return std::string(code);
    }
    return "";
}

auto ReadLoaderLocale(const std::filesystem::path& gameDir, const std::filesystem::path& pluginDir) -> std::string {
    // The game's own loader config (the one the in-game settings menu writes), next to the game's executable. The
    // plugin's folder has its own copy for mods, so the plugin folder is only the fallback.
    std::string toml;
    if (!ReadFile(gameDir / "d2rloader" / "config" / "d2rloader.toml", toml) &&
        !ReadFile(pluginDir / ".." / "config" / "d2rloader.toml", toml)) {
        return "";
    }
    return LocaleFromLoaderToml(toml);
}

auto UiStringsPath(const std::filesystem::path& dataDir, const std::string& locale) -> std::filesystem::path {
    if (locale.empty()) return {};
    return dataDir / "locales" / locale / "ui-strings.txt";
}

void SetKeyboardLocale(const std::string& code) { gKeyboardLocale = code; }
auto KeyboardLocale() -> const std::string& { return gKeyboardLocale; }

}  // namespace esrfe::overlay
