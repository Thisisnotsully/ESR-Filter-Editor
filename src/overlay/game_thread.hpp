// For plugin.cpp: OverlayConfig::queueGameUi / queueGameLogic from D2RLoader's ThreadService. Header only, so the
// overlay library itself does not depend on the SDK.
//
//   const D2RL::ThreadService* threads = nullptr;
//   if (plugin->QueryService(&threads) == D2RL::ServiceQueryResult::Success &&
//       D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize)) {
//       config.queueGameUi = esrfe::overlay::GameThreadQueue(plugin, threads, esrfe::overlay::GameThread::Ui);
//       config.queueGameLogic = esrfe::overlay::GameThreadQueue(plugin, threads, esrfe::overlay::GameThread::Game);
//   }
//
// Each queued function is boxed on the heap and runs once from the loader's callback, which frees it. The loader drops
// queued work on unload (and game work on a session change) without calling back; those boxes are not freed (a few
// bytes, at unload or a session change only). runOnGameThread is Unavailable on a TCP/IP client: the queue says false.
#pragma once

#include <D2RLPlugin/threads.h>

#include <functional>
#include <memory>

#include "ui.hpp"

namespace esrfe::overlay {

namespace detail {
inline void __cdecl RunQueued(const D2RL::PluginContext*, void* userData) noexcept {
    std::unique_ptr<std::function<void()>> work(static_cast<std::function<void()>*>(userData));
    try {
        if (work && *work) (*work)();
    } catch (...) {
        // nothing may cross back into the loader
    }
}
}  // namespace detail

inline auto GameThreadQueue(const D2RL::PluginContext* plugin, const D2RL::ThreadService* threads, GameThread which)
    -> std::function<bool(std::function<void()>)> {
    return [plugin, threads, which](std::function<void()> work) -> bool {
        if (plugin == nullptr || threads == nullptr || !work) return false;
        const D2RL::Threads::RunFn run = which == GameThread::Ui ? threads->runOnUiThread : threads->runOnGameThread;
        if (run == nullptr) return false;
        auto boxed = std::make_unique<std::function<void()>>(std::move(work));
        if (run(plugin, &detail::RunQueued, boxed.get()) != D2RL::Threads::Result::Success) return false;
        boxed.release();  // the callback owns it now
        return true;
    };
}

}  // namespace esrfe::overlay
