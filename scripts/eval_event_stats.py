#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""eval_event_stats.py -- 从 EVAL 日志统计「超阈事件」的时长分布与持续时间过滤效果。

用途
    FEAT-A7-07（免疫机制适用性评估）里那两张表的可复算来源：
      表1 事件时长分布   —— 超阈帧聚成多少个事件、多少是瞬时毛刺、多少是长突发
      表2 持续时间过滤   —— 若要求「连续超阈 >= N 帧」才报警，还剩多少事件

口径（重要，改参数会改结论，故显式暴露）
    超阈帧   : EVAL 第 2 字段 d_x100 > 第 3 字段 th_x100（×100 定点，整数比较即可）
    事件分组 : 相邻超阈帧的 tick 间隔 > --gap-ms 即视为新事件（默认 2000 ms）
               ⚠️ 该阈值影响「事件数」，故 --sensitivity 会同时给出 1000/2000/5000 三档

用法
    python scripts/eval_event_stats.py --log data/eval_quiet_2026-11-05.log
    python scripts/eval_event_stats.py --log data/eval_quiet_2026-11-05.log --sensitivity
    python scripts/eval_event_stats.py --log a.log --log b.log      # 可多份对比

仅标准库，无外部依赖。
"""
from __future__ import annotations

import argparse
import sys
from typing import List, Tuple

# EVAL 字段（据 FEAT-A7-06 §S1）：
#   0 tick_ms | 1 d_x100 | 2 th_x100 | 3 rms_x100 | 4 pk_x100
#   5 band_sum | 6 band_argmax | 7 level | 8 res_x100（2026-11-05 追加，可缺）
IDX_TICK, IDX_D, IDX_TH, IDX_BA = 0, 1, 2, 6


def load_eval(path: str) -> List[List[int]]:
    rows: List[List[int]] = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for ln in f:
            if not ln.startswith('EVAL,'):
                continue
            parts = ln.rstrip('\r\n').split(',')[1:]
            try:
                rows.append([int(x) for x in parts])
            except ValueError:
                continue          # 半行/损坏行直接跳过
    return rows


def group_events(over: List[List[int]], gap_ms: int) -> List[List[List[int]]]:
    """把超阈帧按 tick 间隔分组；间隔 > gap_ms 视为新事件。"""
    events: List[List[List[int]]] = []
    cur: List[List[int]] = []
    prev = None
    for row in over:
        t = row[IDX_TICK]
        if prev is not None and t - prev > gap_ms:
            events.append(cur)
            cur = []
        cur.append(row)
        prev = t
    if cur:
        events.append(cur)
    return events


def report(path: str, gap_ms: int) -> None:
    rows = load_eval(path)
    if not rows:
        print(f'{path}: 无 EVAL 行')
        return

    over = [r for r in rows if len(r) > IDX_TH and r[IDX_D] > r[IDX_TH]]
    ths = sorted({r[IDX_TH] for r in rows if len(r) > IDX_TH})
    events = group_events(over, gap_ms)
    lens = sorted(len(e) for e in events) or [0]

    print(f'===== {path}')
    print(f'  EVAL 行      : {len(rows)}  （10 Hz ⇒ 约 {len(rows)/10/60:.1f} min）')
    print(f'  阈值 th      : {[f"{t/100:.2f}" for t in ths]}')
    print(f'  超阈帧       : {len(over)}  （{len(over)/len(rows)*100:.1f}%）')
    print(f'  事件数       : {len(events)}   （分组间隔 > {gap_ms} ms）')
    if not events:
        return
    print(f'  时长分布     : min={lens[0]} 帧  max={lens[-1]} 帧  中位={lens[len(lens)//2]} 帧')

    # ---- 表1：事件时长分布 ----
    buckets = [('1~2 帧（0.1~0.2 s）', 1, 2), ('3~5 帧（0.3~0.5 s）', 3, 5),
               ('6~9 帧（0.6~0.9 s）', 6, 9), ('10~19 帧（1.0~1.9 s）', 10, 19),
               ('>=20 帧（>=2.0 s）', 20, 10**9)]
    print('\n  [表1] 事件时长分布')
    print('    {:22s} {:>5s} {:>7s} {:>9s}'.format('时长区间', '事件数', '占比', '贡献超阈帧'))
    for name, lo, hi in buckets:
        grp = [e for e in events if lo <= len(e) <= hi]
        if not grp:
            continue
        frames = sum(len(e) for e in grp)
        print('    {:22s} {:5d} {:6.1f}% {:9d}'.format(
            name, len(grp), len(grp) / len(events) * 100, frames))

    # ---- 表2：持续时间过滤效果 ----
    print('\n  [表2] 若要求「连续超阈 >= N 帧」才报警')
    print('    {:>6s} {:>8s} {:>9s} {:>9s}'.format('N(帧)', '时长(s)', '剩余事件', '相对占比'))
    for n in (1, 3, 5, 10, 20, 30):
        k = sum(1 for e in events if len(e) >= n)
        print('    {:6d} {:8.1f} {:9d} {:8.1f}%'.format(
            n, n / 10.0, k, k / len(events) * 100))
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description='EVAL 超阈事件统计（FEAT-A7-07 表1/表2 的可复算来源）')
    ap.add_argument('--log', action='append', required=True, help='EVAL 日志路径，可重复')
    ap.add_argument('--gap-ms', type=int, default=2000, help='事件分组间隔阈值，默认 2000 ms')
    ap.add_argument('--sensitivity', action='store_true', help='额外输出 gap 取 1000/2000/5000 的事件数')
    args = ap.parse_args()

    for p in args.log:
        report(p, args.gap_ms)

    if args.sensitivity:
        print('===== 事件数对分组间隔的敏感性')
        print('    {:28s} {:>8s} {:>8s} {:>8s}'.format('日志', '1 s', '2 s', '5 s'))
        for p in args.log:
            rows = load_eval(p)
            over = [r for r in rows if len(r) > IDX_TH and r[IDX_D] > r[IDX_TH]]
            cnt = [len(group_events(over, g)) for g in (1000, 2000, 5000)]
            print('    {:28s} {:8d} {:8d} {:8d}'.format(p.split('/')[-1], *cnt))
    return 0


if __name__ == '__main__':
    sys.exit(main())
