/**
 * pin_config.h — SoundDog 引脚映射定义
 *
 * 所有硬件引脚统一在此定义，方便修改移植。
 * 对应 CubeMX 中的 GPIO/Pinout 配置。
 *
 * 用法：需要操作引脚时 #include "pin_config.h"，用宏名而非硬编码。
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

/* ================================================================
 * I2S3 — INMP441 数字麦克风 (Half-Duplex Master Receive)
 *
 * SCK = 1.024MHz, Fs = 16kHz, 32-bit 帧
 * STM32 出时钟 → INMP441 出数据
 * ================================================================ */
#define I2S3_INSTANCE       SPI3
#define I2S3_DMA            DMA1
#define I2S3_DMA_STREAM     DMA1_Stream2
#define I2S3_DMA_CHANNEL    DMA_CHANNEL_0

#define I2S3_WS_PIN         GPIO_PIN_4     /* Word Select / LRCK */
#define I2S3_WS_PORT        GPIOA
#define I2S3_CK_PIN         GPIO_PIN_3     /* Bit Clock / SCK    */
#define I2S3_CK_PORT        GPIOB
#define I2S3_SD_PIN         GPIO_PIN_5     /* Serial Data        */
#define I2S3_SD_PORT        GPIOB

/* 时钟链路: HSE(8M) → /PLLM(8) → ×PLLI2S_N(256) → /PLLI2S_R(5) → 51.2MHz → I2SDIV=25 → SCK 1.024MHz (SCK=I2SxCLK/(2*I2SDIV)=51.2M/50) → /64 → Fs 16kHz */
#define I2S3_SCK_FREQ       1024000UL
#define I2S3_SAMPLE_RATE    16000
#define I2S3_FRAME_BITS     32

/* ================================================================
 * USART1 — 调试串口 (printf 输出)
 * ================================================================ */
#define DEBUG_UART          USART1
#define DEBUG_UART_BAUD     115200

#define DEBUG_TX_PIN        GPIO_PIN_9
#define DEBUG_TX_PORT       GPIOA
#define DEBUG_RX_PIN        GPIO_PIN_10
#define DEBUG_RX_PORT       GPIOA

/* ================================================================
 * LED + 蜂鸣器 (GPIO 输出)
 * ================================================================ */
#define LED_ALARM_PIN       GPIO_PIN_0
#define LED_ALARM_PORT      GPIOB
#define LED_STATUS_PIN      GPIO_PIN_1
#define LED_STATUS_PORT     GPIOB
#define BUZZER_PIN          GPIO_PIN_6
#define BUZZER_PORT         GPIOE

/* ================================================================
 * I2C1 — OLED SSD1306 + SHT30 温湿度 (预留，待 CubeMX 配置)
 *
 * OLED (0x3C) 和 SHT30 (0x44) 可共用一条 I2C1 总线。
 * 如要复用 pointer-desk 已有面包板接线，SHT30 也可走 I2C2:
 *   I2C2_SCL = PF1, I2C2_SDA = PF0
 * ================================================================ */
#define OLED_I2C            I2C1
#define OLED_I2C_SCL_PIN    GPIO_PIN_6
#define OLED_I2C_SCL_PORT   GPIOB
#define OLED_I2C_SDA_PIN    GPIO_PIN_7
#define OLED_I2C_SDA_PORT   GPIOB
#define OLED_I2C_ADDR       0x3C
#define SHT30_I2C_ADDR      0x44

/* ================================================================
 * USART3 — RS485 MAX3485 (预留，待 CubeMX 配置)
 * ================================================================ */
#define RS485_UART          USART3
#define RS485_UART_BAUD     115200

#define RS485_TX_PIN        GPIO_PIN_8
#define RS485_TX_PORT       GPIOD
#define RS485_RX_PIN        GPIO_PIN_9
#define RS485_RX_PORT       GPIOD
#define RS485_DE_PIN        GPIO_PIN_11   /* DE/RE 方向控制 */
#define RS485_DE_PORT       GPIOD

/* ================================================================
 * TIM3 — LED PWM (预留，待 CubeMX 配置)
 * ================================================================ */
#define LED_PWM_TIM         TIM3
#define LED_ALARM_CHANNEL   TIM_CHANNEL_3  /* PB0 */
#define LED_STATUS_CHANNEL  TIM_CHANNEL_4  /* PB1 */
#define LED_PWM_FREQ        1000           /* 1kHz */

/* ================================================================
 * SPI2 — Flash (W25Q64, 复用 pointer-desk 已验证引脚)
 * ================================================================ */
#define FLASH_SPI           SPI2

#define FLASH_SCK_PIN       GPIO_PIN_10
#define FLASH_SCK_PORT      GPIOB
#define FLASH_MOSI_PIN      GPIO_PIN_3
#define FLASH_MOSI_PORT     GPIOC
#define FLASH_MISO_PIN      GPIO_PIN_2
#define FLASH_MISO_PORT     GPIOC
#define FLASH_CS_PIN        GPIO_PIN_12
#define FLASH_CS_PORT       GPIOB

/* W25Q64 参数 */
#define FLASH_SIZE_BYTES    (8 * 1024 * 1024)
#define FLASH_SECTOR_SIZE   4096
#define FLASH_PAGE_SIZE     256

/* ================================================================
 * 系统
 * ================================================================ */
/* 时钟源 */
#define HSE_FREQ            8000000UL
#define SYSCLK_FREQ         168000000UL
#define APB1_TIM_CLK        84000000UL    /* PCLK1×2, TIM7 用 */
#define APB2_TIM_CLK        84000000UL    /* PCLK2×2, TIM3 用 */

/* 调试接口 (不可占用) */
#define SWDIO_PIN           GPIO_PIN_13
#define SWDIO_PORT          GPIOA
#define SWCLK_PIN           GPIO_PIN_14
#define SWCLK_PORT          GPIOA

#endif /* PIN_CONFIG_H */
