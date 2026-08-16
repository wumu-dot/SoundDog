# CubeMX 时钟树 PLLI2S_N/R 灰色不可编辑

- **Bug ID**：BUG-20260728-003
- **严重等级**：P2-阻塞
- **发现日期**：2026-07-28
- **修复日期**：2026-07-28

## 现象描述

想给 INMP441 配 I2S 时钟（PLLI2S_N=256, PLLI2S_R=5），切到 CubeMX 的 Clock Configuration 标签页，PLLI2S_N 和 PLLI2S_R 输入框是灰色（不可编辑），无法输入值。

## 复现步骤
1. 新建 CubeMX 项目，只配了系统时钟 168MHz
2. 切到 Clock Configuration → PLLI2S 参数灰色
3. 或者先配了 I2S3 引脚但忘了选 Clock Source

## 根因分析

CubeMX 的时钟树是**被动响应**的——它根据外设的需求来解锁时钟参数。如果没有任何外设声明"我要用 PLLI2S"，时钟树就不会开放 PLLI2S 的编辑权限。

## 修复方案

**正确的操作顺序**（不可颠倒）：

```
1. Pinout 页面 → 分配 I2S3 三根引脚 (PC10/PA4/PC12)
2. I2S3 Mode → 选 Half-Duplex Master（或 Full-Duplex Master）
3. I2S3 Clock Source → 选 I2S PLL Clock   ← 这一步是关键！
4. 切到 Clock Configuration → PLLI2S_N/R 解锁
5. 填入 PLLI2S_N=256, PLLI2S_R=5
```

**错误做法**：先切到 Clock Configuration 直接找 PLLI2S 参数 → 灰色不可改 → 以为 CubeMX 有 bug。

## 影响文件
- `firmware/soundDog/soundDog.ioc`

## 验证方式
1. 按正确顺序操作
2. Clock Configuration 中 PLLI2S_N/R 变为白色可编辑
3. I2S3 区域显示 SCK ≈ 1.024 MHz, Error 0.0%

## 教训
> **CubeMX 的时钟树必须先声明消费者（外设），再配置生产者（PLL）。** 告诉 CubeMX "I2S3 要用 PLLI2S"之后，它才会开放 PLLI2S 参数给你改。这是 CubeMX 的设计逻辑，不是 bug。
