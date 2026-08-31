# collect_dmin.py — A4-02 阶段4：DMIN 序列采集（瞬时 d + 1s 窗峰值 m）+ MFCC13 存证
# 用法：python scripts/collect_dmin.py <秒数> [标签]
# 输出：串口统计 + data/dmin_<标签>.csv（d, m, c0..c12 每秒一行，供 PC 复算/评审）
import sys, time, statistics

try:
    import serial
except ImportError:
    print("[ERR] pyserial 未安装"); sys.exit(1)

PORT = "COM4"
BAUD = 115200
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 70
TAG = sys.argv[2] if len(sys.argv) > 2 else "run"

ser = serial.Serial(PORT, BAUD, timeout=1)
print(f"[CAP] {PORT}@{BAUD} {DUR}s（标签: {TAG}）...")

ds, ms, mfcc_rows = [], [], []
t0 = time.time()
while time.time() - t0 < DUR:
    raw = ser.readline()
    if not raw:
        continue
    try:
        s = raw.decode("ascii", "replace").strip()
    except Exception:
        continue
    if not s:
        continue
    if s.startswith("DMIN d="):
        try:
            body = s[len("DMIN d="):]
            d_part, _, m_part = body.partition(" m=")
            ds.append(int(d_part) / 100.0)
            ms.append(int(m_part) / 100.0)
        except ValueError:
            pass
    elif s.startswith("MFCC13 c="):
        try:
            vals = [int(x) for x in s[len("MFCC13 c="):].split(",")]
            if len(vals) == 13:
                mfcc_rows.append([v / 100.0 for v in vals])
        except ValueError:
            pass
ser.close()

print(f"\n===== DMIN 统计（标签: {TAG}）=====")
for name, xs in (("瞬时 d", ds), ("1s 峰值 m", ms)):
    if xs:
        print(f"{name}: n={len(xs)}  区间 {min(xs):.2f}~{max(xs):.2f}  "
              f"均值 {statistics.mean(xs):.2f}  中位 {statistics.median(xs):.2f}")
    else:
        print(f"{name}: 0 行（检查固件/前缀/门控 §3.4）")
print(f"MFCC13 存证行: {len(mfcc_rows)}")

if ds:
    out = f"data/dmin_{TAG}.csv"
    with open(out, "w", encoding="ascii") as f:
        f.write("d,m,c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12\n")
        for i in range(len(ds)):
            mf = mfcc_rows[i] if i < len(mfcc_rows) else [""] * 13
            f.write(f"{ds[i]:.4f},{ms[i]:.4f}," + ",".join(str(v) for v in mf) + "\n")
    print(f"CSV: {out}")
