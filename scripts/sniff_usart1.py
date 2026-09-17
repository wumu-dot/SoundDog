#!/usr/bin/env python3
"""sniff_usart1.py — 只读嗅探候选串口，识别哪一个是 USART1(调试口/音频刷屏)。
只读，不发任何字节；打开后立即拉低 DTR/RTS 避免误触发板端复位。
用法：python sniff_usart1.py [COM3 COM4 ...] [秒数]
"""
import sys
import time
import serial

def main():
    ports = sys.argv[1:-1] or ["COM3", "COM4"]
    secs = float(sys.argv[-1]) if sys.argv[-1:][0].isdigit() else 8.0
    ser = {}
    try:
        for p in ports:
            try:
                s = serial.Serial(p, 115200, timeout=0.2, rtscts=False, dsrdtr=False)
                s.dtr = False
                s.rts = False
                s.reset_input_buffer()
                ser[p] = s
                print(f"[open] {p} @115200 8N1 (r{secs:.0f}s)", flush=True)
            except Exception as e:
                print(f"[SKIP] {p}: {e}", flush=True)
        if not ser:
            print("no port opened"); return
        end = time.time() + secs
        buf = {p: b"" for p in ser}
        while time.time() < end:
            for p, s in ser.items():
                data = s.read(512)
                if data:
                    buf[p] += data
                    # 只输出可打印行，供人工辨识 BANDS/MFCC/每题/末尾滚动模式
                    txt = data.decode("latin1", "replace")
                    for line in txt.splitlines():
                        if line.strip():
                            print(f"[{p}] {line.rstrip()}", flush=True)
        # 汇总统计
        for p, b in buf.items():
            bands = b.count(b"BANDS")
            mfcc = b.count(b"MFCC")
            rxmark = b.count(b"RX[")
            print(f"[sum:{p}] bytes={len(b)} BANDS={bands} MFCC={mfcc} RX[]mark={rxmark}", flush=True)
    finally:
        for s in ser.values():
            s.cancel_read()
            s.close()

if __name__ == "__main__":
    main()