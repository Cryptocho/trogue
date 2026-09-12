# 里程碑 17：模板 AGENTS.md 定位重构 + 下游实证缺陷与结构缺口修复

- 计划日期：2026-09-11
- 状态：已完成（D 节四项全部纳入本期）
- 前置：里程碑 16（碰撞几何原语）完成。一份基于本模板从零开发的**真实下游游戏**（「深渊行记」，本仓库外、非交付物）第二轮的反馈清单已内联到本文件（读者无需访问外部文件）。
- 参考：`AGENTS.md`「项目目标/交付物与验证台」「功能准入判据」「引擎公共 API 边界」「编码规范」；`template/{AGENTS.md,CMakeLists.txt,game/CMakeLists.txt,README.md}`、`template/scripts/sync_from_source.sh`、`engine/src/{render,scene_asset,animation}.cpp`、`engine/src/scene_impl.hpp`、`engine/include/trogue/{scene,animation,collision}.hpp`、`pixellab/*.py`

## 1. 目的与问题

### 1.1 起因

下游游戏在本模板上从零开发完毕后，交付了一份分五层的观察：**引擎缺陷**、**模板文档定位问题**、**引擎结构层建议**、**PixelLab 管线问题**、**方法论反思**。经逐条核对上游源码，其中**多数技术断言属实，但有一条系误判**（见 §2 核对记录）。

本里程碑的**核心不是修那几个 bug，而是修正模板 `AGENTS.md` 的定位**：

> **模板 `AGENTS.md` 是「教 Agent 怎么做游戏」的指南，不是引擎 API 说明书。**

现状恰恰相反：`template/AGENTS.md` 共 197 行，其中**引擎/工具性章节（API 边界 + schema + IPC + 构建依赖 + Godot/PixelLab）约 124 行 ≈ 六成**（脚本实测；下游原文称「九成」属估算，已更正）；玩法深度只有开篇一句流程性的「先写文档理清设计」。下游的诊断——「**一份引擎 API 说明书被放在了游戏设计宪章的位置上**」——依然成立，并给出了因果链：阅读顺序（先读完 API、再设计游戏）→ 题材从「原语能廉价做什么」里长出来，而非从体验目标出发 → API 被当成菜单而非约束 → 交付「功能齐全但深度不足」。

> **用户拍板（2026-09-11）**：模板 `AGENTS.md` 的定位即「本来就是教 AGENTS 怎么做游戏的」。因此本次的文档重构不是「加一段免责声明」，而是**重排主线**：以**体验目标与设计深度**开篇，引擎能力降为**约束与参考**。

### 1.2 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

本里程碑**同时**包含引擎项与文档项，逐条自检：

- **引擎项（B4/D2/D3）**：全部是**既有原语的缺口或缺失**——`render_sprite` 缺缩放、`tro-animations` 有独立资产格式却无独立加载入口、`SceneAsset` 无可变 tile 层更新。均满足「机制性、确定性、可无头测试」，**不是玩法决策**。
- **引擎项（D1）**：**碰撞查询的输入载体抽象**——把查询从「只收 `const SceneAsset&`」泛化为「可接受调用方自持的 solid 视图数组」。这是**接口一般化**，不含碰撞规则/物理（那仍归 game）；机制性、确定性、可无头测试。**收益方是「逻辑真值在 game 侧」的程序生成游戏**（属引擎可用性），**这是下游影响最大的一条**，本里程碑**纳入实现**（§3.5.1）。
- **引擎项（B3）**：**纪律对齐**（同一文件的 palette 分支与 `draw_rect` 用了两种绘制原语）。**注意**：下游称此处存在「整数截断」，经核对**不成立**（`origin_x/origin_y` 与 tile 尺寸均为 `int`，位置恒为整数，见 §2）；本项按「一致性」而非「修 bug」处理。
- **模板/文档项**：模板是引擎能力的**分发载体**——里程碑 14 已把 `template/` 列入引擎能力线 Roadmap（先例）。模板文档定位错误会**系统性削弱引擎被正确使用的能力**，即「引擎能否像 LÖVE2D 一样用」的一部分。
	- **明确不进本里程碑**：E1–E6 属 PixelLab skill/管线（非引擎能力线），另起计划（§5.4）。

	> 反向自检：本里程碑没有任何一项只能回答「让某款游戏更好玩」；碰撞规则/物理仍明确归 game（§3.5.1 只动输入载体，不动规则）。

## 2. 来源核对记录（哪些已核实为真）

对下游清单的每条技术断言，已在上游源码核对（**含一条误判的更正**）：

| 编号 | 断言 | 核对结果 | 证据 |
|------|------|----------|------|
| B1 | 模板顶层缺 `enable_testing()`，`add_test` 静默失效 | ✅ 真 | 源仓库顶层 `CMakeLists.txt:16` 用 `include(CTest)`；`template/CMakeLists.txt` 全程无；而 `template/game/CMakeLists.txt:17` 注释 `add_test(...)` 引导启用 → 照做后 `ctest` 报 "No tests were found"，**CI 显示「无测试」而非「失败」** |
| B2 | bare 场景仍强制要求 `tilemap` 键 | ✅ 真 | `engine/src/scene_asset.cpp:1121-1125`「tilemap 必须存在且为 object」；而模板 `AGENTS.md:80` 的 bare 描述读起来像「可省略」 |
| B3 | palette 分支用 `DrawRectangle(int)` **导致截断** | ⚠️ **代码属实、结论不成立** | `render.cpp:191` 确用 `int`；但 `LayerInfo::origin_x/origin_y` 是 `int`（`scene.hpp:74`）、`impl.tile_w/tile_h` 是 `int`、`tx/ty` 是 `int` → `wx = origin_x + tx*tile_w` **恒为整数值**，`static_cast<int>` 无损。**今天不存在可观测截断**。真正的问题是**纪律不一致**（同文件 `draw_rect:288` 用浮点 `DrawRectanglePro`） |
| B4 | `render_sprite` 无缩放 | ✅ 真 | `render.hpp:27-28` 签名仅 `pos + tint` |
| B5 | clip 名集内唯一 × PixelLab rotations/idle 撞名 | ✅ 真 | `scene_asset.cpp:852`「clip name 集内重复」（校验本身合理，冲突在转换层命名） |
| B6 | CJK 字体经 `LoadFontEx` 加载失败 | ✅ 真（**真因已更正**：raylib 只按扩展名分派 `.ttf`/`.otf`，`.ttc` 不被支持且**静默回退默认字体**；`stbtt` 固定 `offset=0` 取不到 TTC 内 face。**CFF 轮廓本身受支持**——独立 `.otf` 可载。措辞以 `rtext.c:545/583/634` 为准） | 无引擎字体模块；属「用引擎做中文游戏」的坑，但有更优路径（用独立 `.otf`/`.ttf`） |
| B7 | `ipc_smoke.py` 绑起步骨架命令 | ✅ 真，但**非 bug** | 文档已声明它是项目自有文件、随命令集增改 |
| C1–C8 | 模板 `AGENTS.md` 定位偏差 | ✅ 真**（篇幅数字已更正：引擎/工具性章节约 124/197 行 ≈ 六成，脚本实测）** | 开篇 L4 原文「直到**功能完成**」；七步流程无试玩；唯一设计指令 L3「先写文档理清设计」 |
| D1 | 碰撞查询绑死 `SceneAsset`，惩罚「逻辑真值在 game 侧」 | ✅ 真（设计缺口） | `collision.hpp:50/78` + `scene.hpp:137-148` 全部收 `const SceneAsset&` |
| D2 | `tro-animations v1` 是独立格式却无独立加载入口 | ✅ 真 | 只有 `SceneAsset::animation_set`（`animation.hpp:53`）；`grep load_animations` 零命中 |
| D3 | `SceneAsset` 无法原地替换 tile 层 | ✅ 真 | 无对应 API；程序生成游戏换层须重建整个 asset |
| E1–E6 | PixelLab skill/管线问题 | 记录，未逐条核（属 skill 范围，§5.4 另起） | `pixellab/character.py` 确为逐帧 URL 自建 sheet；全仓无并发上限处理 |

