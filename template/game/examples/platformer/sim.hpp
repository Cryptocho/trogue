#pragma once
// sim.hpp —— 平台跳跃范例的**纯逻辑层**：不 include raylib，可在无窗口进程里跑。
//
// 为什么分两层：位移解算、状态机与关卡规则与渲染/窗口无关，拆出来才能被
// sim_test 直接断言（确定性、可回归）；窗口、键盘与绘制只在 main.cpp。
//
// 对象模型（OOP 范式：少数对象、富封装状态机、多态敌人）：
//   Player —— 速度/状态机/计时器全私有，外部只读查询 + update；
//   Enemy 基类 + Patroller/Jumper 派生（行为各异），集合按基类多态持有；
//   Level —— 持有场景、单向平台与对象列表，是规则与碰撞的唯一入口。
//
// 位移一律经 tg::kinematic_step（引擎给贴边扫掠与探地/探墙/landed 事件），
// 本层只做手感（土狼时间/跳跃缓冲/可变跳高）与玩法判定，不手写 sweep。

#include <memory>  // std::unique_ptr（多态敌人集合）
#include <string>
#include <vector>

#include "trogue/collision.hpp"  // kinematic_step / SolidGrid / aabb_overlap
#include "trogue/scene.hpp"      // SceneAsset

namespace plat {

constexpr int kTile = 16;  // tile 边长（场景与逻辑同坐标系）
constexpr float kFixedDt = 1.0f / 60.0f;       // 固定步长：模拟只在整步边界推进
constexpr float kBoxW = 12.0f, kBoxH = 16.0f;  // 碰撞盒（比 tile 略窄，过缝不卡）

// 一步的输入快照：逻辑层不认识键盘，也不认识 raylib 键码
struct Input {
    bool left = false, right = false;
    bool jump = false;     // 「按住」而非边沿：跳跃缓冲与可变跳高都要按住状态
    bool restart = false;  // 重开本关（不推进本步模拟）
};

// 关卡布局：ASCII 行（'#' = solid）。单一来源——窗口层与无窗口测试都经 build_scene
// 构造场景（内部复用 examples/common/scene_make.hpp 的同一份转换）。
const std::vector<std::string>& level_rows();
tg::SceneAsset build_scene(int tile_size);

class Level;  // 前向声明：Player/Enemy 的 update 里要用 Level 的碰撞入口

class Player {
public:
    enum class State { Idle, Run, Jump, Fall };  // 渲染选色与测试断言都读它

    void reset(tg::Vec2 spawn);
    void update(const Input& in, const Level& lv);

    State state() const { return st_; }
    bool grounded() const { return grounded_; }
    float vy() const { return vy_; }  // 负 = 向上（踩敌人后为负）
    int hp() const { return hp_; }  // 0 = 死亡（受伤两次）
    bool invulnerable() const { return invuln_ > 0.0f; }
    bool blink_visible() const;  // 无敌帧闪烁：显隐由逻辑层决定
    tg::Rect box() const { return {x_, y_, kBoxW, kBoxH}; }

