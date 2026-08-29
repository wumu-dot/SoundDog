/* FEAT-A2-03: SSD1306 OLED 驱动（0.96" 128×64, I2C1 @0x3C, 400kHz）
 * 模式：1KB 静态 framebuffer + 页式刷新
 * 参考实现（R27）：github.com/4ilo/ssd1306-stm32HAL —— 本文件低层（命令/数据
 * 传输方式与初始化序列）与其 lib/ssd1306.c 逐行对齐，不自行合并/优化帧格式。
 *
 * ⚠️ 历史教训（2026-08-29 两轮乱码）：曾自作聪明把命令+数据拼进一次
 * HAL_I2C_Master_Transmit，控制字节序列违反 SSD1306 手册 8.1.5.2（Co 位
 * 规则），芯片把显存数据当命令执行 → 屏幕乱码且 I2C 层零报错（静默失败）。
 * 4ilo 用 HAL_I2C_Mem_Write（0x00=命令寄存器 / 0x40=数据寄存器）单笔发送，
 * 由 HAL 构造合法帧——照抄此模式，禁止再手拼帧。
 *
 * 上下文：任务级调用（displayTask）。不碰 USART1/FFT。I2C 失败仅计数
 * 不重试不阻塞（AC-07）。
 */

#include "oled_drv.h"
#include "i2c.h"
#include "pin_config.h"
#include <string.h>

/* ---- 屏幕几何（4ilo 默认 128×64）---- */
#define OLED_WIDTH      128u
#define OLED_HEIGHT     64u
#define OLED_PAGES      8u    /* 64 行 / 8 行/页 */

/* ---- I2C 帧控制字节（= HAL_I2C_Mem_Write 的 MemAddress）---- */
#define SSD1306_CMD_REG  0x00u   /* 后续为命令 */
#define SSD1306_DATA_REG 0x40u   /* 后续为显存数据 */

/* 1KB framebuffer：[页][列]，每字节=8 行（LSB=顶行） */
static uint8_t fb[OLED_PAGES][OLED_WIDTH];

/* I2C 错误累计（AC-07） */
static uint32_t err_count = 0u;

/* ---- 底层：发单条命令（4ilo ssd1306_WriteCommand 同构）----
 * HAL_I2C_Mem_Write 在总线上发 [START][0x78][0x00][cmd][STOP]，
 * 控制字节 0x00 + 单命令 = 手册 8.1.5.2 合法序列。 */
static void oled_write_cmd(uint8_t cmd)
{
  if (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(OLED_I2C_ADDR << 1),
                        SSD1306_CMD_REG, 1u, &cmd, 1u, 10u) != HAL_OK) {
    err_count++;
  }
}

/* ---- 底层：刷新整屏（4ilo ssd1306_UpdateScreen 逐行同构）----
 * 每页 3 条单独的地址命令 + 一次独立的 128B 数据写（0x40 寄存器）。 */
static void oled_update_screen(void)
{
  for (uint8_t p = 0u; p < OLED_PAGES; p++) {
    oled_write_cmd(0xB0u | p);   /* 页地址 */
    oled_write_cmd(0x00u);       /* 列低 nibble = 0 */
    oled_write_cmd(0x10u);       /* 列高 nibble = 0 */
    if (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(OLED_I2C_ADDR << 1),
                          SSD1306_DATA_REG, 1u, fb[p], OLED_WIDTH,
                          100u) != HAL_OK) {
      err_count++;
    }
  }
}

/* ---- 初始化序列 ----
 * ⚠️ 电气参数（0xD5/0xD9/0xDB）取自用户 F1 工程《4-1 OLED 显示屏》
 * (C:\Projects\stem32project)——与当前这块屏实际验证过能点亮的值。
 * 2026-08-29 黑屏根因：照抄 4ilo 时带入 0xD9/0x22、0xDB/0x20、0xD5/0xF0，
 * 兼容片面板对预充电/VCOMH 敏感 → 参数偏低直接黑屏（v1 用 0xF1 曾点亮）。
 * 传输方式仍为 4ilo 的 HAL_I2C_Mem_Write 分笔模式（防拼帧错误）。 */
