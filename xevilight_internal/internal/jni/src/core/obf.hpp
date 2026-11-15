
// ================================================================================
// [Telegram] Bot: @imguisource_bot | Channel: @ImGui_Source_FIXNEY (ImGui Source) | Second: @il2cpp_src (shit src 🧨)
// ================================================================================

//обфускатор жесткий
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace xev::obf {

constexpr std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

constexpr std::uint8_t stream(std::uint32_t seed, std::uint32_t i) {
    return static_cast<std::uint8_t>(mix(seed + i * 0x9e3779b9u) >> 24);
}

constexpr std::uint8_t rol(std::uint8_t b, int r) {
    return static_cast<std::uint8_t>((b << r) | (b >> (8 - r)));
}
constexpr std::uint8_t ror(std::uint8_t b, int r) {
    return static_cast<std::uint8_t>((b >> r) | (b << (8 - r)));
}

constexpr void encBlock(std::uint32_t& a, std::uint32_t& b, std::uint32_t k0, std::uint32_t k1) {
    std::uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        a += (((b << 4) ^ (b >> 5)) + b) ^ (sum + k0);
        sum += 0x9e3779b9u;
        b += (((a << 4) ^ (a >> 5)) + a) ^ (sum + k1);
    }
}

constexpr void decBlock(std::uint32_t& a, std::uint32_t& b, std::uint32_t k0, std::uint32_t k1) {
    std::uint32_t sum = 0x9e3779b9u * 16u;
    for (int i = 0; i < 16; ++i) {
        b -= (((a << 4) ^ (a >> 5)) + a) ^ (sum + k1);
        sum -= 0x9e3779b9u;
        a -= (((b << 4) ^ (b >> 5)) + b) ^ (sum + k0);
    }
}

constexpr std::uint32_t seedOf(std::uint32_t line, std::uint32_t counter) {
    return mix(line * 0x9e3779b9u ^ counter * 0x85ebca6bu ^ 0xC0FFEEu);
}

// Compile-time encrypted payload. The constructor is constexpr, so the bytes are
// baked into .rodata; only the seed-dependent runtime decoder lives in .text.
template <std::size_t N, std::uint32_t Seed>
struct Enc {
    static constexpr std::uint32_t kSeed = Seed;
    static constexpr std::size_t kSize = (N + 7) / 8 * 8;  // full blocks only
    std::array<std::uint8_t, kSize> bytes{};

    constexpr Enc(const char (&s)[N]) {
        const std::uint32_t k0 = mix(Seed), k1 = mix(Seed ^ 0x85ebca6bu);
        for (std::size_t i = 0; i < kSize; i += 8) {
            std::uint32_t a = 0, b = 0;
            for (int j = 0; j < 8; ++j) {
                const std::uint8_t v = (i + j < N) ? static_cast<std::uint8_t>(s[i + j]) : 0;
                if (j < 4) a |= std::uint32_t(v) << (8 * j);
                else b |= std::uint32_t(v) << (8 * (j - 4));
            }
            a ^= mix(std::uint32_t(i) + 0x51ed270bu);
            b ^= mix(std::uint32_t(i) + 0x1b56c4e9u);
            encBlock(a, b, k0, k1);
            for (int j = 0; j < 8; ++j) {
                bytes[i + j] = (j < 4) ? static_cast<std::uint8_t>(a >> (8 * j))
                                       : static_cast<std::uint8_t>(b >> (8 * (j - 4)));
            }
        }
        for (std::size_t i = 0; i < kSize; ++i) {
            std::uint8_t v = bytes[i];
            v = rol(v, 3);
            v ^= stream(Seed ^ 0xA5A5A5A5u, std::uint32_t(i));
            v = static_cast<std::uint8_t>(v + stream(Seed ^ 0x5A5A5A5Au, std::uint32_t(i)));
            bytes[i] = v;
        }
    }
};

// Shared decoder is implemented in core/obf.cpp.
void decode(const std::uint8_t* src, std::size_t n, std::uint32_t seed, char* out);

}  // namespace xev::obf

// Each expansion is a unique lambda, so its ciphertext and plaintext buffer
// belong to this exact call site (no cross-TU collisions).
// The gate is a three-state latch (0 = fresh, 1 = decoding, 2 = ready): two
// threads that reach a call site at the same time never write `out` together.
#define XS(lit)                                                                     \
    ([]() -> const char* {                                                          \
        static constexpr ::xev::obf::Enc<sizeof(lit),                               \
            ::xev::obf::seedOf(__LINE__, __COUNTER__)> enc{lit};                    \
        static char out[decltype(enc)::kSize];                                       \
        static ::std::atomic<unsigned> gate{0};                                     \
        if (gate.load(::std::memory_order_acquire) != 2) {                          \
            unsigned expected = 0;                                                  \
            if (gate.compare_exchange_strong(expected, 1,                           \
                    ::std::memory_order_acq_rel, ::std::memory_order_acquire)) {    \
                ::xev::obf::decode(enc.bytes.data(), decltype(enc)::kSize,          \
                                   decltype(enc)::kSeed, out);                      \
                gate.store(2, ::std::memory_order_release);                         \
            } else {                                                                \
                while (gate.load(::std::memory_order_acquire) != 2) {}              \
            }                                                                       \
        }                                                                           \
        return out;                                                                 \
    }())