## 3. 方案

### 3.1 阶段 A：模板 `AGENTS.md` 定位重构（**本里程碑核心**）

**信息不丢是硬要求**，故本方案 = **重排**（不删节），并配**机器可核的逐节映射表**。

#### 3.1.1 目标章节顺序（参考区单一收口）

```
1. 目标与工作方式（新增，替换现开篇）
2. 项目结构（保留现 L10-28：目录、快照、项目自有文件、更新器）
3. 设计指南（新增）
4. 资产与内容生产（PixelLab / Godot / 手写 JSON）
5. 实现
6. 验证（含试玩）
7. 调试工作流
8. 附录：参考区（引擎 API 边界 + 资产 schema + IPC 契约 + 构建依赖 + 已知坑）
```

> **设计决定（消除审查指出的两处矛盾）**：引擎 API 边界**只出现在第 8 章附录**；第 5 章「实现」仅**指路**（一句「引擎契约见附录 A」），**不复制内容**——避免同一内容双份维护导致漂移。「项目结构」必须保留（现 L10-28，对 Agent 定位项目极关键）。

#### 3.1.2 旧节 → 新节映射表（**强制产出，作为「零丢失」证据**）

重构**前**先产出下表；重构**后**用脚本抽取旧/新标题集做 `diff`，逐条勾核：

| 现 `template/AGENTS.md` 节 | 新位置 | 处置 |
|---------------------------|--------|------|
| L1-8 标题 + 目标 + schema 权威源声明 | 第 1 章开篇 | **改写**（终态、流程）+ **保留** schema 权威源声明 |
| L10-28 项目结构 / 快照 / 项目自有文件 / 更新器 | 第 2 章 | **原样保留** |
| L30-64 引擎公共 API 边界（重要） | 第 8 章附录 A | **保留**（并入 C7 的「这不是你的优先级排序」声明） |
| L66-70 内存加载程序生成场景 | 第 8 章附录 A | **保留** |
| L72-103 资产规范（tro-scene/tileset/animations） | 第 8 章附录 B | **保留** |
| L105-117 Godot 编辑器（可选） | 第 4 章 | **保留** |
| L119-135 PixelLab 资产管线 | 第 4 章 | **保留** + 补 B5/E3 命名隔离提示 |
| L137-149 IPC 协议 | 第 8 章附录 C | **保留**（命令语义指路 + 契约） |
| L151-169 Agent 调试工作流 + 已知坑 | 第 7 章（调试工作流） | **保留** + 补 B6（能力提示）、B7（工作流契约） |
| L171-177 测试 | 第 6 章 | **改写**（并入 C4「乐趣无法被 ctest 覆盖」） |
| L179-186 引擎构建依赖 | 第 8 章附录 D | **保留** |
| L188-196 开发流程（七步） | 第 1 章 | **改写**（插入非可选「试玩与迭代」环节） |
| （新增）设计清单 | 第 3 章 | 新增 |
| （新增）资产验收 | 第 6 章 | 新增 |

#### 3.1.3 逐条对应（C 节全部落到具体改法）

| 编号 | 改法 |
|------|------|
| C1 | 第 1 章终态：「直到**好玩**——功能完成只是及格线」 |
| C2 | 开发流程在「实现」之后插入**非可选**「试玩与迭代」环节（视觉子代理/真人试玩，问「有趣吗/难度合适吗/还想再来一局吗」，不达标回设计） |
| C3 | 新增第 3 章「设计指南」+ 最小设计清单：核心循环一句话、玩家决策点、失败与重开的钩子、纵深来源——**没有这些不许进实现**（流程第 1 步引用它） |
| C4 | 第 6 章「验证」补：「逻辑测试 + 截图只证明『没坏』；**乐趣无法被 ctest 覆盖**，另需真人/视觉子代理试玩与难度标定（批量无头模拟至少覆盖 bot 通关率与平均深度）」 |
| C5 | 第 1 章补「先定体验与深度目标，再打开 API 找实现路径/绕开/补足；**不要让 API 的形状替你选题材**」 |
| C6 | 「不重复造轮子」补**反向律**：「引擎**没有**的能力，该建就建；**不要让引擎的空白变成你玩法的天花板**」 |
| C7 | 附录 A 显式限定：「本节描述引擎契约，**不是你的优先级排序**；音频/粒子/UI/场景管理是**你的活**，引擎的沉默不等于省略的许可」 |
| C8 | 「数值精度纪律」首句限定：「**仅当**你的移动是网格对齐/回合制时」；即时连续运动不受此约束 |
| F2 | 第 1 章补：「如实区分**真实限制**与**自设限制**；资源充足时『没做』就是『选择不做』」 |
| F3 | 并入 C6 反向律：按**目标**而非**阻力**决定是否自建 |
| F4/E6 | 第 6 章新增「**资产验收**」：`get_*` 不是状态查询而是质量审查入口；在**游戏实际展示尺度**下看图、看动画循环、与已有资产并排看一致性；不达标就改提示词重掷/修图；**生成后核对计划**（是否有生成却从未使用的 clip） |
| B5/E3 | 第 4 章 PixelLab 小节：作为**命名约定**陈述——「PixelLab 的 rotations 行与动画行方向同名；建议 rotations 用 `<name>_rot_<dir>` 前缀区分」——**理由**是「同名会撞引擎的 clip 名唯一契约」，**不写成**「不改名整个场景会被拒」 |
| B6 | 第 7 章补一条**能力提示（非“坑”语气）**：「优先用**独立 `.otf`/`.ttf`** 的 CJK 字体——`LoadFontEx` 直接可用；否则用 `template/tools/gen_font.py` 离线烘焙。检测字体是否真加载：`IsFontValid`（**不能只看 `baseSize`**）」。**不写**「`.ttc` 因扩展名分派/offset 而失败」这类内部机制——那是引擎侧实现细节 |
| B7 | 第 7 章把 `tools/ipc_smoke.py` 作为一条**工作流契约**写清：「冒烟脚本的断言随你的命令集增改（它是项目自有文件）」——陈述职责归属，**不写**「你把它换成别的命令集它会全红」这种失效描述 |
| **文档原则（新增）** | 第 1 章或附录开头**显式定一条规则**：「**文档给契约与意图，不给绕坑说明**。凡是需要写『你必须先 X，否则失败』的地方，先问：这是契约，还是实现事故漏出来了？若是后者→**修代码让这句消失**；若读者不需要知道，就不写」——本条由 B2 的教训提炼，并作为后续文档改动的自检标准（见 §3.3 B2） |

