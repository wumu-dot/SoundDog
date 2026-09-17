/* ==========================================================================
 * 临时探针总开关（R35/R36）— 链路确诊前保留，确诊后整块删除。
 *
 * MB_PROBE_ENABLE = 1（默认）：保留 RX 回环 / RX 打印 / TX 翻灯 三处探针。
 * MB_PROBE_ENABLE = 0        ：编译出纯净生产路径，探针代码不进固件。
 *
 * 覆盖方式：Makefile 的 C_DEFS 追加 -DMB_PROBE_ENABLE=0 即可，无需改本文件。
 * 拆除条件（满足任一，必须立刻按 R36 删除三处探针体 + 本开关块）：
 *   (a) RS485 物理链路确认打通（回环自测通过 + 主站读到合法应答）；
 *   (b) 准备提交/发布任何固件。
 * ========================================================================== */
#ifndef MB_PROBE_ENABLE
#define MB_PROBE_ENABLE             1
#endif

#include "port.h"
#include "main.h"
#include "cmsis_os2.h"
#include "pin_config.h"   /* RS485_DE_PORT / RS485_DE_PIN (PD11) 权威定义 */
#if MB_PROBE_ENABLE
#include <stdio.h>        /* RX探针[临时] printf：随探针一并移除 */
#include "rs485_drv.h"    /* RX回环探针[临时] RS485_Send：随探针一并移除 */
#endif

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

/* ----------------------- Static variables ---------------------------------*/
static volatile uint8_t rx_buff[256];
static int rx_index;
static int put_index;
static osThreadId_t receiveDataHandle;
static osEventFlagsId_t xSerialEventHandle;
/* P2-收帧栈 512→1024（审查缺陷）：收帧任务原 attr=NULL → 128字=512B，任务内跑 printf
 * (MB_PROBE_ENABLE=1 时)对 512B 栈偏紧。扩容到 1024B，单位字节（cmsis_os2.c:475
 * stack = attr->stack_size/sizeof(StackType_t)）。仅填 stack_size、priority 保持默认
 * osPriorityNormal（osPriorityNone 会回落 Normal，cmsis_os2.c:449-466），不清默认。 */
static const osThreadAttr_t receiveDataTask_attributes = {
    .name = "mbRxRecv",
    .stack_size = 1024,
    /* priority 不填 → osPriorityNone → 回落 osPriorityNormal（保守） */
};

extern UART_HandleTypeDef huart3;

/* ----------------------- Defines ------------------------------------------*/
#define EVENT_MBSLAVE_HANDLE_RECEIVED_DATA  0x00000001UL
#define huart                               huart3
/* RS485 半双工方向控制：DE=PD11，高=发送 / 低=接收（pin_config.h 权威，本工程电参数，
 * 与 BSP/rs485_drv.c 的 RS485_SetDirTx/Rx 同源，非照搬 quanghona 示例电气值）。 */
#define RS485_SLAVE_TX_MODE()               HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET)
#define RS485_SLAVE_RX_MODE()               HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET)

/* ----------------------- static functions ---------------------------------*/
static void handleReceivedDataTask(void *argument);

/* ----------------------- Start implementation -----------------------------*/
BOOL xMBPortSerialInit(UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits,
        eMBParity eParity)
{
    /* set serial configure parameter */
    huart.Init.BaudRate = ulBaudRate;
    huart.Init.WordLength = ucDataBits == 8 ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    huart.Init.StopBits = UART_STOPBITS_1;
    switch (eParity)
    {
        case MB_PAR_NONE:
            huart.Init.Parity = UART_PARITY_NONE;
            break;

        case MB_PAR_ODD:
            huart.Init.Parity = UART_PARITY_ODD;
            break;

        case MB_PAR_EVEN:
            huart.Init.Parity = UART_PARITY_EVEN;
            break;

        default:
            return FALSE;
    }
    huart.Init.Mode = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    /* open serial device */
    if (HAL_UART_Init(&huart) != HAL_OK)
    {
        Error_Handler();
        return FALSE;
    }
    /* software initialize */
    __HAL_UART_ENABLE_IT(&huart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_TC);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
    receiveDataHandle = osThreadNew(handleReceivedDataTask, NULL, &receiveDataTask_attributes);

    return TRUE;
}

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xRxEnable)
    {
        /* switch 485 to receive mode */
        RS485_SLAVE_RX_MODE();
    }
    else
    {
        /* switch 485 to transmit mode */
        RS485_SLAVE_TX_MODE();
    }
    if (xTxEnable)
    {
        /* start serial transmit */
        rx_index = 0;
        put_index = 0;
        pxMBFrameCBTransmitterEmpty();
    }
    else
    {
        /* stop serial transmit */
        HAL_UART_AbortTransmit_IT(&huart);
    }
}

void vMBPortClose(void)
{
	HAL_UART_AbortReceive_IT(&huart);
	HAL_UART_AbortTransmit_IT(&huart);
}

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    HAL_UART_Transmit_IT(&huart, (uint8_t*)&ucByte, 1);
    return TRUE;
}

/*
 * A Function to send all bytes in one call.
 */
BOOL xMBPortSerialPutBytes(volatile UCHAR *ucByte, USHORT usSize)
{
#if MB_PROBE_ENABLE
	/* TX探针[临时]（FEAT-A4-04 §3.7，R36）：进入此处 = FreeModbus 判定应答并经总线发送。
	 * 判据：绿灯随主站轮询闪一次=从站发出应答；不闪=未进发送态。验证后移除。
	 * 注意：GPIOB PIN_1 即 main.h:LED_STATUS（main.c 用它表示初始化成功），
	 *       本翻转会破坏该灯语义，链路确诊后必须随探针整块删除。 */
	HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
#endif
	HAL_UART_Transmit_IT(&huart, (uint8_t *)ucByte, usSize);
	return TRUE;
}

