# 里程碑 18：确定性随机原语（种子 RNG 与坐标哈希）

- 计划日期：2026-09-13
- 状态：计划中（待审查）
- 前置：里程碑 5（C++ 引擎）、12（autotile/内存加载）、15/16/17 已完成。Roadmap「通用原语补齐·确定性 RNG 与固定步长（P1，2026-09-13 拍板）」为直接输入；本计划将 P1 范围**收窄为随机原语**（RNG + 坐标哈希），固定步长累加器移出，理由见 §1.3。
- 参考：`AGENTS.md`「项目目标/交付物与验证台」「架构分层·功能准入判据」「引擎公共 API 边界」「编码规范」；`engine/include/trogue/{types,terrain}.hpp`（值类型/自由函数风格基线）、`game/src/ai.{hpp,cpp}`（消费方 1）、`game/src/main.cpp`（消费方 2）

## 1. 目的与问题

### 1.1 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

本里程碑补齐的能力 = **「确定性随机原语」**：种子化的伪随机流 + 纯坐标哈希。它逐条满足功能准入判据：

| 判据 | 本里程碑 |
|------|----------|
| 机制性 | 是：纯算法原语（位混演、拒绝采样、Fisher–Yates），不含任何玩法决策 |
| 确定性 | 是：同 seed 同调用序列逐位同输出；算法在头文件注释中钉死，跨编译器/平台一致（不依赖 `std::` 分布实现） |
| 可无头测试 | 是：不触窗口/GL，纯公共 API 单测（黄金序列对照 + 统计 sanity） |
| 与美学/玩法无关 | 是：不含生成策略（放什么怪、噪声阈值）、不含关卡结构知识 |

> 反向自检：本计划没有任何一项只能回答「让某款游戏更好玩」。**「用随机数做什么」（生成什么、阈值多少、何时摇）仍完全归 game**。

### 1.2 需求实证（为什么是现在，而不是预造）

两个**互相独立**的消费方各自手写了随机机制——命中「不重复造轮子」纪律，且两处正好覆盖两类互补原语（流式 RNG 与坐标哈希）：

| 消费方 | 手写实现 | 位置 | 内容 | 拟消费的原语 |
|--------|----------|------|------|--------------|
| 探针 `game/` 敌人 AI | `std::mt19937` + `std::uniform_real_distribution` / `uniform_int_distribution` | `game/src/ai.hpp:33`、`game/src/ai.cpp:114-118` | 70% 游走判定 + 4 向均匀选取（固定种子 20260909） | `tg::Random`（流式） |
| 探针 `game/` demo `genmap` | `gen_hash(x, y, seed)` 自写坐标散列 + 双线性 value-noise | `game/src/main.cpp:501-525` | 程序生成地形的逐格噪声采样 | `tg::hash_u64`/`hash_combine`（坐标哈希） |

**`std::mt19937` 路线的具体缺陷**（为什么手写路线不满足长期需要）：`mt19937` 本身由标准钉死，但 `std::uniform_*_distribution` 的算法**实现定义**——同代码跨 libc++/libstdc++ 输出不同序列，「确定性」退化为本机确定性。引擎提供算法显式钉死的 RNG 后，同 seed 跨平台逐位一致才成立，Agent 的「同 seed 同运行」可复现验证才可靠。

**准入证据逐项对号（每个进引擎的符号都必须有实证消费方）**：

| 拟入符号 | 消费方实证 |
|----------|------------|
| `tg::Random`（next_u64/next_int/next_double/next_bool） | `ai.cpp` 的 `uniform_real`（→`next_double` 比较）与 `uniform_int`（→`next_int`） |
| `tg::Random::pick` / `shuffle` | `pick` 直接对号 `ai.cpp:117-118` 的「均匀选方向」；`shuffle` 为同族完整性（洗牌是种子 RNG 的标配组合操作，Fisher–Yates 消费 `next_int`，无新机制）——单消费方符号，保留理由写明于此 |
| `tg::hash_u64` / `tg::hash_combine` | `main.cpp` `gen_hash`（坐标+seed 散列进 value-noise） |

