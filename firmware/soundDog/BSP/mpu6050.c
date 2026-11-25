/**
 * @file    mpu6050.c
 * @brief   MPU6050 振动通道驱动（I2C2 独立总线，PF1=SCL / PF0=SDA）
 *
 * 为什么自建 I2C2 初始化（照 BSP/sht30.c 的 I2C3 样板）：
 *   Src/i2c.c 的 HAL_I2C_MspInit 只处理 Instance == I2C1，
 *   对 I2C2 不做任何事 ⇒ 本驱动自行完成 GPIOF/I2C2 时钟与引脚复用。
 *
 * 硬件前提（已核）：
 *   PF1 = I2C2_SCL (AF4)   PF0 = I2C2_SDA (AF4)
 *   两脚在 gpio.c 中原始配置为 ANALOG（未占用）；FMC 业务代码未使用。
 *   GPIO_AF4_I2C2 见 Drivers/.../stm32f4xx_hal_gpio_ex.h:84
 *
 * 数据处理口径（FEAT-A7-05 §3.2）：
 *   1) 上电静止 2 s 取三轴均值当重力零点
 *   2) 每帧去重力后算合成加速度 a = sqrt(ax²+ay²+az²)
 *   3) 1 s 窗口内统计 RMS 与峰值，换算 mg（±8 g ⇒ 4096 LSB/g，mg = raw×125/512）
 *   4) 写 g_vib_rms / g_vib_peak（0~5000 钳位）
 *
 * ⚠️ 采样节拍用 vTaskDelayUntil 固定 2 ms。若任务被更高优先级抢占，
 *    实际样本数 N 会小于 500 —— 因此每窗口打印实际 N，便于发现退化。
 *    RMS 是统计量，N 略低不影响趋势；但 N 显著偏低（<300）说明被饿死。
 */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include "mpu6050.h"

/* ---------------- 寄存器 ---------------- */
#define REG_SMPLRT_DIV      0x19u
#define REG_CONFIG          0x1Au
#define REG_ACCEL_CONFIG    0x1Cu
#define REG_ACCEL_XOUT_H    0x3Bu
#define REG_PWR_MGMT_1      0x6Bu
#define REG_WHO_AM_I        0x75u

#define MPU_ADDR8           (MPU6050_ADDR << 1)
#define MPU_TMO_MS          20u

/* ---------------- 采样与窗口 ----------------
 * ⚠️ 采样率推导（RM-MPU-6000A Rev4.0 §4.2/§4.3，2026-11-25 核对）：
 *    Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV)
 *    Gyro Output Rate = 8 kHz when DLPF_CFG = 0 or 7；1 kHz when DLPF_CFG = 1~6
 *    本驱动取 DLPF_CFG = 0（带宽 260 Hz，芯片上限）
 *      ⇒ Fs = 8 kHz ⇒ SMPLRT_DIV = 15 得 8000/(1+15) = 500 Hz  ✓
 *    ⛔ 若误写 SMPLRT_DIV=1，会得到 4000 Hz；而手册明示
 *       "accelerometer output rate is 1 kHz … sample may be output more than once"
 *       ⇒ 每 4 个读数有 3 个重复，严重污染 RMS/峰值统计。 */
#define REG_SMPLRT_DIV_VAL  0x0Fu            /* 8 kHz / (1+15) = 500 Hz */
#define REG_CONFIG_VAL      0x00u            /* DLPF_CFG=0 → Accel 260 Hz（芯片上限）*/
#define REG_ACCEL_CFG_VAL   0x10u            /* AFS_SEL=2 → ±8 g，4096 LSB/g */
#define REG_PWR_MGMT_VAL    0x01u            /* 清 SLEEP + CLKSEL=1（PLL 由 X 轴陀螺参考）
                                              * 手册原文：highly recommended，用陀螺作参考可提高稳定性；
                                              * ⛔ 写 0x00 会退回内部 8 MHz RC，频率随温漂 */

#define SAMPLE_HZ           500u
#define SAMPLE_MS           2u                       /* 1000/500 */
#define WIN_SAMPLES         SAMPLE_HZ                /* 1 s 窗口 */
#define CAL_SAMPLES         (SAMPLE_HZ * 2u)         /* 2 s 零标定 */
#define LSB_PER_G           4096.0f                  /* ±8 g */
#define MG_PER_LSB          (1000.0f / LSB_PER_G)
#define VIB_MAX_MG          5000u
#define CLIP_LSB            32000  /* ±8g 满量程 32768，留 2.3% 余量判削顶 */

/* 上电时序（RM-MPU-6000A / 产品规格书）：复位后 100 ms、唤醒后 ≥30 ms 再配置 */
#define RESET_WAIT_MS       100u
#define WAKE_WAIT_MS        50u

