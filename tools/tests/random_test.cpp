// random_test.cpp —— 确定性随机原语单测。
//
// 覆盖：黄金值序列（第二来源 = 独立参考实现生成的硬编码值，锁死算法不漂移）、
// 同/异 seed 确定性、复制语义（状态独立）、next_int 闭区间/无越界/无模偏差
// sanity/极端范围、next_double 值域、next_bool 边界短路、pick 覆盖、
// shuffle 多重集保持/确定性/分布 sanity、draws 原始抽取计数（含拒绝采样与
// 短路边界）、hash 雪崩与组合敏感性。
// 纯公共 API，无窗口、无文件。
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::Random;
using tg::hash_combine;
using tg::hash_u64;

// ── 黄金值（独立 Python 参考实现按算法规范另写生成，2026-09-13 冻结） ──

bool test_golden_hash() {
    CHECK(hash_u64(0) == 0xe220a8397b1dcdafULL);
    CHECK(hash_u64(1) == 0x910a2dec89025cc1ULL);
    CHECK(hash_u64(0xdeadbeefULL) == 0x4adfb90f68c9eb9bULL);
    CHECK(hash_combine(42, 1, 2, 3) == 0x0942686a1d7b898fULL);
    CHECK(hash_combine(42, 3, 2, 1) == 0xc5463ae5ca874924ULL);  // 顺序敏感
    CHECK(hash_combine(42) == 42);  // 无 part → 原样返回 seed
    return true;
}

bool test_golden_stream() {
    // Random(42) 前 8 个 next_u64
    const std::uint64_t want42[8] = {
        0x15780b2e0c2ec716ULL, 0x6104d9866d113a7eULL,
        0xae17533239e499a1ULL, 0xecb8ad4703b360a1ULL,
        0xfde6dc7fe2ec5e64ULL, 0xc50da53101795238ULL,
        0xb82154855a65ddb2ULL, 0xd99a2743ebe60087ULL,
    };
    Random r(42);
    for (const std::uint64_t w : want42) CHECK(r.next_u64() == w);

    // Random(1) / Random(0) 前 4 个（异 seed 序列不同的黄金锚点）
    const std::uint64_t want1[4] = {
        0xb3f2af6d0fc710c5ULL, 0x853b559647364ceaULL,
        0x92f89756082a4514ULL, 0x642e1c7bc266a3a7ULL,
    };
    Random r1(1);
    for (const std::uint64_t w : want1) CHECK(r1.next_u64() == w);

    const std::uint64_t want0[4] = {
        0x99ec5f36cb75f2b4ULL, 0xbf6e1f784956452aULL,
        0x1a5f849d4933e6e0ULL, 0x6aa594f1262d2d2cULL,
    };
    Random r0(0);
    for (const std::uint64_t w : want0) CHECK(r0.next_u64() == w);
    return true;
}

bool test_golden_derived() {
    // next_int(0,3) 与 next_double：种子 7 的派生方法黄金值
    const int want_int[12] = {3, 2, 1, 3, 0, 3, 2, 3, 2, 0, 2, 0};
    Random ri(7);
    for (const int w : want_int) CHECK(ri.next_int(0, 3) == w);

    // 浮点为 IEEE754 确定运算（53 位整数 × 2^-53），可逐位对照
    const double want_d[6] = {
        0.7005764821796896, 0.2787512294737843, 0.8396274618764198,
        0.9810977250149351, 0.9908602788330683, 0.872773938745132,
    };
    Random rd(7);
    for (const double w : want_d) CHECK(rd.next_double() == w);
    return true;
}

// ── 确定性与复制语义 ──

