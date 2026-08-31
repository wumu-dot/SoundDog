# collect_mfcc.py — A4-01 阶段3：采集 13 维 MFCC 特征到 CSV（600 帧 ≈ 10 分钟）
# 设计定稿（FEAT-A4-01 §1.5）：MFCC13 c=<13 维 ×100 整数逗号串> 前缀过滤 → /100.0 还原 → 维数校验
# 用法：python scripts/collect_mfcc.py data/mfcc_normal_600.csv --n 600 [--port COM4]
import sys, time, argparse, os

try:
    import serial
except ImportError:
    print("[ERR] pyserial 未安装"); sys.exit(1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outfile")
    ap.add_argument("--n", type=int, default=600)
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.outfile) or ".", exist_ok=True)
    ser = serial.Serial(args.port, args.baud, timeout=2)
    print(f"[CAP] {args.port}@{args.baud}，目标 {args.n} 帧（~{args.n} 秒，保持目标环境声）...")
    print("[CAP] Ctrl+C 可提前结束（已采数据保留）")

    frames, dropped, raw_lines = [], 0, 0
    t0 = time.time()
    try:
        while len(frames) < args.n:
            raw = ser.readline()
            if not raw:
                continue
            try:
                s = raw.decode("ascii", "replace").strip()
            except Exception:
                continue
            if not s.startswith("MFCC13 c="):
                continue
            raw_lines += 1
            try:
                vals = [int(x) for x in s[len("MFCC13 c="):].split(",")]
            except ValueError:
                dropped += 1; continue
            if len(vals) != 13:            # 断行/粘行（坑 6）
                dropped += 1; continue
            frames.append([v / 100.0 for v in vals])   # ×100 定标还原（坑 2）
            if len(frames) % 60 == 0:
                print(f"[CAP] {len(frames)}/{args.n} 帧，丢弃 {dropped} 行，{time.time()-t0:.0f}s")
    except KeyboardInterrupt:
        print("\n[CAP] 用户中断")
    finally:
        ser.close()

    if not frames:
        print("[ERR] 0 帧：检查 COM 口/波特率/固件 MFCC13 打印（A3-04 状态）"); sys.exit(1)

    with open(args.outfile, "w", encoding="ascii") as f:
        for fr in frames:
            f.write(",".join(f"{v:.4f}" for v in fr) + "\n")

    drop_pct = 100.0 * dropped / max(raw_lines, 1)
    print(f"\n===== 结果 =====")
    print(f"实际帧数: {len(frames)}（文件 {args.outfile}）")
    print(f"丢弃行: {dropped}/{raw_lines}（{drop_pct:.2f}%，>1% 查串口质量）")
    c0 = [fr[0] for fr in frames]
    print(f"c0 范围: {min(c0):.2f} ~ {max(c0):.2f}（量级检查 |c0|<200，地板簇≈-110 报警）")
    if min(c0) < -90:
        print("[WARN] 检测到疑似静音地板帧（c0<-90）→ 建议重采（采集纪律：全程目标环境声）")
    return 0

if __name__ == "__main__":
    sys.exit(main())