/* ---------------- 状态 ---------------- */
static I2C_HandleTypeDef s_hi2c2;
static uint8_t  s_present = 0u;
static float    s_zero[3] = {0.0f, 0.0f, 0.0f};

volatile uint16_t g_vib_rms  = 0u;
volatile uint16_t g_vib_peak = 0u;

/* ================================================================
 * I2C2 引脚与时钟（CubeMX 的 HAL_I2C_MspInit 不管 I2C2）
 * ================================================================ */
static void mpu_gpio_i2c2_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    /* PF1 —— I2C2_SCL */
    g.Pin       = GPIO_PIN_1;
    g.Mode      = GPIO_MODE_AF_OD;          /* I2C 必须开漏 */
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOF, &g);

    /* PF0 —— I2C2_SDA */
    g.Pin       = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOF, &g);
}

/* ---------------- 基本读写 ---------------- */
static int mpu_write_reg(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(&s_hi2c2, MPU_ADDR8, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1u, MPU_TMO_MS) == HAL_OK)
           ? 0 : -1;
}

static int mpu_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&s_hi2c2, MPU_ADDR8, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, MPU_TMO_MS) == HAL_OK)
           ? 0 : -1;
}

/* ================================================================
 * 初始化
 * ================================================================ */
int mpu6050_init(void)
{
    uint8_t who = 0u;
    HAL_StatusTypeDef st;

    s_present = 0u;

    mpu_gpio_i2c2_init();

    s_hi2c2.Instance             = I2C2;
    s_hi2c2.Init.ClockSpeed      = 400000u;              /* MPU6050 支持 400 kHz */
    s_hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_hi2c2.Init.OwnAddress1     = 0u;
    s_hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c2.Init.OwnAddress2     = 0u;
    s_hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    st = HAL_I2C_Init(&s_hi2c2);
    if (st != HAL_OK) {
        printf("[MPU] I2C2 init fail st=%d\r\n", (int)st);
        return -1;
    }

    /* ① 探测：地址是否有 ACK */
    if (HAL_I2C_IsDeviceReady(&s_hi2c2, MPU_ADDR8, 3u, MPU_TMO_MS) != HAL_OK) {
        printf("[MPU] no ACK at 0x%02X (接线/供电/AD0?)\r\n", (unsigned)MPU6050_ADDR);
        return -1;
    }

    /* ② WHO_AM_I 必须回 0x68（低 7 位），否则是别的片子或地址不对 */
    if (mpu_read_regs(REG_WHO_AM_I, &who, 1u) != 0) {
        printf("[MPU] WHO_AM_I read fail\r\n");
        return -1;
    }
    if ((who & 0x7Eu) != (MPU6050_ADDR & 0x7Eu)) {
        printf("[MPU] WHO_AM_I=0x%02X, expect 0x68\r\n", (unsigned)who);
        return -1;
    }

    /* ③ 配置序列（顺序与等待时间均有手册依据）
     *   1) DEVICE_RESET=1 使寄存器回到已知默认态，再等 100 ms
     *   2) 清 SLEEP + 选 PLL(X gyro) 作时钟源，等 50 ms 让陀螺稳定
     *   3) 依次写 ODR 分频 / DLPF / 量程 */
    if (mpu_write_reg(REG_PWR_MGMT_1, 0x80u) != 0) { printf("[MPU] reset fail\r\n"); return -1; }
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_MS));

    if (mpu_write_reg(REG_PWR_MGMT_1, REG_PWR_MGMT_VAL) != 0) {
        printf("[MPU] wake fail\r\n"); return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(WAKE_WAIT_MS));

    if (mpu_write_reg(REG_SMPLRT_DIV,   REG_SMPLRT_DIV_VAL) != 0) { printf("[MPU] ODR fail\r\n");   return -1; }
    if (mpu_write_reg(REG_CONFIG,       REG_CONFIG_VAL)     != 0) { printf("[MPU] DLPF fail\r\n");  return -1; }
    if (mpu_write_reg(REG_ACCEL_CONFIG, REG_ACCEL_CFG_VAL)  != 0) { printf("[MPU] range fail\r\n"); return -1; }

    s_present = 1u;
    printf("[MPU] ok: WHO_AM_I=0x%02X  SMPLRT_DIV=%u (Fs=8k/%u=500Hz) DLPF=%u(260Hz) AFS=+-8g CLKSEL=PLL-Xgyro\r\n",
           (unsigned)who, (unsigned)REG_SMPLRT_DIV_VAL,
           (unsigned)(REG_SMPLRT_DIV_VAL + 1u), (unsigned)REG_CONFIG_VAL);
    return 0;
}