#### 3.1.4 配套：`DESIGN.md` 约定

- 模板**不预置** `DESIGN.md`（避免空壳形式主义），但在第 3 章规定其**最小字段**并声明它是流程第 1 步的产物；已有设计文档允许就地引用，不强制改名。

### 3.2 阶段 A：引擎修复（B3/B4/D2）

#### 3.2.1 B3：palette 分支改用浮点绘制（**纪律对齐，非修 bug**）

- **定性**：不是「修截断」（`origin` 为 int，位置恒为整数，今天无可观测问题），而是**消除纪律不一致**——同文件 `draw_rect` 用 `DrawRectanglePro` 并注释了浮点理由，palette 分支却用 `DrawRectangle(int)`。
- **改动**：`engine/src/render.cpp:191` 的 `DrawRectangle(int,int,...)` → 直接 `DrawRectanglePro`（与 `draw_rect` **同一原语**）。
- **明确不采用**「复用 `draw_rect`」：`draw_rect` 自身会执行段② `++window_checks`（`render.cpp:283`），而 `render_scene` 已做过一次窗口检查（`:142`）；逐格调用会让 `window_checks` 按 tile 数暴增，**破坏 `render_test.cpp:105` 的精确断言 `== 3`**。故只对齐原语，不引入额外窗口检查。
- **不写「非整数 origin」测试**（不可构造：origin 是 int）。本项为**纯代码审查项**（§6 不设 E2E 验证点），唯一可断言的是「palette 分支与 `draw_rect` 用同一绘制原语」这一代码事实。

#### 3.2.2 B4：`render_sprite` 可选缩放

- **API**：`RenderResult render_sprite(const SceneAsset&, const SpriteDesc&, Vec2 pos, Color tint = 白色, Vec2 scale = {1,1})`——**追加尾参，默认值保持现有行为**（源兼容；仅取函数指针为精确类型者受影响，本项目无此用法）。
- **实现（写死公式，防漏）**：`DrawTextureRec(tex, r, pos, tint)` 等价于 `DrawTexturePro(tex, r, {pos, r.w, r.h}, {0,0}, 0, tint)`。缩放时：
  ```
  DrawTexturePro(tex, r, { pos.x + offset.x, pos.y + offset.y,
                           r.w * scale.x, r.h * scale.y },
                 {0,0}, 0.0f, tint)
  ```
  **`scale == {1,1}` 时走原 `DrawTextureRec` 路径**（保证与改动前像素级一致）；否则走 `DrawTexturePro`。
- **两分支一致**：`render_sprite` 有图集形态（`:249`）与独立贴图形态（`:267`）两条路径，`scale` 对**两者产出的源矩形 `r` 同尺度缩放**（不改变两者各自的 `r` 求法）。
- **语义**：`scale` 作用于纹理原始尺寸；`pos + offset` 仍是**目标矩形左上角**（与既有锚点语义正交、不变）。
- **校验顺序**：`scale ≤ 0` 或非有限 → `Invalid`，且必须置于**段①（窗口检查之前）**并 `++param_failures`——否则无窗口单测无法断言。
- **测试**：`render_test`（seam 库，无窗口）覆盖 `scale` 非法 → `param_failures` 增 + 段②未执行；`scale == {1,1}` 与旧路径等价（可用统计计数或 E2E 像素比对）。

#### 3.2.3 D2：独立 `tro-animations` 加载入口（**需新增拥有型类型**）

**结构硬伤（审查确认）**：现 `AnimationSet` 只有 `const detail::AnimData* data_`（`animation.hpp:51`），是**非拥有视图**，`AnimData` 定义在私有 `scene_impl.hpp`。因此 `expected<AnimationSet, Error> load_animations(...)` **按字面实现必然悬垂**（指向已释放数据），「asset 销毁后仍可用」不可能达成。

**方案：新增拥有型资产类型 `tg::AnimationAsset`**（**不改造** `AnimationSet` 视图语义与 `SceneAsset` 生命周期契约，避免波及既有 API）：

```cpp
// animation.hpp
class AnimationAsset {
public:
    static expected<AnimationAsset, Error> load(std::string_view path);
    static expected<AnimationAsset, Error> load_json(std::string_view text,
                                                     std::string_view name = "<memory>");
    AnimationAsset(AnimationAsset&&) noexcept;              // RAII、不可拷贝
    AnimationAsset& operator=(AnimationAsset&&) noexcept;
    ~AnimationAsset();

    // 只读视图（存活期 = 本 asset）；形态与 SceneAsset::animation_set 一致
    // 返回引用（非按值）——否则 bind(asset.view()) 会绑定到临时对象。
    const AnimationSet& view() const;
    std::string_view name() const;   // 独立文件无 entity → 取文件基名（load_json 取 name 参）
};
```

