# FEAT-A4-04-参数可调

> **AI 执行规则**：必须按阶段顺序执行，每个阶段开头声明角色。严禁跳跃、越界、自审自判。
> 阶段间停等：每完成一个阶段，停下来，输出结果，等人说"继续"。
> 阶段5通过后：回填父FEAT项目表该行 + 更新 `docs/features/INDEX.md` + `docs/features/维护地图.md`。
> 历史记录只更新状态字段，严禁删除/篡改已完成记录。
> 事实来源：`.sop-agent规格.md` §3（禁止自造数值）。

## 0. 元数据

- **父FEAT**：FEAT-A4-模版比对检测

- **优先级**：P2

- **预估总耗时**：2.5h

- **当前状态**：🟡 阶段3 代码落地（Modbus RTU 从站已集成、编译通过），待阶段4 实测

## 1. 总体目标与硬边界

### 1.1 核心目标（一句话）

> 通过 RS485 下发异常检测的**阈值**（A4-02 d\_min 阈值）与**帧数**（A4-03 的 30/60），使检测参数可在线调整，无需改代码重烧。

### 1.4a 帧协议最终决议（Modbus 迁移定稿 2026-09-08，人确认；替代原 A5-02 0xA5 私有帧）

- **协议**：**Modbus RTU 从站**（FreeModbus），**废弃原 0xA5/0x5A 私有帧**。用成熟方案而非自造协议（R27/R32）。

- **数据流**：PC(Modbus Master) → USB转RS485适配器 → A/B 差分 → 板上 MAX3485#1 → TTL → STM32 USART3(PD8/PD9) → FreeModbus 栈 → `eMBRegHoldingCB`(modbus\_app.c) → `param_apply`(freertos.c，校验单一源) → `debounce_set_params` → debounce 状态机。

- **链路参数**：从站地址 `0x01`、USART3 `115200`、`8N1`、Modbus RTU（静默超时定帧，无帧头/帧尾）。

- **寄存器映射（保持寄存器 1 基址，与 modbus\_app.c** **`reg_off_to_pid`** **一一对应，禁止自造）**：

  | RTU地址  | 参数ID | 名称       | 默认值 | 范围                    | 传输格式                        |
  | ------ | ---- | -------- | --- | --------------------- | --------------------------- |
  | 0x0001 | 0x01 | 阈值 TH    | 6.0 | 1.0\~20.0             | `uint16_t` 定点 ×100（600=6.0） |
  | 0x0002 | 0x02 | 触发连续帧 30 | 30  | 1\~100                | `uint16_t`                  |
  | 0x0003 | 0x03 | 解除连续帧 60 | 60  | 1\~100                | `uint16_t`                  |
  | 0x0004 | 0x04 | 滑窗长度 100 | 100 | **固定=100**（拒改，见影响分析①） | `uint16_t`                  |
  | 0x0005 | 0x05 | 窗内触发帧 30 | 30  | 1\~100                | `uint16_t`                  |

- **功能码**：`0x03` 读保持寄存器（读快照）、`0x06` 写单寄存器、`0x10` 写多寄存器；读路径实时 `debounce_get_param()` 快照，写路径**两遍校验**——先整体范围校验（任一越界→整帧拒绝 `MB_ENOREG`→异常 0x02），全部合法才逐一 `param_apply`。

- **越界/坏帧策略**：越界值（含组合写中任一越界）**整体拒绝并保留旧值**（查 §3.5 用例①②）；坏帧（CRC16 错/地址不符）**由 FreeModbus 丢弃**（用例③④）。

- **运行期生效**：不下发时用编译期默认，行为与 A4-03 完全一致（AC-12 回归）。

- **已知限制**：参数不落 Flash，重启恢复默认（A7-03 未实现，待定）。

### 1.4b 影响范围与风险（readonly 代码实测 + 编译证据）

