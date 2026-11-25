#include "sprite.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace esrfe::ui {

namespace {

auto U16(const std::uint8_t* p) -> std::uint32_t { return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8; }
auto U32(const std::uint8_t* p) -> std::uint32_t { return U16(p) | U16(p + 2) << 16; }

}  // namespace

auto ReadSprite(const std::string& path, int maxSide, Picture& out) -> bool {
    out = Picture{};
    std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!file) return false;
    std::uint8_t header[40]{};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || std::memcmp(header, "SpA1", 4) != 0) return false;
    const std::uint32_t width = U16(header + 6);
    const std::uint32_t height = U32(header + 8);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    if (!file.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()))) return false;

    // Smaller: each output pixel the average of the source pixels it covers (colour weighted by alpha).
    const int side = std::max(1, maxSide);
    const double scale = std::min(1.0, static_cast<double>(side) / std::max(width, height));
    out.width = std::max(1, static_cast<int>(width * scale));
    out.height = std::max(1, static_cast<int>(height * scale));
    out.bgra.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    for (int y = 0; y < out.height; ++y) {
        const std::uint32_t y0 = static_cast<std::uint32_t>(static_cast<double>(y) * height / out.height);
        const std::uint32_t y1 = std::max(y0 + 1, static_cast<std::uint32_t>(static_cast<double>(y + 1) * height / out.height));
        for (int x = 0; x < out.width; ++x) {
            const std::uint32_t x0 = static_cast<std::uint32_t>(static_cast<double>(x) * width / out.width);
            const std::uint32_t x1 = std::max(x0 + 1, static_cast<std::uint32_t>(static_cast<double>(x + 1) * width / out.width));
            double r = 0, g = 0, b = 0, a = 0;
            std::uint32_t count = 0;
            for (std::uint32_t sy = y0; sy < y1 && sy < height; ++sy)
                for (std::uint32_t sx = x0; sx < x1 && sx < width; ++sx) {
                    const std::uint8_t* p = &rgba[(static_cast<std::size_t>(sy) * width + sx) * 4];
                    const double alpha = p[3];
                    r += p[0] * alpha;
                    g += p[1] * alpha;
                    b += p[2] * alpha;
                    a += alpha;
                    ++count;
                }
            std::uint8_t* o = &out.bgra[(static_cast<std::size_t>(y) * out.width + x) * 4];
            if (a > 0) {
                o[0] = static_cast<std::uint8_t>(b / a);
                o[1] = static_cast<std::uint8_t>(g / a);
                o[2] = static_cast<std::uint8_t>(r / a);
            }
            o[3] = static_cast<std::uint8_t>(count > 0 ? a / count : 0);
        }
    }
    return true;
}

}  // namespace esrfe::ui