bool test_determinism_and_copy() {
    // 同 seed 两实例序列逐位一致（跨全方法）
    Random a(123), b(123);
    for (int i = 0; i < 64; ++i) CHECK(a.next_u64() == b.next_u64());
    Random c(123), d(123);
    for (int i = 0; i < 32; ++i) CHECK(c.next_int(-5, 5) == d.next_int(-5, 5));
    for (int i = 0; i < 32; ++i) CHECK(c.next_double() == d.next_double());
    for (int i = 0; i < 32; ++i) CHECK(c.next_bool(0.3) == d.next_bool(0.3));

    // 不同 seed 序列不同（31 组，各比 8 个值的异或和，防单值碰撞误判）
    for (std::uint64_t s = 1; s < 32; ++s) {
        Random x(s), y(s + 1);
        std::uint64_t xs = 0, ys = 0;
        for (int i = 0; i < 8; ++i) {
            xs ^= x.next_u64();
            ys ^= y.next_u64();
        }
        CHECK(xs != ys);
    }

    // 复制语义：拷贝后序列一致；推进原实例不影响副本（副本停在拷贝时刻）
    Random orig(9), copy = orig;
    for (int i = 0; i < 16; ++i) CHECK(orig.next_u64() == copy.next_u64());
    orig.next_u64();
    orig.next_u64();
    Random fresh(9);  // 与拷贝同处第 16 步：下一值应相同（不受 orig 推进影响）
    for (int i = 0; i < 16; ++i) fresh.next_u64();
    CHECK(copy.next_u64() == fresh.next_u64());
    return true;
}

// ── next_int ──

bool test_next_int() {
    Random r(777);
    // 闭区间端点可达 + 无越界
    bool saw_lo = false, saw_hi = false;
    for (int i = 0; i < 100000; ++i) {
        const int v = r.next_int(2, 7);
        if (v < 2 || v > 7) { CHECK(false); return true; }
        saw_lo = saw_lo || v == 2;
        saw_hi = saw_hi || v == 7;
    }
    CHECK(saw_lo);
    CHECK(saw_hi);

    // lo==hi 恒返回该值
    for (int i = 0; i < 100; ++i) CHECK(r.next_int(5, 5) == 5);

    // 均匀性 sanity：6 值 10 万次，单桶 1/6≈16667；宽松阈值 [14700, 18700]
    {
        std::vector<int> buckets(6, 0);
        for (int i = 0; i < 100000; ++i) ++buckets[r.next_int(0, 5)];
        for (const int b : buckets) CHECK(b >= 14700 && b <= 18700);
    }

    // 极端范围不崩溃、在域内
    for (int i = 0; i < 1000; ++i) {
        const int v = r.next_int(-2147483647 - 1, 2147483647);
        CHECK(v >= -2147483647 - 1 && v <= 2147483647);
    }
    return true;
}

// ── next_double / next_bool ──

bool test_next_double_bool() {
    Random r(2024);
    double mn = 1.0, mx = 0.0;
    for (int i = 0; i < 100000; ++i) {
        const double v = r.next_double();
        if (!(v >= 0.0 && v < 1.0)) { CHECK(false); return true; }
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    CHECK(mx < 1.0);
    CHECK(mn < 0.01);  // 近 0 可达（宽松下界）

    // 边界短路：p=0 恒 false、p=1 恒 true
    for (int i = 0; i < 1000; ++i) {
        CHECK(r.next_bool(0.0) == false);
        CHECK(r.next_bool(-0.5) == false);
        CHECK(r.next_bool(1.0) == true);
        CHECK(r.next_bool(1.5) == true);
    }
    // p=0.5 频率 sanity
    int hits = 0;
    for (int i = 0; i < 100000; ++i) hits += r.next_bool(0.5) ? 1 : 0;
    CHECK(hits >= 48000 && hits <= 52000);
    return true;
}

// ── draws() 原始抽取计数 ──

bool test_draws_counting() {
    // next_u64 逐次 +1
    Random r(99);
    CHECK(r.draws() == 0);
    for (std::uint64_t i = 1; i <= 10; ++i) {
        r.next_u64();
        CHECK(r.draws() == i);
    }

    // 高层 API 的消耗粒度为原始抽取：next_double 恰好 1；
    // next_bool 边界短路 0、非短路 1
    {
        Random b(5);
        const std::uint64_t d0 = b.draws();
        b.next_double();
        CHECK(b.draws() == d0 + 1);
        const std::uint64_t d1 = b.draws();
        CHECK(b.next_bool(0.0) == false);
        CHECK(b.next_bool(1.0) == true);
        CHECK(b.next_bool(-0.5) == false);
        CHECK(b.next_bool(1.5) == true);
        CHECK(b.draws() == d1);  // 短路零消耗
        b.next_bool(0.5);
        CHECK(b.draws() == d1 + 1);
    }

    // 拒绝采样必须计入原始抽取（distinguishing case）：
    // next_int(INT_MIN, 0) 的 r = 2^31+1、limit = 2^31+1 → 拒绝率 ≈ 1/2。
    // 同 seed 孪生实例复刻拒绝循环独立计 raw，断言 draws() == raw——
    // 若 draws() 误计为「高层 API 调用次数」则其值 = n 而 raw ≈ 2n，断言失败。
    {
        const int lo = std::numeric_limits<int>::min();
        const int hi = 0;
        const int n = 200;
        Random a(2024), b(2024);
        for (int i = 0; i < n; ++i) a.next_int(lo, hi);

        const std::uint64_t r =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) -
                                       static_cast<std::int64_t>(lo)) + 1;
        const std::uint64_t limit = 0x100000000ULL - 0x100000000ULL % r;
        std::uint64_t raw = 0;
        for (int i = 0; i < n; ++i) {
            std::uint32_t x;
            do {
                x = static_cast<std::uint32_t>(b.next_u64() >> 32);
                ++raw;
            } while (static_cast<std::uint64_t>(x) >= limit);
        }
        CHECK(a.draws() == raw);
        CHECK(raw > static_cast<std::uint64_t>(n));  // 确实发生了拒绝
    }

    // 拷贝 = 复制状态与计数；原实例推进不影响副本
    {
        Random orig(7);
        for (int i = 0; i < 5; ++i) orig.next_u64();
        Random copy = orig;
        CHECK(copy.draws() == orig.draws());
        orig.next_u64();
        orig.next_u64();
        CHECK(copy.draws() == 5);
        copy.next_u64();
        CHECK(copy.draws() == 6 && orig.draws() == 7);
    }
    return true;
}

