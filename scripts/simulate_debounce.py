# simulate_debounce.py — A4-03 阶段4：轨1（连续30帧）边界用例 PC 仿真验证
# 用法：python scripts/simulate_debounce.py
#
# 背景：轨1（连续30帧异常→报警）硬件人工复现困难（d_min 围绕 TH=6.0 抖动，
# 间歇清零无法凑齐连续30帧），本脚本在 PC 上完整复刻 firmware/soundDog/BSP/debounce.c
# 状态机逻辑（含 100bit 位图滑窗、双轨 OR、报警态不重复触发、解除复位清位图），
# 对 §3.3 边界用例逐条断言，形成 AC-01/AC-03 边界验证证据链（R21：结论附证据）。
#
# 注意：本脚本只是"逻辑等价复现"用于边界验证，不替代硬件全流程测试；
# 轨2（mofn 30/100）已在硬件实测触发（src=mofn n=30 / 解除 n=60）。
import sys

# ---- 参数（与 debounce.h 一致，禁止自造） ----
DB_CONSEC_ANOM_ALARM = 30   # 轨1：连续异常帧数 → 报警
DB_CONSEC_NORM_RELEASE = 60 # 解除：连续正常帧数 → 解除
DB_WIN_LEN = 100            # 轨2：滑窗长度
DB_WIN_ANOM_CNT = 30        # 轨2：窗内异常帧数阈值
DB_COUNT_CAP = 60000

DB_NORMAL = 0
DB_ALARMED = 1
DB_SRC_NONE = 0
DB_SRC_CONSEC = 1
DB_SRC_MOFN = 2


