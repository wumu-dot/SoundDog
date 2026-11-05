#!/usr/bin/env python3
# eval_gate.py — FEAT-A7-06 §11.4-1 频带门控离线验证（三源核对后第一步）
#
# 目的：在不动固件的前提下，用现有串口日志（BANDS + EVAL）验证「频带门控」能否
#       压掉 §10.4 的三次环境声误报，同时量化对高分事件的保留率。
#
# 判据来源（R27/R32，链接见 FEAT-A7-06 §12）：
#   ① 标准：IEC 61672-1 频率计权（人声能量集中在 1~4 kHz 共振峰）
#   ③ 交叉：node-red-contrib-condition-monitoring 的迟滞/连续样本/退出死区逻辑；
#      python-acoustics 的 A 加权频带系数（本脚本只用其"频带分布"思想，不搬系数）
#
# 频带映射（本工程 spectrum.c）：256 点 FFT @16 kHz → bin 宽 62.5 Hz，每 4 bin 聚 1 带
#   带 b 覆盖 4b~4b+3 bin ⇒ 250b ~ 250b+187.5 Hz
#   low    = 带 0~3   ⇒   0~1000 Hz
#   speech = 带 4~15  ⇒ 1000~4000 Hz   ← 人声共振峰
#   high   = 带 16~31 ⇒ 4000~8000 Hz
#
# 用法：
#   python scripts/eval_gate.py --logs data/eval_normal_2026-11-04.log data/eval_quiet_2026-11-04.log
#
# ⚠️ 本脚本只能证明"压误报"，**不能**证明"不误伤真实故障"——故障样本（不平衡/
#    松动）尚未采集（S4）。灵敏度损失必须等真故障数据才能判。
import argparse
import glob
import os

TH = 6.00          # 生产阈值（d ×100 打印，这里用真实值）
CONSEC_S = 0.3     # 报警连续时长（固件 30 帧 @100fps）
RELEASE_S = 0.6    # 解除连续时长（固件 60 帧 @100fps）

BAND_IDX_LOW = (0, 3)      # 含首含尾
BAND_IDX_SPEECH = (4, 15)
BAND_IDX_HIGH = (16, 31)


def parse_log(path):
    """返回 [(t, d, rms, pk, bs, bands)]：BANDS 不参与 EVAL 节拍，取最近一条沿用。"""
    rows = []
    last_bands = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("BANDS"):
                v = line.split(" ", 1)[1]
                last_bands = [int(x) for x in v.split(",")]
            elif line.startswith("EVAL,"):
                fld = line.split(",")
                if last_bands is None or len(fld) < 9:
                    continue
                try:
                    rows.append((int(fld[1]) / 1000.0, int(fld[2]) / 100.0,
                                 int(fld[4]) / 100.0, int(fld[5]) / 100.0,
                                 int(fld[6]), last_bands))
                except ValueError:
                    continue
    return rows


def shares(bands):
    total = sum(bands)
    if total <= 0:
        return 0.0, 0.0, 0.0
    low = sum(bands[BAND_IDX_LOW[0]:BAND_IDX_LOW[1] + 1])
    sp = sum(bands[BAND_IDX_SPEECH[0]:BAND_IDX_SPEECH[1] + 1])
    hi = sum(bands[BAND_IDX_HIGH[0]:BAND_IDX_HIGH[1] + 1])
    return low / total, sp / total, hi / total


