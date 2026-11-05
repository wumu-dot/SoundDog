#!/usr/bin/env python3
# eval_baselines.py — FEAT-A7-06 S3：对比基线离线评测（E1/E2/E3）
#
# 目的：把"检测能力有没有用"变成可复算的数字（FAR 误报率 / DR 检出 / TD 延迟），
#       而不是凭印象。三组方法在同一批数据上只换"特征 + 阈值"，保证可比。
#
# 数据源（两种，均可单独跑）：
#   1) --data-dir data   → dmin_*.csv（1 Hz，列：d,m,c0..c12）⇒ 只能评 E3（无 rms 列）
#   2) --eval-log LOG    → 串口日志中的 EVAL 行（10 Hz）
#      格式：EVAL,<t_ms>,<d_x100>,<th_x100>,<rms_x100>,<pk_x100>,<bs>,<ba>,<lv>
#      ⇒ 同一条日志可同时评 E1(rms) / E2(频带能量和) / E3(d)
#
# 评测口径（统一，避免方法间不可比）：
#   - 以"正常工况"为参考，算每个特征的 μ/σ，阈值取 k·σ 之上（z-score 归一化）
#   - 报警状态机：连续 consec 个采样越限才置位，连续 release 个采样回落才解除
#     （生产固件为 100 fps 下 30/60 帧 = 0.3/0.6 s；本脚本按采样率换算）
#   - FAR = 报警次数 / 小时；DR = 故障段内是否出现报警；TD = 故障起点→首次报警（秒）
#
# ⚠️ 已知限制（必须写进论文，不得含糊）：
#   - dmin_*.csv 是 1 Hz 抽稀，只能做"阈值/防抖粗评"，不能复现 10 s 级 TD；
#     精确 TD 必须用 EVAL 行（10 Hz）。
#   - 现有 data/ 中没有"安静 60 min"基线文件，脚本会以 normal_* 作代理并打印告警；
#     正式 M-1（A7-05）必须在安静环境补采。
#
# 用法：
#   python scripts/eval_baselines.py --selftest
#   python scripts/eval_baselines.py --data-dir data
#   python scripts/eval_baselines.py --eval-log data/eval_run.log --data-dir data
#
# 依赖：仅标准库（csv/statistics/argparse），与项目其余脚本一致。
import argparse
import csv
import glob
import os
import statistics
import sys

# ---------------------------------------------------------------- 数据模型


class Series:
    """一段单工况数据：一个时间轴 + 若干特征通道（缺列则 None）。"""

    def __init__(self, name, label, fs_hz, ts, channels):
        self.name = name
        self.label = label
        self.fs_hz = float(fs_hz)
        self.ts = ts                  # 秒
        self.channels = channels      # dict: feat -> [float]

    @property
    def duration_s(self):
        return (self.ts[-1] - self.ts[0]) if len(self.ts) > 1 else 0.0


def label_of(filename):
    """按文件名推断工况标签（用于 FAR/DR 分组，勿在生产路径复用）。"""
    base = os.path.basename(filename).lower()
    if "normal" in base or "quiet" in base:
        return "normal"
    if "metal" in base:
        return "acoustic"
    if "imbalance" in base:
        return "imbalance"
    if "loose" in base or "loosen" in base:
        return "loose"
    if "anomaly" in base:
        return "broadband"      # 吹气类：A4-02 已归因为不可分，保留以作反例
    return "unknown"


def load_dmin_csv(path):
    """读 data/dmin_*.csv（1 Hz：d,m,c0..c12）→ Series（仅 d 通道）。"""
    ds = []
    with open(path, "r", encoding="ascii") as f:
        r = csv.DictReader(f)
        for row in r:
            try:
                ds.append(float(row["d"]))
            except (KeyError, TypeError, ValueError):
                continue
    if not ds:
        return None
    ts = [float(i) for i in range(len(ds))]      # 1 行 = 1 s
    return Series(os.path.basename(path), label_of(path), 1.0, ts, {"d": ds})


EVAL_PREFIX = "EVAL,"