- **持有方式与地址稳定性**：内部用 `std::unique_ptr<detail::AnimData>` 持有解析结果（堆分配、地址稳定），并持有 `AnimationSet view_` 视图成员。
  - **对外生命周期契约**：`view()` 返回的是**本对象视图成员**的引用，故**有效期 = 本 `AnimationAsset` 对象存活期**（与 `SceneAsset::animation_set` 契约一致）。**移动**后 `AnimData` 堆地址不变，故内部视图成员无需修补、直接可用；但**已 `bind()` 的播放器持有的是指向源对象成员的指针**，源对象销毁即失效——移动后须重新 `view()`/`bind()`。
  - **显式声明析构/移动操作**：`detail::AnimData` 在此头**仅有前置声明**（完整定义在私有 `scene_impl.hpp`），故 `unique_ptr<detail::AnimData>` 作为成员时，析构与移动构造/赋值**必须 out-of-line 定义**在可见完整类型处（不能 `= default`）。
- **共享核心的签名**：抽出的校验核心改为 `(const json& obj, where, asset_id, name) → AnimData`（或等价）；内嵌路径传 `SceneAsset::asset_id()`，独立路径传 `0`、`name` 取文件基名/`load_json` 的 name 参。
- **`name()` 的存储**：返回的 `string_view` 必须指向**被拥有的存储**（存进 `AnimData`，如文件基名），**不得指向临时串**。
- **`AnimationSet` 需新增一个 friend**：现有 `set_data` 为私有，仅 `AnimationPlayer`/`detail::SceneLoader` 可调（`animation.hpp:52-54`，`parse_animations` 起于 `scene_asset.cpp:790`）；`AnimationAsset` 需 `friend class AnimationAsset;` 才能构建视图。**文件清单已含此改动。**
- **解析：独立文件先校验外层格式，再交内部对象给共享核心**（关键，否则共享核心会拒绝合法资产）：现有 `parse_animations`（`scene_asset.cpp:790-798`）**拒绝未知键**（仅接受 `textures`/`animations`），而独立 `tro-animations v1` 文件顶层含 `format`/`version`（`assets/animations/pxlab_soldier.json`）。故：
  - `AnimationAsset::load` 先校验 `format == "tro-animations" && version == 1`；
  - 再把 `{textures, animations}` 子对象交给**共用的校验核心**（从 `parse_animations` 抽出，内嵌/独立两路径同一套限额/帧索引/region-offset/clip 名唯一校验）。
- **`load()` 路径校验**：镜像 `SceneAsset::load` 的路径 grammar 校验（非空/无 NUL/UTF-8/`is_safe_relative_path`/`kPathMax`，`scene_asset.cpp:1261-1271`）——建议复用同一 helper。
- **语义（必须写明）**：
  - `AnimationSet::name()`：内嵌路径 = entity id；独立文件**无 entity** → 取**文件基名**（`load_json` 时取 `name` 参数）。
  - 帧的 `asset_id`：内嵌 = 所属 `SceneAsset::asset_id()`；**独立文件 = 0**。后果：`render_sprite` 的归属校验（`render.cpp:211`）在 `asset_id==0` 时**允许跨 asset 绘制**——这对下游有利（动画可脱离其来源 asset 绘制），**作为契约明确写出**。
  - 为此**不提供** `AnimationAsset::asset_id()`（独立文件无自身可绘制的 asset；帧 `asset_id` 恒 0）——避免推出无消费者的 API 面；若未来需要诊断，再按需加。
- **残留依赖（写明，避免半真）**：绘制仍需一个 `const SceneAsset&`（`render_sprite` 签名），故「不必把角色捏进 bare 场景」只解决**加载**一侧；**绘制宿主**可用内存构造的最小 bare 资产 `SceneAsset::load_json(R"({"format":"tro-scene","version":2})")`（本里程碑 B2 落地后等价 bare；**不能用 `"{}"`**——缺 `format`/`version` 会被拒）。
- **文档**：更正根/模板 `AGENTS.md` 中「独立 tro-animations 无运行时加载器」的表述。
- **测试**：新增 `animations_load_test`（纯公共 API）：用既有 `assets/animations/*.json` → clip 名/fps/frames 与**内嵌路径逐项一致**；`view()` 生命周期契约（§6）；非法输入（`format`/`version` 不符、坏帧索引、重复 clip 名、超限）拒绝；`asset_id==0` 时的绘制语义。

### 3.3 阶段 A：模板缺陷与文档修正

| 项 | 改动 |
|----|------|
| **B1** | `template/CMakeLists.txt` 补 `include(CTest)`（在 `project()` 之后、`add_subdirectory` 之前），使 `template/game/CMakeLists.txt` 注释里的 `add_test` 示例真正生效；并在该注释里点明「顶层已 `include(CTest)`」 |
| **B2** | **根因修复，让文档不需要多余支。** ① **修代码**：`scene_asset.cpp:1121-1125` 允许 `tilemap` 键**真正缺省**（缺省 = `{}`）；显式提供但非 object 仍拒绝。② **文档不写键机制**：bare 的描述改为**意图语言**——「只有 `entities`、没有地形（如只放角色帧动画）」——**不写**「`tilemap` 缺省或 `{}`、`layers` 为 空/缺省」。读者永远不必知道容器键的可选性。③ **实现细节只入资产参考附录/资产 loader 的 docstring**（不是游戏开发指南的正文）：那儿可以写「bare 三类等价输入：缺省 / `{}` / `layers:[]`」。**风险缓解**：未知根键已有 warning（`:1099-1104`）；对「无 `tilemap` 且无 `entities`」加提示日志 |
| **B5** | 见 §3.1.3（模板 PixelLab 小节命名隔离提示） |
| **B7** | 见 §3.1.3（模板调试小节冒烟脚本提示） |
| **B6** | 见 §3.1.3（模板已知坑补 CJK 字体条目）；**参考实现按 §3.5.3 交付**（由「仅文档」升级） |

**B2 对既有测试的影响（必须显式处理）**：`tools/tests/scene_schema_test.cpp:96` 现有用例 `expect_reject("tilemap 缺失", {"format":"tro-scene","version":2})` 本次**由 reject 翻转为 ok**（该输入现在等价于 bare）。这不是「回归」，是**语义变更**——步骤 4 显式改此用例并加新用例（缺省 = bare 合法；`"tilemap": 123` 仍拒绝）。§6 的表述据此调整。

### 3.4 阶段 A：README 与一致性

- `template/README.md` 的定位语（「面向 Agent 的开发指南见 `AGENTS.md`」）同步为「**游戏开发指南**（体验目标优先，引擎边界为参考区）」。
- 根 `AGENTS.md`「项目模板（template/）」小节补记：模板 `AGENTS.md` 定位 = 游戏开发指南；并补 `AnimationAsset`/`load_animations` 描述（D1/D2）。**bare 描述按 §3.3 B2 的意图语言改写（不写键机制）**。