def events(rows, gate_fn, fs, th=None):
    """报警状态机（复刻固件双轨：连续 30 帧 OR 滑窗 30/100 帧，10 Hz 下即 3/3/10）。

    th 缺省用全局 TH（生产值）。返回 (事件起点秒列表, 被门控拦下的越限采样数)。
    """
    th = TH if th is None else th
    consec = max(1, int(round(CONSEC_S * fs)))          # 0.3s → 3 个采样
    win = max(1, int(round(1.0 * fs)))                  # 1s 滑窗 → 10 个采样
    need = max(1, int(round(0.3 * fs)))                 # 滑窗内至少 3 个越限
    release = max(1, int(round(RELEASE_S * fs)))        # 0.6s → 6 个采样

    out, blocked = [], 0
    hot, c, r = False, 0, 0
    ring = []

    for t, d, _rms, _pk, _bs, b in rows:
        over = d > th
        gated = bool(gate_fn) and (not gate_fn(b)) if gate_fn else False
        if over and gated:
            blocked += 1
        ok = over and not gated

        ring.append(1 if ok else 0)
        if len(ring) > win:
            ring.pop(0)
        c = c + 1 if ok else 0

        if not hot:
            if (c >= consec) or (sum(ring) >= need):
                hot = True
                out.append(t)
                c = 0
                ring = []
        else:
            r = r + 1 if not ok else 0
            if r >= release:
                hot = False
                r = 0
    return out, blocked


def event_details(rows, fs, th=None):
    """T3 事件明细：起点/持续/峰值 d，以及峰值时刻的 rms/pk/bs/频带占比。

    只复用 events() 给出的起点，避免重复实现触发判据；结束点按"连续 release
    个采样回落"判定，复刻固件的解除逻辑。
    """
    th = TH if th is None else th
    starts, _ = events(rows, None, fs, th)
    release = max(1, int(round(RELEASE_S * fs)))
    consec = max(1, int(round(CONSEC_S * fs)))
    det = []
    for i, t0 in enumerate(starts):
        i0 = min(range(len(rows)), key=lambda j: abs(rows[j][0] - t0))
        # 触发点是"连续第 consec 个越限"，峰值可能落在它前面：回看同一段越限
        i_run = i0
        while i_run > 0 and rows[i_run - 1][1] > th and (i0 - i_run) < 2 * consec:
            i_run -= 1
        limit = starts[i + 1] if i + 1 < len(starts) else rows[-1][0]
        j, cool, end_t = i0, 0, rows[-1][0]
        while j < len(rows) and rows[j][0] <= limit:
            cool = cool + 1 if rows[j][1] <= th else 0
            if cool >= release and j > i0:
                end_t = rows[j][0]
                break
            j += 1
        seg = rows[i_run:j + 1]
        peak = max(seg, key=lambda r: r[1])
        lo, sp, hi = shares(peak[5])
        det.append({"t0": t0, "t_run": rows[i_run][0], "d0": rows[i_run][1],
                    "dur": max(0.0, end_t - t0),
                    "d_max": peak[1], "rms": peak[2], "pk": peak[3],
                    "bs": peak[4], "low": lo, "sp": sp, "hi": hi})
    return det


