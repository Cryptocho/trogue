#!/usr/bin/env python3
"""起步游戏 IPC 冒烟测试（模板自有文件，随你的游戏命令集增改）。

用法:
    python3 tools/ipc_smoke.py [--port 48764]

前提: 游戏以 DEBUG 构建运行中 (./build/bin/trogue)。
覆盖: 握手 / ping / status / list_entities / get_entity / move（排队语义） /
screenshot / log / quit + 确定性模拟：pause（真实时间冻结）/ step_frames（授步
精确推进）/ input（虚拟注入步边界消费）/ input_flush / input_stats / resume +
每条命令的负向路径（类型错/缺字段/越界/未知命令 → 错误包络，且非引擎兜底
`internal error`）。
（本文件不随 template/scripts/sync_from_source.sh 覆盖；新增命令时在此补断言。）
"""

import argparse
import json
import socket
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

    def entity(id_="player"):
        r = rpc(cmd="get_entity", id=id_)
        return r["data"]["entity"] if r.get("ok") else None

    print("== 握手 ==")
    hello = reader.next_line()
    check("hello 事件", hello.get("ok") is True and hello.get("event") == "hello",
          hello)

    print("== 基础命令 ==")
    r = rpc(cmd="ping")
    check("ping", r.get("ok") and r["data"].get("pong") is True, r)

    r = rpc(cmd="status")
    check("status 有场景名与模拟口径字段",
          r.get("ok") and len(r["data"].get("scene", "")) > 0
          and "paused" in r["data"] and "steps" in r["data"], r)

    r = rpc(cmd="list_entities")
    check("list_entities 非空", r.get("ok") and r["data"].get("count", 0) > 0, r)

    r = rpc(cmd="get_entity", id="player")
    check("get_entity player", r.get("ok") and
          r["data"]["entity"]["id"] == "player", r)

    r = rpc(cmd="get_entity", id="__no_such_entity__")
    check("get_entity 不存在报错", r.get("ok") is False, r)

    print("== 确定性模拟：pause / move 排队 / step_frames ==")
    r = rpc(cmd="pause")
    check("pause", r.get("ok") and r["data"]["paused"] is True, r)

    start = entity()
    sx, sy = start["x"], start["y"]
    r = rpc(cmd="move", dx=1, dy=0)
    check("move 排队受理（queued 语义）",
          r.get("ok") and r["data"].get("queued") is True, r)

    time.sleep(0.2)  # 暂停下真实时间不产生步：位置必须冻结
    frozen = entity()
    check("pause 下 move 不执行（真实时间冻结）",
          (frozen["x"], frozen["y"]) == (sx, sy), (start, frozen))

    r = rpc(cmd="step_frames", n=1)
    stepped = entity()
    check("step_frames 1 → 恰好消费一条动作（精确 +1 格）",
          r.get("ok") and stepped["x"] == sx + 16 and stepped["y"] == sy,
          (start, stepped, r))

    # 连续两格（起步场景 crate 在玩家右侧第 4 格，向右最多 +3）：
    # 动作队列按消费计数推进，不按像素
    for _ in range(2):
        rpc(cmd="move", dx=1, dy=0)
    rpc(cmd="step_frames", n=2)
    moved3 = entity()
    check("step_frames 2 → 累计 +3 格",
          moved3["x"] == sx + 48 and moved3["y"] == sy, (start, moved3))

    print("== 虚拟注入：input 在步边界生效 ==")
    r = rpc(cmd="input", key="KEY_DOWN", down=True)  # 下方无阻挡
    check("input 注入受理", r.get("ok") and r["data"].get("queued") is True, r)
    before_inject = entity()
    rpc(cmd="step_frames", n=2)  # 步 A：down 边沿→动作（本步消费）；步 B：空闲
    stats = rpc(cmd="input_stats")["data"]
    check("注入已消费（pending==0 且 down 置位）",
          stats["pending"] == 0 and stats.get("down"), stats)
    after_inject = entity()
    check("注入动作同边界生效（+1 格）",
          after_inject["y"] == before_inject["y"] + 16
          and after_inject["x"] == before_inject["x"],
          (before_inject, after_inject))

    r = rpc(cmd="input_flush")
    check("input_flush", r.get("ok") and r["data"]["flushed"] is True, r)

    print("== 视觉收敛（固定步 tween 整步边界精确落格） ==")
    rpc(cmd="step_frames", n=30)  # kMoveDuration≈0.12s@60Hz≈8 步，30 步留足裕量
    final = entity()
    tf = final["transform"]
    check("tween 完成（moving==false）", tf["moving"] is False, tf)
    check("visual 精确等于逻辑格（无残差）",
          abs(tf["visual"][0] - final["x"]) < 1e-3
          and abs(tf["visual"][1] - final["y"]) < 1e-3, (tf, final))

    print("== 负向路径（IPC 参数是不可信输入）==")

    def check_err(name, r):
        # 错误包络且非引擎兜底：`internal error` 意味着 handler 抛了异常 = 校验缺失
        check(name, r.get("ok") is False
              and "internal error" not in r.get("error", ""), r)

    def check_tolerates(name, r):
        # 无参数命令对多余字段宽容（不应因此报错）
        check(name, r.get("ok") is True, r)

    r = rpc(cmd="get_entity", id=123)
    check_err("get_entity id 非字符串报错", r)
    r = rpc(cmd="get_entity")
    check_err("get_entity 缺 id 报错", r)

    r = rpc(cmd="move", dx=0.5, dy=0)
    check_err("move dx 浮点报错", r)
    r = rpc(cmd="move", dx=2, dy=0)
    check_err("move dx 越界报错", r)
    r = rpc(cmd="move", dx="x", dy=0)
    check_err("move dx 字符串报错", r)

    r = rpc(cmd="step_frames", n="x")
    check_err("step_frames n 非整数报错", r)
    r = rpc(cmd="step_frames", n=0)
    check_err("step_frames n=0 报错", r)
    r = rpc(cmd="step_frames", n=99999)
    check_err("step_frames n 超上限报错", r)

    r = rpc(cmd="input", key="KEY_RIGHT", down="yes")
    check_err("input down 非布尔报错", r)
    r = rpc(cmd="input", key="KEY_NOPE", down=True)
    check_err("input 未知键名报错", r)
    r = rpc(cmd="input", key=1.5, down=True)
    check_err("input key 浮点报错", r)
    r = rpc(cmd="input", key=-2147483649, down=True)
    check_err("input key 低于 int 下界报错", r)
    r = rpc(cmd="input", key="KEY_RIGHT", down=True, t="bad")
    check_err("input t 非数值报错", r)

    r = rpc(cmd="screenshot", path=123)
    check_err("screenshot path 非字符串报错", r)
    r = rpc(cmd="log", msg=123)
    check_err("log msg 非字符串报错", r)

    r = rpc(cmd="no_such_command")
    check_err("未知命令报错（不崩溃）", r)

    # 无参数命令：多余字段应被宽容
    r = rpc(cmd="help")
    cmds = r.get("data", {}).get("commands", [])
    check("help 返回命令集",
          r.get("ok") and all(c in cmds for c in
                              ("status", "list_entities", "get_entity", "move",
                               "screenshot", "log", "quit", "pause", "resume",
                               "step_frames", "input", "input_flush",
                               "input_stats")), r)
    check_tolerates("help 容忍多余字段", rpc(cmd="help", junk=1))
    check_tolerates("status 容忍多余字段", rpc(cmd="status", junk=[1, 2]))
    check_tolerates("list_entities 容忍多余字段",
                    rpc(cmd="list_entities", junk="x"))
    check_tolerates("input_stats 容忍多余字段", rpc(cmd="input_stats", junk=1))
    check_tolerates("input_flush 容忍多余字段", rpc(cmd="input_flush", junk=1))
    check_tolerates("pause 容忍多余字段", rpc(cmd="pause", junk=1))

    print("== resume 与退出 ==")
    up0 = rpc(cmd="status")["data"]["uptime_s"]
    time.sleep(0.3)
    r = rpc(cmd="resume", junk=1)  # 负向：多余字段应被宽容
    check("resume（容忍多余字段）",
          r.get("ok") and r["data"]["paused"] is False, r)
    up1 = rpc(cmd="status")["data"]["uptime_s"]
    check("uptime 为墙钟（暂停/授步不冻结）", up1 > up0, (up0, up1))
    # 实时通道恢复：resume 后排队动作在真实时间步内自行消费（不再依赖授步）
    rt0 = entity()
    rpc(cmd="move", dx=0, dy=-1)  # 玩家在 (13,7)，上方 (13,6) 无阻挡
    time.sleep(0.25)
    rt1 = entity()
    check("resume 后真实时间步恢复（动作自动消费）",
          rt1["y"] == rt0["y"] - 16, (rt0, rt1))

    r = rpc(cmd="screenshot", path="build/smoke_shot.png")
    check("screenshot 受理", r.get("ok") and r["data"].get("path"), r)

    r = rpc(cmd="quit", junk=1)  # 负向：多余字段应被宽容
    check("quit（容忍多余字段）",
          r.get("ok") and r["data"].get("bye") is True, r)

    print(f"\n== 结果: {PASS} passed, {FAIL} failed ==")
    sock.close()
    return 1 if FAIL else 0


if __name__ == "__main__":
    raise SystemExit(main())
