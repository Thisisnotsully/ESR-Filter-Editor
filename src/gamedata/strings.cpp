#include "strings.h"

#include "table.h"

#include <algorithm>
#include <filesystem>

namespace gamedata {

namespace {

constexpr std::array<std::string_view, LanguageCount> Codes = {
    "enUS", "deDE", "esES", "frFR", "itIT", "koKR", "plPL",
    "ruRU", "zhCN", "zhTW", "esMX", "jaJP", "ptBR",
};

auto AppendUtf8(std::string& out, unsigned cp) -> void {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Just enough JSON for the string files: an array of flat objects. Other
// values (numbers, nested things) are skipped.
class JsonReader {
public:
    explicit JsonReader(std::string_view text) : s_(text) {}

    auto Ok() const -> bool { return ok_; }
    auto Where() const -> std::size_t { return i_; }

    auto SkipSpace() -> void {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
    }
    auto Peek() -> char {
        SkipSpace();
        return i_ < s_.size() ? s_[i_] : '\0';
    }
    auto Eat(char c) -> bool {
        if (Peek() != c) return false;
        ++i_;
        return true;
    }
    auto Expect(char c) -> void {
        if (!Eat(c)) ok_ = false;
    }

    auto String(std::string& out) -> void {
        out.clear();
        if (!Eat('"')) { ok_ = false; return; }
        while (i_ < s_.size()) {
            // Plain runs in one go.
            std::size_t end = i_;
            while (end < s_.size() && s_[end] != '"' && s_[end] != '\\') ++end;
            out.append(s_.data() + i_, end - i_);
            i_ = end;
            if (i_ >= s_.size()) break;
            const char c = s_[i_++];
            if (c == '"') return;
            if (i_ >= s_.size()) break;
            const char e = s_[i_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = Hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        const unsigned low = Hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out += e; break;  // \" \\ \/
            }
        }
        ok_ = false;
    }

    // Skips any value.
    auto Skip() -> void {
        const char c = Peek();
        std::string dummy;
        if (c == '"') {
            String(dummy);
        } else if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i_;
            if (Eat(close)) return;
            do {
                if (c == '{') { String(dummy); Expect(':'); }
                Skip();
            } while (ok_ && Eat(','));
            Expect(close);
        } else {
            while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']') ++i_;
        }
    }

private:
    auto Hex4() -> unsigned {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            if (i_ >= s_.size()) { ok_ = false; return 0; }
            const char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<unsigned>(h - 'A' + 10);
            else ok_ = false;
        }
        return v;
    }

    std::string_view s_;
    std::size_t i_ = 0;
    bool ok_ = true;
};

auto LanguageIndex(std::string_view code) -> int {
    for (int i = 0; i < LanguageCount; ++i)
        if (Codes[static_cast<std::size_t>(i)] == code) return i;
    return -1;
}

const std::string Empty;

}  // namespace

auto LanguageCode(Language language) -> std::string_view {
    const int i = static_cast<int>(language);
    return i >= 0 && i < LanguageCount ? Codes[static_cast<std::size_t>(i)] : Codes[0];
}

auto ParseLanguage(std::string_view code, Language& out) -> bool {
    const int i = LanguageIndex(code);
    if (i < 0) return false;
    out = static_cast<Language>(i);
    return true;
}

auto Strings::LoadFolder(const std::string& folder, std::vector<std::string>& problems) -> bool {
    std::error_code ec;
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path().string());
    }
    if (ec) {
        problems.push_back("cannot list " + folder);
        return false;
    }
    if (files.empty()) {
        problems.push_back("no .json string files in " + folder);
        return false;
    }
    std::sort(files.begin(), files.end());
    bool ok = true;
    for (const auto& f : files) ok = LoadFile(f, problems) && ok;
    return ok;
}

auto Strings::LoadFile(const std::string& path, std::vector<std::string>& problems) -> bool {
    std::string text;
    if (!ReadFile(path, text)) {
        problems.push_back("cannot read " + path);
        return false;
    }
    std::string_view view(text);
    if (view.size() >= 3 && view.substr(0, 3) == "\xEF\xBB\xBF") view.remove_prefix(3);

    JsonReader json(view);
    json.Expect('[');
    if (json.Eat(']')) return true;
    std::string name, value, key;
    std::array<std::string, LanguageCount> texts;
    do {
        json.Expect('{');
        key.clear();
        for (auto& t : texts) t.clear();
        if (!json.Eat('}')) {
            do {
                json.String(name);
                json.Expect(':');
                if (json.Peek() == '"') {
                    json.String(value);
                    if (name == "Key") {
                        key = value;
                    } else if (const int lang = LanguageIndex(name); lang >= 0) {
                        texts[static_cast<std::size_t>(lang)] = std::move(value);
                    }
                } else {
                    json.Skip();
                }
            } while (json.Ok() && json.Eat(','));
            json.Expect('}');
        }
        if (!json.Ok()) break;
        if (key.empty()) continue;
        const auto it = index_.find(std::string_view(key));
        if (it != index_.end()) {
            texts_[it->second] = std::move(texts);
        } else {
            index_.emplace(key, texts_.size());
            texts_.push_back(std::move(texts));
        }
    } while (json.Eat(','));
    json.Expect(']');
    if (!json.Ok()) {
        problems.push_back(path + ": bad JSON near byte " + std::to_string(json.Where()));
        return false;
    }
    return true;
}

auto Strings::Has(std::string_view key) const -> bool { return index_.find(key) != index_.end(); }

auto Strings::Find(std::string_view key, Language language) const -> const std::string* {
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    const auto& texts = texts_[it->second];
    const auto& text = texts[static_cast<std::size_t>(language)];
    return text.empty() ? &texts[0] : &text;
}

auto Strings::Text(std::string_view key, Language language) const -> const std::string& {
    const std::string* text = Find(key, language);
    return text != nullptr ? *text : Empty;
}

auto Strings::StripColors(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        // "ÿc" is C3 BF 63; the code is the next character (ASCII in practice).
        if (i + 2 < text.size() && text[i] == '\xC3' && text[i + 1] == '\xBF' && text[i + 2] == 'c') {
            i += 3;
            if (i < text.size()) {
                const auto c = static_cast<unsigned char>(text[i]);
                std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                i += len;
            }
            continue;
        }
        out += text[i++];
    }
    return out;
}

auto Strings::Plain(std::string_view text) -> std::string {
    std::string out = StripColors(text);
    // Grammar tags: "[xx]" with 1-3 lowercase letters at the start.
    const auto tagLength = [&](std::size_t at) -> std::size_t {
        if (at >= out.size() || out[at] != '[') return 0;
        std::size_t k = at + 1;
        while (k < out.size() && k - at <= 4 && out[k] >= 'a' && out[k] <= 'z') ++k;
        return (k < out.size() && out[k] == ']' && k > at + 1) ? k - at + 1 : 0;
    };
    if (const std::size_t first = tagLength(0); first > 0) {
        out.erase(0, first);
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (tagLength(i) > 0) {
                out.erase(i);
                break;
            }
        }
    }
    return out;
}

}  // namespace gamedata