def load_eval_log(path):
    """解析串口日志中的 EVAL 行（10 Hz）→ Series（d/rms/bs 三通道）。"""
    t, d, rms, bs = [], [], [], []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                idx = line.find(EVAL_PREFIX)
                if idx < 0:
                    continue
                parts = line[idx:].strip().split(",")
                if len(parts) < 9:
                    continue
                try:
                    t.append(int(parts[1]) / 1000.0)
                    d.append(int(parts[2]) / 100.0)
                    rms.append(int(parts[4]) / 100.0)
                    bs.append(float(parts[6]))
                except ValueError:
                    continue
    except OSError:
        return None
    if not t:
        return None
    # EVAL 是 10 Hz 名义输出；实测时间轴更可信，按中位间隔估算 fs
    gaps = [t[i + 1] - t[i] for i in range(len(t) - 1) if t[i + 1] > t[i]]
    fs = (1.0 / statistics.median(gaps)) if gaps else 10.0
    t = [x - t[0] for x in t]
    return Series(os.path.basename(path), label_of(path), fs, t,
                  {"d": d, "rms": rms, "bs": bs})

# ---------------------------------------------------------------- 状态机


def alarm_events(x, thr, consec, release):
    """连续越限置位 / 连续回落解除；返回每次报警的采样下标。"""
    events = []
    hot = False
    cnt = 0
    cool = 0
    for i, v in enumerate(x):
        if not hot:
            cnt = cnt + 1 if v > thr else 0
            if cnt >= consec:
                hot = True
                events.append(i)
                cnt = 0
        else:
            cool = cool + 1 if v <= thr else 0
            if cool >= release:
                hot = False
                cool = 0
    return events


def zscore_threshold(ref_vals, k):
    """正常参考段的 μ+kσ（σ 若为 0 则退回 μ+1e-9，避免恒定序列除零）。"""
    mu = statistics.mean(ref_vals)
    sd = statistics.pstdev(ref_vals) if len(ref_vals) > 1 else 0.0
    return mu + k * sd, mu, sd

# ---------------------------------------------------------------- 评测


def eval_one(series, feat, ref_vals, k, alarm_sec, release_sec):
    thr, _, _ = zscore_threshold(ref_vals, k)
    # 按【时间】换算采样数：固件生产配置是 100 fps 下 30/60 帧 = 0.3/0.6 s，
    # 换成 10 Hz 的 EVAL 即 3/6 个采样。若直接用 consec=1，会把 0.1 s 的
    # 单点毛刺也算成报警，FAR 被系统性放大（首版实测 d 多算 121 次）。
    consec = max(1, int(round(alarm_sec * series.fs_hz)))
    release = max(1, int(round(release_sec * series.fs_hz)))
    events = alarm_events(series.channels[feat], thr, consec, release)
    dur_h = series.duration_s / 3600.0
    far = (len(events) / dur_h) if dur_h > 0 else 0.0
    td = series.ts[events[0]] if events else None
    return {
        "file": series.name,
        "label": series.label,
        "feat": feat,
        "fs_hz": series.fs_hz,
        "dur_s": round(series.duration_s, 1),   # 暴露时长：短片段的 FAR/h 是外推值
        "k": k,
        "thr": round(thr, 3),
        "consec": consec,
        "release": release,
        "events": len(events),
        "far_per_h": round(far, 3),
        "td_s": (round(td, 2) if td is not None else None),
    }


