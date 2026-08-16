# CubeMX 未配置 Serial Wire 导致 SWD 锁死、无法再次烧录

- **Bug ID**：BUG-20260815-001
- **严重等级**：P0-致命（芯片变砖，无法二次烧录）
- **发现日期**：2026-08-15
- **修复日期**：2026-08-15

## 现象描述

第一次烧录成功。之后无论怎么烧录都报 `Error: init mode failed (unable to connect to the target)`：

```
ST-LINK SN  : 37FF71064E57343627BF1143
ST-LINK FW  : V2J37S7
Voltage     : 3.27V
Error: Unable to get core ID
Error: No STM32 target found!
```

供电正常（3.27V）、ST-Link 能识别、接线没变、其他项目同一块板能烧——唯独 SoundDog 连不上。

## 复现步骤

1. CubeMX 新建工程时，**SYS → Debug 未配置为 "Serial Wire"**（保持默认 Disable）
2. 生成代码 → gpio.c 把 PA13/PA14 当作普通 GPIO，设成 `GPIO_MODE_ANALOG`
3. 第一次烧录（芯片还是空白，PA13/PA14 保持默认 SWD 功能）→ 成功 ✅
4. 程序运行到 `MX_GPIO_Init()`，执行 `HAL_GPIO_Init(GPIOA, GPIO_PIN_13|GPIO_PIN_14)` 把 SWD 引脚改成模拟输入
5. SWD 立即失效 → 后续任何烧录都连不上 ❌

## 根因分析

**CubeMX 未启用 Serial Wire，导致 PA13(SWDIO)/PA14(SWCLK) 被配置成普通 GPIO（Analog）。**

时间线揭示了"为什么第一次能烧"：

```
第一次烧录：芯片空白 → PA13/PA14 保持上电默认的 SWD 功能 → 能连上
         ↓ 烧入坏固件
之后上电：芯片执行坏固件 → MX_GPIO_Init() 把 PA13/PA14 改 Analog → SWD 关闭
         ↓
再也连不上（死锁）
```

```c
// 坏固件的 gpio.c（关键行）：
GPIO_InitStruct.Pin = GPIO_PIN_0|...|GPIO_PIN_13|GPIO_PIN_14|...;  // 含 PA13/PA14
GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;  // 把 SWD 脚变成模拟输入
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);   // SWD 失效！
```

## 修复方案

分两层：

### 第一层：救回芯片（当前死锁）

SWD 被禁用后，OpenOCD 和 STM32_Programmer_CLI 的普通 SWD 模式都连不上。两个办法：

**方案 A：ISP 串口烧录（推荐，最可靠）**
- 用 USB 转串口接 PA9/PA10（TX→RX 交叉）
- BOOT0 拉高（按住 BOOT0 键）→ 上电 → 芯片进系统 bootloader
- 用 `STM32_Programmer_CLI -c port=COM4 br=115200 -w xxx.hex -v -s -ob` 串口烧录
- 脚本：`firmware/flash_isp.bat`

**方案 B：Under Reset 连接**
- ST-Link 接上 NRST 复位线
- `STM32_Programmer_CLI -c port=SWD mode=UR`（复位瞬间抢在固件执行前连接）

### 第二层：根治（避免复发）

CubeMX → `System Core → SYS → Debug → 选 "Serial Wire"` → 重新生成代码。

正确配置后 gpio.c 的 PA 引脚 Analog 列表**不应包含 PA13/PA14**。

## 影响文件
- `firmware/soundDog/soundDog.ioc`（SYS Debug 配置）
- `firmware/soundDog/Src/gpio.c`（PA13/PA14 被误设为 Analog）
- `firmware/flash_isp.bat`（新增 ISP 救砖脚本）

## 验证方式
1. 重新生成代码后，检查 gpio.c 中 PA 的 `GPIO_MODE_ANALOG` 列表不含 `GPIO_PIN_13|GPIO_PIN_14`
2. 连续两次烧录都成功（第二次能连上 = 没锁死）

## 教训
> **CubeMX 新建工程后，第一件事就是配 SYS → Debug → Serial Wire。** 不配的话，第一次烧录"看起来正常"（因为芯片空白），烧进去的固件会在运行时把 SWD 脚关掉，导致芯片"假砖"——只能靠 BOOT0 + ISP 串口救回来。这是 STM32 新手最容易踩的坑，也是"第一次能烧、之后不能烧"的经典根因。
>
> 排查 SWD 连不上时的思维顺序：电压正常 + ST-Link 能识别 + 其他项目能烧 → 不是接线问题，是**固件把 SWD 禁用了** → 查 gpio.c 的 PA13/PA14 是否被配成非 SWD 功能。

## 实测救砖记录（2026-08-16）

**失败路径**（都试过，无效）：
1. OpenOCD 普通 SWD → `unable to connect to the target`
2. STM32_Programmer_CLI `mode=UR`（Under Reset）→ `Unable to get core ID`（因为 ST-Link 只接了 3 根线，NRST 没接，UR 模式不生效）
3. ISP 串口第一次 → `Timeout waiting for acknowledgement`（按 BOOT0 上电，时序不对，芯片没进 bootloader）
4. ISP 串口第二次 → `COM_CMD_TIMEOUT`（还是没进 bootloader）

**最终成功路径**：
1. 核心板 3 个按键确认：`RST`、`A15`（PA15 用户键，无关）、`BOOT`（BOOT0 键）
2. 进入 ISP 用**方法 2**（不用断电）：上电后先按 RST 不松 → 再按 BOOT 不松 → 松 RST（BOOT 还按着）→ 松 BOOT
3. 关键时序：**松 RST 的瞬间 BOOT 必须按着**，芯片复位释放时采样 BOOT0=高
4. `STM32_Programmer_CLI -c port=COM4 br=115200 -w xxx.hex -v -s` 串口烧录成功

**成功标志**：`Activating device: OK` + `Chip ID: 0x413` + `Download verified successfully` + 绿灯亮。

**关键教训补充**：
- 进 ISP 一定要用「RST + BOOT 组合」而非「按 BOOT 上电」，后者时序难把控
- 核心板 BOOT0 是按键不是跳线帽，按键名是 `BOOT`（不是 BOOT0，别和 A15 用户键搞混）
- 串口烧录要交叉接：模块 TXD → PA10(RXD)，RXD → PA9(TXD)
- `-ob` 参数 STM32_Programmer_CLI 不支持，会报错但烧录已完成，用 `-v -s` 即可
