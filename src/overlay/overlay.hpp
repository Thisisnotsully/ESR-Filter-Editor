// The in-game window of the filter editor, taken from the ESR Wiki overlay: a layered Win32 window over the game, on
// its own UI thread, sized from the game's client area, with the wiki's look, its drawn cursor, controller support,
// on-screen keyboard and help window. It knows nothing about filters: it hosts Views (view.hpp) built from the toolkit
// (widgets.hpp). No Windows headers here.
//
// Threads
//   * Start, Stop: the plugin's load / unload thread.
//   * Open, Close, Toggle, IsOpen, PostToUi: any thread.
//   * Views, widgets and everything they own live on the overlay's UI thread: the root view is made there (the
//     factory passed to Start runs there), and posted work runs there.
//   * Game work: from the UI thread, Ui::RunOnGame queues work on D2RLoader's UI or game thread with the callbacks in
//     OverlayConfig (game_thread.hpp makes them from the ThreadService) and brings the result back to the UI thread.
//
// While the window shows
//   * A nearly clear window over the game's client area takes the mouse, so the game sees no clicks; the overlay
//     window holds the keyboard focus, so the game sees no keys (as the wiki).
//   * The controller is read by the overlay itself (gamepad.cpp). To keep the game from also acting on it, install
//     the input block (input_block.hpp) from the plugin with IsOpen as its predicate, as the wiki's plugin does.
//   * When another program comes to the front the window hides, and shows again when the game is back in front.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "theme.hpp"
#include "ui.hpp"
#include "view.hpp"

namespace esrfe::overlay {

struct OverlayConfig {
    // The plugin's data folder: images\background.png (Theme::backgroundImage), images\controller\xbox|ps4\*.png (the
    // help window's button sprites), controller.txt ("xbox" or "playstation" forces the help's controller kind).
    std::filesystem::path dataDir;
    std::string title = "Loot Filter Editor";  // the window's name (the header shows the view's title)
    std::string instanceId = "main";           // keeps the window class names unique
    Theme theme;
    std::string keyboardLocale;  // the loader's language code (deDE ...): the on-screen keyboard's layout; "" English
    // The key bound to the open action; it closes the window while the window has the keyboard (the game's actions
    // do not fire then). Runs on the UI thread.
    std::function<bool(unsigned int virtualKey)> isCloseKey;
    // Queue work on D2RLoader's UI thread / game thread. Called on the overlay's UI thread; false when refused.
    std::function<bool(std::function<void()>)> queueGameUi;
    std::function<bool(std::function<void()>)> queueGameLogic;
    // Optional: called on the UI thread when the window is opened (true) or closed (false) by the player.
    std::function<void(bool open)> onOpenChanged;
};

using ViewFactory = std::function<std::shared_ptr<View>()>;

struct OverlayHost;  // the window and its state, in overlay_host.cpp

class Overlay {
public:
    Overlay();
    ~Overlay();
    Overlay(const Overlay&) = delete;
    auto operator=(const Overlay&) -> Overlay& = delete;

    // Starts the UI thread and creates the (hidden) window; `rootView` is called on that thread. False when the
    // window cannot be created (the log says why).
    auto Start(OverlayConfig config, ViewFactory rootView) -> bool;
    // Closes the window and ends the UI thread. Posted work that has not run is dropped. Safe to call twice.
    void Stop();

    void Open();
    void Close();
    void Toggle();
    // True while the window is showing (the input block's predicate).
    [[nodiscard]] auto IsOpen() const -> bool;
    // Runs `work` on the UI thread with the top view's Ui. False when the overlay is not running.
    auto PostToUi(std::function<void(Ui&)> work) -> bool;

private:
    std::unique_ptr<OverlayHost> impl_;
};

}  // namespace esrfe::overlay
