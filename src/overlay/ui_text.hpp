// The overlay's own labels in the loader's language, as the ESR Wiki overlay does it: locales\<code>\ui-strings.txt,
// one "English<TAB>text" line each (UTF-8). Text with no translation is shown in English. UiFormat replaces {0}, {1},
// ... with the arguments.
//
// Threads: load the strings before Overlay::Start; after that the table is only read (by the UI thread).
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace esrfe::overlay {

// Reads "English<TAB>text" lines. Lines without a tab, or starting with one, are skipped; CR and a UTF-8 BOM are
// dropped.
auto ParseUiStrings(std::string_view content) -> std::map<std::string, std::string>;

// Replaces the table. Returns how many strings it read (0 for an empty path or a missing file: English).
auto LoadUiStrings(const std::filesystem::path& path) -> std::size_t;
void SetUiStrings(std::map<std::string, std::string> strings);

auto UiText(std::string_view english) -> std::string;
// The same lookup, but a text with no entry is normal and not logged (names that come from data).
auto UiDataText(std::string_view text) -> std::string;
auto UiFormat(std::string_view english, const std::vector<std::string>& args) -> std::string;
// Replaces {0}, {1}, ... in `text` (already translated).
auto FormatArgs(std::string text, const std::vector<std::string>& args) -> std::string;

// The loader's language: text_locale in d2rloader.toml. Returns the code (deDE ...), or "" for English, a missing
// key or a code that is not one of the game's languages.
auto LocaleFromLoaderToml(std::string_view toml) -> std::string;
// <game>\d2rloader\config\d2rloader.toml, else <pluginDir>\..\config\d2rloader.toml (as the wiki reads it).
auto ReadLoaderLocale(const std::filesystem::path& gameDir, const std::filesystem::path& pluginDir) -> std::string;
// locales\<code>\ui-strings.txt under `dataDir`, or an empty path for English.
auto UiStringsPath(const std::filesystem::path& dataDir, const std::string& locale) -> std::filesystem::path;

// The loader's language code; the on-screen keyboard's layout follows it. "" is English.
void SetKeyboardLocale(const std::string& code);
auto KeyboardLocale() -> const std::string&;

}  // namespace esrfe::overlay
