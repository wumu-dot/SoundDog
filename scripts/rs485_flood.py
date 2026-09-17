"""RS485 适配器(COM5)连续发 0x55 字节流，配合板上 STATUS(PB1)灯判定 MCU 是否收到。

判据:
  STATUS 灯快速闪烁  -> 字节进了 USART3(RXNE 触发) => 适配器↔MAX3485 链路通
  STATUS 灯不闪/不匀  -> 数据没进 MCU             => 卡在链路这级
"""
import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

ser = serial.Serial(port, baud, timeout=0.2)
data = bytes([0x55]) * 200      # 0x55=01010101，最大化跳变
print(f"=== 向 {port}@{baud} 连续发 0x55 字节流，请盯板上 STATUS(PB1)灯 ===")
for i in range(60):
    ser.write(data)
    time.sleep(0.05)
print("=== 发送完成 ===")
ser.close()