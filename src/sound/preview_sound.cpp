#include "preview_sound.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// miniaudio (public domain or MIT-0, third_party/miniaudio): decoding and playback only.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_VORBIS
#define MINIAUDIO_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace esrfe::sound {
namespace {

struct Request {
    std::wstring path;
    int volume = 100;
};

std::mutex Lock;
std::condition_variable Signal;
std::optional<Request> Next;  // only the latest: previews replace each other
std::thread Worker;
bool Started = false;
bool Stopping = false;
LogFn Log = nullptr;

void Say(const std::string& line) {
    if (Log != nullptr) Log(line);
}

void Run() noexcept {
    ma_engine engine{};
    const bool ready = ma_engine_init(nullptr, &engine) == MA_SUCCESS;
    if (!ready) Say("Loot Filter Editor: no sound device for previews.");
    ma_sound sound{};
    bool loaded = false;
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(Lock);
            // Wake for a request, or now and then to free a sound that has ended.
            Signal.wait_for(lock, std::chrono::milliseconds(500), [] { return Stopping || Next.has_value(); });
            if (Stopping) break;
            if (!Next) {
                if (loaded && ma_sound_at_end(&sound)) {
                    ma_sound_uninit(&sound);
                    loaded = false;
                }
                continue;
            }
            request = std::move(*Next);
            Next.reset();
        }
        if (!ready) continue;
        if (loaded) {
            ma_sound_uninit(&sound);  // stops it
            loaded = false;
        }
        if (ma_sound_init_from_file_w(&engine, request.path.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                      nullptr, &sound) != MA_SUCCESS) {
            Say("Loot Filter Editor: a sound could not be read.");
            continue;
        }
        loaded = true;
        ma_sound_set_volume(&sound, static_cast<float>(std::clamp(request.volume, 0, 100)) / 100.0f);
        if (ma_sound_start(&sound) != MA_SUCCESS) {
            ma_sound_uninit(&sound);
            loaded = false;
        }
    }
    if (loaded) ma_sound_uninit(&sound);
    if (ready) ma_engine_uninit(&engine);
}

}  // namespace

void SetLog(LogFn log) noexcept { Log = log; }

void Play(const std::wstring& path, int volume) noexcept {
    try {
        std::lock_guard<std::mutex> lock(Lock);
        if (Stopping) return;
        if (!Started) {
            Worker = std::thread(Run);
            Started = true;
        }
        Next = Request{path, volume};
        Signal.notify_one();
    } catch (...) {
    }
}

void Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(Lock);
        if (!Started) return;
        Stopping = true;
    }
    Signal.notify_one();
    if (Worker.joinable()) Worker.join();
}

}  // namespace esrfe::sound