BOOL xMBPortSerialGetByte(CHAR * pucByte)
{
    *pucByte = rx_buff[rx_index++];

    return TRUE;
}

/*
 * Create an interrupt handler for the transmit buffer empty interrupt
 * (or an equivalent) for your target processor. This function should then
 * call pxMBFrameCBTransmitterEmpty( ) which tells the protocol stack that
 * a new character can be sent. The protocol stack will then call
 * xMBPortSerialPutByte( ) to send the character.
 */
void prvvUARTTxReadyISR(void)
{
    pxMBFrameCBTransmitterEmpty();
}

/*
 * Create an interrupt handler for the receive interrupt for your target
 * processor. This function should then call pxMBFrameCBByteReceived( ). The
 * protocol stack will then call xMBPortSerialGetByte( ) to retrieve the
 * character.
 *
 * quanghona: This function is called when IDLE is detected. thus will then
 * trigger the received message handler.
 */
void prvvUARTRxISR(void)
{
    osEventFlagsSet(xSerialEventHandle, EVENT_MBSLAVE_HANDLE_RECEIVED_DATA);
}

/*
 * Create an interrupt handler for the receive character for your target processor.
 * This function should store data to a buffer for received message thread to
 * handle later.
 */
void prvvUARTRxReceiveCharISR(CHAR data)
{
    /* P2-13（审查缺陷）：MODBUS ADU 上限恰为 256 字节，rx_buff 恰好无余量。
     * 原 `rx_buff[put_index++] = data` 无边界判断——任何超过 256 字节的连续
     * 字节流（噪声/误接线/主站异常长帧）都会越界写相邻 .bss。加范围判断，
     * 满则静默丢弃（溢出流本就非法，丢弃比写穿更安全）。非探针派生，关
     * MB_PROBE_ENABLE 后依然需要，属真正的生产路径缺陷。 */
    if (put_index < (int)sizeof(rx_buff)) {
        rx_buff[put_index++] = data;
    }
}

/*
 * A thread to handle the received message.
 * This thread is wait until a received message flag is set, then it will call
 * pxMBFrameCBByteReceived() until all characters in the receive buffer is handled.
 */
static void handleReceivedDataTask(void *argument)
{
    (void)argument;
    /* 事件句柄必须在等待前创建，否则 osEventFlagsWait(NULL,...) 硬错误
     * （quanghona 原版漏建，移入本线程内惰性创建，线程先于 eMBPoll 跑也不与他处抢占）。 */
    xSerialEventHandle = osEventFlagsNew(NULL);
    while (1)
    {
        osEventFlagsWait(xSerialEventHandle, EVENT_MBSLAVE_HANDLE_RECEIVED_DATA,
                         osFlagsWaitAny, osWaitForever);
        /* 修正 quanghona 原版收帧 bug：原 `while(put_index>0){ pxMBFrameCBByteReceived();
         * put_index--; }` 只减 put_index 而 xMBPortSerialGetByte 自增 rx_index →
         * 下一帧 put_index 归 0 重写但 rx_index 未归 0 → 错位读脏数据（BUG-20260908-xxx）。
         * 改为按 rx_index<put_index 消费，收完统一归零两根索引。 */
        while (rx_index < put_index)
        {
            (void)pxMBFrameCBByteReceived();   /* 内部经 xMBPortSerialGetByte 读 rx_buff[rx_index++] */
        }
#if MB_PROBE_ENABLE
        /* RX探针[临时]（FEAT-A4-04 §3.7，R36）：打印本帧收到的原始字节，验完整性/CRC。
         * 判读：8字节`01 03 00 00 00 01 C4 0B`=帧完整；7字节/尾部缺=末字节被掐。验证后移除。
         * 注意：本线程栈仅 512B（configMINIMAL_STACK_SIZE），printf 叠加栈溢出钩子未使能，
         *       溢出将无告警直接 HardFault。 */
        {
            printf("RX[%d]:", put_index);
            for (int i = 0; i < put_index; i++) printf(" %02X", (unsigned)rx_buff[i]);
            printf("\r\n");
        }
        /* RX回环探针[临时]（R35/R36）：把刚收的字节原样经 RS485 回发。
         * 原理：适配器发单字节(如 0x55)→FreeModbus 判坏帧丢弃(不产生应答)→总线上唯一回显
         * 即本回环。适配器能读回自己的字节 = STM32↔适配器 双向链路+接线全通。
         * 验证后随本探针一并移除。
         *
         * 【判读前提，务必遵守】本回环仅在「从站不产生应答」时才给出可判读结果。
         * 若主站发的是合法 Modbus 帧，总线上会先后出现「回环 echo」+「真应答」两段，
         * 主站一次读缓冲区将拼成一帧 → CRC 校验失败。故判链路请用单字节/坏 CRC 帧。
         *
         * 【对被测系统的扰动，判读时需计入】RS485_Send 内部是阻塞发送 + 忙等 TC，
         * 期间 USART3 RXNE 中断仍使能（优先级 5，高于本任务），可继续写 rx_buff 并
         * 递增 put_index；而下面第 206 行会把 put_index 强零 → 回环窗口内到达的新字节
         * 被静默丢弃。故「回环成功但偶发丢帧」不等于硬件问题。 */
        if (put_index > 0)
        {
            (void)RS485_Send((uint8_t *)rx_buff, (uint16_t)put_index);   /* 回发原始字节 */
        }
#endif
        put_index = 0;
        rx_index = 0;
    }
}
