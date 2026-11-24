// UTF-8 <-> UTF-16 without Windows. Text is UTF-8 everywhere in the editor; the overlay converts to UTF-16 at the
// Win32 edge (on Windows wchar_t is UTF-16, so std::wstring and std::u16string hold the same units).
// Invalid UTF-8 bytes become U+FFFD; a lone surrogate in UTF-16 becomes U+FFFD in UTF-8.
#pragma once

#include <string>
#include <string_view>

namespace esrfe::overlay {

auto Utf8ToUtf16(std::string_view utf8) -> std::u16string;
auto Utf16ToUtf8(std::u16string_view utf16) -> std::string;
// wchar_t text (pad_logic.hpp's key labels): UTF-16 on Windows, UTF-32 elsewhere.
auto WideToUtf8(std::wstring_view wide) -> std::string;
auto Utf8ToWide(std::string_view utf8) -> std::wstring;

// Code point helpers for UTF-16 positions.
constexpr auto IsHighSurrogate(char16_t c) -> bool { return c >= 0xD800 && c <= 0xDBFF; }
constexpr auto IsLowSurrogate(char16_t c) -> bool { return c >= 0xDC00 && c <= 0xDFFF; }

}  // namespace esrfe::overlay
