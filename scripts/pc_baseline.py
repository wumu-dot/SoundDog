#!/usr/bin/env python3
"""pc_baseline.py — 同步对照：COM5(适配器,主站) 发 FC03 读快照 + 抓 COM4(STM32 USART1) 的 RX[n] 帧。
判读：
  COM4 出现 RX[8]: 01 03 00 00 00 05 ...   = 板侧收帧正常 → 问题在其他主站
  COM4 出现 RX[1]: 00                        = 板侧物理收链路坏
  COM4 无 RX[n] 且适配器无应答          = 帧没到 STM32
"""
import threading
import time
import serial
import gen_param_frames as gpf

STM_UART1 = "COM4"   # STM32 调试口（USART1）
ADAPTER   = "COM5"   # USB-RS485 适配器
BAUD = 115200

def read_frame_for_snapshot():
    body = bytes([gpf.SLAVE_ADDR, gpf.FC_READ_HOLDING, 0x00, 0x00, 0x00, 0x05])
    c = gpf.crc16_modbus(body)
    return bytearray(body + bytes([c & 0xFF, (c >> 8) & 0xFF]))

def sniff_stm(port, secs, out):
    """只读抓 STM32 USART1，收集 RX[n] 行。"""
    try:
        s = serial.Serial(port, BAUD, timeout=0.2, rtscts=False, dsrdtr=False)
        s.dtr = False; s.rts = False
        s.reset_input_buffer()
    except Exception as e:
        out.append((port, f"[open fail] {e}")); return
    end = time.time() + secs
    raw = b""
    while time.time() < end:
        data = s.read(512)
        if data:
            raw += data
    try:
        s.cancel_read(); s.close()
    except Exception:
        pass
    lines = raw.decode("latin1", "replace").splitlines()
    rx = [l for l in lines if "RX[" in l]
    out.append((port, f"raw_bytes={len(raw)} rx_lines={len(rx)}"))
    for l in rx:
        out.append((port, l))

def main():
    results = []
    secs = 2.0
    t = threading.Thread(target=sniff_stm, args=(STM_UART1, secs, results))
    t.start()
    # 等抓包线程打开再发包
    time.sleep(0.6)
    frame = read_frame_for_snapshot()
    print(f"[adapter] 发送读快照帧: {frame.hex(' ').upper()}", flush=True)
    try:
        ser = serial.Serial(ADAPTER, BAUD, timeout=0.5,
                            parity=serial.PARITY_NONE, bytesize=8, stopbits=1)
        ser.reset_input_buffer()
        ser.write(bytes(frame))
        time.sleep(0.15)
        resp = ser.read(64)
        ser.close()
        print(f"[adapter] 应答: {resp.hex(' ').upper() if resp else '(空 / 无应答)'}", flush=True)
        if len(resp) >= 5 and resp[1] == 0x03 and resp[2] == 10:
            vals = [(resp[3 + 2 * i] << 8) | resp[4 + 2 * i] for i in range(5)]
            print(f"[adapter] 快照值: {vals}  READ_OK", flush=True)
        else:
            print(f"[adapter] 异常/非预期: 未得到 5 寄存器应答", flush=True)
    except Exception as e:
        print(f"[adapter] 打开 {ADAPTER} 失败: {e}", flush=True)
    t.join()
    print("---- STM32 UART1 (COM4) 抓包 ----", flush=True)
    for _, line in results:
        print(f"[COM4] {line}", flush=True)

if __name__ == "__main__":
    main()