int32_t OLED_DRV_Init(void)
{
  HAL_Delay(100u);              /* 上电等待（江协/F1 工程同义延时） */

  oled_write_cmd(0xAEu);        /* display off */
  /* 0x20 寻址模式：4ilo 原样字节 0x10（其注释"10"是二进制 10₂=页模式，
   * 十六进制 0x10 按手册 Table 1-3 实为水平模式）。两种解释下本刷新循环
   * 均正确（水平模式数据自动跨页续写，每页重发地址命令兼容），上板已
   * 实证显示正常——评审 Minor#3：保持 4ilo 字节不改，注释说明实况 */
  oled_write_cmd(0x20u);
  oled_write_cmd(0x10u);
  oled_write_cmd(0xB0u);        /* page start = 0 */
  oled_write_cmd(0xC8u);        /* COM 扫描方向（上下正常） */
  oled_write_cmd(0x00u);        /* 列低 = 0 */
  oled_write_cmd(0x10u);        /* 列高 = 0 */
  oled_write_cmd(0x40u);        /* 起始行 = 0 */
  oled_write_cmd(0xA1u);        /* 段重映射（左右正常） */
  oled_write_cmd(0xA8u);        /* 复用率 */
  oled_write_cmd(OLED_HEIGHT - 1u); /*  = 63 */
  oled_write_cmd(0xD3u);        /* 显示偏移 */
  oled_write_cmd(0x00u);        /*   = 0 */
  oled_write_cmd(0xD5u);        /* 时钟分频/振荡频率 */
  oled_write_cmd(0x80u);        /*   ★F1 实测值（4ilo 为 0xF0） */
  oled_write_cmd(0x81u);        /* 对比度 */
  oled_write_cmd(0xCFu);        /*   ★F1 实测值 */
  oled_write_cmd(0xD9u);        /* 预充电周期 */
  oled_write_cmd(0xF1u);        /*   ★F1 实测值（4ilo 为 0x22，本屏黑屏元凶） */
  oled_write_cmd(0xDAu);        /* COM 引脚硬件配置 */
  oled_write_cmd(0x12u);        /*   128×64 标配（两工程同值） */
  oled_write_cmd(0xDBu);        /* VCOMH */
  oled_write_cmd(0x30u);        /*   ★F1 实测值（4ilo 为 0x20） */
  oled_write_cmd(0xA4u);        /* 输出跟随显存 */
  oled_write_cmd(0xA6u);        /* 正常显示 */
  oled_write_cmd(0x8Du);        /* ⚠️ charge pump 必开，否则不亮 */
  oled_write_cmd(0x14u);
  oled_write_cmd(0xAFu);        /* display on */

  /* ---- 诊断（2026-08-29 黑屏排查）：全屏点亮 2 秒 ----
   * 上电先全白 2 秒再进网格：全亮=数据链路+charge pump 全通；
   * 若全亮但后续黑 = 绘图/刷新层问题；若全白阶段就黑 = init/硬件层。 */
  memset(fb, 0xFFu, sizeof(fb));
  oled_update_screen();
  HAL_Delay(2000u);

  /* 自检画面（AC-09）：8px 网格 + 底行横线（4ilo Fill+UpdateScreen 同构） */
  memset(fb, 0x00u, sizeof(fb));
  for (uint8_t p = 0u; p < OLED_PAGES; p++) {
    for (uint16_t x = 0u; x < OLED_WIDTH; x++) {
      if ((x & 7u) == 0u)          { fb[p][x] |= 0xFFu; }  /* 每 8 列竖线 */
      if (p == (OLED_PAGES - 1u))  { fb[p][x] |= 0x01u; }  /* 底行横线 */
    }
  }
  oled_update_screen();

  return (int32_t)err_count;
}

/* ---- 整数 ilog2（x>0）：返回 log2(x) 向下取整 ----
 * 教训对照 BUG-20260829-001：柱高计算全程 uint32 正值域，无符号陷阱面 */
static uint32_t ilog2_u32(uint32_t x)
{
  uint32_t r = 0u;
  while (x > 1u) { x >>= 1u; r++; }
  return r;
}

/* ---- 32 频带柱状图（AC-01）----
 * 布局：128 列 / 32 柱 = 4px/柱（3px 柱体 + 1px 间隙）
 * 柱高：h = 63 × ilog2(1+band) / ilog2(1+REF)，REF=20000（×100 域满刻度） */
int32_t OLED_DrawSpectrum(const uint32_t *bands)
{
  const uint32_t REF      = 20000u;   /* 对数满刻度（§3.3，阶段 4 可按实测微调 */
  const uint32_t FULL_LOG = ilog2_u32(1u + REF);   /* ≈14 */
  const uint32_t BAR_W    = 3u;       /* 3px 柱体 + 1px 间隙 */

  memset(fb, 0x00u, sizeof(fb));

  for (uint32_t b = 0u; b < 32u; b++)
  {
    uint32_t h = 0u;
    if (bands != NULL) {
      h = (63u * ilog2_u32(1u + bands[b])) / FULL_LOG;  /* 0~63 */
    }
    /* 评审 Important#1：钳位防无符号下溢。REF 微调（§3.3 预留）或数据源
     * 换标度时 h 可能 >63，届时 64u-h 下溢成巨值 → 填充循环一次不执行，
     * 最响的柱子反而一根像素不画（静默失败）。 */
    if (h > 63u) { h = 63u; }
    uint32_t x0 = b * 4u;

    for (uint32_t x = 0u; x < BAR_W; x++) {
      uint32_t col = x0 + x;
      if (col >= OLED_WIDTH) { break; }
      /* 从底行向上填 h 行：行 (63-h) ~ 63 */
      for (uint32_t row = (64u - h); row < 64u; row++) {
        uint8_t  page = (uint8_t)(row / 8u);
        uint8_t  bit  = (uint8_t)(row % 8u);
        fb[page][col] |= (uint8_t)(1u << bit);
      }
    }
  }

  oled_update_screen();

  return (int32_t)err_count;
}

uint32_t OLED_ErrCount(void)
{
  return err_count;
}
