#!/usr/bin/env python3
"""tro-ipc v1.2 冒烟测试（v1.1 之上只增不改）。

用法:
    python3 tools/ipc_smoke.py [--port 48764] [--scene assets/scenes/demo.json]

前提: 引擎以 DEBUG 构建运行中 (./build/trogue)。
覆盖: 握手/ping/status/list_entities/spawn/set_entity/get_entity/
      despawn/观测命令(query_entities/layers/solid_at/get_tile)/
      screenshot/reload(位置保留)/回合命令(turn/move/wait)/
      事件通道(subscribe/unsubscribe/connections/events 协议断言)/
      游戏事件(注册表实表/hp-ai 快照/filter 单实体观测/GameOver 前的伤害)/quit。
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
    """持久行读取器：一次 recv 可能含多行，rest 必须留给下一次。"""

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

    # 碰撞几何原语（probe_collide）：segment 命中/未命中、sweep 接触、非法参数
    r = rpc(cmd="probe_collide", a=[24, 72], b=[640, 72])
    seg = r.get("data", {}).get("segment", {})
    check("probe_collide segment 命中墙",
          r.get("ok") and seg.get("result") == "solid"
          and seg.get("tx") == 20 and seg.get("ty") == 4, r)

    r = rpc(cmd="probe_collide", a=[24, 24], b=[600, 24])
    check("probe_collide segment 空区 clear",
          r.get("ok") and r["data"]["segment"]["result"] == "clear", r)

    r = rpc(cmd="probe_collide", rect=[24, 24, 16, 16], delta=[0, 400])
    sw = r.get("data", {}).get("sweep", {})
    check("probe_collide sweep 停在墙前",
          r.get("ok") and sw.get("result") == "solid"
          and sw.get("blocked_y") is True
          and abs(sw["box"][1] - 352.0) < 0.1, r)

    r = rpc(cmd="probe_collide")
    check("probe_collide 缺参数报错", r.get("ok") is False, r)

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

    print("== 事件通道（tro-ipc v1.2 协议断言）==")
    # 事件目录：首批 6 个对外游戏事件实表（内部事件不登记）
    k_wire_events = {"StateChanged", "MoveSucceeded", "AbilityUsed",
                     "DamageDealt", "EntityDied", "TurnEnded"}
    r = rpc(cmd="events")
    names = {e.get("name") for e in r.get("data", {}).get("events", [])}
    check("events 目录 6 事件", r.get("ok") and names == k_wire_events, r)

    r = rpc(cmd="help")
    cmds = r.get("data", {}).get("commands", [])
    check("help 登记通道命令",
          all(c in cmds for c in
              ("subscribe", "unsubscribe", "connections", "events")), r)
    check("help 登记 probe_collide", "probe_collide" in cmds, r)
    check("help 登记 reload_texture", "reload_texture" in cmds, r)

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

    print("== 游戏事件（EventBus → IPC 桥）==")

    def drain(reader, count_max=64, timeout=0.4):
        """非阻塞收集一段时间内到达的事件行（超时即停；残行留在缓冲）。"""
        reader.sock.settimeout(timeout)
        out = []
        for _ in range(count_max):
            try:
                out.append(reader.next_line())
            except (socket.timeout, TimeoutError):
                break
        return [m for m in out if m.get("event") not in (None, "hello")]

    # 快照注入：goblin 有 hp/ai；coin（惰性实体）两者皆无
    r = rpc(cmd="get_entity", id="goblin_1")
    g = r.get("data", {}).get("entity", {})
    check("goblin 快照含 hp/ai",
          isinstance(g.get("hp"), list) and isinstance(g.get("ai"), dict)
          and g["ai"].get("state") in ("idle", "alerted", "chasing"), r)
    r = rpc(cmd="spawn", id="smoke_coin2", type="coin", x=200, y=200)
    check("spawn coin", r.get("ok"), r)
    r = rpc(cmd="get_entity", id="smoke_coin2")
    c = r.get("data", {}).get("entity", {})
    check("coin 无 hp/ai（惰性门控）",
          "hp" not in c and "ai" not in c, r)
    rpc(cmd="despawn", id="smoke_coin2")

    # 连接 A：全量订阅 6 事件（无 filter）
    a_sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)
    a_reader = LineReader(a_sock)
    a_reader.next_line()  # hello
    a_sock.sendall((json.dumps({
        "cmd": "subscribe",
        "events": ["StateChanged", "MoveSucceeded", "AbilityUsed",
                   "DamageDealt", "EntityDied", "TurnEnded"]
    }) + "\n").encode())
    r = a_reader.next_line()
    check("A 全量订阅", r.get("ok") and len(r["data"]["events"]) == 6, r)

    # 玩家 move（四方向试到成功）：A 应收到玩家 MoveSucceeded + TurnEnded
    moved = False
    for dx, dy in ((1, 0), (0, 1), (0, -1), (-1, 0)):
        r = rpc(cmd="move", dx=dx, dy=dy)
        if r.get("ok") and r["data"].get("result") == "moved":
            moved = True
            break
    check("move（事件段）成功", moved, r)
    evs = drain(a_reader)
    kinds = [m["event"] for m in evs]
    check("A 收到 TurnEnded", "TurnEnded" in kinds, kinds)
    check("A 收到玩家 MoveSucceeded",
          any(m["event"] == "MoveSucceeded"
              and m["data"].get("entity") == "player" for m in evs), kinds)

    # 连接 B：按字段订阅——{entity:goblin_1} 观测状态/移动/攻击，
    # {target:player} 观测受伤（同连接多 filter 并存 + 缺键不匹配反向验证）
    b_sock = socket.create_connection(("127.0.0.1", args.port), timeout=5)
    b_reader = LineReader(b_sock)
    b_reader.next_line()  # hello
    b_sock.sendall((json.dumps({
        "cmd": "subscribe",
        "events": ["StateChanged", "MoveSucceeded", "AbilityUsed", "EntityDied"],
        "filter": {"entity": "goblin_1"},
    }) + "\n").encode())
    b_reader.next_line()
    b_sock.sendall((json.dumps({
        "cmd": "subscribe",
        "events": ["DamageDealt"],
        "filter": {"target": "player"},
    }) + "\n").encode())
    b_reader.next_line()

    # 传送 goblin_1 到玩家邻格：先取玩家当前格（回合命令段可能移动过），
    # 再按 8 邻格探测通行格（攻击射程 chebyshev ≤1 含斜邻）。通行 = 非墙
    # 且无其他实体占位（query_entities rect；否则 set_entity 会造成重叠、
    # 违反实体互斥语义——检查人建议 7）
    r = rpc(cmd="get_entity", id="player")
    p = r["data"]["entity"]
    pgx, pgy = int(p["x"]) // 16, int(p["y"]) // 16
    target = None
    for cx, cy in ((pgx - 1, pgy), (pgx + 1, pgy), (pgx, pgy - 1),
                   (pgx, pgy + 1), (pgx - 1, pgy - 1), (pgx + 1, pgy - 1),
                   (pgx - 1, pgy + 1), (pgx + 1, pgy + 1)):
        r = rpc(cmd="solid_at", x=cx * 16 + 8, y=cy * 16 + 8)
        if not (r.get("ok") and r["data"]["solid"] is False):
            continue
        r = rpc(cmd="query_entities",
                rect=[cx * 16, cy * 16, 16, 16])
        if r.get("ok") and r["data"]["count"] == 0:
            target = (cx, cy)
            break
    check("找到玩家邻格通行格", target is not None)
    r = rpc(cmd="set_entity", id="goblin_1", x=target[0] * 16, y=target[1] * 16)
    check("传送 goblin_1 至玩家邻格", r.get("ok"), r)

    # wait#1：idle→alerted（当回合停；ALERT_DELAY=1）
    rpc(cmd="wait")
    evs = drain(b_reader)
    check("B: idle→alerted",
          any(m["event"] == "StateChanged"
              and m["data"].get("entity") == "goblin_1"
              and m["data"].get("from") == "idle"
              and m["data"].get("to") == "alerted" for m in evs), evs)
    check("B: TurnEnded 不达（缺键不匹配）",
          all(m["event"] != "TurnEnded" for m in evs), evs)

    # wait#2：alerted→chasing → 贴脸攻击：DamageDealt(95) → AbilityUsed
    rpc(cmd="wait")
    evs = drain(b_reader)
    kinds = [m["event"] for m in evs]
    check("B: alerted→chasing",
          any(m["event"] == "StateChanged"
              and m["data"].get("to") == "chasing" for m in evs), evs)
    dd = [m for m in evs if m["event"] == "DamageDealt"]
    au = [m for m in evs if m["event"] == "AbilityUsed"]
    check("B: DamageDealt(target=player, hp=95)",
          len(dd) == 1 and dd[0]["data"].get("source") == "goblin_1"
          and dd[0]["data"].get("target") == "player"
          and dd[0]["data"].get("amount") == 5
          and dd[0]["data"].get("hp") == 95, evs)
    check("B: AbilityUsed(entity=goblin_1)", len(au) == 1, evs)
    check("B: wire 序 DamageDealt → AbilityUsed（原版顺序）",
          len(dd) == 1 and len(au) == 1 and kinds.index("DamageDealt")
          < kinds.index("AbilityUsed"), kinds)
    check("B: TurnEnded 仍不达", all(m["event"] != "TurnEnded" for m in evs), evs)

    # 玩家实际掉血（快照与事件一致）
    r = rpc(cmd="get_entity", id="player")
    check("玩家 hp 快照 95",
          r.get("ok") and r["data"]["entity"].get("hp") == [95, 100], r)

    a_sock.close()
    b_sock.close()
    time.sleep(0.3)

    print("== 地形写入（set_tile → 查询立即可见）==")
    # 依赖 demo 场景（palette 模式，walls 层 index 1 solid），须在 genmap 换场景之前。
    # 坐标口径：set_tile 收层局部 tile 坐标；get_tile/solid_at 收世界像素坐标。
    r = rpc(cmd="solid_at", x=8, y=8)
    check("solid_at (0,0) 基线为墙", r.get("ok") and r["data"]["solid"] is True, r)
    r = rpc(cmd="set_tile", layer=1, tx=0, ty=0, value=-1)
    check("set_tile 挖墙成功",
          r.get("ok") and r["data"].get("set") is True
          and r["data"].get("tx") == 0, r)
    r = rpc(cmd="solid_at", x=8, y=8)
    check("挖墙后 solid_at 翻转", r.get("ok") and r["data"]["solid"] is False, r)
    r = rpc(cmd="get_tile", x=8, y=8)
    check("get_tile 墙格立即可见为空",
          r.get("ok") and all(t["layer"] != 1 for t in r["data"]["tiles"]), r)
    r = rpc(cmd="set_tile", layer=1, tx=0, ty=0, value=1)
    check("set_tile 填墙成功", r.get("ok"), r)
    r = rpc(cmd="solid_at", x=8, y=8)
    check("填墙后 solid_at 还原", r.get("ok") and r["data"]["solid"] is True, r)
    r = rpc(cmd="set_tile", layer=1, tx=0, ty=0, value=99)
    check("set_tile 值超值域报错", r.get("ok") is False, r)
    r = rpc(cmd="set_tile", layer=1, tx=999, ty=0, value=-1)
    check("set_tile 坐标越界报错", r.get("ok") is False, r)
    r = rpc(cmd="set_tile", layer=1, tx=0, ty=0)
    check("set_tile 缺参报错", r.get("ok") is False, r)
    r = rpc(cmd="help")
    check("help 登记 set_tile", "set_tile" in r["data"]["commands"], r)

    print("== 程序生成地图（genmap 确定性契约）==")
    # 注意：genmap 会 swap 场景（实体清空、reloads 自增），故置于实体相关段之后；
    # reloads 为计数器字段，两次调用必然不同，不参与内容一致性比较。
    r = rpc(cmd="genmap", seed=42)
    g1 = r.get("data", {}) if r.get("ok") else None
    check("genmap 生成成功", g1 is not None and g1.get("generated") is True
          and g1.get("seed") == 42 and g1.get("w") == 40 and g1.get("h") == 40, r)
    r = rpc(cmd="genmap", seed=42)
    g2 = r.get("data", {}) if r.get("ok") else None
    check("genmap 同 seed 确定性复现",
          g2 is not None and g2.get("nonempty") == g1.get("nonempty")
          and g2.get("w") == g1.get("w") and g2.get("h") == g1.get("h"), (g1, g2))
    # 异 seed：nonempty 计数有极小概率恰好相等（~1% 量级），按序多试几个
    # seed 取首个不同者，消除常跑冒烟的偶发假失败
    g3 = None
    for seed in (43, 44, 45, 46, 47):
        r = rpc(cmd="genmap", seed=seed)
        g3 = r.get("data", {}) if r.get("ok") else None
        if g3 and g3.get("nonempty") != g1.get("nonempty"):
            break
    check("genmap 异 seed 结果不同",
          g3 is not None and g3.get("nonempty") != g1.get("nonempty"), (g1, g3))
    r = rpc(cmd="genmap", seed="bad")
    check("genmap 非整数 seed 报错",
          r.get("ok") is False and "integer" in r.get("error", ""), r)

    print("== 贴图缓存失效（reload_texture）==")
    # demo.json 的 tex_probe 实体持有 textures/goblin.png（独立贴图缓存路径）。
    # 失效语义：合法路径 = 生效（无论此前是否已缓存），命令级软失败不存在——
    # 引擎拒绝（路径不安全）→ demo 转错误包络。
    r = rpc(cmd="reload_texture", path="textures/goblin.png")
    check("reload_texture 合法路径生效",
          r.get("ok") and r["data"].get("reloaded") is True
          and r["data"].get("path") == "textures/goblin.png", r)
    r = rpc(cmd="reload_texture", path="../evil.png")
    check("reload_texture 不安全路径拒绝",
          r.get("ok") is False and "unsafe" in r.get("error", ""), r)
    r = rpc(cmd="reload_texture")
    check("reload_texture 缺 path 报错", r.get("ok") is False, r)

    print("== 退出 ==")
    r = rpc(cmd="quit")
    check("quit", r.get("ok") and r["data"].get("bye") is True, r)

    sock.close()
    print(f"\n结果: {PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