### 1.3 明确不在范围

- **固定步长累加器（`FixedStep`）移出本里程碑**：Roadmap P1 原文含「固定步长」，但**本仓库内零消费方**（`TweenManager::tick`/`AnimationPlayer::advance` 均由 game 喂可变 dt，无固定步长模拟循环）。预先纳入违反「需求驱动、不预先纳入」纪律；且它本就是「动态运动与物理能力评估」主线「时间步进」的评估对象。**处置**：实现里程碑完成后将 Roadmap P1 条目改写——RNG 已落地、`FixedStep` 并入动态运动主线评估。
- **概率分布对象**（正态、泊松、加权表等）：无消费方。game 需要时可用 `next_double`/`next_int` 自行组合。
- **全局随机源**（`tg::rand()` 之类的进程级单例）：引擎不持有隐式全局状态——确定性要求消费方显式拥有并管理种子，全局源会破坏「同 seed 同运行」的隔离性（多系统共享一个流会互相扰动）。
- **线程安全**：引擎单线程（见「架构分层·设计约定」），`Random` 是可复制值类型，不设锁。
- **crypto/统计质量**：xoshiro256** 是模拟/游戏级 PRNG，不做密码学声明。
- **不改写探针的 AI 决策逻辑**：`ai.cpp` 只换随机源，三态状态机、游走概率、方向集、触发时机不动；游走序列会改变（新算法新序列），确定性契约（同种子可复现）不变，见 §3 步骤 6 的基线说明。

## 2. 方案

新增引擎模块 `engine/include/trogue/random.hpp` + `engine/src/random.cpp`。风格与既有 `tg::` 一致：`Random` 为**可复制纯值类型**（无 RAII 资源、无继承），哈希为**自由函数**；不暴露 raylib 类型、无第三方依赖（仅 `<cstdint>`）。

### 2.1 坐标哈希（自由函数）

```cpp
// splitmix64：单输入 64 位雪崩混演。坐标/索引空间散列的确定性地基
//（空间噪声、按坐标取元素、map 键散列）。纯函数，同输入恒同输出。
std::uint64_t hash_u64(std::uint64_t x) noexcept;

// 多键组合：h = seed; (h = mix(h ^ mix(part)) for each part)。
// part 为整数类型（static_assert(std::is_integral_v<T>)，防指针/浮点静默转换）；
// 顺序敏感（键顺序不同 → 结果不同）。
template <typename... Ts>
std::uint64_t hash_combine(std::uint64_t seed, Ts... parts) noexcept;
```

- `hash_u64` = splitmix64 终版常量（`0x9E3779B97F4A7C15`，_golden gamma_，两次 xor-shift + 乘法混演）——公开、广泛审阅的算法，不自创。
- `gen_hash` 类消费方的改写形态：`hash_combine(seed, x, y)`（或 `hash_u64(hash_combine(seed, x) ^ y)` 之类组合由消费方表达）——**引擎只给混演原语，噪声插值/阈值等生成策略仍归 game**。

### 2.2 流式随机（`tg::Random`）

```cpp
// 种子确定性 PRNG：算法钉死为 xoshiro256**，种子经 splitmix64 扩散为 256 位
// 状态（避免弱种子全零带）。同 seed 同调用序列逐位一致，跨平台/编译器一致。
// 可复制值类型：拷贝 = 复制状态（两实例此后输出相同序列）——用复制表达
//「存档/回放某刻的随机状态」。无全局状态、无线程安全承诺。
class Random {
public:
    explicit Random(std::uint64_t seed) noexcept;

    std::uint64_t next_u64() noexcept;          // 原始 64 位流
    // 闭区间 [lo, hi] 均匀整数，无模偏差（Lemire 乘法拒绝采样，64 位）。
    // 前置：lo <= hi（违反为程序错误，见钉死 7）。
    int next_int(int lo, int hi) noexcept;
    double next_double() noexcept;              // [0,1)，53 位精度构造
    // 以概率 p 返回 true；p<=0 恒 false、p>=1 恒 true（边界精确定义，无舍入歧义）。
    bool next_bool(double p) noexcept;

    // 均匀选取一个元素。前置：v 非空（违反为程序错误，见钉死 7）。
    template <typename T>
    const T& pick(const std::vector<T>& v) noexcept;

    // Fisher–Yates 原地均匀洗牌（消费 next_int）。多重集保持；同状态同结果。
    template <typename T>
    void shuffle(std::vector<T>& v) noexcept;
};
```