### 3.5 阶段 B：引擎结构缺口（D1/D3/D4，本次一并完成）

#### 3.5.1 D1：碰撞/查询的输入载体抽象（解绑 `SceneAsset`）

**问题**：`is_solid_at`/`rect_hits_solid`/`tile_at`/`tile_grid`/`solid_mask`（`scene.hpp`）与 `sweep_move`/`segment_hits_solid`（`collision.hpp`）**全部只收 `const SceneAsset&`**，默认了「地形真值住在 `SceneAsset` 里」。而引擎又明确鼓励「对象模型/运行时状态归 game」——**惩罚了它自己鼓励的架构**。下游因此让整块 collision API 闲置（碰撞全自判）。

**设计原则**：只抽象**输入载体**，不动查询/几何**语义**（floor、半开、层外不阻挡、多 solid 层短路、逐层 origin 全部保持逐位一致）。新增只读、非拥有、无所有权纠缠的「solid 视图」。

**方案：新增 `tg::SolidGridView`（`trogue/collision.hpp`）**——纯值类型，描述调用方自持的**单个 solid 网格**（单层语义，可叠多个视图）：

```cpp
struct SolidGridView {
    int width = 0, height = 0;      // tile 网格尺寸
    int tile_w = 0, tile_h = 0;     // tile 像素尺寸（本视图自己的；asset 重载用 asset 全局值）
    int origin_x = 0, origin_y = 0; // 层左上角世界偏移（同上 SceneAsset 层）
    const std::uint8_t* mask = nullptr;  // 行主序；非 0 = 阻挡
    int stride = 0;                 // 行跨度（元素数；0 → 视为 width；maks 至少 stride*height）
    int layer_id = -1;              // 回填到 TileHit.layer（调用方自定；asset 重载填 asset 层号）
};
```

- **`TileHit.layer` / `SweepResult` 的索引空间（必须钉死，否则行为漂移）**：视图重载下，命中的 `layer` **回填 `SolidGridView::layer_id`**（而非视图数组下标）。asset 重载把 asset 的 solid 层展开为临时视图时，`layer_id` 填**原 asset 层号**——因此现有 `probe_collide`（`game/src/main.cpp`）与 `collision_test` 的 `layer` 语义**结构上保证不变**（如 `demo.json`/`forest.json` 的 solid 层是 `layers[1]` → `layer==1`）。视图数组 = asset 中 solid 层的**有序投影**；只传部分层 = 这些层的并集，不代表整个 asset。

- **视图校验/退化（必须定义）**：`mask == nullptr` 或 `width/height <= 0` 或 `tile_w/tile_h <= 0` → 该视图**无阻挡**（对齐现有 `tile_w<=0 → clear`）；`stride == 0` → 视为 `width`（否则要求 `mask` 至少 `stride*height`）。
- **生命周期契约**：视图**不持有**、不复制 `mask`；mask 生命周期由调用方保证——**与 `SceneAsset` 的「引用仅在 asset 存活期」句式对齐**，失效后不得再查询。
- **查询：四个一律收视图数组**（消除审查指出的形状不一致）——多 solid 层不同 origin 时，单视图点/矩形查询**无法表达并集**：
  - `is_solid_at(const SolidGridView* views, int count, Vec2 world)`、`rect_hits_solid(const SolidGridView*, int count, Rect)`；
  - `segment_hits_solid(const SolidGridView*, int count, Vec2 a, Vec2 b)`、`sweep_move(const SolidGridView*, int count, Rect, Vec2)`；
  - 语义 = 现有逐层逻辑（多视图按数组序短路/取最紧）。
- **物化方式（修正）**：**不得用 `solid_mask` 物化为单视图**——`solid_mask`（`scene_asset.cpp:1427-1462`）把各 solid 层**合成到同一张 tile 网格且完全忽略每层 origin**，无法还原各层世界位置。
  - **最小反例**：单 solid 层 `origin=(8,8)`、本地 `(2,2)` 实心 → `is_solid_at(asset,{48,48}) = solid`；而 `solid_mask(asset,0,0,4,4)` 当 origin=0 的单视图 → `floor(48/16)=3` → 格 (3,3)非实心 → **clear**，**不等价**。
  - **正确路径**：对**每个 solid 层**用公共 `tile_grid(asset, li, ...)`（层本地坐标）+ `layer(li).origin_*` + asset 级 tile 尺寸，**逐层构造视图**。`solid_mask` 仅适用于「所有 solid 层共享同一原点/网格」的常见场景，文档需写明这一前提。
- **语义一致性（硬要求，按逐层物化表述）**：同一地形经 `SceneAsset` vs 经**逐层物化**的视图数组，四个查询**逐位一致**。
- **`SceneAsset` 侧不退化**：现有重载保留（不改签名、不改行为），内部实现主体改为接受「视图数组」，asset 重载在此之上薄包装（把 asset 的 solid 层 + 全局 tile 尺寸展开为临时视图）。
- **不做的事（明确）**：不引入可变资产、不做「第二份运行时实体状态」、不做掩码的拥有型容器（掩码归调用方，视图**非拥有**）。
- **文档**：`AGENTS.md` 公共 API 边界补「碰撞查询的输入载体：`SceneAsset` 或调用方自持的 `SolidGridView` 数组（**逐 solid 层**用 `tile_grid` + 层 origin 物化后自持；`solid_mask` 只在单原点/同网格时可直接用）」。
- **测试**：新增用例——同一地形 `SceneAsset` vs 逐层物化视图数组的四个查询**逐位等价**（含**单层非零 origin** 与**双层不同 origin** 两个关键用例；后者**不能**经 `solid_mask`）；视图退化参数（null/零尺寸）→ clear；**asset 路径的 `layer` 值重构前后逐位不变**（用 solid 层不在 0 号位的资产，如 `demo.json`/`forest.json` 的 `layer==1`）。

#### 3.5.2 D3：`SceneAsset` 的 tile 层更新（**受限可变**，不破坏只读契约）

**问题**：程序生成游戏每换一层地形就要 `new` 一个 `SceneAsset`（图集贴图随之重载）。

**设计约束（避免与值语义/悬垂冲突）**：不开放「外部可写缓冲」，只提供**资产自有的受控更新**：

