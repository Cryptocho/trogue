// sim.cpp —— 平台跳跃范例的逻辑实现（无 raylib、无窗口、无随机）。
#include "sim.hpp"

#include <algorithm>  // std::min / std::max
#include <cstdio>     // std::fprintf
#include <cstdlib>    // std::abort
#include <utility>    // std::move

#include "../common/scene_make.hpp"  // hp::make_ascii_scene（不依赖 raylib）

namespace plat {
namespace {

// ── 手感与规则常量（钉死数值：手感回归靠它，改了要同步测试期望；px / 秒）──
// 上升重力小、下落重力大 → 上升有滞空感、下落不拖沓；跳高 ≈ 420²/(2·1500) ≈ 59px。
constexpr float kGravity = 1500.0f, kFallGravity = 1900.0f;
constexpr float kJumpVy = -420.0f, kJumpCutVy = -150.0f;  // 起跳速度 / 松键截断（可变跳高）
constexpr float kRunSpeed = 110.0f, kAccel = 900.0f, kFriction = 1200.0f, kMaxFall = 520.0f;
constexpr float kEnemyGravity = 1200.0f, kCoyote = 0.10f, kBuffer = 0.10f, kInvuln = 1.2f, kBlink = 0.10f;  // 土狼/缓冲/无敌帧/闪烁
constexpr float kStompTol = 8.0f, kPitMargin = 48.0f;  // 踩踏容差 / 出界判定余量

// 场景配色（与窗口层同一份常量：场景在内存里构造，配色即「调色板」）
constexpr tg::Color kFloorColor{20, 20, 28, 255}, kWallColor{90, 105, 140, 255},
                     kBackground{10, 10, 16, 255};

// 朝目标靠拢（一阶线性趋近：加减速只有这一个原语，行为可预测）
float approach(float v, float t, float d) { return v < t ? std::min(v + d, t) : std::max(v - d, t); }

// ── 关卡几何：60×18 侧视网格，程序化摆放 ——
// 用矩形而不是手写 60 字符的字符串行：改布局不用数列、不会数错格，且每块地形的
// 用途写在注释里（台阶 / 高台 / 悬浮板 / 立柱 / 地板与坑）。
std::vector<std::string> make_rows() {
    std::vector<std::string> rows(18, std::string(60, '.'));
    auto fill = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) rows[y][x] = '#';
    };
    fill(6, 15, 8, 15); fill(11, 14, 13, 15); fill(30, 12, 33, 12);  // 台阶 / 高台 / 悬浮板
    fill(45, 13, 46, 15); fill(59, 12, 59, 15);                      // 立柱 / 右端墙（封住关卡）
    fill(0, 16, 21, 17); fill(26, 16, 59, 17);                       // 地板（22..25 = 坑）
    return rows;
}

}  // namespace

const std::vector<std::string>& level_rows() {
    static const std::vector<std::string> rows = make_rows();
    return rows;
}

// 场景构造：复用 examples/common/scene_make.hpp（不依赖 raylib，纯逻辑层可直接用），
// 因此「窗口路径」与「无窗口测试」共用同一份 ASCII → tro-scene 转换，不存在同构两份。
tg::SceneAsset build_scene(int tile_size) {
    return hp::make_ascii_scene(level_rows(), tile_size, kFloorColor, kWallColor,
                                kBackground, "platformer");
}

// headless 验证口径：第 i 步的输入只由 i 决定（持续右行 + 周期性按住跳键）
Input scripted_input(long long i) { Input in; in.right = true; in.jump = (i % 45) < 12; return in; }

// ── Player ──

void Player::reset(tg::Vec2 spawn) {
    x_ = spawn.x; y_ = spawn.y; vx_ = vy_ = 0; st_ = State::Fall; grounded_ = jump_held_ = false;
    coyote_ = buffer_ = invuln_ = blink_ = 0; hp_ = 2;
}

bool Player::blink_visible() const { return invuln_ <= 0.0f || (static_cast<int>(blink_ / kBlink) % 2 == 0); }

bool Player::hurt() {
    if (invuln_ > 0.0f) return false;  // 无敌帧：贴脸接触不叠加扣血
    --hp_; invuln_ = kInvuln; blink_ = 0; return true;
}

