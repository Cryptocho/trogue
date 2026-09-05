#!/usr/bin/env python3
"""tro-ipc v1.1 冒烟测试。

用法:
    python3 tools/ipc_smoke.py [--port 48764] [--scene assets/scenes/demo.json]

前提: 引擎以 DEBUG 构建运行中 (./build/trogue)。
覆盖: 握手/ping/status/list_entities/spawn/set_entity/get_entity/
      despawn/观测命令(query_entities/layers/solid_at/get_tile)/
      screenshot/reload(位置保留)/quit。
"""

import argparse
import json
import os
import socket
import sys
import time

PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {detail}")


def recv_line(sock):
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("connection closed")
        buf += chunk
    line, _, rest = buf.partition(b"\n")
    return json.loads(line), rest


def rpc(sock, **msg):
    sock.sendall((json.dumps(msg) + "\n").encode())
    resp, _ = recv_line(sock)
    return resp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=48764)
    args = ap.parse_args()

    sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)

    print("== 握手 ==")
    hello, _ = recv_line(sock)
    check("hello 事件", hello.get("ok") is True and hello.get("event") == "hello", hello)

    print("== 基础命令 ==")
    r = rpc(sock, cmd="ping")
    check("ping", r.get("ok") and r["data"].get("pong") is True, r)

    r = rpc(sock, cmd="status")
    check("status 有场景名", r.get("ok") and len(r["data"].get("scene", "")) > 0, r)

    r = rpc(sock, cmd="list_entities")
    base_count = r.get("data", {}).get("count", 0)
    check("list_entities >= 6", base_count >= 6, r)

    print("== 实体操作 ==")
    r = rpc(sock, cmd="spawn", id="smoke_coin", type="coin", x=100, y=100, color="#00ff00")
    check("spawn", r.get("ok") and r["data"]["entity"]["id"] == "smoke_coin", r)

    r = rpc(sock, cmd="get_entity", id="smoke_coin")
    check("get_entity 新实体", r.get("ok") and abs(r["data"]["entity"]["x"] - 100) < 0.01, r)

    r = rpc(sock, cmd="set_entity", id="player", x=64, y=80)
    check("set_entity 移动玩家",
          r.get("ok") and abs(r["data"]["entity"]["x"] - 64) < 0.01
          and abs(r["data"]["entity"]["y"] - 80) < 0.01, r)

    r = rpc(sock, cmd="set_entity", id="player", color="#ff00ff")
    check("set_entity 颜色被接受", r.get("ok"), r)

    r = rpc(sock, cmd="despawn", id="smoke_coin")
    check("despawn", r.get("ok"), r)

    r = rpc(sock, cmd="get_entity", id="smoke_coin")
    check("despawn 后查询报错", r.get("ok") is False, r)

    print("== 观测命令（v1.1）==")
    # radius 按实体中心距离判定：从实体快照取 w/h 算中心，不硬编码尺寸
    r = rpc(sock, cmd="get_entity", id="player")
    e = r["data"]["entity"]
    pcx, pcy = e["x"] + e["w"] / 2, e["y"] + e["h"] / 2
    r = rpc(sock, cmd="query_entities", x=pcx, y=pcy, radius=8)
    check("query_entities 命中玩家",
          r.get("ok") and any(en["id"] == "player" for en in r["data"]["entities"]), r)

    r = rpc(sock, cmd="query_entities", type="nonexistent", x=0, y=0, radius=10000)
    check("query_entities type 过滤为空", r.get("ok") and r["data"]["count"] == 0, r)

    r = rpc(sock, cmd="query_entities", rect=[0, 0, 10000, 10000])
    # 前提：demo.json 全部实体 x,y≥0 且 w,h>0（与 [0,0,10000,10000] 相交），smoke_coin 已 despawn
    check("query_entities rect 全图", r.get("ok") and r["data"]["count"] == base_count, r)

    r = rpc(sock, cmd="layers")
    names = [l["name"] for l in r.get("data", {}).get("layers", [])]
    check("layers 列出 ground/walls",
          r.get("ok") and "ground" in names and "walls" in names
          and r["data"]["layers"][0].get("tileset") is None, r)

    r = rpc(sock, cmd="solid_at", x=8, y=8)
    check("solid_at 返回 bool", r.get("ok") and isinstance(r["data"]["solid"], bool), r)

    r = rpc(sock, cmd="get_tile", x=8, y=8)
    check("get_tile 结构", r.get("ok") and isinstance(r["data"]["tiles"], list)
          and isinstance(r["data"]["solid"], bool), r)

    r = rpc(sock, cmd="query_entities", x=0, y=0)
    check("query_entities 缺 radius 报错", r.get("ok") is False, r)

    r = rpc(sock, cmd="get_entity", id="player")
    check("实体快照含 color（回归）", r.get("ok") and "color" in r["data"]["entity"], r)

    print("== 截图 ==")
    shot = "/tmp/trogue_smoke.png"
    if os.path.exists(shot):
        os.remove(shot)
    r = rpc(sock, cmd="screenshot", path=shot)
    check("screenshot 请求", r.get("ok") and r["data"]["path"] == shot, r)
    for _ in range(30):
        time.sleep(0.1)
        if os.path.exists(shot) and os.path.getsize(shot) > 0:
            break
    check("截图文件已写出", os.path.exists(shot) and os.path.getsize(shot) > 0)

    print("== 热重载（位置保留）==")
    r = rpc(sock, cmd="reload")
    check("reload 命令", r.get("ok") and r["data"].get("reloads", 0) >= 1, r)
    r = rpc(sock, cmd="get_entity", id="player")
    px = r["data"]["entity"]["x"]
    check("重载后运行时位置保留 (x≈64)",
          r.get("ok") and abs(px - 64) < 0.01, f"x={px}")

    print("== 退出 ==")
    r = rpc(sock, cmd="quit")
    check("quit", r.get("ok") and r["data"].get("bye") is True, r)

    sock.close()
    print(f"\n结果: {PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