```cpp
// scene.hpp（SceneAsset 成员）
// 用一层新 tile 值替换第 layer_index 层（长度必须 = width*height，值域合法）。
// 不改层元数据（尺寸/origin/tileset/solid）与图集贴图（asset 级 RAII 不变）；
// 不影响已取得的 AnimationSet/entity 快照；不增删层。
// 失败（层越界/长度不符/值越界/asset 为 bare）→ 返回 Error，不改动任何数据。
ErrorOr<void> update_layer_tiles(int layer_index, const std::vector<int>& tiles);
```

- **语义**：原子性（先校验后写入）；值域按该层 tileset/palette 校验（与 load 期同规则：图集层用 `tilesets[i].tile_count`，palette 层用 `palette.size()`，bare 无层直接拒绝）；**引用稳定性**：替换的是 `layer_tiles[i]` 这一**内层 `vector<int>` 对象**（其自身缓冲**可能重分配**，但不影响外层 `layer_tiles`/`layers` 容器与 `LayerInfo` 引用）；已在用的 `const LayerInfo&` 引用仍有效（但**旧层 tiles 缓冲引用失效**——公共面无暴露该缓冲的 API，契约写明「只返回快照」）。
- **必须同步重算 `LayerInfo::nonempty`**（否则快照静默失真）：`nonempty` 是快照字段且有真实消费者（`oop_client_smoke.cpp:85` 断言 `walls.nonempty>0`；`game/src/main.cpp:862` 用它输出 `layers[].tiles`）。实现里替换 tiles 后同步 `impl->layers[i].nonempty = 非 -1 计数`。
- **明确不做**：不开放 tileset/palette/尺寸/origin 变更（那会影响图集贴图与层几何，风险高）；不提供层增删。
- **文档**：说明「程序生成换层的推荐路径」：`update_layer_tiles` 更新视觉层；若同时需要碰撞网格，**逐层**用 `tile_grid` + 层 origin 取快照后在 game 自持（与 D1 配合）。并点明：改层后 game 侧派生状态（碰撞网格/actor 位置/`map_w/h`）需自行重建（引擎不管）。
- **测试**：更新后 `tile_at`/`tile_grid`/`solid_mask` 反映新值；**`LayerInfo::nonempty` 同步正确**；失败路径（越界/长度/值域）不改动旧数据；预先取得的 `const LayerInfo&` 引用仍有效；图集贴图未被重载。

#### 3.5.3 D4：CJK / 非 ASCII 字体（模板可选参考实现）

**实证与更正（本机 + raylib 6.0 源码核实）**：
- 失败现象：`LoadFontEx(noto-cjk/*.ttc, 32, 中文码点)` **拿不到中文字形**。拿到的 `Font` 看起来「有效」，但中文渲染成 `?`。
- **注意（措辞精确化）**：未建窗口时**任何** `LoadFontEx` 都返回 `baseSize=0`（内部依赖 GL），**不足以区分** `.ttc` 特有问题；**已建窗口**时 `.ttc` 才表现为**静默回退默认字体**（`baseSize>0` 但只有 ASCII 字形）。故判据必须是 `IsFontValid`（`rtext.c:588`）**加实际中文字形存在性**。
- **真因（非 CFF！）**：`LoadFontFromMemory`（`reference/raylib/src/rtext.c:545-546`）**只按扩展名**分派 `.ttf`/`.otf`；`.ttc` 落到 `font.glyphs = NULL` → **静默回退 `GetFontDefault()`**（`:583`，仅一条 WARNING）。且 `stbtt_InitFont(&fontInfo, fileData, 0)` 硬编 `offset=0`（`:634`），而 TTC 需 `stbtt_GetFontOffsetForIndex` 取集合内 face → 即使改名 `.otf` 仍失败。
- **CFF/PostScript 轮廓本身受支持**：独立 `.otf`（CFF）可正常光栅化（实测 32px/95 字形）。**先前「CFF 不支持」的结论已作废。**
- **正确检测**：`IsFontValid(font)`（`rtext.c:588`）或 `baseSize>0 && glyphCount>0`；**不能只看是否崩/是否非空**（默认字体会「成功」画出 `?`）。

**交付**（模板可选项，不进引擎——引擎不含字体模块）：
- **优先路径（先说）**：若能用**独立 `.otf`/`.ttf`**（非 `.ttc` 集合）的 CJK 字体，`LoadFontEx` **即可直用**，无需烘焙。文档先给这条。
- **离线烘焙（仅有 `.ttc` / 字符集极大 / 图集超 GPU 单边上限时）**：`template/tools/gen_font.py` 用 Pillow/FreeType 烘焙「基线对齐的多行图集 PNG + 度量 JSON」，支持任意字符集，**自动折行**避 GPU 纹理单边上限（不硬编 16384，取保守值可配）。
- `template/game/src/ui_font.{hpp,cpp}`：游戏侧组装 raylib `Font` 的参考实现（载入图集 + 度量 JSON）。
- **可选接入（必须明确 CMake 处理）**：**不作为起步骨架硬依赖**；`template/game/CMakeLists.txt` 增一个**注释掉的可选块**（或独立可选目标），并写明「启用时把 `ui_font.cpp` 加入 `trogue` 可执行目标」；文件清单列该 CMake 改动；`template/AGENTS.md` 第 4 章/已知坑指向它。
- **验证**：烘焙中文字形 → 模板实例加载并 `DrawText` 中文 → **断言 `IsFontValid(font) && baseSize>0`**（否则默认字体会「成功」画 `?`）→ 截图 `read` 读图核对字形可辨、无 `?`。
- **依赖**：Pillow（本机 12.3.0 可用）；若派生环境无 Pillow，文档说明需自行安装（**不静默失败**）。

## 4. 步骤（含开发流程 3~7）