void Player::bounce() { vy_ = kJumpVy; grounded_ = false; buffer_ = 0; }

void Player::update(const Input& in, const Level& lv) {
    constexpr float dt = kFixedDt;
    invuln_ = std::max(0.0f, invuln_ - dt);
    blink_ = invuln_ > 0.0f ? blink_ + dt : 0.0f;

    // 水平：直接给目标速度（平台跳跃要跟手，不做动量），只在加减速上做限制
    const float dir = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);
    vx_ = approach(vx_, dir * kRunSpeed, (dir != 0.0f ? kAccel : kFriction) * dt);

    // 跳跃缓冲（按下沿计时）与土狼时间：落地/离地瞬间都给一段宽限
    if (in.jump && !jump_held_) buffer_ = kBuffer;
    jump_held_ = in.jump;
    buffer_ = std::max(0.0f, buffer_ - dt);
    coyote_ = grounded_ ? kCoyote : std::max(0.0f, coyote_ - dt);
    if (buffer_ > 0.0f && coyote_ > 0.0f) { vy_ = kJumpVy; buffer_ = coyote_ = 0.0f; grounded_ = false; }
    if (!in.jump && vy_ < kJumpCutVy) vy_ = kJumpCutVy;  // 可变跳高：松键即截断上升

    vy_ = std::min(vy_ + (vy_ < 0.0f ? kGravity : kFallGravity) * dt, kMaxFall);
    // 位移经引擎解算（贴边扫掠 + 探地/探墙事件）；blocked_* 只说明「本步该轴被
    // 截断」，故对应轴速度清零；是否贴地仍由引擎探地事件给出，不由 y 反推。
    const tg::KinematicResult r = lv.move(box(), tg::Vec2{vx_ * dt, vy_ * dt});
    x_ = r.sweep.box.x; y_ = r.sweep.box.y;
    if (r.sweep.blocked_x) vx_ = 0.0f;
    if (r.sweep.blocked_y) vy_ = 0.0f;
    grounded_ = r.ev.grounded;

    // 贴地用「输入意图」判状态：顶着墙按方向键仍是 Run（速度被墙吃掉的那一帧
    // 不该闪回 Idle）；空中按纵向速度分起跳/下落。
    st_ = grounded_ ? ((in.left || in.right) ? State::Run : State::Idle)
                    : (vy_ < 0.0f ? State::Jump : State::Fall);
}

// ── Enemy ──

tg::KinematicResult Enemy::advance(const Level& lv, float dt) {
    vy_ = std::min(vy_ + kEnemyGravity * dt, kMaxFall);
    const tg::KinematicResult r = lv.move(box(), tg::Vec2{vx_ * dt, vy_ * dt});
    x_ = r.sweep.box.x; y_ = r.sweep.box.y; grounded_ = r.ev.grounded;
    return r;  // 撞墙不掉头：是否折返由派生类决定（引擎只报告事件）
}

Patroller::Patroller(float x, float y, float speed) : speed_(speed) { x_ = x; y_ = y; vx_ = speed; }

tg::Color Patroller::color() const { return tg::Color{190, 120, 220, 255}; }

void Patroller::update(const Level& lv, float dt) {
    if (grounded_) {  // 临空检测：前方脚下不再是 solid 就折返（否则会径直走进坑里）
        const float px = (vx_ > 0.0f) ? x_ + kBoxW + 2.0f : x_ - 2.0f;
        if (!lv.solid_at(tg::Vec2{px, y_ + kBoxH + 2.0f})) vx_ = -vx_;
    }
    if (advance(lv, dt).ev.hit_wall) vx_ = -vx_;  // 撞墙（立柱）折返
}

Jumper::Jumper(float x, float y, float period) : t_(period), period_(period) { x_ = x; y_ = y; }

tg::Color Jumper::color() const { return tg::Color{220, 170, 80, 255}; }

void Jumper::update(const Level& lv, float dt) {
    t_ -= dt;
    if (grounded_ && t_ <= 0.0f) { vy_ = kJumpVy * 0.7f; t_ = period_; }  // 落地后才重新计时
    advance(lv, dt);
}