**语义钉死（消除歧义，供审查）**：

1. **算法与种子扩散**：`xoshiro256**`；`Random(seed)` 用 splitmix64 连续扩散 4 个 64 位状态字。**状态不可能全零（论证写入头文件注释）**：四个状态字来自 splitmix64 对四个互不相同的输入（`seed + k·0x9E3779B97F4A7C15`，k=0..3）的求值，splitmix64 为 64 位双射 → 四个输出互不相同 → 全零状态在数学上不可达（非概率保证）。算法名与常量写入头文件注释——这是可复现契约的一部分，不是实现细节。
2. **整数均匀性**：`next_int` 用拒绝采样，**不用 `%` 取模**（模偏差），**不用 `std::uniform_int_distribution`**（实现定义）。实现取原始流的 32 位高字，拒绝 ≥ limit（不超过 2^32 的最大 r 倍数）后取模——数学上与 64 位 Lemire 乘法拒绝采样等价，但规避 `__int128`（其触发 `-Wpedantic` 告警，破坏零告警基线）。返回 `int`，`[lo,hi]` 闭区间与 `ai.cpp` 现用 `uniform_int_distribution(0,3)` 的闭区间语义一致，消费方零改写心智。
3. **浮点构造**：`next_double = (next_u64() >> 11) * (1.0 / 9007199254740992.0)`（2^53）——除法是编译期常量倒数乘法，IEEE754 下逐位确定；不依赖 libm。
4. **`next_bool(p)` 边界**：`p <= 0` → false、`p >= 1` → true 先短路，再 `next_double() < p`。
5. **`next_int` 返回类型**：`int`（32 位）。范围参数为 `int`；内部以 `uint64_t` 无偏差计算。极端范围（`INT_MIN..INT_MAX`）由 64 位路径天然覆盖。
6. **确定性 vs 平台**：全部实现为整数运算 + 常量浮点乘法，无 `libm`、无环境依赖（不走 `std::default_random_engine` 等实现定义路径）。
7. **前置条件违反 = 程序错误，不设断言**：`next_int(lo>hi)`、`pick(空容器)` 属程序错误。引擎既有基线**不使用 assert**（`SceneAsset::layer(i)` 的真实策略 = 契约注释「调用方先查 layer_count」+ 直接索引；engine/ 全目录零 `<cassert>` 引用），本模块**不引入** `<cassert>`（避免引入 NDEBUG 下的 Debug/Release 行为分叉，保持确定性契约的单一表述）：前置违反为未定义行为，由契约注释声明，与 `layer(i)` 同策略。

### 2.3 公共 API 边界（本里程碑后的增量）

`AGENTS.md`「引擎公共 API 边界」追加：

- **确定性随机**：`tg::hash_u64`/`tg::hash_combine`（splitmix64 坐标哈希自由函数）+ `tg::Random`（xoshiro256** 种子化流式 PRNG，可复制值类型；next_u64/next_int/next_double/next_bool/pick/shuffle）。算法在 `random.hpp` 注释中钉死为可复现契约；**生成策略（何用、阈值、分布形状）归 game**。
- 明确**不进引擎**：全局随机源、概率分布对象、固定步长累加器（并入动态运动主线评估）、线程安全。

## 3. 步骤（含开发流程 3~7）

