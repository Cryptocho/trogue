#pragma once
// random.hpp —— 确定性随机原语：坐标哈希与种子化伪随机流。
//
// 边界：本模块只提供算法原语（位混演、均匀采样、洗牌）；「用随机数做什么」
// （生成策略、阈值、分布形状、触发时机）归调用方。引擎不持有任何全局随机
// 状态——确定性要求消费方显式拥有并管理种子，多个系统共享一个流会互相扰动。
//
// 可复现契约：同 seed 同调用序列逐位同输出，跨平台/编译器一致。实现只含
// 整数运算与编译期常量浮点乘法（无 libm、无 std:: 随机设施——其分布算法
// 为实现定义，不满足跨平台确定性）。算法在此钉死，属于契约的一部分：
//   - 哈希：splitmix64（常量 0x9E3779B97F4A7C15 / 0xBF58476D1CE4E5B9 /
//     0x94D049BB133111EB）；
//   - 流式：xoshiro256**（4×uint64 状态；更新 s[2]^=s[0]; s[3]^=s[1];
//     s[1]^=s[2]; s[0]^=s[3]; s[2]^=(s[1]<<17); s[3]=rotl(s[3],45)；
//     输出 rotl(s[1]*5,7)*9）。
// 替换算法即改变输出序列（既有黄金序列失效）——属破坏性变更。
//
// 前置条件违反（next_int 的 lo>hi、pick 的空容器）= 程序错误：不设断言、
// 不抛异常，行为未定义（与 SceneAsset::layer(i) 的契约注释 + 直接索引同
// 策略；引入 assert 会造成 NDEBUG 下 Debug/Release 行为分叉，破坏确定性
// 契约的单一表述）。

#include <cstdint>
#include <type_traits>
#include <utility>   // std::swap（shuffle）
#include <vector>

namespace tg {

// splitmix64：单输入 64 位雪崩混演。空间噪声、按坐标取值等场景的确定性
// 地基。纯函数，同输入恒同输出。
std::uint64_t hash_u64(std::uint64_t x) noexcept;

// 多键组合哈希：h = seed；对每个 part 依序 h = hash_u64(h ^ hash_u64(part))。
// part 须为整数类型（static_assert 防指针/浮点静默转换）；顺序敏感
//（键顺序不同 → 结果不同）；无 part 时原样返回 seed。
template <typename... Ts>
std::uint64_t hash_combine(std::uint64_t seed, Ts... parts) noexcept {
    static_assert((std::is_integral_v<Ts> && ...),
                  "hash_combine parts must be integral");
    std::uint64_t h = seed;
    // void 转换使赋值表达式成为合法折叠操作数（且空 pack 实例化无告警）
    ((void)(h = hash_u64(h ^ hash_u64(static_cast<std::uint64_t>(parts)))),
     ...);
    return h;
}

// 种子化 PRNG（xoshiro256**，算法见文件头契约）。可复制值类型：拷贝 =
// 复制状态，两实例此后输出相同序列（用复制表达「存档/回放某刻的随机状态」）。
class Random {
public:
    // splitmix64 连续扩散 seed 为 4 个状态字。状态不可能全零：四个输入
    //（seed + k·golden gamma，k=0..3）互不相同（gamma 为奇数），splitmix64
    // 为 64 位双射 → 四个输出互异 → 全零状态数学上不可达（非概率保证）。
    explicit Random(std::uint64_t seed) noexcept;

    std::uint64_t next_u64() noexcept;  // 原始 64 位流
    // 闭区间 [lo, hi] 均匀整数，无模偏差（拒绝采样）。前置：lo <= hi。
    int next_int(int lo, int hi) noexcept;
    // [0,1)，53 位精度：(u64 >> 11) × (1/2^53)。1 不可达。
    double next_double() noexcept;
    // 以概率 p 返回 true；p<=0 恒 false、p>=1 恒 true（边界短路，无舍入歧义）。
    bool next_bool(double p) noexcept;

    // 均匀选取一个元素。前置：v 非空；v.size() ≤ INT_MAX。
    template <typename T>
    const T& pick(const std::vector<T>& v) noexcept {
        return v[static_cast<std::size_t>(
            next_int(0, static_cast<int>(v.size()) - 1))];
    }

    // Fisher–Yates 原地均匀洗牌（消费 next_int）。多重集保持；同状态同结果。
    // 前置：v.size() ≤ INT_MAX。
    template <typename T>
    void shuffle(std::vector<T>& v) noexcept {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1],
                      v[static_cast<std::size_t>(next_int(0, static_cast<int>(i) - 1))]);
    }

private:
    std::uint64_t s_[4];
};

}  // namespace tg
