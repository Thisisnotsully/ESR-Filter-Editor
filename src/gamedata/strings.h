// The game's string files (data/local/lng/strings/*.json), all 13 languages.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

enum class Language : int {
    enUS, deDE, esES, frFR, itIT, koKR, plPL, ruRU, zhCN, zhTW, esMX, jaJP, ptBR,
};
constexpr int LanguageCount = 13;

// "enUS", "deDE", ...
auto LanguageCode(Language language) -> std::string_view;

// False if the code is not one of the 13.
auto ParseLanguage(std::string_view code, Language& out) -> bool;

class Strings {
public:
    // Reads every *.json file of the folder, in name order. A key that comes
    // again replaces the earlier text (ESR has no such clash).
    auto LoadFolder(const std::string& folder, std::vector<std::string>& problems) -> bool;
    auto LoadFile(const std::string& path, std::vector<std::string>& problems) -> bool;

    auto Has(std::string_view key) const -> bool;
    auto Count() const -> std::size_t { return texts_.size(); }

    // The text in the language, the enUS text where that one is empty, an
    // empty string for an unknown key. Raw: with color codes and grammar tags.
    auto Text(std::string_view key, Language language) const -> const std::string&;

    // Same, but nullptr for an unknown key.
    auto Find(std::string_view key, Language language) const -> const std::string*;

    // Without color codes ("ÿc" and one character) and grammar tags: of
    // "[ms]a[fs]b" only "a" is kept, a leading "[pl]" is dropped.
    static auto Plain(std::string_view text) -> std::string;

    // Only the color codes removed.
    static auto StripColors(std::string_view text) -> std::string;

private:
    struct Hash {
        using is_transparent = void;
        auto operator()(std::string_view s) const -> std::size_t { return std::hash<std::string_view>{}(s); }
    };
    std::unordered_map<std::string, std::size_t, Hash, std::equal_to<>> index_;
    std::vector<std::array<std::string, LanguageCount>> texts_;
};

}  // namespace gamedata
