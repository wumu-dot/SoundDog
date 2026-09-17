#!/usr/bin/env python3
"""loopback_test.py — R35 软件回环判定：COM5(适配器)发单字节 0x55，
STM32 回环探针原样回发；同时抓 COM4(STM32 UART1) 的 RX[n]。
判读（适配器发→收）：
  读到 0x55            = STM32↔适配器 双向链路+接线 全通
  COM4 无 RX[1]: 55    = STM32 没收(适配器→STM32 方向/接线坏)
  有 RX 但回不来        = STM32→适配器 方向坏(PD11 DE 或 A/B 反)
"""
import threading, time, serial

STM_UART1 = "COM4"
ADAPTER   = "COM5"
BAUD = 115200
SENDBYTE = b"\x55"

def sniff(port, secs, out):
    try:
        s = serial.Serial(port, BAUD, timeout=0.2, rtscts=False, dsrdtr=False)
        s.dtr=False; s.rts=False; s.reset_input_buffer()
    except Exception as e:
        out.append((port, f"[open fail] {e}")); return
    end = time.time() + secs
    lines=[]
    while time.time() < end:
        d = s.read(512)
        if d: lines.append(d.decode("latin1","replace"))
    try: s.cancel_read(); s.close()
    except: pass
    out.append((port, "".join(lines)))

def main():
    out=[]
    t=threading.Thread(target=sniff, args=(STM_UART1, 2.0, out)); t.start()
    time.sleep(0.6)
    # 适配器发送
    try:
        ser = serial.Serial(ADAPTER, BAUD, timeout=0.5, rtscts=False, dsrdtr=False)
        ser.dtr=False; ser.rts=False
        ser.reset_input_buffer()
        ser.write(SENDBYTE)
        r = ser.read(16)
        ser.close()
        print(f"[adapter] 发送={SENDBYTE.hex()} 读回={r.hex(' ').upper() if r else '(空)'}")
        ok = r == SENDBYTE
        print(f"[adapter] 回环={('OK 链路通' if ok else 'FAIL')}")
    except Exception as e:
        print(f"[adapter] {ADAPTER} 打开失败: {e}")
    t.join()
    for _, txt in out:
        rx = [l for l in txt.splitlines() if "RX[" in l]
        print(f"[COM4] 最后RX行: {rx[:4] if rx else '无'}")

if __name__=="__main__":
    main()