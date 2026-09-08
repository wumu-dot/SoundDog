#!/usr/bin/env python3
"""A4-04 Modbus RTU 参数寄存器下发帧生成器（R27/R32 三源交叉验证：crcmod 权威库锁定）。

  协议（FEAT-A4-04 Modbus 迁移定稿，替代原 0xA5 私有帧）：
    - 栈：FreeModbus（quanghona/Freemodbus-CMSIS-RTOS2-STM32，bytes/callback 复用，电参数本地测）
    - 从站地址 0x01 / USART3 115200-8N1 / 保持寄存器(FC03 读、FC06 写单寄存器、FC10 写多)
    - 保持寄存器映射（1 基址，与 modbus_app.c reg_off_to_pid / §1.4a 参数表一一对应，禁止自造）：
        RTU 地址 0x0001 = TH 阈值（×100 定点，600=6.0）
        RTU 地址 0x0002 = 轨1 连续异常帧 → 报警
        RTU 地址 0x0003 = 解除 连续正常帧 → 解除
        RTU 地址 0x0004 = 轨2 滑窗长（固定=100，拒改）
        RTU 地址 0x0005 = 轨2 窗内异常帧阈值
    - CRC16 = Modbus 标准（poly 0x8005 / init 0xFFFF / 反射输入输出 / xorOut 0）
    - 发送序 = **低字节在前（小端）**（Modbus RTU 规定，与官方样例 `01 03 00 00 00 01`→`84 0A` 一致）
    - 越界写：板端整体拒绝 → 异常码 0x02（MB_ENOREG），参数全保留旧值

  阶段4 用例帧由本脚本生成，PC 端（pyserial Modbus master）照此发送；
  校验向量由 crcmod 交叉锁定（CAMMOD/位算法双实现，防自造，BUG-20260901-001 教训）。

  crcmod 交叉验算（本机已验证）：
    crcmod(`01 03 00 00 00 01`)=0x0A84 -> 完整帧 `01 03 00 00 00 01 84 0A`（小端 CRC）
  官方样例来源：modbus.org spec RTU 帧格式 + docs.chipkin.com Modbus CRC & LRC 排错。
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


SLAVE_ADDR = 0x01
FC_WRITE_SINGLE = 0x06


def make_write_frame(reg_addr, value):
    """FC06 写单保持寄存器帧。字段值大端，CRC 小端（低字节在前，Modbus RTU 规定）。"""
    body = bytes([SLAVE_ADDR, FC_WRITE_SINGLE,
                  (reg_addr >> 8) & 0xFF, reg_addr & 0xFF,
                  (value >> 8) & 0xFF, value & 0xFF])
    c = crc16_modbus(body)
    return bytearray(body + bytes([c & 0xFF, (c >> 8) & 0xFF]))  # CRC 小端


# 寄存器地址（1 基址，见 modbus_app.c reg_off_to_pid）与下发值
CASES = [
    (0x0001, 0x0320, "RTU 0x0001 TH=8.0 (800=0x0320)"),
    (0x0002, 40,     "RTU 0x0002 触发连续帧 30->40"),
    (0x0003, 60,     "RTU 0x0003 解除连续帧 60 (默认值下发)"),
    (0x0004, 100,    "RTU 0x0004 滑窗100 (拒改帧，仍构造以验解析拒绝)"),
    (0x0005, 30,     "RTU 0x0005 窗内触发帧 30 (默认值下发)"),
]

FAIL = 0
# 官方样例锁：`01 03 00 00 00 01` -> CRC 0x0A84 -> 帧尾 `84 0A`（小端）
m1 = crc16_modbus(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x01]))
print(f"官方样例 01 03 00 00 00 01 -> 0x{m1:04X} (期望0x0A84) {'PASS' if m1 == 0x0A84 else 'FAIL'}")
if _HAS_CRCMOD:
    m2 = _CRCMOD(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x01]))
    print(f"crcmod 同向量=0x{m2:04X} 位算法=0x{m1:04X} {'PASS' if m1 == m2 else 'FAIL'}")
else:
    print("(crcmod 未装，跳过交叉验算；官方样例位算法已验)")

if m1 != 0x0A84:
    FAIL = 1

print("\n=== 阶段4 下发用例帧（Modbus RTU，PC 端照此发送，8字节/帧） ===")
for reg, val, name in CASES:
    f = make_write_frame(reg, val)
    c = (f[6] | (f[7] << 8))   # 小端读回 CRC16
    print(f"[{name}]  RTU地址=0x{reg:04X} 值={val}  CRC16=0x{c:04X}  帧={f.hex(' ').upper()}")

print("\n=== 阶段4 读取帧（FC03 读保持寄存器，用于验证读快照） ===")
read_frames = [(0x0001, 5, "读 0x0001..0x0005 (TH/触发/解除/滑窗/窗内)")]
for reg, cnt, name in read_frames:
    body = bytes([SLAVE_ADDR, 0x03, (reg >> 8) & 0xFF, reg & 0xFF,
                  (cnt >> 8) & 0xFF, cnt & 0xFF])
    c = crc16_modbus(body)
    f = bytearray(body + bytes([c & 0xFF, (c >> 8) & 0xFF]))
    print(f"[{name}]  帧={f.hex(' ').upper()}")

if FAIL:
    sys.exit(1)