// random.cpp —— 确定性随机原语实现（契约见 random.hpp 文件头）。

#include "trogue/random.hpp"

namespace tg {

namespace {

inline constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

inline std::uint64_t rotl(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
}

}  // namespace

std::uint64_t hash_u64(std::uint64_t x) noexcept {
    std::uint64_t z = x + kGoldenGamma;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

Random::Random(std::uint64_t seed) noexcept {
    for (int k = 0; k < 4; ++k)
        s_[k] = hash_u64(seed +
                         static_cast<std::uint64_t>(k) * kGoldenGamma);
}

std::uint64_t Random::next_u64() noexcept {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
}

int Random::next_int(int lo, int hi) noexcept {
    // 拒绝采样（无模偏差）：范围 r ≤ 2^32（int 全域）。取原始流的 32 位
    // 高字，拒绝 ≥ limit（limit = 不超过 2^32 的最大 r 倍数）后取模——
    // [0, limit) 恰为 limit/r 个完整周期，取模无偏差。不用 128 位乘法
    //（__int128 触发 -Wpedantic 告警，破坏零告警基线）。
    const std::uint64_t r =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) -
                                   static_cast<std::int64_t>(lo)) + 1;
    const std::uint64_t limit = 0x100000000ULL - 0x100000000ULL % r;
    std::uint32_t x;
    do {
        x = static_cast<std::uint32_t>(next_u64() >> 32);
    } while (static_cast<std::uint64_t>(x) >= limit);
    return lo + static_cast<int>(static_cast<std::uint64_t>(x) % r);
}

double Random::next_double() noexcept {
    return static_cast<double>(next_u64() >> 11) *
           (1.0 / 9007199254740992.0);  // 1/2^53，编译期常量
}

bool Random::next_bool(double p) noexcept {
    if (p <= 0.0) return false;
    if (p >= 1.0) return true;
    return next_double() < p;
}

}  // namespace tg
