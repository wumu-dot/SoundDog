#!/usr/bin/env python3
"""_b_probe_tx.py — 判据B（2026-09-20）：USB转TTL(COM5).TX直接灌PD9(USART3_RX)，
绕过RS1模块，验证STM32 USART3 收路径 / PD9 是否正常。
不再期待TTL读回（TTL RX 未接），只看 STM32 UART1(COM4) 的 RX[n] 探针。
判读：
  COM4 出现 RX[x]: 55  → PD9 + USART3 收路径正常 → 问题在 RS1 模块（换模块）
  COM4 仍无 RX[        → PD9/USART3 收路径异常   → 换模块也没用（查板子）
"""
import threading, time, serial

STM_UART1 = "COM4"   # STM32 USART1 调试口，看 RX 探针
STORAGE   = "COM5"   # USB转TTL，TX→PD9 发包（只发，不回读）
BAUD      = 115200
SEQUENCE  = b"\x55\x55\x55\x55\x55\x55\x55\x55"  # 8 个 0x55，便于抓

def sniff(port, secs, out):
    s = serial.Serial(port, BAUD, timeout=0.2, rtscts=False, dsrdtr=False)
    s.dtr=False; s.rts=False; s.reset_input_buffer()
    end = time.time() + secs
    lines=[]
    while time.time() < end:
        d = s.read(512)
        if d: lines.append(d.decode("latin1","replace"))
    s.close(); out.append("".join(lines))

def main():
    out=[]
    t=threading.Thread(target=sniff, args=(STM_UART1, 2.5, out)); t.start()
    time.sleep(0.6)
    s = serial.Serial(STORAGE, BAUD, timeout=0.5, rtscts=False, dsrdtr=False)
    s.dtr=False; s.rts=False
    s.reset_input_buffer()
    for _ in range(3):
        s.write(SEQUENCE); time.sleep(0.05)
    s.close()
    print(f"[TTL={STORAGE}] 已发 3 组 0x55 @115200 (TTL.TX->PD9, 不回读)")
    t.join()
    txt = out[0]
    print("=== COM4 抓到原始输出 ===")
    print(txt[:800] if txt else "(COM4 全程无任何字节)")
    rx = [l for l in txt.splitlines() if "RX[" in l]
    print("--- RX 探针:", rx[:8] if rx else "无")
    if rx:
        print(">>> 判定: PD9/USART3 收路径正常 → 问题在 RS1 模块,换模块")
    else:
        print(">>> 判定: PD9/USART3 收路径异常或探针未开 → 换模块也没用,查板子PD9/复用/BSP")

if __name__=="__main__":
    main()