| 文件                                        | 改动                                                                                        | 影响                            |
| ----------------------------------------- | ----------------------------------------------------------------------------------------- | ----------------------------- |
| `Middlewares/Third_Party/FreeModbus/`（新增） | quanghona 仓库协议栈 core + portserial/porttimer/portevent/portcritical（port 适配 USART3/RS485）  | 复用成熟栈，port 层本地化               |
| `App/modbus_app.c/h`（新增）                  | 保持寄存器读写回调（0x0001\~0x0005）→ `param_apply`                                                  | 参数访问单一入口，读快照/写两遍校验            |
| `App/freertos.c`                          | 删 paramTask/RS485LoopTask；新增 MBSlavePollTask（eMBInit→eMBEnable→eMBPoll）+ `param_apply` 定义 | 不碰 specTask 关键路径              |
| `BSP/param.h`                             | 保留参数ID/范围宏 + `param_apply`（校验单一源）；废弃 0xA5 帧接口                                             | A4-03 逻辑不变                    |
| `Src/stm32f4xx_it.c`                      | 新增 `USART3_IRQHandler`（RXNE/IDLE/TC/ORE + HAL\_UART\_IRQHandler）                          | 供 FreeModbus portserial 中断收/发 |
| `Src/usart.c`                             | USART3 NVIC 使能（收中断关发送中断，UltraLong 收字节型）                                                   | 半双工方向由 portserial 控 DE(PD11)  |
| Makefile                                  | C\_SOURCES 收录 FreeModbus 源 + modbus\_app.c；C\_INCLUDES 加头路径；移除 param.c                    | 防链接失败（BUG-002 教训）             |
| `scripts/gen_param_frames.py`             | 0xA5 帧 → Modbus RTU 帧（FC03/FC06）+ crcmod 官方样例锁                                            | PC 端测试帧与板端一致                  |

**主动预警（过程问题 + 对策）**：

- ① 位图容量耦合：滑窗改大越界 → **滑窗 RTU 0x0004 拒改**（reg\_is\_valid 要求严格=100）+ 位图按最大预留。

- ② 宏→变量回归风险：默认值=编译期值，下线默认、零行为变化；烧录后回归 30/60 防抖（AC-12）。

- ③ RS485 链路未过（A5-01 阶段4/5 未验收）：A4-04 阶段4 依赖 USB转RS485 适配器 → 直接连板上 MAX3485#1 A/B/GND，做 Modbus 读写实测。

- ④ CRC/字节序两侧一致：Modbus RTU 规范固定（CRC 小端先发），crcmod 交叉锁定。

- ⑤ 半双工/阻塞：FreeModbus 中断驱动，MBSlavePollTask 独立轮询，不阻塞 specTask 100 帧/s 关键路径。

- ⑥ 资源预算：MBSlavePollTask 栈 \~1KB、port 环形缓冲 + 寄存器快照 \~100B、Flash 固件 FreeModbus 栈 +\~5KB（实测 flash 176752B / 0x2A9F0，总固件 274432B）。CPU：eMBPoll 低占，空闲不忙转。

### 1.4c 傻瓜式操作单（照做即可）

#### Modbus 帧（PC 端用 pyserial + gen\_param\_frames.py 生成，禁止手算 CRC）

- **写单参数**（FC06，8 字节）：`01 06 HHHL VVVV CCCL`

  - 字节0 `0x01` 从站地址；字节1 `0x06` 功能码写单寄存器

  - 字节2=寄存器地址高、字节3=低（大端）；字节4=值高、字节5=值低（大端）

  - 字节6=CRC16 低（小端先发）、字节7=CRC16 高

  - 例：TH=8.0(800=0x0320) → 地址 0x0001 → 帧 `01 06 00 01 03 20 D9 22`（`gen_param_frames.py` 输出，crcmod 已验）

- **读保持寄存器**（FC03）：`01 03 HHHL CCNC CLLC` → 应答帧读 5 寄存器回快照。

- 全量用例帧见 `python scripts/gen_param_frames.py` 输出（R27/R32 可点证据）。

##### 协议来源（2026-09-08 实采，可点击验证，禁止口头声称）