int mpu6050_present(void)
{
    return (s_present != 0u) ? 1 : 0;
}

/* ================================================================
 * 零标定：静止 2 s 取三轴均值（去重力）
 * ================================================================ */
static void mpu_calibrate(void)
{
    uint8_t  buf[6];
    int32_t  sum[3] = {0, 0, 0};
    uint16_t n = 0u;
    TickType_t last = xTaskGetTickCount();

    for (uint16_t i = 0u; i < CAL_SAMPLES; i++) {
        if (mpu_read_regs(REG_ACCEL_XOUT_H, buf, 6u) == 0) {
            sum[0] += (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
            sum[1] += (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
            sum[2] += (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
            n++;
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
    }

    if (n == 0u) {
        s_zero[0] = s_zero[1] = s_zero[2] = 0.0f;
        printf("[MPU] calibrate: no sample, zero=0\r\n");
        return;
    }
    for (uint8_t k = 0u; k < 3u; k++) {
        s_zero[k] = (float)sum[k] / (float)n;
    }
    printf("[MPU] calibrate done n=%u zero=(%.1f,%.1f,%.1f) LSB\r\n",
           (unsigned)n, s_zero[0], s_zero[1], s_zero[2]);
}

/* ================================================================
 * 任务：500 Hz 采样 → 1 s 窗口结算
 * ================================================================ */
void mpu6050_task(void *argument)
{
    (void)argument;

    uint8_t  buf[6];
    float    acc_sq = 0.0f;     /* Σ(ax²+ay²+az²)，单位 LSB² */
    float    peak_lsb = 0.0f;
    uint16_t n = 0u;
    uint16_t clip = 0u;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        /* 器件不在：每 10 s 重试一次，支持后接线 */
        if (s_present == 0u) {
            if (mpu6050_init() != 0) {
                vTaskDelay(pdMS_TO_TICKS(10000u));
                continue;
            }
            mpu_calibrate();
            last = xTaskGetTickCount();
        }

        /* 一个窗口 = WIN_SAMPLES 次采样（名义 1 s） */
        acc_sq = 0.0f;
        peak_lsb = 0.0f;
        n = 0u;
        clip = 0u;

        for (uint16_t i = 0u; i < WIN_SAMPLES; i++) {
            if (mpu_read_regs(REG_ACCEL_XOUT_H, buf, 6u) == 0) {
                int16_t r0 = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
                int16_t r1 = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
                int16_t r2 = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);

                /* 削顶检测（±8 g ⇒ 满量程 32768 LSB）：接近满量程说明量程不够，
                 * RMS/峰值会被低估（方波化），必须显式告警而不是照常输出。 */
                if (r0 > CLIP_LSB || r0 < -CLIP_LSB ||
                    r1 > CLIP_LSB || r1 < -CLIP_LSB ||
                    r2 > CLIP_LSB || r2 < -CLIP_LSB) {
                    clip++;
                }

                float ax = (float)r0 - s_zero[0];
                float ay = (float)r1 - s_zero[1];
                float az = (float)r2 - s_zero[2];
                float a2 = ax * ax + ay * ay + az * az;   /* 合成加速度平方 */
                acc_sq += a2;
                if (a2 > peak_lsb * peak_lsb) {
                    peak_lsb = sqrtf(a2);
                }
                n++;
            }
            vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
        }

        if (n > 0u) {
            float rms_lsb = sqrtf(acc_sq / (float)n);
            uint32_t rms_mg  = (uint32_t)(rms_lsb  * MG_PER_LSB + 0.5f);
            uint32_t peak_mg = (uint32_t)(peak_lsb * MG_PER_LSB + 0.5f);

            if (rms_mg  > VIB_MAX_MG) { rms_mg  = VIB_MAX_MG; }
            if (peak_mg > VIB_MAX_MG) { peak_mg = VIB_MAX_MG; }

            g_vib_rms  = (uint16_t)rms_mg;
            g_vib_peak = (uint16_t)peak_mg;

            /* 每窗口一行：N 用于判断采样是否被饿死（<300 即退化） */
            printf("VIB,%lu,%lu,%u\r\n",
                   (unsigned long)rms_mg, (unsigned long)peak_mg, (unsigned)n);

            if (clip > 0u) {
                printf("[MPU] WARN clip=%u/%u (range +-8g too small, RMS underestimated)\r\n",
                       (unsigned)clip, (unsigned)n);
            }
        } else {
            /* 整窗读失败 ⇒ 器件掉线，重新探测 */
            printf("[MPU] window lost, re-probe\r\n");
            s_present = 0u;
        }
    }
}
