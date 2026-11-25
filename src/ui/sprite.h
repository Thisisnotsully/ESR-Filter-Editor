// D2R .sprite pictures (the loot filter's minimap icons), read for the editor's previews. Standard C++ only.
//
// The file: "SpA1", a 16-bit version, the width (16 bits at 6) and height (32 bits at 8), a 40-byte header in all,
// then width x height pixels of RGBA, 8 bits each (the first frame; icons have one).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esrfe::ui {

struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;  // width x height, as the overlay's Canvas::Pixels takes it
};

// Reads `path` (UTF-8), made smaller (averaged) so no side is over `maxSide`. False when it is not a sprite.
auto ReadSprite(const std::string& path, int maxSide, Picture& out) -> bool;

}  // namespace esrfe::ui