1. **§3.2.1 B3**：改 `render.cpp` palette 分支为 `DrawRectanglePro`（不调用 `draw_rect`）→ 构建 + `render_test` 零回归（`window_checks == 3` 不变）。
2. **§3.2.2 B4**：`render.hpp` 加 `scale` 尾参 → `render.cpp` 按写死公式实现（`scale==1` 走 `rect` 路径）→ `render_test` 加校验用例。
3. **§3.2.3 D2**：抽 `parse_animations` 核心为内部公共函数 → 新增 `AnimationAsset`（RAII + `view()`）→ `animations_load_test` → `tools/CMakeLists.txt` 注册。
4. **§3.3 B2**：改 `scene_asset.cpp`（`tilemap` 缺省 = bare）→ **翻转** `scene_schema_test.cpp:96` 的用例 + 新增用例 → 跑 `scene_schema_test` 全绿。
5. **§3.5.1 D1**：抽内部「视图数组」实现 → 新增公共 `SolidGridView` + 查询/几何重载（`SceneAsset` 侧保留）→ `collision_test`/`scene_query_test` 加等价性用例。
6. **§3.5.2 D3**：`SceneAsset::update_layer_tiles`（先校后写、原子、引用稳定）→ `scene_query_test`/`scene_schema_test` 加用例。
7. **§3.5.3 D4**：`template/tools/gen_font.py` + `template/game/src/ui_font.{hpp,cpp}` → 模板实例烘焙并截图核对中文。
8. **§3.3 B1**：改 `template/CMakeLists.txt`（`include(CTest)`）+ `template/game/CMakeLists.txt` 注释 → 空目录起项目，加一个假 `add_test` 验证 `ctest` 能收集到（再删）。
9. **产出 §3.1.2 映射表**（重构前提）→ 按 §3.1 结构重写 `template/AGENTS.md` → **脚本抽取旧/新标题集 `diff` 逐节勾核**，确认信息零丢失。
10. **§3.4**：同步 `template/README.md` 与根 `AGENTS.md`。
11. **模板快照刷新**：`cd template && ./scripts/sync_from_source.sh`（维护者模式；不触碰模板自有文件）→ 带上 B3/B4/D1/D2/D3 的引擎改动。
12. **回归**：Debug + Release 构建零告警；`ctest` 全绿（13 个 + 新增）；`python3 tools/ipc_smoke.py` 全绿。
13. **E2E**：起 demo → `screenshot` 同步命令 → read 工具读图，核对 B4 缩放（摆放一个 `scale!=1` 的 sprite 并确认按目标尺寸绘制）。
14. **模板端到端验证**：空目录安装 → 构建零告警 → 起服 + 冒烟全过 → `ctest` 能收集到游戏测试（B1）；D4 字体可选路径实测。
15. **subagent 审查**未提交代码与文档（合理/优雅/风格统一/无逻辑问题；**禁止自检**）。
16. 更新 `CHANGELOG.md`（审查通过后）。
17. 检查是否需更新根 `AGENTS.md`（§3.4 已含）。
18. 询问用户 commit message（**英文**预览，确认后提交**所有**变更并推送，禁止直接提交）。

### 文件清单（新增 / 修改）

| 文件 | 动作 | 对应 |
|------|------|------|
| `template/AGENTS.md` | **重构**（定位反转 + 映射表勾核） | §3.1（核心） |
| `engine/src/render.cpp` | 改（palette `DrawRectanglePro`；`render_sprite` 缩放） | B3、B4 |
| `engine/include/trogue/render.hpp` | 改（`render_sprite` 加 `scale`） | B4 |
| `engine/include/trogue/animation.hpp` | 改（新增 `AnimationAsset` + `AnimationSet` 的 friend） | D2 |
| `engine/src/animation.cpp` | 改（抽公共解析核心 + `AnimationAsset` 实现） | D2 |
| `engine/src/scene_asset.cpp` | 改（`tilemap` 缺省 = bare；`update_layer_tiles`） | B2、D3 |
| `engine/include/trogue/scene.hpp` | 改（`update_layer_tiles`） | D3 |
| `engine/include/trogue/collision.hpp` | 改（`SolidGridView` + 查询/几何重载） | D1 |
| `engine/src/collision.cpp` | 改（视图数组实现） | D1 |
| `engine/src/scene_impl.hpp` | 可能改（若解析核心需共享） | D2 |
| `template/tools/gen_font.py` | **新增**（D4 字体烘焙） | D4 |
| `template/game/src/ui_font.hpp` `ui_font.cpp` | **新增**（D4 字体组装参考） | D4 |
| `template/game/CMakeLists.txt` | 改（D4 可选接入块 + 注释） | D4 |
| `template/CMakeLists.txt` | 改（`include(CTest)`） | B1 |
| `template/game/CMakeLists.txt` | 改（注释） | B1 |
| `template/README.md` | 改（定位措辞） | §3.4 |
| `template/engine/**` | 刷新快照 | 步骤 8 |
| `AGENTS.md`（根） | 改（模板小节 + `AnimationAsset`/bare 描述 + Roadmap backlog） | §3.4、D2、B2、§5.1 |
| `tools/tests/render_test.cpp` | 改（`scale` 用例） | B4 |
| `tools/tests/scene_schema_test.cpp` | 改（B2 用例翻转 + 新增） | B2 |
| `tools/tests/scene_query_test.cpp` | 改（`update_layer_tiles` 用例） | D3 |
| `tools/tests/collision_test.cpp` | 改（`SolidGridView` 等价性） | D1 |
| `tools/tests/animations_load_test.cpp` | **新增** | D2 |
| `tools/CMakeLists.txt` | 改（注册新测试） | D2 |
| `CHANGELOG.md` | 改 | 步骤 13 |
| `docs/plan-17.md` | 新增（本文件） | — |

## 5. 遗留与后续计划（**防遗忘清单**）

> 下游清单里**本里程碑不做的每一条**都记录在案，附归属与排期；并同步进根 `AGENTS.md` 的 Roadmap backlog。

### 5.1 D1/D3/D4：已在**本期实现**（不再遗留）

- **D1**（碰撞/查询输入载体抽象）→ §3.5.1（`SolidGridView`）。
- **D3**（`SceneAsset` 受限可变 tile 层）→ §3.5.2（`update_layer_tiles`）。
- **D4**（CJK 字体参考实现）→ §3.5.3（`gen_font.py` + `ui_font.*`）。

> 历史背景（供审阅）：D1 曾拟推迟至 plan-18（因其需先定语义），D3 曾拟暂缓（因其与只读资产契约有张力）。**用户拍板：D 节应在本次一并完成**。因此 D3 改为**受限可变**（只开放 tile 值更新、不开放缓冲/元数据/增删层，避开值语义/悬垂风险），D1 改为**只抽象输入载体、不动查询语义**的保守形态（两层设计都在 §3.5 写明了「不做的事」）。

### 5.2 E1–E6：PixelLab skill 与管线（另起，属 `~/.agents/skills/pixellab-mcp` 与本仓库 `pixellab/`）

