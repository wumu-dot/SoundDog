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
import time
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

READ_ORDER = ["TH(x100)", "触发帧", "解除帧", "滑窗", "窗内触发"]  # -> 0x0001..0x0005

# 越界用例：用于验证异常应答（另构造，不入 CASES 正常下发列表）
BAD_CASES = [
    (0x0001, 0x0032, "越界 TH=0.5(<1.0) 期望异常0x02"),
    (0x0001, 0x0FA0, "越界 TH=25.0(>20.0) 期望异常0x02"),
    (0x0004, 200,    "越界 滑窗=200(!=100) 期望异常0x02"),
]


def classify_write_response(resp, frame):
    """FC06 写应答分类：正常写=回显原帧；越界/地址错=异常帧(0x86 + 异常码)。"""
    if not resp:
        return "无应答（链路不通 / 地址不符 / 静默超时）"
    if bytes(resp[:len(frame)]) == bytes(frame):
        return "OK 已生效（回显原帧）"
    if len(resp) >= 3 and resp[1] == (FC_WRITE_SINGLE | 0x80):
        code = resp[2]
        return ("异常0x%02X（%s）" % (code, "MB_ENOREG 越界拒绝" if code == 0x02 else "其他"))
    return "未知应答: %s" % resp.hex(" ").upper()


def send_and_read(port, baud=115200):
    """--com 模式：逐帧下发并回读板端应答（验证链路/生效/越界，§3.4/§3.5）。

    依赖：USB转RS485 一体适配器（自动方向），A/B/GND 直连板上 MAX3485#1。
    判据：
      正常写 -> 应答=回显原帧（FC06 回显）
      越界写 -> 应答=异常帧 01 86 02 <CRC>（MB_ENOREG，全保留旧值）
    从站地址 0x01 / 115200 8N1。
    """
    try:
        import serial
    except ImportError:
        print("[ERR] 未安装 pyserial：pip install pyserial"); return 2
    ser = serial.Serial(port, baud, timeout=0.3,
                        parity=serial.PARITY_NONE,
                        bytesize=8, stopbits=1)
    print(f"\n=== {port}@{baud} 下发并回读（Modbus RTU） ===")
    try:
        # 1) 正常下发用例（§3.4）
        print("-- 正常下发 --")
        for reg, val, name in CASES:
            frame = make_write_frame(reg, val)
            ser.reset_input_buffer()
            ser.write(bytes(frame))
            time.sleep(0.06)            # 从站处理 + RTU 应答窗口
            resp = ser.read(64)
            print(f"[{name}]  发={frame.hex(' ').upper()}")
            print(f"          应答={resp.hex(' ').upper()}  -> {classify_write_response(resp, frame)}")
            time.sleep(0.4)

        # 2) 越界用例（§3.5 ①②，期望异常）
        print("-- 越界用例（期望异常 0x02） --")
        for reg, val, name in BAD_CASES:
            frame = make_write_frame(reg, val)
            ser.reset_input_buffer()
            ser.write(bytes(frame))
            time.sleep(0.06)
            resp = ser.read(64)
            print(f"[{name}]  发={frame.hex(' ').upper()}")
            print(f"          应答={resp.hex(' ').upper()}  -> {classify_write_response(resp, frame)}")
            time.sleep(0.4)

        # 3) 读回快照（FC03 读 0x0001..0x0005，验证读路径 + 生效值）
        print("-- 读保持寄存器快照（FC03） --")
        body = bytes([SLAVE_ADDR, 0x03, 0x00, 0x01, 0x00, 0x05])
        c = crc16_modbus(body)
        frame = bytearray(body + bytes([c & 0xFF, (c >> 8) & 0xFF]))
        ser.reset_input_buffer()
        ser.write(bytes(frame))
        time.sleep(0.06)
        resp = ser.read(64)
        if len(resp) >= 5 and resp[1] == 0x03 and resp[2] == 10:
            regs = [(resp[3 + 2 * i] << 8) | resp[4 + 2 * i] for i in range(5)]
            print(f"  读帧   ={frame.hex(' ').upper()}")
            print(f"  应答   ={resp.hex(' ').upper()}")
            for nme, v in zip(READ_ORDER, regs):
                print(f"   {nme:>8} = {v}")
        else:
            print(f"  读帧   ={frame.hex(' ').upper()}")
            print(f"  应答   ={resp.hex(' ').upper()}  -> 非预期（读路径可能未通）")
    finally:
        ser.close()
    return 0


def main(argv):
    if "--com" in argv:
        i = argv.index("--com")
        port = argv[i + 1] if i + 1 < len(argv) else None
        if not port:
            print("[ERR] 用法: python gen_param_frames.py --com COMx [--baud 115200]")
            return 2
        baud = 115200
        if "--baud" in argv:
            bj = argv.index("--baud")
            baud = int(argv[bj + 1])
        return send_and_read(port, baud)
    return 0

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

if __name__ == "__main__":
    _rc = main(sys.argv[1:])
    if FAIL:
        sys.exit(1)
    if _rc:
        sys.exit(_rc)