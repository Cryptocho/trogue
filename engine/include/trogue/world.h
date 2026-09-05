#ifndef TROGUE_WORLD_H
#define TROGUE_WORLD_H

#include <stdbool.h>

#include "config.h"

#define TROGUE_MAX_LAYERS    4
#define TROGUE_MAX_TILESETS  8
#define TROGUE_MAX_ENTITIES  256
#define TROGUE_MAX_PALETTE   32

struct TgTileset; // 定义见 tileset.h；world 只持有指针，所有权归 world 的 tilesets[]

// 实体贴图（tro-scene v2 sprite 字段）。has=false 时实体渲染为色块。
// 图集形态：tileset >= 0（world.tilesets 索引），tile = 该 tileset 的 tile id；
// 独立贴图形态：tileset = -1，texture 相对 assets/，rw/rh <= 0 表示整图。
// 绘制锚点 = 实体 x/y + offset，贴图按原始像素尺寸绘制（不缩放），color 作 tint。
typedef struct TgSprite {
    bool has;
    int tileset;   // -1 = 独立贴图
    int tile;
    char texture[TROGUE_PATH_MAX];
    float rx, ry, rw, rh; // 独立贴图 region（像素）
    float ox, oy;         // 绘制偏移（相对实体左上角）
} TgSprite;

// 实体：纯数据。坐标单位为像素，x/y 为左上角，世界原点在 tilemap 左上角，y 向下。
typedef struct TgEntity {
    char id[TROGUE_NAME_MAX];    // 场景内唯一
    char type[TROGUE_NAME_MAX];  // 游戏逻辑据此区分行为（如 "player"）
    float x, y;
    float w, h;
    float vx, vy;                // 运行时速度，游戏逻辑使用
    int z;                       // 渲染排序键（y 相同时的次级键，见 render）
    unsigned char r, g, b, a;
    bool active;
    bool solid;                  // true = 参与 solid 碰撞（AABB），LÖVE Solid 组件的对应物
    TgSprite sprite;
} TgEntity;

// tile 层：行主序一维数组，-1 表示空。solid 层参与碰撞，层矩形之外视为该层无数据（不阻挡）。
typedef struct TgTileLayer {
    char name[TROGUE_NAME_MAX];
    int width, height;           // 单位: tile 数
    int origin_x, origin_y;      // 层左上角的世界像素偏移（可负，Godot 负坐标 cell 由它表达）
    bool solid;
    struct TgTileset *tileset;   // 本层图集；NULL = palette 色块模式
    short *tiles;                // width*height 个
} TgTileLayer;

typedef struct TgWorld {
    char scene_name[TROGUE_NAME_MAX];
    char scene_path[TROGUE_PATH_MAX];  // 载入路径，供 IPC reload 使用
    unsigned char bg[4];               // 背景色 RGBA
    int tile_w, tile_h;                // 像素
    int palette_count;                 // palette 模式（无 tilesets）下的颜色表
    unsigned char palette[TROGUE_MAX_PALETTE][4];
    int tileset_count;                 // 场景级 tilesets（tro-scene v2 tilemap.tilesets）
    char tileset_names[TROGUE_MAX_TILESETS][TROGUE_NAME_MAX];
    struct TgTileset *tilesets[TROGUE_MAX_TILESETS];
    int layer_count;
    TgTileLayer layers[TROGUE_MAX_LAYERS];
    int entity_count;                  // 槽位数（active 与否见 TgEntity.active）
    TgEntity entities[TROGUE_MAX_ENTITIES];

    int   reload_count;                // 热重载次数（调试观测）
    float uptime_s;                    // 由应用每帧更新
    int   fps;                         // 由应用每帧更新
} TgWorld;

TgWorld *tg_world_create(void);
void     tg_world_destroy(TgWorld *w);

TgEntity *tg_world_find_entity(TgWorld *w, const char *id);
// color_hex 形如 "#rrggbb" 或 "#rrggbbaa"，可为 NULL（默认白色）。
// id 为空则自动生成 "ent_N"。失败返回 NULL。
TgEntity *tg_world_spawn(TgWorld *w, const char *id, const char *type,
                         float x, float y, float ew, float eh,
                         const char *color_hex);
bool tg_world_despawn(TgWorld *w, const char *id);

// "#rrggbb" / "#rrggbbaa" → RGBA（alpha 缺省 255）。hex 为 NULL 或非法时返回 false。
bool tg_parse_hex_color(const char *hex, unsigned char out_rgba[4]);

// 像素坐标处是否有 solid tile（层矩形之外 = 该层无数据 = 不阻挡；solid 实体一并判定）
bool tg_world_is_solid_at(TgWorld *w, float px, float py);
// AABB 是否与任何 solid tile 重叠（solid 实体 AABB 一并判定）
bool tg_world_rect_hits_solid(TgWorld *w, float x, float y, float rw, float rh);
// 像素坐标处该层的 tile 值（层矩形之外或空格返回 -1）；像素→tile 换算的唯一实现
int tg_world_tile_at(const TgWorld *w, const TgTileLayer *l, float px, float py);

#endif // TROGUE_WORLD_H
