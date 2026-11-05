#!/usr/bin/env python3
# eval_residual.py — FEAT-A7-06 §14 实验 B：噪声底跟踪 + 残差确认（离线，仅用 BANDS + EVAL）
#
# 目的：回答"能不能把环境噪声滤掉"。做法不是 FIR/IIR，而是**噪声底归一化**：
#   1) 每频带维护慢变噪声底：1 s 块最小值 + 每秒 2% 上升泄漏
#      （背景降得快、升得慢；持续故障不会被立刻吸收）
#   2) 残差比 = (当前带能量和 + 伪计数) / (噪声底和 + 伪计数)
#   3) 两种用法：
#      - 单独判据：残差比 > tau + 0.3/0.6 s 防抖
#      - 级联判据：d>6 事件 + 残差比 > tau 确认（保留 d 的灵敏度）
#
# 数据：串口日志中的 BANDS（32 带，实测 4.8 Hz）与 EVAL（10 Hz）
# 局限（必须写进论文）：
#   - BANDS 是 1 LSB 量化，底噪接近 0 的频带比值不稳（低频带残差因此不可用）
#   - 无故障样本 ⇒ 只能看"压误报"，不能证明"不误伤"
#   - 持续 >1 min 的故障会被 2%/s 的泄漏慢慢吸收
#
# 用法：
#   python scripts/eval_residual.py --quiet data/eval_quiet_hour_2026-11-05.log \
#                                   --activity data/eval_quiet_2026-11-05.log
import argparse
import math
import statistics

BAND_N = 32
LOW = slice(0, 4)       # 0~1 kHz
SPEECH = slice(4, 16)   # 1~4 kHz
HIGH = slice(16, 32)    # 4~8 kHz


def parse(path):
    """返回 ([(t, bands)], [(t, d)])：BANDS 无时间戳，用最近一条 EVAL 的 tick 对齐。"""
    rows, ev = [], []
    ev_t = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("EVAL,"):
                p = line.split(",")
                if len(p) >= 9:
                    ev_t = int(p[1])
                    ev.append((ev_t / 1000.0, int(p[2]) / 100.0))
            elif line.startswith("BANDS "):
                try:
                    b = [int(x) for x in line[6:].split(",")]
                except ValueError:
                    continue
                if len(b) >= BAND_N and ev_t is not None:
                    rows.append((ev_t / 1000.0, b[:BAND_N]))
    return rows, ev


def perc(xs, q):
    xs = sorted(xs)
    if not xs:
        return 0.0
    i = int(q * (len(xs) - 1))
    i = min(len(xs) - 1, max(0, i))
    return float(xs[i])


def static_floor(rows):
    """静态底：安静段每频带 20 分位（模拟"先在安静环境标定"）。"""
    return [max(1.0, perc([r[1][i] for r in rows], 0.20)) for i in range(BAND_N)]


def ratios(b, floor):
    # 加伪计数：BANDS 1 LSB 量化，底噪为 0 的频带直接相除会爆表
    return ((sum(b) + BAND_N) / (sum(floor) + BAND_N),
            (sum(b[LOW]) + 4) / (sum(floor[LOW]) + 4),
            (sum(b[SPEECH]) + 12) / (sum(floor[SPEECH]) + 12),
            (sum(b[HIGH]) + 16) / (sum(floor[HIGH]) + 16))


def adaptive(rows):
    """自适应底：1 s 块最小值 + 每秒 2% 上升泄漏。"""
    step = rows[1][0] - rows[0][0] if len(rows) > 1 else 0.2
    n_init = max(1, int(20.0 / step))
    floor = [max(1.0, perc([r[1][i] for r in rows[:n_init]], 0.20)) for i in range(BAND_N)]
    block_min = list(rows[0][1])
    last = rows[0][0]
    out = []
    for t, b in rows:
        if t - last >= 1.0:
            floor = [max(1.0, min(float(block_min[i]), floor[i] * 1.02))
                     for i in range(BAND_N)]
            block_min = list(b)
            last = t
        else:
            block_min = [min(block_min[i], b[i]) for i in range(BAND_N)]
        out.append((t,) + ratios(b, floor))
    return out


