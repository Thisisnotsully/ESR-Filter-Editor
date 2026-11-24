// Plays a sound file for the editor's previews (a rule's drop sound, the filter volume's test), the way the loot
// filter plays it: miniaudio, on a thread of its own. One sound at a time: a new one stops the one still playing.
#pragma once

#include <string>

namespace esrfe::sound {

// `volume` 0-100. Returns at once; a file that cannot be read is skipped (logged through `log` when set).
void Play(const std::wstring& path, int volume) noexcept;
void Stop() noexcept;  // at shutdown: ends the thread

using LogFn = void (*)(const std::string& line);
void SetLog(LogFn log) noexcept;

}  // namespace esrfe::sound
