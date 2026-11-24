#include "utf.hpp"

namespace esrfe::overlay {
namespace {

// Decodes one code point at `i`, moving `i` past it.
auto NextCodePoint(std::string_view s, std::size_t& i) -> char32_t {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char lead = byte(i);
    if (lead < 0x80) {
        ++i;
        return lead;
    }
    int extra = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) { extra = 1; value = lead & 0x1F; minimum = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; value = lead & 0x0F; minimum = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; value = lead & 0x07; minimum = 0x10000; }
    else {
        ++i;
        return 0xFFFD;
    }
    if (i + static_cast<std::size_t>(extra) >= s.size()) {  // not enough bytes left
        ++i;
        return 0xFFFD;
    }
    for (int k = 1; k <= extra; ++k) {
        const unsigned char next = byte(i + static_cast<std::size_t>(k));
        if ((next & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        value = (value << 6) | (next & 0x3F);
    }
    i += static_cast<std::size_t>(extra) + 1;
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0xFFFD;
    return value;
}

void AppendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

void AppendUtf16(std::u16string& out, char32_t c) {
    if (c < 0x10000) {
        out += static_cast<char16_t>(c);
    } else {
        c -= 0x10000;
        out += static_cast<char16_t>(0xD800 + (c >> 10));
        out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
    }
}

}  // namespace

auto Utf8ToUtf16(std::string_view utf8) -> std::u16string {
    std::u16string out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) AppendUtf16(out, NextCodePoint(utf8, i));
    return out;
}

auto Utf16ToUtf8(std::u16string_view utf16) -> std::string {
    std::string out;
    out.reserve(utf16.size());
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        const char16_t unit = utf16[i];
        if (IsHighSurrogate(unit) && i + 1 < utf16.size() && IsLowSurrogate(utf16[i + 1])) {
            const char32_t c = 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                               (static_cast<char32_t>(utf16[i + 1]) - 0xDC00);
            AppendUtf8(out, c);
            ++i;
        } else if (IsHighSurrogate(unit) || IsLowSurrogate(unit)) {
            AppendUtf8(out, 0xFFFD);
        } else {
            AppendUtf8(out, unit);
        }
    }
    return out;
}

auto WideToUtf8(std::wstring_view wide) -> std::string {
    if constexpr (sizeof(wchar_t) == 2) {
        std::u16string units(wide.begin(), wide.end());
        return Utf16ToUtf8(units);
    } else {
        std::string out;
        for (wchar_t c : wide) {
            const auto point = static_cast<char32_t>(c);
            AppendUtf8(out, point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF) ? 0xFFFD : point);
        }
        return out;
    }
}

auto Utf8ToWide(std::string_view utf8) -> std::wstring {
    std::wstring out;
    if constexpr (sizeof(wchar_t) == 2) {
        const std::u16string units = Utf8ToUtf16(utf8);
        out.assign(units.begin(), units.end());
    } else {
        for (std::size_t i = 0; i < utf8.size();) out += static_cast<wchar_t>(NextCodePoint(utf8, i));
    }
    return out;
}

}  // namespace esrfe::overlay
