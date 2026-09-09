#!/usr/bin/env python3
"""tro-ipc v1.2 冒烟测试（v1.1 之上只增不改）。

用法:
    python3 tools/ipc_smoke.py [--port 48764] [--scene assets/scenes/demo.json]

前提: 引擎以 DEBUG 构建运行中 (./build/trogue)。
覆盖: 握手/ping/status/list_entities/spawn/set_entity/get_entity/
      despawn/观测命令(query_entities/layers/solid_at/get_tile)/
      screenshot/reload(位置保留)/回合命令(turn/move/wait)/
      事件通道(subscribe/unsubscribe/connections/events 协议断言)/quit。
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


class LineReader:
    """持久行读取器：一次 recv 可能含多行，rest 必须留给下一次（plan-5.6 §3）。"""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def next_line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed")
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")  # rest 保留在 buf
        return json.loads(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=48764)
    args = ap.parse_args()

    sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)
    reader = LineReader(sock)

    def rpc(**msg):
        sock.sendall((json.dumps(msg) + "\n").encode())
        return reader.next_line()

    print("== 握手 ==")
    hello = reader.next_line()
    check("hello 事件", hello.get("ok") is True and hello.get("event") == "hello", hello)

    print("== 基础命令 ==")
    r = rpc(cmd="ping")
    check("ping", r.get("ok") and r["data"].get("pong") is True, r)

    r = rpc(cmd="status")
    check("status 有场景名", r.get("ok") and len(r["data"].get("scene", "")) > 0, r)

    r = rpc(cmd="list_entities")
    base_count = r.get("data", {}).get("count", 0)
    check("list_entities >= 6", base_count >= 6, r)

    print("== 实体操作 ==")
    r = rpc(cmd="spawn", id="smoke_coin", type="coin", x=100, y=100, color="#00ff00")
    check("spawn", r.get("ok") and r["data"]["entity"]["id"] == "smoke_coin", r)

    r = rpc(cmd="get_entity", id="smoke_coin")
    # 回合制：实体坐标统一 tile 网格（像素 = grid*16），100 → grid 6 → 96px
    grid_x = (100 // 16) * 16
    check("get_entity 新实体（网格对齐）",
          r.get("ok") and abs(r["data"]["entity"]["x"] - grid_x) < 0.01, r)

    r = rpc(cmd="set_entity", id="player", x=64, y=80)
    check("set_entity 移动玩家",
          r.get("ok") and abs(r["data"]["entity"]["x"] - 64) < 0.01
          and abs(r["data"]["entity"]["y"] - 80) < 0.01, r)

    r = rpc(cmd="set_entity", id="player", color="#ff00ff")
    check("set_entity 颜色被接受", r.get("ok"), r)

    r = rpc(cmd="despawn", id="smoke_coin")
    check("despawn", r.get("ok"), r)

    r = rpc(cmd="get_entity", id="smoke_coin")
    check("despawn 后查询报错", r.get("ok") is False, r)

    print("== 观测命令（v1.1）==")
    # radius 按实体中心距离判定：从实体快照取 w/h 算中心，不硬编码尺寸
    r = rpc(cmd="get_entity", id="player")
    e = r["data"]["entity"]
    pcx, pcy = e["x"] + e["w"] / 2, e["y"] + e["h"] / 2
    r = rpc(cmd="query_entities", x=pcx, y=pcy, radius=8)
    check("query_entities 命中玩家",
          r.get("ok") and any(en["id"] == "player" for en in r["data"]["entities"]), r)

    r = rpc(cmd="query_entities", type="nonexistent", x=0, y=0, radius=10000)
    check("query_entities type 过滤为空", r.get("ok") and r["data"]["count"] == 0, r)

    r = rpc(cmd="query_entities", rect=[0, 0, 10000, 10000])
    # 前提：demo.json 全部实体 x,y≥0 且 w,h>0（与 [0,0,10000,10000] 相交），smoke_coin 已 despawn
    check("query_entities rect 全图", r.get("ok") and r["data"]["count"] == base_count, r)

    r = rpc(cmd="layers")
    names = [l["name"] for l in r.get("data", {}).get("layers", [])]
    check("layers 列出 ground/walls",
          r.get("ok") and "ground" in names and "walls" in names
          and r["data"]["layers"][0].get("tileset") is None, r)

    r = rpc(cmd="solid_at", x=8, y=8)
    check("solid_at 返回 bool", r.get("ok") and isinstance(r["data"]["solid"], bool), r)

    r = rpc(cmd="get_tile", x=8, y=8)
    check("get_tile 结构", r.get("ok") and isinstance(r["data"]["tiles"], list)
          and isinstance(r["data"]["solid"], bool), r)

    r = rpc(cmd="query_entities", x=0, y=0)
    check("query_entities 缺 radius 报错", r.get("ok") is False, r)

    r = rpc(cmd="get_entity", id="player")
    check("实体快照含 color（回归）", r.get("ok") and "color" in r["data"]["entity"], r)

    # inspector transform 视图：静止时视觉位置 == 逻辑坐标（Agent 观测锚点；
    # 若迁移/插值造成错位，此处即暴露——2026-09-09 用户拍板）
    tf = r["data"]["entity"].get("transform")
    tfv = tf and tf.get("visual")
    check("实体快照含 transform 视图",
          r.get("ok") and tf is not None and isinstance(tfv, list)
          and isinstance(tf.get("moving"), bool), r)
    check("静止时 visual == 逻辑坐标",
          r.get("ok") and tfv is not None
          and abs(tfv[0] - r["data"]["entity"]["x"]) < 1e-6
          and abs(tfv[1] - r["data"]["entity"]["y"]) < 1e-6, r)

    print("== 截图 ==")
    shot = "/tmp/trogue_smoke.png"
    if os.path.exists(shot):
        os.remove(shot)
    r = rpc(cmd="screenshot", path=shot)
    check("screenshot 请求", r.get("ok") and r["data"]["path"] == shot, r)
    for _ in range(30):
        time.sleep(0.1)
        if os.path.exists(shot) and os.path.getsize(shot) > 0:
            break
    check("截图文件已写出", os.path.exists(shot) and os.path.getsize(shot) > 0)

    print("== 热重载（位置保留）==")
    r = rpc(cmd="reload")
    check("reload 命令", r.get("ok") and r["data"].get("reloads", 0) >= 1, r)
    r = rpc(cmd="get_entity", id="player")
    px = r["data"]["entity"]["x"]
    check("重载后运行时位置保留 (x≈64)",
          r.get("ok") and abs(px - 64) < 0.01, f"x={px}")

    print("== 回合命令（turn/move/wait，场景无关）==")
    r = rpc(cmd="turn")
    check("turn 返回结构",
          r.get("ok") and r["data"].get("phase") in ("player", "enemy")
          and isinstance(r["data"].get("turn_count"), int)
          and r["data"].get("player") is not None
          and isinstance(r["data"].get("enemies"), list), r)
    tc0 = r["data"]["turn_count"]

    # 尝试四个方向直到某个 move 成功（demo 墙很少，必然有一个方向可走）
    moved = False
    for dx, dy in ((1, 0), (0, 1), (0, -1), (-1, 0)):
        r = rpc(cmd="move", dx=dx, dy=dy)
        if r.get("ok") and r["data"].get("result") == "moved":
            moved = True
            break
    check("move 于某方向成功", moved, r)
    r = rpc(cmd="turn")
    check("move 结算后回合 +1",
          r.get("ok") and r["data"]["turn_count"] == tc0 + 1,
          f"{tc0} → {r['data']['turn_count']}")

    r = rpc(cmd="move", dx=0, dy=0)
    check("move (0,0) 拒绝为 invalid",
          r.get("ok") and r["data"].get("result") == "invalid", r)

    r = rpc(cmd="move", dx=0.5, dy=0)
    check("move 非整数 dx 报错（不静默截断）",
          r.get("ok") is False and "integer" in r.get("error", ""), r)

    r = rpc(cmd="wait")
    check("wait 成功", r.get("ok") and r["data"].get("result") == "waited", r)
    r = rpc(cmd="turn")
    check("wait 后回合再 +1",
          r.get("ok") and r["data"]["turn_count"] == tc0 + 2,
          f"{tc0+1} → {r['data']['turn_count']}")

    print("== 事件通道（tro-ipc v1.2 协议断言，无 game 事件）==")
    # 事件目录：game 当前未注册事件（机制就位，注册表为空）
    r = rpc(cmd="events")
    check("events 目录（空注册表）",
          r.get("ok") and r["data"].get("events") == [], r)

    r = rpc(cmd="help")
    cmds = r.get("data", {}).get("commands", [])
    check("help 登记通道命令",
          all(c in cmds for c in
              ("subscribe", "unsubscribe", "connections", "events")), r)

    # 独立长连接订阅（主 sock 保持短连接 RPC 语义，互不干扰）
    ev_sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)
    ev_reader = LineReader(ev_sock)
    hello2 = ev_reader.next_line()
    check("长连接 hello 问候", hello2.get("event") == "hello", hello2)

    def evrpc(**msg):
        ev_sock.sendall((json.dumps(msg) + "\n").encode())
        return ev_reader.next_line()

    r = evrpc(cmd="subscribe", events=["X"], filter={"entity": "player"})
    check("subscribe 回显全量集（含 filter）",
          r.get("ok") and r["data"]["events"] ==
          [{"event": "X", "filter": {"entity": "player"}}], r)

    r = evrpc(cmd="connections")
    subs = [c for c in r.get("data", {}).get("connections", []) if c.get("events")]
    check("connections 列出订阅连接",
          r.get("ok") and len(subs) == 1
          and subs[0]["events"][0]["event"] == "X", r)

    r = evrpc(cmd="subscribe", events=["hello"])
    check("subscribe 保留名拒绝",
          r.get("ok") is False and "reserved" in r.get("error", ""), r)

    r = evrpc(cmd="subscribe", events=[])
    check("subscribe 空数组报错", r.get("ok") is False, r)

    r = evrpc(cmd="subscribe", events=["Y"], filter="bad")
    check("subscribe filter 非 object 报错", r.get("ok") is False, r)

    r = evrpc(cmd="unsubscribe", events=[])
    check("unsubscribe 空数组 no-op 回显",
          r.get("ok") and [e["event"] for e in r["data"]["events"]] == ["X"], r)

    r = evrpc(cmd="unsubscribe", events=["X"])
    check("unsubscribe 按名移除（含 filter 变体）",
          r.get("ok") and r["data"]["events"] == [], r)

    # 短连接 RPC 零干扰
    r = rpc(cmd="ping")
    check("短连接 RPC 零干扰", r.get("ok") and r["data"].get("pong") is True, r)

    ev_sock.close()
    time.sleep(0.3)  # 等 server poll 感知 EOF（断开即订阅清零）
    r = rpc(cmd="connections")
    check("断开即订阅清零（connections 回落）",
          r.get("ok") and len(r["data"]["connections"]) == 1
          and all(not c.get("events") for c in r["data"]["connections"]), r)

    print("== 退出 ==")
    r = rpc(cmd="quit")
    check("quit", r.get("ok") and r["data"].get("bye") is True, r)

    sock.close()
    print(f"\n结果: {PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
