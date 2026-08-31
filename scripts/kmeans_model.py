# kmeans_model.py — A4-01 阶段3：K-means(K=16, seed=42) 聚类 → 导出 model_normal.c/h
# 设计定稿（FEAT-A4-01 §1.5）：C 常量表编译期集成（micro_speech 同构模式）
# 用法：python scripts/kmeans_model.py data/mfcc_normal_600.csv firmware/soundDog/BSP/model_normal.c
#       （.h 自动生成在 .c 同目录；--check 仅复跑对比不写文件——阶段4 独立复现用）
import sys, os, re, argparse, datetime
import numpy as np
from sklearn.cluster import KMeans

K = 16          # 事实包 §3：禁改
D = 13          # 13 维 MFCC
SEED = 42       # AC-04 可复现
N_INIT = 10

def load_csv(path):
    rows = []
    with open(path, "r", encoding="ascii") as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            vals = line.split(",")
            if len(vals) != D:
                print(f"[ERR] 第 {i} 行维数 {len(vals)} ≠ {D}"); sys.exit(1)
            rows.append([float(v) for v in vals])
    if len(rows) < K:
        print(f"[ERR] 样本 {len(rows)} < K={K}，16 簇无法分配"); sys.exit(1)
    return np.array(rows, dtype=np.float32)

def run_kmeans(X):
    km = KMeans(n_clusters=K, random_state=SEED, n_init=N_INIT)
    labels = km.fit_predict(X)
    centers = km.cluster_centers_.astype(np.float32)
    counts = np.bincount(labels, minlength=K)
    return centers, counts

def check_clusters(centers, counts, X):
    ok = True
    if (counts == 0).any():
        print(f"[FAIL] 空簇: {np.where(counts == 0)[0].tolist()} → 回 §3.1 补采 300 帧")
        ok = False
    # 地板簇检查（坑 3）：c0≈-110 且其余维≈0
    for i in range(K):
        if counts[i] > 0 and centers[i][0] < -90 and np.abs(centers[i][1:]).max() < 0.5:
            print(f"[FAIL] 簇 {i} 疑似静音地板簇（c0={centers[i][0]:.2f}）→ 重采")
            ok = False
    return ok

def export_c(centers, out_c, nsamples, check_only=False):
    out_h = out_c.replace(".c", ".h")
    date = datetime.date.today().isoformat()
    hdr = (f"/* model_normal.c - A4-01 normal template (K-means K=16 centers, 13-dim MFCC)\n"
           f" * gen: scripts/kmeans_model.py | samples: {nsamples} | seed: {SEED} | n_init: {N_INIT} | date: {date}\n"
           f" * consumer: A4-02 distance compare; compile-time const (swap to A7-03 Flash / A4-04 RS485 later, interface unchanged)\n"
           f" * DO NOT EDIT MANUALLY */\n")
    body = '#include "model_normal.h"\n\nconst float32_t model_normal[MODEL_NORMAL_K][MODEL_NORMAL_D] = {\n'
    for i in range(K):
        body += "  {" + ",".join(f"{v:+.4f}f" for v in centers[i]) + "},\n"
    body += "};\n"
    h_body = (f"#ifndef MODEL_NORMAL_H\n#define MODEL_NORMAL_H\n\n"
              f"#include \"arm_math.h\"\n\n"
              f"#define MODEL_NORMAL_K  {K}\n"
              f"#define MODEL_NORMAL_D  {D}\n\n"
              f"extern const float32_t model_normal[MODEL_NORMAL_K][MODEL_NORMAL_D];\n\n"
              f"#endif /* MODEL_NORMAL_H */\n")
    if check_only:
        with open(out_c, "r", encoding="ascii") as f:
            old = f.read()
        gen = hdr + body
        # 剔除易变头部（生成日期）后逐字节比较：跨日期复跑 AC-04/AC-10 不误报
        norm = lambda s: re.sub(r"date: \d{4}-\d{2}-\d{2}", "date: X", s)
        if norm(old) != norm(gen):
            print("[FAIL] 复跑结果与已存文件不一致（AC-04/AC-10）")
            return False
        print("[OK] 复跑一致（模表逐字节一致，seed=42 生效；生成日期已归一化）")
        return True
    with open(out_c, "w", encoding="ascii", newline="\n") as f:
        f.write(hdr + body)
    with open(out_h, "w", encoding="ascii", newline="\n") as f:
        f.write(h_body)
    print(f"[OK] 导出 {out_c} + {out_h}（纯 ASCII 无 BOM，ARM 口径）")
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("outfile", nargs="?", default="firmware/soundDog/BSP/model_normal.c")
    ap.add_argument("--check", action="store_true", help="复跑对比模式（AC-04/10）")
    args = ap.parse_args()

    if not os.path.exists(args.infile):
        print(f"[ERR] 输入文件不存在: {args.infile}"); sys.exit(1)   # AC-08 文件缺失

    X = load_csv(args.infile)
    print(f"样本: {X.shape[0]} 帧 × {X.shape[1]} 维")
    centers, counts = run_kmeans(X)
    print(f"簇中心: {K} × {D}（K/维度固定）")
    print(f"各簇样本数: {counts.tolist()}（总和 {counts.sum()}）")
    if not check_clusters(centers, counts, X):
        sys.exit(1)
    print("[OK] 无空簇、无地板簇")
    mu = X.mean(axis=0)
    print(f"整体均值 c0..c2: {mu[0]:+.2f} {mu[1]:+.2f} {mu[2]:+.2f}（量级参考 |c0|<200）")
    print(f"簇 0 中心前 3 维: {centers[0][0]:+.4f} {centers[0][1]:+.4f} {centers[0][2]:+.4f}")
    if not export_c(centers, args.outfile, X.shape[0], check_only=args.check):
        sys.exit(1)
    return 0

if __name__ == "__main__":
    sys.exit(main())