    bool hurt();    // 受伤：无敌帧内直接忽略；返回本次是否真扣血
    void bounce();  // 踩敌人成功：固定向上弹（不依赖是否按住跳键）
    void die() { hp_ = 0; }  // 即死（掉出场景）：与扣血共用同一条死亡路径

private:
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0;
    State st_ = State::Fall;
    float coyote_ = 0, buffer_ = 0, invuln_ = 0, blink_ = 0;
    bool grounded_ = false, jump_held_ = false;
    int hp_ = 2;  // 2 = 一击受伤（无敌帧容错）+ 一击死亡
};

class Enemy {
public:
    // 类型标识：main 用此选贴图（替代按颜色魔法数区分）；新增敌人只需 override 一行。
    // 0 = Patroller，1 = Jumper；与 Level::Spawn::kind 同号。
    virtual int kind() const = 0;
    virtual ~Enemy() = default;
    virtual void update(const Level& lv, float dt) = 0;  // 各自行为
    virtual tg::Color color() const = 0;
    tg::Rect box() const { return {x_, y_, kBoxW, kBoxH}; }
    bool alive() const { return alive_; }
    void kill() { alive_ = false; }  // 保留在集合里：下标稳定，确定性不受影响

protected:
    // 重力 + 位移（统一走 kinematic_step）；返回事件供派生类做折返决策
    tg::KinematicResult advance(const Level& lv, float dt);
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0;
    bool alive_ = true, grounded_ = false;
};

class Patroller final : public Enemy {  // 巡逻兵：撞墙或临空折返，不需要地图脚本
    float speed_ = 0;  // 巡逻速度（含方向：正 = 右）；成员写在 public 前 = 私有
public:
    Patroller(float x, float y, float speed);
    int kind() const override { return 0; }
    void update(const Level& lv, float dt) override;
    tg::Color color() const override;
};

class Jumper final : public Enemy {  // 跳跃兵：落地静默片刻后起跳（节奏型威胁）
    float t_ = 0, period_ = 1.0f;  // 起跳倒计时 / 周期；成员写在 public 前 = 私有
public:
    Jumper(float x, float y, float period);
    int kind() const override { return 1; }
    void update(const Level& lv, float dt) override;
    tg::Color color() const override;
};

class Level {
public:
    explicit Level(tg::SceneAsset asset);
    void reset();                // 重建对象回到出生点（死亡/重开共用）
    void step(const Input& in);  // 一个固定步：输入 → 各对象 → 玩法规则

    // 位移的唯一入口（玩家与敌人共用）：单向平台数组由此一并传给引擎，
    //「平台布局与下跳摘除归 game」这条边界就落在这里。
    tg::KinematicResult move(tg::Rect box, tg::Vec2 delta) const;
    bool solid_at(tg::Vec2 p) const;  // 单点探地（巡逻兵的临空检测用）
    void set_spawn(tg::Vec2 p);       // 指定出生点并立即重建（检查点/测试用）

    const tg::SceneAsset& scene() const { return asset_; }
        const std::vector<tg::Rect>& one_way() const { return one_way_; }
        const Player& player() const { return player_; }
        const std::vector<std::unique_ptr<Enemy>>& enemies() const { return enemies_; }
        int alive_enemies() const;
        int deaths() const { return deaths_; }
        bool won() const { return won_; }
        tg::Rect goal() const { return goal_; }

        // 收集物与检查点：3 枚晶体 + 1 个检查点。晶体被拾取后从列表移除；
        // 检查点一旦触发，重生点改为该位置（死亡/重开后生效）。
        struct Gem { tg::Vec2 pos; bool taken = false; };
        const std::vector<Gem>& gems() const { return gems_; }  // main 绘制未拾取晶体用
        int gem_count() const;          // 剩余未拾取晶体数
        int gems_collected() const;      // 累计拾取数
        bool checkpoint_activated() const { return checkpoint_active_; }
        tg::Vec2 checkpoint_pos() const { return checkpoint_pos_; }

private:
    struct Spawn {  // 敌人重建的最小描述：reset 完全由数据决定，无随机
        int kind;   // 0 = Patroller，1 = Jumper
        float x, y, p;
    };
    void build_objects();

    tg::SceneAsset asset_;
        tg::SolidGrid grid_;             // 场景 solid 层物化出的碰撞掩码
        std::vector<tg::Rect> one_way_;  // game 自持单向平台（tile 层里不存在）
        std::vector<Spawn> spawns_;
        std::vector<std::unique_ptr<Enemy>> enemies_;
        Player player_;
        tg::Vec2 spawn_{0, 0};           // 当前重生点（检查点触发后会变）
        tg::Vec2 initial_spawn_{0, 0};  // 初始重生点（reset 回到这里）
        tg::Rect goal_{0, 0, 0, 0};
        std::vector<Gem> gems_;          // 晶体列表
        tg::Vec2 checkpoint_pos_{0, 0};  // 检查点位置
        bool checkpoint_active_ = false;
        int gems_collected_ = 0;
        int deaths_ = 0;
        bool won_ = false;
    };

// 确定性脚本输入（headless 验证口径）：第 i 步的输入只由 i 决定——一直按住右
// 方向；每 45 步（0.75s）按住跳跃 12 步（0.2s，松键即减少跳高）。固定步 + 固定
// 脚本 ⇒ 两次运行逐位一致。
Input scripted_input(long long i);

}  // namespace plat
