#ifndef TROGUE_IPC_H
#define TROGUE_IPC_H

#include <stdbool.h>

#include "world.h"

// DEBUG 构建（TROGUE_DEBUG）专用调试通道：tro-ipc v1。
// TCP 仅监听 127.0.0.1，JSON-lines 协议，协议细节见 AGENTS.md。
// Release 构建下所有函数为安全 no-op（start 返回 NULL）。

typedef struct TgIpc TgIpc;

TgIpc *tg_ipc_start(int port, TgWorld *world);
void   tg_ipc_destroy(TgIpc *ipc);

// 每帧调用一次：accept 新连接、处理命令、回复
void tg_ipc_poll(TgIpc *ipc);

// 本帧收到 screenshot 请求时返回 true 并填入输出路径（无 path 参数时自动生成）
bool tg_ipc_take_screenshot(TgIpc *ipc, char *path, int cap);

// 收到 quit 命令后为 true（应用据此退出主循环）
bool tg_ipc_quit_requested(TgIpc *ipc);

#endif // TROGUE_IPC_H