- **FreeModbus 栈**（本工程采用、与 STM32F407+CMSIS-RTOS2+GCC 环境匹配）：[quanghona/Freemodbus-CMSIS-RTOS2-STM32](https://github.com/quanghona/Freemodbus-CMSIS-RTOS2-STM32)（portserial.c 收帧 put\_index bug 已修；RS485 宏对齐 PD11；仅用 Modbus slave，master 关 `MB_MASTER_RTU_ENABLED=0`）。

- **Modbus RTU 规范**（帧格式 + CRC16 多项式/初值/字节序）：官方 [modbus.org Modbus Protocol Access Guide](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf) 与 [docs.chipkin.com Modbus CRC & LRC 排错](https://docs.chipkin.com/articles/modbus-crc-lrc-error-checking-and-troubleshooting/)（官方样例 `01 03 00 00 00 01`→CRC `0x0A84`→帧尾 `84 0A`，CRC 小端）。

- **CRC16-Modbus 交叉锁定**：`crcmod(0x18005, rev=True, initCrc=0xFFFF)` + 位算法双实现，见 `gen_param_frames.py`（BUG-20260901-001 教训：禁自造向量；官方样例 ALL PASS）。

- **Modbus 寄存器访问范本**：`modbus_app.c eMBRegHoldingCB`，错误语义 `prveMBError2Exception`（mb.c），对齐官方端口例程。

#### 3.1 编译烧录

1. `firmware\build.bat`：期望无错误退出、无新增警告（记录：\_\_\_\_\_\_）。
2. `firmware\build_and_flash.bat`：期望 `wrote ...` / `Verified OK`（记录：\_\_\_\_\_\_）。
3. 兜底：OpenOCD 连不上 → 查 SWD 四线；仍不行 → `flash_isp.bat`。

#### 3.2 RS485 接线（全程断电）

1. 板上 MAX3485#1：A/B/GND 引出；**RXD→PD8(USART3\_TX)、TXD→PD9(USART3\_RX)、EN←PD11**（GPIO 型，高=发/低=收，由 portserial 控制）。
2. PC 端 **USB转RS485 一体适配器**（CH343/CP2102，自动方向）→ A/A、B/B、GND 与板端 A/B/GND 对接（**共地**）。
3. 上电前核对 A/B 与电源（记录：\_\_\_\_\_\_）。

#### 3.3 PC→设备直连实测

1. 上电，SSCOM(USART1,115200) 见 boot 三行。
2. PC 端 `python scripts/gen_param_frames.py` 生成帧，pyserial 打开适配器 COM 口（115200-8N1，`parity=N`）发送 FC06 写帧 → 板端打印生效值。
3. 异常 → 查 §3.6：A/B 反接 / 未共地 / COM口选错 / RTU 超时参数。

#### 3.4 参数下发生效测试

1. 下发 TH=8.0（RTU 0x0001，值 0x0320=800，帧 `01 06 00 01 03 20 D9 22`）：记录下发：___；板端打印/生效值：___（应=8.0）。
2. 下发触发帧 30→40（RTU 0x0002）：记录生效帧数：\_\_\_（应=40）。
3. 确认 DMIN/ALARM 打印仍正常、30/60→40/60 逻辑生效（改参后回归 A4-03）。

#### 3.5 越界与坏帧用例

| 用例 | 操作                | 期望                  | 实际          |
| -- | ----------------- | ------------------- | ----------- |
| ①  | 下发 TH=0.5（<1.0）   | 拒绝，保留旧值（TH 不回退）     | 行为：\_\_\_   |
| ②  | 下发 TH=25.0（>20.0） | 拒绝保留旧值              | 行为：\_\_\_   |
| ③  | 从站地址错/CRC16 错     | FreeModbus 丢弃，无异常应答 | 是否丢弃：\_\_\_ |
| ④  | 组合写中一寄存越界         | 整帧拒绝(异常0x02)，全保留旧值  | 是否拒绝：\_\_\_ |

#### 3.6 故障速查表

| 现象      | 原因                   | 处理                                  |
| ------- | -------------------- | ----------------------------------- |
| 回环/下发不通 | A/B 反接/未共地/COM口错     | 断电核对；适配器 A/B/GND 直接连板端              |
| 下发不生效   | 寄存器地址/值/CRC 错/RTU 超时 | 用 gen\_param\_frames.py 生成帧；核对 §3.2 |
| 越界被接受   | 校验未实现                | 退回阶段3补 reg\_is\_valid               |
| 半双工冲突   | DE/RE 方向错            | 确认 PD11 高=发/低=收 + portserial        |
| 重启参数丢失  | 未持久化（A7-03）          | 已知限制，只保运行期生效                        |

## 2. 执行路线图（5 阶段）

### 🟢 阶段 1：准备（角色：Dev Agent）【✅ 2026-09-01 完成】

- 确认 A4-03 已通过（防抖 30/60 生效）

- 确认 USART3 已初始化（`usart.c` MX\_USART3\_UART\_Init + huart3，115200）+ rs485\_drv.c 存在（EN=PD11 方向控制）

- 帧协议初版：沿用 A5-02（0xA5/0x5A/CRC16-Modbus参数），人已确认

- 影响范围/风险已输出（§1.4b）

### 🔵 阶段 2：设计（角色：Dev Agent）【✅ 2026-09-08 定稿（Modbus 迁移）】

- ~~帧格式 8 字节 0xA5~~ → **改为 Modbus RTU 从站**（2026-09-08 人确认，废弃 0xA5 私有帧，用 FreeModbus 成熟栈）

- 参数集：5 项（§1.4a 表）；滑窗 RTU 0x0004 拒改

- 越界整体拒绝保留旧值 + 坏帧丢弃

- MBSlavePollTask 独立轮询，不阻塞 specTask

### 🟠 阶段 3：实现（角色：Dev Agent）【✅ 2026-09-08 代码落地】

- [x] 步骤1：接入 FreeModbus（Middleware/Third\_Party/FreeModbus，port 适配 USART3/RS485，修 portserial put\_index bug，RS485 宏对齐 PD11，关 master）

- [x] 步骤2：新建 `modbus_app.c/h`（保持寄存器 0x0001\~0x0005 读写回调，读快照/写两遍校验）

- [x] 步骤3：改 `freertos.c`：删 paramTask/RS485LoopTask，新增 MBSlavePollTask + `param_apply` 定义

- [x] 步骤4：`stm32f4xx_it.c` 加 USART3\_IRQHandler；`usart.c` USART3 NVIC

- [x] 步骤5：Makefile 收录 FreeModbus 源 + 头路径，移除 param.c（BUG-002 教训）

- [x] 步骤6：`build.bat` 编译无错误；`gen_param_frames.py` → Modbus RTU 帧

- **判定标准**：编译/烧录输出证据已记录（flash 176752B，总 274432B）

- **⏸️ 停等：人确认后进入阶段 4**

### 🟣 阶段 4：测试（角色：Test Agent）

- [ ] 步骤1：链路直连自检（§3.3）

- [ ] 步骤2：正常下发→生效（§3.4），记录下发值 vs 生效值

- [ ] 步骤3：越界/坏帧用例（§3.5）逐条记录

- [ ] 步骤4：回归 A4-03（改参后 30/60→40/60 逻辑仍按新帧数生效，AC-12）

- [ ] 步骤5：逐条核对 §4 AC-01\~AC-12

- **判定标准**：AC 有实际记录值

- **⏸️ 停等：人确认后进入阶段 5**

### 🔴 阶段 5：审查（角色：Review Agent）

- 独立审查：AC 逐条打勾 + 寄存器读写健壮性 + 方向控制

- 通过 → 回填「维护与调试」+ 父FEAT项目表 + INDEX + 维护地图

- 未通过 → 退回对应阶段

## 3. 傻瓜式操作单（照做即可）

> 已并入 §1.4c，编号 3.0\~3.6 以 §1.4c 为准。

## 4. 验收标准（12 条）

### 功能验收

- [ ] AC-01 RS485(Modbus) 下发阈值/帧数后立即生效（记录下发值 vs 生效值）

- [ ] AC-02 越界值被整体拒绝并保留旧值（记录越界值与拒绝行为，对应用例①②）

- [ ] AC-03 坏帧（CRC/地址错）被丢弃且不生效（记录用例③④结果）

- [ ] AC-04 链路直连自检通过（板端回显/打印生效，记录结果）

- [ ] AC-05 编译通过：`build.bat` 无错误退出（记录结果）

- [ ] AC-06 无新增警告

- [ ] AC-07 文档已更新（本 FEAT 维护章节 + 父表回填 + INDEX + 维护地图）

### 代码质量 / 安全红线 / 测试边界

- [ ] AC-08 无硬编码密钥：**N/A**（无密钥/凭据概念——说明）

- [ ] AC-09 错误处理：半双工方向切换/接收缓冲正确（记录一次实测）；坏帧丢弃

- [ ] AC-10 输入校验：寄存器范围校验生效（记录越界拒绝，对应 AC-02）

- [ ] AC-11 新增测试：正常下发/越界下发/坏帧各若干次（次数：待定（需人确认）），逐条记录（§3.4/§3.5）

- [ ] AC-12 全部回归：A4-03 防抖判决不受影响（改参后 30/60 帧逻辑仍按新帧数生效）；覆盖率 **N/A**（嵌入式手测——说明）

## 5. 维护与调试（阶段5通过后回填）

### 5.1 影响文件

| 文件                                                      | 改动类型 | 说明                                                                              |
| ------------------------------------------------------- | ---- | ------------------------------------------------------------------------------- |
| `firmware/soundDog/Middlewares/Third_Party/FreeModbus/` | 新增   | FreeModbus 协议栈（port: portserial/porttimer/portevent/portcritical + modbus core） |
| `firmware/soundDog/App/modbus_app.c/h`                  | 新增   | 保持寄存器 0x0001\~0x0005 读写回调（读快照/写两遍校验）                                            |
| `firmware/soundDog/App/freertos.c`                      | 修改   | MBSlavePollTask（eMBInit→eMBEnable→eMBPoll）+ param\_apply                        |
| `firmware/soundDog/Src/stm32f4xx_it.c`                  | 修改   | USART3\_IRQHandler（RXNE/IDLE/TC/ORE）                                            |
| `firmware/soundDog/BSP/param.h`                         | 修改   | 参数ID/范围宏 + param\_apply 声明（废弃 0xA5 接口）                                          |
| `firmware/soundDog/Inc/pin_config.h`                    | 已有   | RS485 DE = PD11                                                                 |

### 5.2 调试要点

- 链路不通 → 查 A/B 反接、共地、USB转RS485 适配器 COM 口/驱动

- 下发不生效 → 用 `gen_param_frames.py` 生成帧核对寄存器地址/值/CRC；确认 RTU 静默超时

- 参数重启丢失 → A7-03 Flash 持久化未实现（已知限制）

### 5.3 测试入口

- §3.3 链路直连 + §3.4 下发生效 + §3.5 越界/坏帧用例表（对应 AC-01\~AC-04、AC-11）

## 6. 执行日志

| 时间         | 阶段  | 角色        | 摘要                                                                                                                             | 遇阻 |
| ---------- | --- | --------- | ------------------------------------------------------------------------------------------------------------------------------ | -- |
| 2026-09-08 | 阶段3 | Dev Agent | FreeModbus 集成 + modbus\_app 寄存器回调 + USART3 中断 + Makefile 收录 + gen\_param\_frames.py 改 Modbus RTU；编译通过（flash 176752B/总 274432B） | 无  |
| 2026-09-08 | 阶段2 | Dev Agent | 协议定稿：废弃 0xA5 → Modbus RTU（人确认）；寄存器映射/链路/越界策略定稿                                                                                 | 无  |
| <br />     | 阶段1 | Dev Agent | 准备完成（A4-03 通过、USART3/RS485 确认、初版 0xA5 协议记录）                                                                                    | 无  |