| 编号 | 内容 | 归属 |
|------|------|------|
| E1 | skill 未提 **`/characters/{id}/spritesheet`** 端点（一次 zip = 统一网格 sheet + 机器可读布局 JSON，**优于逐帧 URL**）；下游因此绕过 `pxlab.py` 自写导入器 | skill + `pixellab/` |
| E2 | **全局并发上限 8** 未进 skill；动画**按方向计 job**（一次 8 向 = 8 job）→ 下游因反复排队，imp/ogre 行走动画**始终没排上**（真实资产缺口） | skill |
| E3 | `rotations` 行与 `idle` 动画**方向命名天然相同** → 内嵌引擎撞「clip 名唯一」；须命名隔离（与 B5 同源） | skill + 转换文档 |
| E4 | `/tileset/metadata` 的 `bounding_box` 是**服务端现算**、且**剥掉每 tile 的 image**（无 per-tile URL）；转换器须先勘察真实响应 | skill |
| E5 | 网格纪律工具（`unzoom_image`/`correct_pixelart`/`reduce_colors`）**无使用判据**（下游一次没用） | skill |
| E6 | **缺产物验收清单**（何时重掷/改提示词）；下游零次重掷、零次改提示词（与 F4 同源） | skill |

> **落地载体**：skill 侧条目记入 `~/.agents/skills/pixellab-mcp` 的待办；仓库侧（`pixellab/`）记入根 `AGENTS.md` 的 Roadmap backlog。两者都在本里程碑步骤中登记，确保「另起」不至遗忘。

### 5.3 F 节（方法论反思）的用途

F 节本身**不是**待办项，而是**优化模板 `AGENTS.md` 的依据**。每条已映射到 §3.1：

| 编号 | 映射 |
|------|------|
| F1 浅完成信号 | → C1/C4（终态改「好玩」+ 验证章节声明「乐趣无法被 ctest 覆盖」） |
| F2 虚构时间预算 | → §3.1.3 F2（如实区分真实限制与自设限制） |
| F3 被挡反抗 / 仅不顺服从 | → C6 反向律（按**目标**而非**阻力**决定是否自建） |
| F4 `get_*` 当状态查询 | → §3.1.3 资产验收小节 + E6 |
| F5 读 API 在设计之前 | → C5（先定体验，再打开 API） |

### 5.4 已知结构性事实（继续保留）

- 模板 `AGENTS.md` 的 schema 段落与根 `AGENTS.md` 双份维护（模板顶部已声明权威源）；本次重构后仍成立。
- 模板 `tools/CMakeLists.txt`/`ipc_smoke.py` 随上游变化需手工跟进。
- **D4 字体烘焙的依赖**：`gen_font.py` 需 Pillow（本机 12.3.0 可用）；若派生环境无 Pillow，文档说明需自行安装，不静默失败。**优先路径是独立 `.otf`/`.ttf` 直用（无需烘焙）**；烘焙仅在只能拿到 `.ttc` 集合/字符集过大/图集超限时使用。

## 6. 验证清单

- [ ] **B1**：模板起的新项目里，`add_test` 被 `ctest` 收集到（不再 "No tests were found"）。
- [ ] **B2**：省略 `tilemap` 的纯实体场景加载成功；`"tilemap": 123` 仍拒绝；`scene_schema_test.cpp:96` 的用例**按计划翻转**为 ok，其余 schema 用例零回归。**文档验收（关键）**：模板 `AGENTS.md` 的 bare 描述**不出现**「`tilemap` 键」「缺省或 `{}`」这类键机制措辞，而是意图语言；且新增的「文档给契约与意图，不给绕坑说明」原则已写入。
- [ ] **B3**：palette 分支改为 `DrawRectanglePro`（代码事实）；`render_test` 的 `window_checks == 3` **零回归**（未引入额外窗口检查）。
- [ ] **B4**：`render_sprite` 带 `scale` 正确缩放；`scale == {1,1}` 时与改动前**像素级一致**；非法 `scale` → `Invalid` 且 `param_failures` 增（段①，无窗口可断言）；图集/独立贴图两分支一致。
- [ ] **D2**：`AnimationAsset::load(assets/animations/*.json)`（先校 `format=="tro-animations" && version==1`）与实体内嵌路径的 clip 名/fps/frames **逐项一致**；`view()` 返回 `const AnimationSet&`、有效期 = 源 `AnimationAsset` 存活期（移动后**重新** `view()`/`bind()`，已绑定播放器随源对象销毁失效——按此契约断言）；`name()` = 文件基名（指向被拥有存储）、帧 `asset_id == 0`（跨 asset 绘制）语义明确；`format`/`version` 不符与非法输入（坏帧索引/重复 clip 名/超限）均拒绝。
- [ ] **D1**：同一地形经 `SceneAsset` 与经**逐层物化**的视图数组的 `is_solid_at`/`rect_hits_solid`/`sweep_move`/`segment_hits_solid` **逐位等价**（必含**单层非零 origin** 与**双层不同 origin** 两用例——后者**不得**经 `solid_mask`）；视图退化参数（null/零尺寸）→ clear；**asset 路径的 `layer` 值重构前后逐位不变**（solid 层不在 0 号位时仍为原 asset 层号）；现有 `SceneAsset` 重载行为零变化。
- [ ] **D3**：`update_layer_tiles` 更新后 `tile_at`/`tile_grid`/`solid_mask` 反映新值且 **`LayerInfo::nonempty` 同步正确**；失败路径（层越界/长度不符/值域非法）**不改动旧数据**；预先取得的 `const LayerInfo&` 引用仍有效；图集贴图未被重载。
- [ ] **D4**：优先路径（独立 `.otf`/`.ttf` 的 CJK 字体）`LoadFontEx` 直用可行；离线烘焙路径（`gen_font.py` + `ui_font.*`）在模板实例中 `DrawText` 中文 → **`IsFontValid(font) && baseSize>0` 成立**且截图无 `?`；`template/game/CMakeLists.txt` 的可选接入块能零告警编译；未装 Pillow 时文档说明而非静默失败。
- [ ] **模板 `AGENTS.md`**：开篇终态为「好玩」；含设计清单、非可选试玩环节、乐趣不可自动验证声明、引擎边界≠优先级排序、不重复造轮子反向律、数值纪律限定语、资产验收小节；**旧节 → 新节映射表逐条勾核，信息零丢失**（脚本 `diff` 标题集为证）。
- [ ] Debug/Release 零告警、`ctest` 全绿、`ipc_smoke` 全绿。
- [ ] 模板空目录安装 → 构建零告警 → 起服 + 冒烟全过。
- [ ] 模板快照与源仓库一致（`diff -q` 关键文件）；新代码无 `plan-N`/内部文档指针（注释自足纪律）。
- [ ] 根 `AGENTS.md` 的 Roadmap backlog 已登记 E1–E6（D1/D3/D4 本期完成，勾选并注明）。
- [ ] subagent 审查通过（代码 + 文档）。