1. **实现 `engine/include/trogue/random.hpp`**：§2.1/§2.2 声明 + 契约注释（注释自足：算法名、常量、确定性契约直接陈述，不引用内部文档/计划号）。模板（`hash_combine`/`pick`/`shuffle`）定义于头文件。
2. **实现 `engine/src/random.cpp`**：`hash_u64`、`Random` 构造与四个 next 方法；前置条件违反不设断言（引擎既有基线无 `<cassert>`，契约注释声明，见 §2.2 钉死 7）。
3. **伞头**：`engine/include/trogue/trogue.hpp` 追加 `#include "trogue/random.hpp"`。
4. **构建定义**：`engine/CMakeLists.txt` 的 `ENGINE_SOURCES` 追加 `src/random.cpp`（生产库与测试库同源，自动生效）。
5. **单测** `tools/tests/random_test.cpp`（纯公共 API，链接生产 `trogue_engine`）→ `tools/CMakeLists.txt` 注册为 `random_test`。用例见 §4.1；黄金值生成后须经**第二来源抽验**（独立参考实现或手算 spot-check 若干值）再冻结，防「用实现验证实现」的循环论证。
6. **消费方 1（AI）**：`game/src/ai.hpp` 删 `std::mt19937`，改持 `tg::Random`；`ai.cpp` 的 `uniform_real`/`uniform_int` 改 `next_double`/`next_int`。**基线说明**：游走序列将改变（新算法），`game_core_test` 的 `test_ai_wander_deterministic` 是「同种子跨运行复现」对比断言，预期不受影响；若其中含黄金位置期望则显式重排基线并在测试注释中注明换源，**不放宽断言**。
7. **消费方 2（genmap）**：`main.cpp` 的 `gen_hash` 改为组合 `tg::hash_u64`/`hash_combine`（value-noise 插值与阈值逻辑不动）。**基线说明**：同 seed 生成的地图图案会改变；`genmap` 契约是「同 seed 同尺寸逐位一致」（运行内/跨运行），非持久黄金图，重定基线零迁移成本。**冒烟增补（阻塞修复）**：现状 `tools/ipc_smoke.py` **无任何 genmap 断言**——本次新增：同 seed 两次 `genmap` 响应逐项一致（确定性复现）、不同 seed 结果不同、非法 seed 错误包络；对齐 plan-16 为 `probe_collide` 增补冒烟断言的先例，消费方 2 的 E2E 契约由本次增补兜底。
8. **回归**：Debug + Release（`TROGUE_DEBUG=OFF`）构建**零告警**（`-Wall -Wextra -Wpedantic`）；`ctest` 全绿（含新 `random_test`）；`python3 tools/ipc_smoke.py` 全绿（61 项基线 + 步骤 7 新增 genmap 断言）。
9. **模板刷新**：`cd template && ./scripts/sync_from_source.sh`（维护者模式）→ 把新模块刷进 `template/engine/` 快照；验证模板独立副本可构建（模板 `game/` 不要求切换随机源，属下游自主决策）。
10. **文档**：`AGENTS.md`——架构分层图 `random` 模块行、「引擎公共 API 边界」追加 §2.3、Roadmap P1 条目改写（RNG 勾选完成；`FixedStep` 移交动态运动主线评估，注明零消费方理由）。
11. **subagent 审查**未提交代码（合理/优雅/风格统一/无逻辑问题；**禁止自检**）。
12. 更新 `CHANGELOG.md`（审查通过后）。
13. 检查是否需要更新 `AGENTS.md`（同步骤 10，已含）。
14. 询问用户 commit message（**英文**预览，确认后提交**所有**变更并推送，禁止直接提交）。

### 文件清单（新增 / 修改）

| 文件 | 动作 | 对应 |
|------|------|------|
| `engine/include/trogue/random.hpp` | **新增** | §2.1–2.2 |
| `engine/src/random.cpp` | **新增** | §2.1–2.2 |
| `engine/include/trogue/trogue.hpp` | 改（伞头追加） | 步骤 3 |
| `engine/CMakeLists.txt` | 改（源文件） | 步骤 4 |
| `tools/tests/random_test.cpp` | **新增** | §4.1 |
| `tools/CMakeLists.txt` | 改（注册测试） | 步骤 5 |
| `game/src/ai.hpp` / `game/src/ai.cpp` | 改（换随机源） | 步骤 6 |
| `game/src/main.cpp` | 改（`gen_hash` 换哈希原语） | 步骤 7 |
| `tools/ipc_smoke.py` | 改（新增 genmap 断言） | 步骤 7 |
| `template/engine/**` | 刷新（vendored 快照） | 步骤 9 |
| `AGENTS.md` | 改（分层/边界/Roadmap） | 步骤 10 |
| `CHANGELOG.md` | 改 | 步骤 12 |
| `docs/plan-18.md` | 新增（本文件） | — |

