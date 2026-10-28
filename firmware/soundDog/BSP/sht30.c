/**
 * @file    sht30.c
 * @brief   SHT30 温湿度驱动（I2C3，PA8/PC9）—— 从站侧实现
 *
 * 协议（Sensirion SHT3x-DIS Datasheet §4.13）：
 *   1. 发 2 字节命令 0x2C 0x06   （单次测量 / 高重复性 / 关闭 clock stretching）
 *   2. 等 20 ms
 *   3. 读 6 字节 [T_MSB, T_LSB, T_CRC, H_MSB, H_LSB, H_CRC]
 *   4. T[°C] = -45 + 175 * S_T/65535 ；RH[%] = 100 * S_RH/65535
 *
 * ⚠️ 本实现**跳过 CRC 校验**（与 ov-watch 原实现一致）。
 *    如需更严：CRC8 多项式 0x31、初值 0xFF，逐字节校验第 2/5 字节。
 *
 * ⚠️ 硬件前提（2026-10-20 经 STM32F407 数据手册 AF 表核实）：
 *    PA8  = I2C3_SCL (AF4)
 *    PC9  = I2C3_SDA (AF4)
 *    两脚在 LXBF407ZG-P1 上由排针丝印 A8 / C9 引出，且原始配置为 ANALOG（未占用）。
 */

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sht30.h"

/* ---------------- 常量 ---------------- */
#define SHT30_ADDR7       0x44u                      /* 7 位地址（ADDR 脚接 GND） */
#define SHT30_ADDR8       (SHT30_ADDR7 << 1)         /* HAL 用 8 位形式 */
#define SHT30_CMD_T_HI    0x2Cu                      /* 单次测量 高重复性 */
#define SHT30_CMD_T_LO    0x06u
#define SHT30_WAIT_MS     20u                        /* 测量等待 */
#define SHT30_RX_LEN      6u
#define SHT30_TMO_MS      100u

#define SHT30_T_MSB       0u
#define SHT30_T_LSB       1u
#define SHT30_H_MSB       3u
#define SHT30_H_LSB       4u

/* ---------------- 模块状态 ---------------- */
I2C_HandleTypeDef hi2c3;
static uint8_t    s_present = 0u;

/* 最新读数（供 Modbus 回调读取；未读到前为 0） */
volatile int16_t g_temp_x10 = 0;
volatile int16_t g_humi_x10 = 0;

#define SHT30_TASK_PERIOD_MS   2000u    /* 2 s 读一次（温湿度变化慢） */
#define SHT30_TASK_STACK       512u
#define SHT30_TASK_PRIO        2u

/* ================================================================
 * 内部：I2C3 的 GPIO + 时钟配置
 *
 * ⚠️ 刻意不放进 HAL_I2C_MspInit：
 *    Src/i2c.c 的 HAL_I2C_MspInit 只处理 `Instance == I2C1`，
 *    对 I2C3 不做任何事 ⇒ 本驱动自行完成引脚与时钟使能。
 * ================================================================ */
static void sht30_gpio_i2c3_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_I2C3_CLK_ENABLE();

    /* PA8 —— I2C3_SCL */
    g.Pin       = GPIO_PIN_8;
    g.Mode      = GPIO_MODE_AF_OD;          /* I2C 规定开漏 */
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOA, &g);

    /* PC9 —— I2C3_SDA */
    g.Pin       = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &g);
}

/* ================================================================
 * 公共 API
 * ================================================================ */

int sht30_init(void)
{
    HAL_StatusTypeDef st;

    s_present = 0u;

    sht30_gpio_i2c3_init();

    hi2c3.Instance             = I2C3;
    hi2c3.Init.ClockSpeed      = 100000u;                /* SHT30 支持 400k，100k 更稳 */
    hi2c3.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c3.Init.OwnAddress1     = 0u;
    hi2c3.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2     = 0u;
    hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;  /* 允许从机拉低 SCL */

    st = HAL_I2C_Init(&hi2c3);
    if (st != HAL_OK) {
        return -1;
    }

    /* 探测：发地址看有没有 ACK。3 次尝试、每次 100 ms 超时 */
    st = HAL_I2C_IsDeviceReady(&hi2c3, SHT30_ADDR8, 3u, SHT30_TMO_MS);
    s_present = (st == HAL_OK) ? 1u : 0u;

    return s_present ? 0 : -1;
}