// ── pick / shuffle ──

bool test_pick() {
    Random r(55);
    const std::vector<int> v{10, 20, 30, 40, 50};
    std::vector<int> hits(5, 0);
    for (int i = 0; i < 50000; ++i) {
        const int x = r.pick(v);
        const auto it = std::find(v.begin(), v.end(), x);
        if (it == v.end()) { CHECK(false); return true; }
        ++hits[static_cast<std::size_t>(it - v.begin())];
    }
    for (const int h : hits) CHECK(h > 0);  // 全下标可达
    return true;
}

bool test_shuffle() {
    Random r(88);
    // 多重集保持 + 确定性
    std::vector<int> v(64);
    std::iota(v.begin(), v.end(), 0);
    std::vector<int> sorted_before = v;
    r.shuffle(v);
    std::sort(v.begin(), v.end());
    CHECK(v == sorted_before);

    std::vector<int> a(32);
    std::iota(a.begin(), a.end(), 100);
    std::vector<int> b = a;
    Random ra(5), rb(5);
    ra.shuffle(a);
    rb.shuffle(b);
    CHECK(a == b);  // 同状态同结果

    // 首元素分布 sanity：64 元素 64000 次洗牌，元素 0 居首位 ≈ 1/64 = 1000 次
    std::vector<int> base(64);
    std::iota(base.begin(), base.end(), 0);
    int first0 = 0;
    for (int i = 0; i < 64000; ++i) {
        std::vector<int> w = base;
        r.shuffle(w);
        if (w.front() == 0) ++first0;
    }
    CHECK(first0 > 700 && first0 < 1300);
    return true;
}

// ── 哈希雪崩 / 敏感性 ──

bool test_hash_avalanche() {
    // 单 bit 翻转 → 输出约半数位变化（宽松阈值 [16, 48]/64）
    for (const std::uint64_t base : {0x1234567890abcdefULL, 1ULL, 0ULL,
                                     0xffffffffffffffffULL}) {
        for (int bit = 0; bit < 64; bit += 7) {  // 抽样 10 个翻转位
            const std::uint64_t flipped = base ^ (1ULL << bit);
            const int diff = std::popcount(hash_u64(base) ^ hash_u64(flipped));
            CHECK(diff >= 16 && diff <= 48);
        }
    }
    // 键值敏感：不同键不同输出（黄金值已锚定，此处补充批量）
    Random r(3);
    for (int i = 0; i < 100; ++i) {
        const std::uint64_t x = r.next_u64();
        CHECK(hash_u64(x) != hash_u64(x ^ 1));
        CHECK(hash_combine(0, x) != hash_combine(0, x + 1));
    }
    return true;
}

}  // namespace

int main() {
    test_golden_hash();
    test_golden_stream();
    test_golden_derived();
    test_determinism_and_copy();
    test_next_int();
    test_next_double_bool();
    test_draws_counting();
    test_pick();
    test_shuffle();
    test_hash_avalanche();
    std::printf("[random test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}
