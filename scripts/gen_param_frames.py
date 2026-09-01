#!/usr/bin/env python3
"""A4-04 参数帧 CRC16 校验向量生成器（R27 三源交叉验证：crcmod 权威库锁定）。
   帧字段：byte0=0xA5 帧头 / byte1=0x01 地址 / byte2=0x21 命令(写单参数)
           byte3=参数ID / byte4..5=参数值(uint16 LSB-first) / byte6..7=CRC16(大端，高在前)
   算法：Modbus CRC16，poly 0x8005(表序0xA001), init 0xFFFF, 反射, 结果异或0x0000。
   发送序：大端（高字节在前，低字节在后）——三源实测一致（csdn/工业monitor/wiki + crcmod）。
   阶段4 用例帧的完整字节序列由此脚本生成，PC 端(收集脚本)与板端 proto 解析必须一致。

   crcmod 交叉验算（本机已验证）：
     crcmod(`05 06 05 02 00 01`)=0x82E8 -> 帧尾 82 E8
     crcmod(`01 03 00 00 00 0A`)=0xCDC5 -> 帧尾 CD C5
   ⚠️ 曾自造 0xF4D7/低字节先发，均错；均已钉死为本脚本 + crcmod 一致值。
"""
import sys
try:
    import crcmod
    _CRCMOD = crcmod.mkCrcFun(0x18005, rev=True, initCrc=0xFFFF, xorOut=0x0000)
    _HAS_CRCMOD = True
except ImportError:
    _HAS_CRCMOD = False


def crc16_modbus(data):
    """位算法 CRC16，与 crcmod(rev=True,poly0x8005,init0xFFFF) 等价（已交叉验算）。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def make_frame(pid, value):
    """构造 8 字节参数下发帧。value 为 uint16。CRC 域=大端(高在前)。"""
    body = bytes([0xA5, 0x01, 0x21, pid, value & 0xFF, (value >> 8) & 0xFF])
    c = crc16_modbus(body)
    return bytearray(body + bytes([(c >> 8) & 0xFF, c & 0xFF]))  # 大端


CASES = [
    (0x01, 800,   "TH=8.0 (0x0320=800)"),
    (0x02, 40,    "触发连续帧 30->40"),
    (0x03, 60,    "解除连续帧 60 (默认值下发)"),
    (0x04, 100,   "滑窗100 (拒改帧，仍构造以验解析拒绝)"),
    (0x05, 30,    "窗内触发帧 30 (默认值下发)"),
]
FAIL = 0
m1 = crc16_modbus(_ := bytes([0x05, 0x06, 0x05, 0x02, 0x00, 0x01]))
m2 = crc16_modbus(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x0A]))
print(f"取样 05 06 05 02 00 01 -> 0x{m1:04X} (期望0x82E8) {'PASS' if m1==0x82E8 else 'FAIL'}")
print(f"取样 01 03 00 00 00 0A -> 0x{m2:04X} (期望0xCDC5) {'PASS' if m2==0xCDC5 else 'FAIL'}")
if _HAS_CRCMOD:
    m3 = _CRCMOD(bytes([0xA5, 0x01, 0x21, 0x01, 0x20, 0x03]))
    m4 = _CRCMOD(bytes([0xA5, 0x01, 0x21, 0x02, 0x28, 0x00]))
    m5 = _CRCMOD(bytes([0xA5, 0x01, 0x21, 0x04, 0x64, 0x00]))
    print(f"crcmod(TH8.0帧)=0x{m3:04X} 位算法=0x{crc16_modbus(bytes([0xA5,0x01,0x21,0x01,0x20,0x03])):04X} {'PASS' if m3==crc16_modbus(bytes([0xA5,0x01,0x21,0x01,0x20,0x03])) else 'FAIL'}")
else:
    print("(crcmod 未装，跳过交叉验算；位算法取样已验)")

print("\n=== 阶段4 下发用例帧 (hex，PC端照此发送) ===")
for pid, val, name in CASES:
    f = make_frame(pid, val)
    c = (f[6] << 8) | f[7]
    print(f"[{name}]  ID=0x{pid:02X} 值={val}  CRC16=0x{c:04X}  帧={f.hex(' ').upper()}")
if FAIL:
    sys.exit(1)