def run(data_dir, eval_log, k_list, out_csv, alarm_sec, release_sec, label=None):
    series_list = []
    if data_dir:
        for p in sorted(glob.glob(os.path.join(data_dir, "dmin_*.csv"))):
            s = load_dmin_csv(p)
            if s:
                series_list.append(s)
    if eval_log:
        s = load_eval_log(eval_log)
        if s:
            if label:
                # 文件名与真实工况不符时（如把"安静"文件误录成活动段）强制覆盖，
                # 避免 label_of 的字符串推断污染 FAR/参考段选择。
                s.label = label
            series_list.append(s)
    if not series_list:
        print("[ERR] 无可用数据：检查 --data-dir / --eval-log")
        return 1

    # 参考段：按特征池化所有 normal/quiet 序列。
    # （不同数据源通道不同：dmin CSV 只有 d；EVAL 日志有 d/rms/bs。若只认单一
    #  参考文件，E1/E2 会被误判为"无参考"而整组跳过。）
    ref_pool = {}
    for s in series_list:
        if s.label in ("normal", "quiet"):
            for feat, vals in s.channels.items():
                ref_pool.setdefault(feat, []).extend(vals)
    if not ref_pool:
        print("[ERR] 缺少正常工况参考段（文件名需含 normal/quiet）")
        return 1
    if not any("quiet" in s.name.lower() for s in series_list):
        print("[WARN] 无安静基线（quiet）文件：FAR 以 normal 工况作代理，"
              "正式 M-1 需补采安静 60 min（FEAT-A7-05 M-1）")
    if len(ref_pool.get("d", [])) and len(series_list) == 1:
        print("[WARN] 参考段与评测段是同一文件（自参考）：μ/σ 来自被评数据本身，"
              "FAR 偏乐观，仅作首版粗看，不作论文结论")

    rows = []
    for s in series_list:
        for feat in s.channels:
            if feat not in ref_pool:
                continue            # 参考段没有该特征（如 CSV 无 rms）→ 跳过
            for k in k_list:
                rows.append(eval_one(s, feat, ref_pool[feat], k,
                                     alarm_sec, release_sec))

    with open(out_csv, "w", newline="", encoding="ascii") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # 控制台摘要：以 k=3 为工作点（μ+3σ，工程常用三分法）
    k_op = 3.0 if 3.0 in k_list else k_list[len(k_list) // 2]
    print(f"\n===== 对比基线（k={k_op}·σ，来源 {out_csv}）=====")
    hdr = f"{'file':<24}{'label':<11}{'feat':<6}{'fs':>5}{'evt':>5}{'FAR/h':>8}{'TD(s)':>8}"
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        if r["k"] == k_op:
            td = "-" if r["td_s"] is None else f"{r['td_s']:.1f}"
            print(f"{r['file']:<24}{r['label']:<11}{r['feat']:<6}"
                  f"{r['fs_hz']:>5.1f}{r['events']:>5}{r['far_per_h']:>8.2f}{td:>8}")
    print("\n口径提示：1 Hz CSV 只能粗评；10 s 级 TD 需 EVAL 10 Hz 日志。")
    return 0

# ---------------------------------------------------------------- 自检


def selftest():
    """合成序列自检：不依赖硬件，验证状态机与指标换算。"""
    # 正常段：100 个 ~0±1 的确定性抖动；故障段：50 个整体抬到 5
    normal = [0.0, 1.0, -1.0, 0.5, -0.5] * 20
    fault = [5.0 + (1.0 if i % 2 == 0 else -1.0) for i in range(50)]

    thr, mu, sd = zscore_threshold(normal, 3.0)
    assert abs(mu) < 1e-9, f"正常段均值应≈0，实际 {mu}"
    assert sd > 0.5, f"正常段 σ 应显著，实际 {sd}"

    ev_n = alarm_events(normal, thr, 1, 1)
    ev_f = alarm_events(fault, thr, 1, 1)
    assert len(ev_n) == 0, f"正常段不应报警，实际 {len(ev_n)} 次"
    assert len(ev_f) == 1, f"故障段应报警 1 次（连续保持），实际 {len(ev_f)} 次"
    assert ev_f[0] == 0, f"故障应在首个采样即触发，实际下标 {ev_f[0]}"

    # 迟滞解除：故障后回到正常，应只产生 1 次报警（不抖动重报）
    mixed = fault + normal + fault
    ev_m = alarm_events(mixed, thr, 1, 1)
    assert len(ev_m) == 2, f"两段故障应报警 2 次，实际 {len(ev_m)} 次"
    print("[PASS] selftest 5/5：阈值换算 / 正常段零报警 / 故障即时触发 / "
          "回落解除 / 分段计数")
    return 0


def main():
    ap = argparse.ArgumentParser(description="FEAT-A7-06 对比基线离线评测")
    ap.add_argument("--data-dir", default=None, help="含 dmin_*.csv 的目录")
    ap.add_argument("--eval-log", default=None, help="含 EVAL 行的串口日志")
    ap.add_argument("--out", default="data/eval_summary.csv", help="输出 CSV")
    ap.add_argument("--ks", default="1,2,3,4,5,6", help="阈值扫描（μ+kσ 的 k）")
    ap.add_argument("--alarm-sec", type=float, default=0.3,
                    help="连续越限多久才报警（秒；固件生产值 0.3s=30帧@100fps）")
    ap.add_argument("--release-sec", type=float, default=0.6,
                    help="连续回落多久才解除（秒；固件生产值 0.6s=60帧@100fps）")
    ap.add_argument("--label", default=None,
                    help="覆盖 --eval-log 的工况标签（normal/quiet/activity/...）")
    ap.add_argument("--selftest", action="store_true", help="跑合成自检（无硬件）")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    ks = [float(x) for x in a.ks.split(",") if x.strip()]
    if not (a.data_dir or a.eval_log):
        a.data_dir = "data"
    return run(a.data_dir, a.eval_log, ks, a.out, a.alarm_sec, a.release_sec, a.label)


if __name__ == "__main__":
    sys.exit(main())