class Debounce:
    """与 debounce.c 逐行对应的状态机复刻（静态字段 → 实例字段）。"""

    def __init__(self):
        # 静态初始化 = 0（坑 4：上电/复位自动回 NORMAL，计数清零）
        self.state = DB_NORMAL
        self.consec_anom = 0
        self.consec_norm = 0
        self.win_bits = [0] * ((DB_WIN_LEN + 31) // 32)  # 100 → 4 words
        self.win_anom = 0
        self.win_head = 0

    # ---- 内部工具（与 db_bitmap_get/set 对应） ----
    def _bitmap_get(self, bit):
        return (self.win_bits[bit >> 5] >> (bit & 31)) & 1

    def _bitmap_set(self, bit, v):
        mask = 1 << (bit & 31)
        if v:
            self.win_bits[bit >> 5] |= mask
        else:
            self.win_bits[bit >> 5] &= ~mask

    # ---- 入窗一帧（与 db_win_push 对应） ----
    def _win_push(self, anomaly):
        old = self._bitmap_get(self.win_head)
        if old:
            self.win_anom -= 1
        self._bitmap_set(self.win_head, 1 if anomaly else 0)
        if anomaly:
            self.win_anom += 1
        self.win_head = (self.win_head + 1) % DB_WIN_LEN

    # ---- 主接口（与 debounce_process 对应，省略 C 指针输出） ----
    def process(self, frame_is_anomaly):
        """返回 (changed, alarm_state, alarm_src, alarm_count)。"""
        changed = 0
        src = DB_SRC_NONE
        count = 0

        if self.state == DB_NORMAL:
            if frame_is_anomaly:
                if self.consec_anom < DB_COUNT_CAP:
                    self.consec_anom += 1
            else:
                self.consec_anom = 0
            self._win_push(frame_is_anomaly)

            if self.consec_anom >= DB_CONSEC_ANOM_ALARM or self.win_anom >= DB_WIN_ANOM_CNT:
                if self.consec_anom >= DB_CONSEC_ANOM_ALARM:
                    src = DB_SRC_CONSEC
                    count = self.consec_anom
                else:
                    src = DB_SRC_MOFN
                    count = self.win_anom
                self.state = DB_ALARMED
                self.consec_anom = 0
                self.consec_norm = 0
                changed = 1
        else:  # DB_ALARMED
            if not frame_is_anomaly:
                if self.consec_norm < DB_COUNT_CAP:
                    self.consec_norm += 1
            else:
                self.consec_norm = 0
            self._win_push(frame_is_anomaly)

            if self.consec_norm >= DB_CONSEC_NORM_RELEASE:
                src = DB_SRC_NONE
                count = self.consec_norm
                self.state = DB_NORMAL
                self.consec_norm = 0
                self.win_anom = 0
                self.win_head = 0
                for i in range(len(self.win_bits)):
                    self.win_bits[i] = 0  # 必须清位：防陈旧 1 位被弹时 win_anom 下溢
                changed = 1

        return changed, self.state, src, count


def run_case(name, anomaly_seq, expect_alarm, expect_src=DB_SRC_CONSEC,
             expect_n=None, note=""):
    """按 anomaly_seq（True=异常帧）逐帧喂入，断言第一个报警事件。

    返回 (pass, 详情str)。报警后若需继续观察解除，传 extra_norm=...。"""
    db = Debounce()
    events = []
    alarm_event = None
    release_event = None
    for i, a in enumerate(anomaly_seq, 1):
        changed, state, src, count = db.process(a)
        if changed and state == DB_ALARMED and alarm_event is None:
            alarm_event = (i, src, count)
        elif changed and state == DB_NORMAL and alarm_event is not None:
            release_event = (i, src, count)

    ok = True
    det = []
    if expect_alarm:
        if alarm_event is None:
            ok = False
            det.append(f"期望报警但未触发")
        else:
            i, src, count = alarm_event
            det.append(f"第{i}帧 ALARM on src={'consec' if src==DB_SRC_CONSEC else 'mofn'} n={count}")
            if expect_src is not None and src != expect_src:
                ok = False
                det.append(f"  期望 src={'consec' if expect_src==DB_SRC_CONSEC else 'mofn'} 实得 {src}")
            if expect_n is not None and count != expect_n:
                ok = False
                det.append(f"  期望 n={expect_n} 实得 {count}")
        if release_event is not None:
            det.append(f"（预期外解除: 第{release_event[0]}帧 ALARM off n={release_event[2]}）")
            ok = False
    else:
        if alarm_event is not None:
            ok = False
            det.append(f"期望不报警但第{alarm_event[0]}帧触发了 src={'consec' if alarm_event[1]==DB_SRC_CONSEC else 'mofn'} n={alarm_event[2]}")
        else:
            det.append("未报警 ✓")
    if note:
        det.append(f"[{note}]")
    print(f"{'PASS' if ok else 'FAIL'}  {name}")
    for d in det:
        print(f"    {d}")
    return ok


def run_release_case(name, normal_seq, expect_off, expect_n=None, note=""):
    """已报警状态下喂 normal_seq 帧（anomaly 标志序列，False=正常帧），断言解除时机。"""
    db = Debounce()
    # 先喂 30 个连续异常帧触发报警（轨1），确保进入 ALARMED
    ev = None
    for i in range(30):
        changed, state, src, count = db.process(True)
        if changed and state == DB_ALARMED:
            ev = (i + 1, src, count)
    assert ev is not None and ev[1] == DB_SRC_CONSEC, f"前置触发失败: {ev}"

    release = None
    for i, a in enumerate(normal_seq, 1):
        changed, state, src, count = db.process(a)
        if changed and state == DB_NORMAL:
            release = (i, src, count)
            break

    ok = True
    det = [f"前置: 第{ev[0]}帧 ALARM on src=consec n={ev[2]}"]
    if expect_off:
        if release is None:
            ok = False
            det.append("期望解除但未触发")
        else:
            i, src, count = release
            det.append(f"第{i}帧 ALARM off n={count}")
            if expect_n is not None and count != expect_n:
                ok = False
                det.append(f"  期望 n={expect_n} 实得 {count}")
    else:
        if release is not None:
            ok = False
            det.append(f"期望不解除但第{release[0]}帧 ALARM off n={release[2]}")
        else:
            det.append("未解除 ✓（状态仍 ALARMED）")
    print(f"{'PASS' if ok else 'FAIL'}  {name}")
    for d in det:
        print(f"    {d}")
    return ok


def main():
    print("===== A4-03 阶段4 轨1边界用例 PC 仿真（复刻 debounce.c）=====\n")
    all_ok = True

    # 用例①：异常 29 帧后恢复 → 不报警（AC-03）
    all_ok &= run_case(
        "① 异常29帧→恢复：不报警",
        [True] * 29 + [False] * 80,
        expect_alarm=False, note="§3.3 用例①")

    # 用例②：异常 30 帧 → 报警（轨1 src=consec n=30）（AC-01）
    all_ok &= run_case(
        "② 异常30帧：轨1触发",
        [True] * 30 + [False] * 10,
        expect_alarm=True, expect_src=DB_SRC_CONSEC, expect_n=30,
        note="§3.3 用例②")

    # 用例③：报警后正常 59 帧 → 仍报警（不解除）（AC-03）
    all_ok &= run_release_case(
        "③ 报警后正常59帧：不解除",
        [False] * 59, expect_off=False, note="§3.3 用例③")

    # 用例④：报警后正常 60 帧 → 解除（AC-02）
    all_ok &= run_release_case(
        "④ 报警后正常60帧：解除",
        [False] * 60, expect_off=True, expect_n=60, note="§3.3 用例④")

    # 附加：轨2 对照——散布异常（无连续30帧）验证 mofn 触发、防双轨回归
    # 散布模式：单帧异常+单帧正常交替 → 连续异常恒为1，轨1永不满足，仅轨2（滑窗）能触发
    scattered29 = [True, False] * 29          # 窗内 29 个散布异常帧
    scattered30 = [True, False] * 30          # 窗内 30 个散布异常帧
    all_ok &= run_case(
        "⑤ 轨2对照：窗内29帧散布异常不报警",
        scattered29 + [False] * 42,
        expect_alarm=False, note="散布异常无连续30帧，窗内29<30 → 双轨均不触发")
    all_ok &= run_case(
        "⑥ 轨2对照：窗内30帧散布异常触发 mofn",
        scattered30 + [False] * 10,
        expect_alarm=True, expect_src=DB_SRC_MOFN, expect_n=30,
        note="散布异常无连续30帧 → 轨1不满足，轨2（滑窗30/100）接管")

    print("\n" + ("===== 全部 PASS：轨1边界 AC-01/AC-02/AC-03 证据链成立 ====="
                  if all_ok else "===== 存在 FAIL：请核对仿真与 debounce.c 一致性 ====="))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