// ── Level ──

Level::Level(tg::SceneAsset asset) : asset_(std::move(asset)) {
    // 层 1 = solid 墙层：与 level_rows/build_scene（以及 hp::make_ascii_scene）的两层
    // 约定一致；物化成掩码后所有位移查询都走它，不再直接读 tile 层。
    auto grid = tg::SolidGrid::load(asset_, 1);
    if (!grid) { std::fprintf(stderr, "[platformer] solid 层缺失\n"); std::abort(); }
    grid_ = std::move(*grid);

    // 单向平台是 game 自持矩形（场景 tile 层里不存在）：薄板，从下往上穿过、自上
    // 落下可站。布局与（未来的）下跳摘除都归本层，引擎只按规则解算并给探地事件。
    one_way_ = {{22.0f * kTile, 14.0f * kTile, 4.0f * kTile, 6.0f},
                {36.0f * kTile, 14.0f * kTile, 4.0f * kTile, 6.0f},
                {41.0f * kTile, 12.0f * kTile, 3.0f * kTile, 6.0f}};
    spawn_ = tg::Vec2{2.0f * kTile, 15.0f * kTile};
    goal_ = tg::Rect{58.0f * kTile, 15.0f * kTile, kTile, 2.0f * kTile};  // 终点旗（右端墙前）
    spawns_ = {{0, 27.0f * kTile, 15.0f * kTile, 45.0f}, {1, 52.0f * kTile, 15.0f * kTile, 1.1f}};
    build_objects();
}

// 位移与探点都直接引用网格掩码视图（view() 返回成员引用，取其地址即可）
tg::KinematicResult Level::move(tg::Rect box, tg::Vec2 delta) const {
    return tg::kinematic_step(&grid_.view(), 1, one_way_.data(), static_cast<int>(one_way_.size()), box, delta);
}

bool Level::solid_at(tg::Vec2 p) const {
    return tg::is_solid_at(&grid_.view(), 1, p) == tg::TileQueryResult::solid;
}

void Level::build_objects() {
    enemies_.clear();
    for (const Spawn& s : spawns_)
        enemies_.push_back(s.kind == 0 ? std::unique_ptr<Enemy>(std::make_unique<Patroller>(s.x, s.y, s.p))
                                       : std::unique_ptr<Enemy>(std::make_unique<Jumper>(s.x, s.y, s.p)));
    player_.reset(spawn_);
}

// 死亡重置与 R 重开共用：重建对象、won_ 归零（本局作废）；deaths_ 保留（跨重试累计）
void Level::reset() { won_ = false; build_objects(); }

void Level::set_spawn(tg::Vec2 p) { spawn_ = p; player_.reset(p); }

int Level::alive_enemies() const {
    int n = 0; for (const auto& e : enemies_) n += e->alive() ? 1 : 0; return n;
}

void Level::step(const Input& in) {
    if (in.restart) { reset(); return; }  // 重开不推进本步模拟
    player_.update(in, *this);
    for (auto& e : enemies_) if (e->alive()) e->update(*this, kFixedDt);

    // 玩家 vs 敌人的规则（引擎不做动态-动态解算，谁踩谁归 game）：下落且底边接近
    // 敌人顶边 = 踩踏（弹跳 + 消灭）；否则侧撞受伤（无敌帧由 Player 兜住）。
    const tg::Rect pb = player_.box();
    for (auto& e : enemies_) {
        if (!e->alive() || !tg::aabb_overlap(pb, e->box())) continue;
        if (player_.vy() > 0.0f && (pb.y + pb.h) <= (e->box().y + kStompTol)) { e->kill(); player_.bounce(); }
        else player_.hurt();
    }

    // 掉出场景（坑底）→ 即死；与受伤共用同一条死亡路径：deaths+1 并重置关卡
    const float map_h = static_cast<float>(asset_.layer(0).height * kTile);
    if (player_.box().y > map_h + kPitMargin) player_.die();
    if (player_.hp() <= 0) { ++deaths_; reset(); return; }
    if (!won_ && tg::aabb_overlap(player_.box(), goal_)) won_ = true;  // 到达终点旗
}

}  // namespace plat