bool sht30_present(void)
{
    return (s_present != 0u) ? true : false;
}

int sht30_read_x10(int16_t *temp_x10, int16_t *humi_x10)
{
    uint8_t cmd[2];
    uint8_t rx[SHT30_RX_LEN];
    HAL_StatusTypeDef st;
    uint16_t t_raw, h_raw;
    int32_t  t10, h10;

    if ((temp_x10 == NULL) || (humi_x10 == NULL) || (s_present == 0u)) {
        return -1;
    }

    /* 1) 发单次测量命令 */
    cmd[0] = SHT30_CMD_T_HI;
    cmd[1] = SHT30_CMD_T_LO;
    st = HAL_I2C_Master_Transmit(&hi2c3, SHT30_ADDR8, cmd, 2u, SHT30_TMO_MS);
    if (st != HAL_OK) {
        s_present = 0u;                    /* 掉线则标记，下次重探 */
        return -1;
    }

    /* 2) 等测量完成（vTaskDelay 会让出 CPU，不用 HAL_Delay 忙等） */
    vTaskDelay(pdMS_TO_TICKS(SHT30_WAIT_MS));

    /* 3) 读 6 字节 */
    st = HAL_I2C_Master_Receive(&hi2c3, SHT30_ADDR8, rx, SHT30_RX_LEN, SHT30_TMO_MS);
    if (st != HAL_OK) {
        s_present = 0u;
        return -1;
    }

    /* 4) 解析原始值（跳过 CRC） */
    t_raw = (uint16_t)(((uint16_t)rx[SHT30_T_MSB] << 8) | (uint16_t)rx[SHT30_T_LSB]);
    h_raw = (uint16_t)(((uint16_t)rx[SHT30_H_MSB] << 8) | (uint16_t)rx[SHT30_H_LSB]);

    /* 5) 换算成 ×10 定点（整数运算，避免浮点）
     *    T   = -45 + 175 * t_raw / 65535
     *    T×10 = -450 + (1750 * t_raw) / 65535
     *    RH×10 = (1000 * h_raw) / 65535
     *    1750*65535 ≈ 1.15e8，用 64 位中间量防溢出（int32 实际也够，这里更稳） */
    t10 = (int32_t)(-450 + (int32_t)((1750LL * (long long)t_raw) / 65535LL));
    h10 = (int32_t)((1000LL * (long long)h_raw) / 65535LL);

    if (t10 > 32767)  { t10 = 32767; }
    if (t10 < -32768) { t10 = -32768; }
    if (h10 > 32767)  { h10 = 32767; }
    if (h10 < 0)      { h10 = 0; }

    *temp_x10 = (int16_t)t10;
    *humi_x10 = (int16_t)h10;

    return 0;
}

/* ================================================================
 * 轮询任务 —— ⚠️ 必须独立运行，不可在 Modbus 回调内调用
 *
 * 失败策略：读失败时 sht30_read_x10() 会把 s_present 清 0，
 * 本任务下一轮先重探；全局值保留上一次有效读数（不置 0，
 * 避免上游看到温度突然变 0 而误判"设备过冷"）。
 * ================================================================ */
void sht30_task(void *argument)
{
    int16_t t = 0;
    int16_t h = 0;

    (void)argument;

    for (;;)
    {
        if (s_present == 0u)
        {
            (void)sht30_init();              /* 首次 / 掉线后重探 */
        }

        if ((s_present != 0u) && (sht30_read_x10(&t, &h) == 0))
        {
            g_temp_x10 = t;
            g_humi_x10 = h;
        }

        vTaskDelay(pdMS_TO_TICKS(SHT30_TASK_PERIOD_MS));
    }
}
