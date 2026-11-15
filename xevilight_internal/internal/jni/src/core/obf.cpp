#include "core/obf.hpp"

namespace xev::obf {
void decode(const std::uint8_t* src, std::size_t n, std::uint32_t seed, char* out) {
    const std::uint32_t k0 = mix(seed), k1 = mix(seed ^ 0x85ebca6bu);
    for (std::size_t i = 0; i + 8 <= n; i += 8) {
        std::uint32_t a = 0, b = 0;
        for (unsigned j = 0; j < 8; ++j) {
            const auto pos = static_cast<std::uint32_t>(i + j);
            auto v = static_cast<std::uint8_t>(src[i + j] - stream(seed ^ 0x5A5A5A5Au, pos));
            v ^= stream(seed ^ 0xA5A5A5A5u, pos);
            v = ror(v, 3);
            if (j < 4) a |= std::uint32_t(v) << (8 * j);
            else b |= std::uint32_t(v) << (8 * (j - 4));
        }
        decBlock(a, b, k0, k1);
        a ^= mix(std::uint32_t(i) + 0x51ed270bu);
        b ^= mix(std::uint32_t(i) + 0x1b56c4e9u);
        for (unsigned j = 0; j < 8; ++j)
            out[i + j] = static_cast<char>((j < 4 ? a >> (8 * j) : b >> (8 * (j - 4))) & 0xff);
    }
}
}
