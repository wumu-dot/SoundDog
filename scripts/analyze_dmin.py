# analyze_dmin.py — A4-02 阶段4诊断：CSV 离线复算 d_min + 分维分离度定位
# 用法：python scripts/analyze_dmin.py data/dmin_normal2.csv data/dmin_anomaly2.csv
# 须从仓库根运行（模版路径按仓库根解析）
import sys, re, statistics

def load_templates(path):
    # 格式假设：model_normal.c 每行一个 {...} 元组，13 个带 f 后缀浮点数（gen 脚本固定格式）
    vals = []
    with open(path, encoding="ascii") as f:
        for line in f:
            if "{" not in line or "}" not in line:
                continue
            m = re.findall(r'[+-]?\d+\.\d+f', line)
            if len(m) == 13:
                vals.append([float(x[:-1]) for x in m])
    return vals

def dmin(feat, centers):
    return min(sum((a - b) ** 2 for a, b in zip(feat, c)) ** 0.5 for c in centers)

def load_csv(path):
    rows = []
    with open(path, encoding="ascii") as f:
        header = f.readline()
        for line in f:
            parts = line.strip().split(",")
            if len(parts) != 15 or parts[2] == "":
                continue
            try:
                rows.append([float(x) for x in parts])
            except ValueError:
                pass
    return rows

def main():
    if len(sys.argv) != 3:
        print("用法: python scripts/analyze_dmin.py <正常CSV> <异常CSV>"); return 1
    centers = load_templates("firmware/soundDog/BSP/model_normal.c")
    print(f"模版加载: {len(centers)} 簇 x {len(centers[0]) if centers else 0} 维")
    if len(centers) != 16:
        print("[ERR] 模版解析失败"); return 1

    groups = {}
    for path, tag in [(sys.argv[1], "正常"), (sys.argv[2], "异常")]:
        rows = load_csv(path)
        feats = [r[2:15] for r in rows]        # c0..c12
        fw_d = [r[0] for r in rows]            # 固件瞬时 d
        pc_d = [dmin(f, centers) for f in feats]
        groups[tag] = (feats, fw_d, pc_d)
        print(f"\n[{tag}] n={len(feats)}")
        print(f"  固件 d:   均值 {statistics.mean(fw_d):.2f}  区间 {min(fw_d):.2f}~{max(fw_d):.2f}")
        print(f"  PC 复算 d: 均值 {statistics.mean(pc_d):.2f}  区间 {min(pc_d):.2f}~{max(pc_d):.2f}")
        print(f"  一致性（|固件-PC| 均值）: {statistics.mean(abs(a-b) for a,b in zip(fw_d, pc_d)):.3f}")

    # 分维分离度：|正常均值-异常均值| / 合并标准差（类 Fisher 判别比，>0.5 有戏）
    print("\n===== 分维判别比 |Δμ|/σ_pooled =====")
    n_feats = groups["正常"][0]
    a_feats = groups["异常"][0]
    for k in range(13):
        nv = [f[k] for f in n_feats]; av = [f[k] for f in a_feats]
        sd = (statistics.pstdev(nv) + statistics.pstdev(av)) / 2 + 1e-9
        ratio = abs(statistics.mean(nv) - statistics.mean(av)) / sd
        flag = " <== 有分离潜力" if ratio > 0.5 else ""
        print(f"  c{k:2d}: 正常 {statistics.mean(nv):+7.2f}  异常 {statistics.mean(av):+7.2f}  判别比 {ratio:.2f}{flag}")

    print("\n===== 最近簇占用（分布形态）=====")
    for tag in ("正常", "异常"):
        counts = [0] * 16
        for f in groups[tag][0]:
            best = min(range(16), key=lambda c: sum((a - b) ** 2 for a, b in zip(f, centers[c])))
            counts[best] += 1
        print(f"  {tag}: {counts}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
