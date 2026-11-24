// What the editor's views ask of the plugin: the loaded filter (through the loot filter's editor bridge), the mod's
// data (read once, in the background), files, and the game's own loot filter screen. The plugin implements it; the
// tests fake it. Every call is made on the overlay's UI thread, except Filter: on the game's UI thread.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gamedata.h"

namespace esrfe::ui {

struct LevelInfo {
    std::int64_t id = 0;
    std::string name;
};

// The loot filter's view of its filter (editor_bridge_api.h FilterState).
struct FilterInfo {
    bool bridge = false;   // the loot filter answered (false: the loot filter plugin is missing or too old)
    bool loaded = false;   // a filter is loaded
    bool shipped = false;  // the file ships with the mod
    std::string path;      // UTF-8
    std::string title;
    std::string failure;   // why it did not load
    std::int64_t level = 0;  // the level the game uses, 0 when the filter has none
    std::vector<LevelInfo> levels;
    std::uint64_t textHash = 0;
};

class Source {
public:
    virtual ~Source() = default;
    // The mod's data, or nullptr while it is still being read (or when it could not be: GameDataProblem says why).
    virtual auto Game() -> std::shared_ptr<const gamedata::GameData> = 0;
    virtual auto GameDataProblem() -> std::string = 0;
    virtual auto Language() -> gamedata::Language = 0;
    virtual auto Filter() -> FilterInfo = 0;
    virtual auto ReadFile(const std::string& path, std::string& out) -> bool = 0;
    // Closes the editor and opens the game's own loot filter screen.
    virtual void OpenGameSettings() = 0;
};

}  // namespace esrfe::ui
