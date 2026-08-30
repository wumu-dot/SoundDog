# serial_capture.py — A3-01 阶段4 补证：抓串口日志（boot 三行 + MFCC/SPEC/BANDS 节拍）
# 用法：python scripts/serial_capture.py [秒数]
import sys, time, subprocess, threading

try:
    import serial
except ImportError:
    print("[ERR] pyserial 未安装"); sys.exit(1)

PORT = "COM4"
BAUD = 115200
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 12

lines = []
t0 = time.time()

def reset_board():
    time.sleep(0.3)
    subprocess.run(["taskkill", "/f", "/im", "openocd.exe"],
                   capture_output=True)
    subprocess.run(["openocd", "-f", "openocd.cfg", "-c", "init; reset; exit"],
                   capture_output=True, cwd="firmware")

ser = serial.Serial(PORT, BAUD, timeout=1)
threading.Thread(target=reset_board, daemon=True).start()
print(f"[CAP] {PORT}@{BAUD} {DUR}s ...")
while time.time() - t0 < DUR:
    raw = ser.readline()
    if raw:
        try:
            s = raw.decode("ascii", "replace").strip()
        except Exception:
            s = repr(raw)
        if s:
            lines.append((time.time() - t0, s))
            print(f"{time.time()-t0:6.2f}s  {s}")
ser.close()

# ---- 分析 ----
boot = [l for t, l in lines if "boot OK" in l or "I2S_DRV" in l or "I2S DMA" in l]
mfcc = [l for t, l in lines if l.startswith("MFCC")]
fvals = [int(l.split("f=")[1].split()[0]) for l in mfcc if "f=" in l]
print("\n===== 分析 =====")
print(f"boot 三行: {len(boot)} 行 -> {boot}")
print(f"MFCC 行数: {len(mfcc)}")
if len(fvals) >= 2:
    dt = DUR - 0.5
    rate = (fvals[-1] - fvals[0]) / max(len(fvals) - 1, 1)
    print(f"f 首末: {fvals[0]} -> {fvals[-1]}，行间帧差均值: {rate:.1f} 帧/行")
    if len(fvals) > 2:
        import re
        wm = [int(l.split("wmid=")[1].split()[0]) for l in mfcc if "wmid=" in l]
        en = [int(l.split("e=")[1].split()[0]) for l in mfcc if "e=" in l]
        print(f"wmid 范围: {min(wm)}~{max(wm)}，e 范围: {min(en)}~{max(en)}")