def main():
    global TH
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", nargs="+", default=None)
    ap.add_argument("--thr", type=float, default=0.60,
                    help="人声带占比上限（超过即门控拦截）")
    ap.add_argument("--th", type=float, default=TH, help="报警阈值（d，默认生产值 6.0）")
    ap.add_argument("--hourly", action="store_true",
                    help="按 1 小时窗口分别统计 FAR（3h 基线用；窗口不足的按实际时长外推）")
    ap.add_argument("--ths", default="6.0,7.0,8.0,8.5",
                    help="--hourly 模式下扫描的阈值列表")
    ap.add_argument("--events", action="store_true",
                    help="T3：打印事件明细（起点/时长/峰值 d/rms/pk/bs/频带占比）")
    ap.add_argument("--merge", action="store_true",
                    help="把多个 --logs 合并成一条连续时间轴（3h 分段采集用）")
    a = ap.parse_args()

    TH = a.th

    logs = a.logs or sorted(glob.glob("data/eval_*.log"))
    loaded = []
    for path in logs:
        rows = parse_log(path)
        if not rows:
            print(f"[SKIP] {path}: 无 EVAL/BANDS 可对齐")
            continue
        loaded.append((path, rows))
    if a.merge and len(loaded) > 1:
        merged = sorted((r for _, rs in loaded for r in rs), key=lambda r: r[0])
        base = merged[0][0]
        merged = [(r[0] - base,) + r[1:] for r in merged]
        name = " + ".join(os.path.basename(p) for p, _ in loaded)
        loaded = [(name, merged)]

    for path, rows in loaded:
        fs = (len(rows) - 1) / (rows[-1][0] - rows[0][0]) if rows[-1][0] > rows[0][0] else 10.0
        dur_min = (rows[-1][0] - rows[0][0]) / 60.0

        ev_all, _ = events(rows, None, fs)
        print(f"\n=== {path}")
        print(f"    samples={len(rows)}  dur={dur_min:.1f}min  fs={fs:.2f}Hz  TH={TH:.2f}")
        print(f"    无门控: 事件 {len(ev_all)} 次  →  {len(ev_all)/dur_min*60:.1f} 次/h")

        if a.hourly:
            ths = [float(x) for x in a.ths.split(",") if x.strip()]
            rows0_t = rows[0][0]
            n_win = max(1, int((rows[-1][0] - rows0_t) // 3600) + 1)
            print("    --- 分小时 FAR（每窗按实际时长外推）---")
            head = "      窗口   时长   " + "  ".join("th=%.1f" % x for x in ths)
            print(head)
            for w in range(n_win):
                lo, hi = w * 3600.0, (w + 1) * 3600.0
                seg = [r for r in rows if lo <= (r[0] - rows0_t) < hi]
                if len(seg) < 2:
                    continue
                h = (seg[-1][0] - seg[0][0]) / 3600.0
                if h <= 0:
                    continue
                vals = []
                for x in ths:
                    evs, _ = events(seg, None, fs, th=x)
                    vals.append("%6.2f" % (len(evs) / h))
                print("      H%d  %5.1fmin  " % (w + 1, h * 60) + "  ".join(vals))
            print("      合计  %5.1fmin  " % dur_min +
                  "  ".join("%6.2f" % (len(events(rows, None, fs, th=x)[0]) / (dur_min / 60.0))
                            for x in ths))

        # 门控阈值扫描：人声带占比上限
        print("    --- 人声带占比门控扫描（speech 越小越干净，但会误杀宽带机械声）---")
        for x in (0.50, 0.60, 0.70, 0.80, 0.90, 1.00):
            gate = lambda b, x=x: shares(b)[1] <= x
            ev_g, blocked = events(rows, gate, fs)
            print(f"      speech≤{x:.2f}: 事件 {len(ev_g)} 次 ({len(ev_g)/dur_min*60:5.1f}/h)"
                  f"  拦截 {blocked:3d}  起点 {['%.0f' % v for v in ev_g][:8]}")

        # 门控阈值扫描：低频带（0~1 kHz）占比下限
        print("    --- 低频带占比门控扫描（low 越大越偏机械，人声/撞击被挡）---")
        for y in (0.30, 0.40, 0.50, 0.60, 0.70, 0.80):
            gate = lambda b, y=y: shares(b)[0] >= y
            ev_g, blocked = events(rows, gate, fs)
            print(f"      low≥{y:.2f}: 事件 {len(ev_g)} 次 ({len(ev_g)/dur_min*60:5.1f}/h)"
                  f"  拦截 {blocked:3d}  起点 {['%.0f' % v for v in ev_g][:8]}")

        # 越限采样的频带占比（用于挑阈值；单调排序便于看分离度）
        over = sorted(((shares(b)[1], t, d) for t, d, _r, _p, _b, b in rows if d > TH),
                      reverse=True)
        print("    越限样本 speech 占比最高的 8 个：")
        for sp, t, d in over[:8]:
            print(f"      t={t:8.1f}s d={d:5.2f} speech={sp:.2f}")

        if a.events:
            det = event_details(rows, fs)
            print(f"    --- T3 事件明细（TH={TH:.2f}，共 {len(det)} 次）---")
            if not det:
                print("      （无事件）")
            for k, e in enumerate(det, 1):
                print("      #%-2d t=%7.1fs run=%7.1fs dur=%4.1fs d_max=%5.2f "
                      "rms=%8.1f pk=%7.1f bs=%5d low=%.2f sp=%.2f hi=%.2f"
                      % (k, e["t0"], e["t_run"], e["dur"], e["d_max"], e["rms"],
                         e["pk"], e["bs"], e["low"], e["sp"], e["hi"]))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
