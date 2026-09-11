#!/usr/bin/env python3
"""起步游戏 IPC 冒烟测试（模板自有文件，随你的游戏命令集增改）。

用法:
    python3 tools/ipc_smoke.py [--port 48764]

前提: 游戏以 DEBUG 构建运行中 (./build/bin/trogue)。
覆盖: 握手 / ping / status / list_entities / get_entity / move / screenshot / quit。
（本文件不随 template/scripts/sync_from_source.sh 覆盖；新增命令时在此补断言。）
"""

import argparse
import json
import socket


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
        line, _, self.buf = self.buf.partition(b"\n")
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
    check("hello 事件", hello.get("ok") is True and hello.get("event") == "hello",
          hello)

    print("== 基础命令 ==")
    r = rpc(cmd="ping")
    check("ping", r.get("ok") and r["data"].get("pong") is True, r)

    r = rpc(cmd="status")
    check("status 有场景名", r.get("ok") and len(r["data"].get("scene", "")) > 0, r)

    r = rpc(cmd="list_entities")
    check("list_entities 非空", r.get("ok") and r["data"].get("count", 0) > 0, r)

    print("== 实体查询与移动 ==")
    r = rpc(cmd="get_entity", id="player")
    check("get_entity player", r.get("ok") and
          r["data"]["entity"]["id"] == "player", r)
    start = r["data"]["entity"] if r.get("ok") else {"x": -1, "y": -1}

    # 尝试四个方向直到某次移动成功（起步场景四面墙，必有方向可走）
    moved = False
    for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
        mr = rpc(cmd="move", dx=dx, dy=dy)
        if mr.get("ok") and mr["data"].get("moved"):
            moved = True
            break
    check("move 至少一方向成功", moved, mr)

    r = rpc(cmd="get_entity", id="player")
    after = r["data"]["entity"] if r.get("ok") else start
    check("move 后逻辑位置改变",
          (after["x"], after["y"]) != (start["x"], start["y"]), (start, after))

    r = rpc(cmd="get_entity", id="__no_such_entity__")
    check("get_entity 不存在报错", r.get("ok") is False, r)

    print("== 截图与退出 ==")
    r = rpc(cmd="screenshot", path="build/smoke_shot.png")
    check("screenshot 受理", r.get("ok") and r["data"].get("path"), r)

    r = rpc(cmd="quit")
    check("quit", r.get("ok") and r["data"].get("bye") is True, r)

    print(f"\n== 结果: {PASS} passed, {FAIL} failed ==")
    sock.close()
    return 1 if FAIL else 0


if __name__ == "__main__":
    raise SystemExit(main())
