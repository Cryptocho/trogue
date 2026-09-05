// trogue 演示应用：展示场景 JSON 载入、热重载与 DEBUG IPC 的最小集成方式。
// 玩法：WASD/方向键移动；F5 手动重载场景；F12 截图。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "trogue/trogue.h"

#define WIN_W  960
#define WIN_H  540
#define ZOOM   2.0f
#define PLAYER_SPEED 120.0f

int main(int argc, char **argv)
{
    const char *scene_path = "assets/scenes/demo.json";
    int port = TROGUE_IPC_PORT_DEFAULT;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--scene") == 0)
            scene_path = argv[i + 1];
        else if (strcmp(argv[i], "--port") == 0)
            port = atoi(argv[i + 1]);
    }
    if (port <= 0 || port > 65535) {
        TraceLog(LOG_ERROR, "[main] 无效端口 %d（需在 [1,65535]）", port);
        return 1;
    }

    InitWindow(WIN_W, WIN_H, "trogue");
    SetTargetFPS(60);

    TgWorld *world = tg_world_create();
    if (!world || !tg_scene_load(world, scene_path)) {
        TraceLog(LOG_ERROR, "[main] 场景载入失败: %s", tg_scene_last_error());
        tg_world_destroy(world);
        CloseWindow();
        return 1;
    }

    TgWatcher *watcher = tg_watcher_start(TROGUE_SCENE_DIR);
    TgIpc *ipc = tg_ipc_start(port, world);

    Camera2D cam = {
        .offset = (Vector2){ WIN_W / 2.0f, WIN_H / 2.0f },
        .target = (Vector2){ 0, 0 },
        .zoom = ZOOM,
    };

    while (!WindowShouldClose() && !tg_ipc_quit_requested(ipc)) {
        float dt = GetFrameTime();

        // ── 调试子系统：IPC 优先于热重载，让 Agent 能触发 reload 命令 ──
        tg_ipc_poll(ipc);

        char changed[TROGUE_NAME_MAX];
        if (tg_watcher_poll(watcher, changed, sizeof(changed)) > 0) {
            TraceLog(LOG_INFO, "[main] 检测到 %s 变更，热重载", changed);
            tg_scene_reload(world);
        }
        if (IsKeyPressed(KEY_F5))
            tg_scene_reload(world);

        // ── 玩家移动（分轴移动实现滑墙）──
        TgEntity *p = tg_world_find_entity(world, "player");
        if (p) {
            float dx = (float)((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1 : 0)
                     - (float)((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1 : 0);
            float dy = (float)((IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1 : 0)
                     - (float)((IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1 : 0);
            if (dx != 0.0f && dy != 0.0f) {
                dx *= 0.7071f;
                dy *= 0.7071f;
            }
            float nx = p->x + dx * PLAYER_SPEED * dt;
            if (!tg_world_rect_hits_solid(world, nx, p->y, p->w, p->h))
                p->x = nx;
            float ny = p->y + dy * PLAYER_SPEED * dt;
            if (!tg_world_rect_hits_solid(world, p->x, ny, p->w, p->h))
                p->y = ny;
            cam.target.x = p->x + p->w * 0.5f;
            cam.target.y = p->y + p->h * 0.5f;
        }

        world->fps = GetFPS();
        world->uptime_s = (float)GetTime();

        // ── 渲染 ──
        BeginDrawing();
        ClearBackground(tg_color(world->bg));
        tg_render_world(world, &cam);

        int actives = 0;
        for (int i = 0; i < world->entity_count; i++)
            actives += world->entities[i].active ? 1 : 0;
        DrawText(TextFormat("trogue %s | %s | fps %d | entities %d | reloads %d",
                            TROGUE_VERSION, world->scene_name, world->fps, actives,
                            world->reload_count),
                 10, 8, 12, RAYWHITE);
        DrawText(TextFormat("WASD move | F5 reload | F12 shot | IPC tcp://127.0.0.1:%d%s",
                            port, ipc ? "" : " (off)"),
                 10, 24, 12, (Color){ 200, 200, 210, 160 });
        EndDrawing();

        // 截图在帧绘制完成后处理。
        // 不用 TakeScreenshot：它会向路径强拼 CWD 前缀，绝对路径会被破坏。
        char shot_path[TROGUE_PATH_MAX];
        const char *shot_target = NULL;
        if (tg_ipc_take_screenshot(ipc, shot_path, sizeof(shot_path)))
            shot_target = shot_path;
        else if (IsKeyPressed(KEY_F12))
            shot_target = "screenshot_manual.png";
        if (shot_target) {
            Image img = LoadImageFromScreen();
            if (ExportImage(img, shot_target))
                TraceLog(LOG_INFO, "[main] 截图已保存: %s", shot_target);
            else
                TraceLog(LOG_ERROR, "[main] 截图保存失败: %s", shot_target);
            UnloadImage(img);
        }
    }

    TraceLog(LOG_INFO, "[main] 退出");
    tg_ipc_destroy(ipc);
    tg_watcher_destroy(watcher);
    tg_world_destroy(world);
    tg_render_shutdown(); // 释放实体独立贴图缓存（tileset 纹理已随 world 释放）
    CloseWindow();
    return 0;
}