## 4. 验证清单

### 4.1 `random_test`（无窗口、纯公共 API）

- **确定性（核心契约）**：
  - 黄金序列：固定 seed 的前 N 个 `next_u64` 与测试内**硬编码黄金值**逐位对照（锁死算法不漂移）；黄金值在实现时生成一次、经第二来源抽验（独立参考实现或手算 spot-check 若干值）后冻结。
  - 同 seed 两实例 → 全方法输出序列逐位一致；不同 seed → 序列不同（抽样断言）。
  - 复制语义：拷贝后两实例序列一致；原实例继续推进不影响副本（状态独立）。
- **`next_int`**：闭区间端点可达性（大量采样下 lo 与 hi 均出现）；范围外永不出现；`lo==hi` 恒返回该值；均匀性 sanity（大样本分桶，卡方级宽松阈值，仅防实现退化）；极端范围 `INT_MIN..INT_MAX` 不崩溃。
- **`next_double`**：∈ [0,1)；0 可达、1 不可达（大样本断言 max < 1）。
- **`next_bool`**：p=0 恒 false、p=1 恒 true（边界短路）；p=0.5 大样本频率 sanity。
- **`pick`**：仅返回容器元素；大样本覆盖全部下标（前置违反为程序错误/UB，不测，见 §2.2 钉死 7）。
- **`shuffle`**：多重集保持（排序后与原序列相等）；同状态两次 shuffle 同结果（确定性）；大样本首元素分布 sanity（均匀性退化检测）。
- **哈希**：`hash_u64` 黄金值对照 + 雪崩 sanity（单 bit 翻转 → 输出约半数位变化，宽松阈值）；`hash_combine` 顺序敏感、键值敏感、同输入恒同输出。

### 4.2 E2E / 回归

- [ ] `ctest` 全绿（含新 `random_test`；Debug + Release）。
- [ ] `game_core_test` 的游走确定性断言不放宽全绿；`ipc_smoke.py` 全绿（61 项基线 + 步骤 7 新增 genmap 确定性/异 seed/非法 seed 断言）。
- [ ] Debug + Release 构建**零告警**。
- [ ] 模板维护者模式刷新后，`template/` 独立副本能构建；`template/engine/include/trogue/random.hpp`、`template/engine/src/random.cpp` 在位。
- [ ] `git diff --name-only` 的本次改动文件中无内部文档指针（注释自足纪律）；`game/src/` 既有阶段标记为已知基线，只要求无新增。

## 5. 遗留与边界

- **固定步长累加器（`FixedStep`）**：移入「动态运动与物理能力评估」主线（`AGENTS.md` Roadmap 已列），理由：仓库内零消费方，预造违反需求驱动纪律；届时与「时间步进」评估合并考量。本里程碑完成时同步改写 Roadmap P1 条目。
- **`shuffle` 单消费方**：`pick` 有直接消费方（AI 方向选取）；`shuffle` 当前无调用点，作为种子 RNG 的标配组合操作随模块进入（Fisher–Yates 消费 `next_int`，无新机制、无新依赖），证据已记于 §1.2。若审查裁定收敛，可裁至 game 侧（`pick` 保留）。
- **分布对象/加权选取**：无消费方，不预造；game 可用 `next_double`/`next_int` 组合。
- **PRNG 算法替换**：xoshiro256** 若未来被证明有质量问题（目前无证据），替换算法 = 破坏既有黄金序列（确定性契约是「同算法版本内可复现」，算法名已写入头文件注释作为版本锚点）。
- **不提供 `Random` 的序列化/状态导出 API**：可复制值类型即状态快照（拷贝构造/赋值），无需额外 API。