def events(series, tau, fs, consec_s=0.3, release_s=0.6):
    """双轨防抖（与固件同构）。fs=4.8 Hz 时 0.3 s 只有 1.44 采样 → 向上取整到 2。"""
    consec = max(2, int(math.ceil(consec_s * fs)))
    win = max(2, int(math.ceil(1.0 * fs)))
    need = max(2, int(math.ceil(0.3 * fs)))
    rel = max(2, int(math.ceil(release_s * fs)))
    out, hot, c, r, ring = [], False, 0, 0, []
    for t, s in series:
        ok = s > tau
        ring.append(1 if ok else 0)
        if len(ring) > win:
            ring.pop(0)
        c = c + 1 if ok else 0
        if not hot:
            if c >= consec or sum(ring) >= need:
                hot, c, ring = True, 0, []
                out.append(t)
        else:
            r = r + 1 if not ok else 0
            if r >= rel:
                hot, r = False, 0
    return out


def stat_line(name, vals):
    print("%-20s p50=%6.2f p90=%6.2f p99=%6.2f max=%7.2f"
          % (name, statistics.median(vals), perc(vals, 0.90), perc(vals, 0.99), max(vals)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", default="data/eval_quiet_hour_2026-11-05.log")
    ap.add_argument("--activity", default="data/eval_quiet_2026-11-05.log")
    a = ap.parse_args()

    qr, qev = parse(a.quiet)
    ar, aev = parse(a.activity)
    if not qr or not ar:
        print("[ERR] 日志缺少 BANDS/EVAL")
        return 1

    q_fs = (len(qr) - 1) / (qr[-1][0] - qr[0][0])
    a_fs = (len(ar) - 1) / (ar[-1][0] - ar[0][0])
    q_h = (qev[-1][0] - qev[0][0]) / 3600.0
    a_h = (aev[-1][0] - aev[0][0]) / 3600.0
    print("quiet:    %d BANDS (%.1f Hz), %.2f h" % (len(qr), q_fs, q_h))
    print("activity: %d BANDS (%.1f Hz), %.2f h" % (len(ar), a_fs, a_h))

    qs = static_floor(qr)
    q_static = [(t,) + ratios(b, qs) for t, b in qr]
    a_static = [(t,) + ratios(b, qs) for t, b in ar]
    qa = adaptive(qr)
    aa = adaptive(ar)

    print("\n--- 残差比分布（全带 / 自适应底）---")
    stat_line("quiet", [r[1] for r in qa])
    stat_line("activity", [r[1] for r in aa])
    stat_line("quiet 低频带", [r[2] for r in qa])
    stat_line("activity 低频带", [r[2] for r in aa])
    stat_line("quiet 静态底", [r[1] for r in q_static])
    stat_line("activity 静态底", [r[1] for r in a_static])

    print("\n--- 单独判据：残差比 > tau（4.8 Hz 双轨防抖）---")
    print("  tau   活动事件/h   安静事件/h")
    for tau in (2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0):
        qn = len(events([(r[0], r[1]) for r in qa], tau, q_fs)) / q_h
        an = len(events([(r[0], r[1]) for r in aa], tau, a_fs)) / a_h
        print("  %4.1f   %9.1f   %9.1f" % (tau, an, qn))

    print("\n--- 只用低频带残差（0~1 kHz，仅供反例）---")
    print("  tau   活动事件/h   安静事件/h")
    for tau in (1.5, 2.0, 2.5, 3.0, 4.0):
        qn = len(events([(r[0], r[2]) for r in qa], tau, q_fs)) / q_h
        an = len(events([(r[0], r[2]) for r in aa], tau, a_fs)) / a_h
        print("  %4.1f   %9.1f   %9.1f" % (tau, an, qn))

    print("\n--- 级联：d>6 事件 + 残差确认（±0.5 s 内取最大残差比）---")
    for name, ev, rr, h, fs in (("quiet", qev, qa, q_h, 10.0),
                                ("activity", aev, aa, a_h, 10.0)):
        dev = events([(t, d) for t, d in ev], 6.0, fs)
        print(" %s: d>6 事件 %d 次 (%.1f/h)" % (name, len(dev), len(dev) / h))
        for tau in (3.0, 4.0, 5.0):
            kept = sum(1 for t0 in dev
                       if [r for r in rr if abs(r[0] - t0) <= 0.5]
                       and max([r for r in rr if abs(r[0] - t0) <= 0.5],
                               key=lambda r: r[1])[1] > tau)
            print("   tau=%.1f 保留 %d/%d (%.1f/h)" % (tau, kept, len(dev), kept / h))

    print("\n注：离线 10 Hz 回放比固件多计（活动 35 vs 9），绝对值不可直接对比，只看相对变化。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
