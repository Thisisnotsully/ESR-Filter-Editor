#include "table.h"

#include <charconv>
#include <cstdio>

namespace gamedata {

auto ReadFile(const std::string& path, std::string& out) -> bool {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buffer[65536];
    for (;;) {
        const std::size_t n = std::fread(buffer, 1, sizeof buffer, f);
        if (n == 0) break;
        out.append(buffer, n);
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

auto IsUtf8(std::string_view text) -> bool {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        int more = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0 && c >= 0xC2) more = 1;
        else if ((c & 0xF0) == 0xE0) more = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) more = 3;
        else return false;
        if (i + static_cast<std::size_t>(more) >= text.size()) return false;
        for (int k = 1; k <= more; ++k)
            if ((static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80) return false;
        i += static_cast<std::size_t>(more) + 1;
    }
    return true;
}

auto Latin1ToUtf8(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out += ch;
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

auto ParseInt(std::string_view text, int& out) -> bool {
    while (!text.empty() && (text.front() == ' ' || text.front() == '"')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '"')) text.remove_suffix(1);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return false;
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) return false;
    out = value;
    return true;
}

namespace {

auto SplitTabs(std::string_view line) -> std::vector<std::string_view> {
    std::vector<std::string_view> cells;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            cells.push_back(line.substr(start));
            break;
        }
        cells.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return cells;
}

auto BaseName(const std::string& path) -> std::string {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

auto Table::Load(const std::string& path, std::vector<std::string>& problems) -> bool {
    name_ = BaseName(path);
    header_.clear();
    rows_.clear();
    if (!ReadFile(path, text_)) {
        problems.push_back("cannot read " + path);
        return false;
    }
    if (text_.size() >= 3 && text_.compare(0, 3, "\xEF\xBB\xBF") == 0) text_.erase(0, 3);
    if (!IsUtf8(text_)) text_ = Latin1ToUtf8(text_);

    std::string_view all(text_);
    bool first = true;
    std::size_t start = 0;
    while (start < all.size()) {
        std::size_t end = all.find('\n', start);
        if (end == std::string_view::npos) end = all.size();
        std::string_view line = all.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            header_ = SplitTabs(line);
            first = false;
            continue;
        }
        if (line.empty()) continue;
        auto cells = SplitTabs(line);
        if (cells[0] == "Expansion") continue;
        rows_.push_back(std::move(cells));
    }
    if (header_.empty()) {
        problems.push_back(name_ + ": no header");
        return false;
    }
    return true;
}

auto Table::Col(std::string_view name) const -> int {
    for (std::size_t i = 0; i < header_.size(); ++i)
        if (header_[i] == name) return static_cast<int>(i);
    return -1;
}

auto Table::NeedCol(std::string_view name, std::vector<std::string>& problems) const -> int {
    const int col = Col(name);
    if (col < 0) problems.push_back(name_ + ": no column '" + std::string(name) + "'");
    return col;
}

auto Table::Cell(int row, int col) const -> std::string_view {
    if (row < 0 || row >= Rows() || col < 0) return {};
    const auto& cells = rows_[static_cast<std::size_t>(row)];
    if (col >= static_cast<int>(cells.size())) return {};
    return cells[static_cast<std::size_t>(col)];
}

auto Table::Int(int row, int col, int fallback) const -> int {
    int value = 0;
    return ParseInt(Cell(row, col), value) ? value : fallback;
}

}  // namespace gamedata
