#include "port.h"
#include "main.h"
#include "cmsis_os2.h"
#include "pin_config.h"   /* RS485_DE_PORT / RS485_DE_PIN (PD11) 权威定义 */

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

/* ----------------------- Static variables ---------------------------------*/
static volatile uint8_t rx_buff[256];
static int rx_index;
static int put_index;
static osThreadId_t receiveDataHandle;
static osEventFlagsId_t xSerialEventHandle;
/* P2-收帧栈 1024B（审查缺陷修复）：原 attr=NULL → 128字=512B，栈余量偏紧。
 * 单位字节（cmsis_os2.c:475 stack = attr->stack_size/sizeof(StackType_t)）。
 * 仅填 stack_size、priority 保持默认 osPriorityNormal
 * （osPriorityNone 会回落 Normal，cmsis_os2.c:449-466），不清默认。 */
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
    /* 【2026-10-25 修复】从站应答过快导致主站丢帧
     * 现象（网关侧实证）：`drop data received outside of transaction` ×20
     *   + `ESP_ERR_INVALID_RESPONSE (0x108)` ×18，云端上传间隔劣化到 11~80 s。
     * 机理：主站发完请求 → 硬件 RTS 切接收需要时间；本从站在
     *   eMBRTUSend() 里立刻切 TX 并应答，应答首字节撞在主站的切换窗口里，
     *   被主站当成「事务外数据」丢弃或截断成无效帧。
     * 依据（三源）：
     *   ① stackoverflow.com/questions/69550430 —— "slave replied before bus driver
     *      was switched to receive mode"，解法即「从站应答前等待」；
     *   ② merobix.com/blog/how-to-fix-rs-485-turnaround-problems-with-converters
     *      —— "adding a small delay to a fast-answering device is often the
     *      cleanest fix"；
     *   ③ Modbus over Serial Line V1.02 —— 从站须在主站帧结束后经 3.5 字符
     *      静默才可应答（115200 下仅 0.3 ms，不含收发器切换恢复时间）。
     * 取值 4 ms：涵盖主站 RTS 切换延迟 + 收发器 t_ZH/t_ZL 恢复，且远小于
     *   主站 1000 ms 响应超时，不影响轮询节拍。 */
    if ((xRxEnable == FALSE) && (xTxEnable == TRUE))
    {
        /* ⛔【2026-10-26 23:3x 已作废并回滚】原以"从站应答过快"为假设加了 4 ms/10 ms
         * 忙等，实测成功率反而从 8% 掉到 0.5%（10 ms 时 3 分钟仅 1 次成功）。
         * ⇒ 假设被推翻：延迟越大越糟，说明瓶颈不在从站应答时机。
         * 真正的字节实证（DEBUG 抓包）：
         *   PORT_SEND ×12、正确应答 ×1、回显 `03 00 00 00 05` ×1
         * 回显的存在说明主站侧 RX 窗口内确实有自身 TX 残余，
         * 但**延长从站延迟不能消除它**（回显已在 TX 期间产生，与从站无关）。
         * ⇒ 保持原样：不加延迟。 */
        (void)0;
    }

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
     * 满则静默丢弃（溢出流本就非法，丢弃比写穿更安全）。
     * 属生产路径保护，与任何诊断代码无关。 */
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
        /* 本帧消费完毕，两根索引统一归零，供下一帧复用。 */
        put_index = 0;
        rx_index = 0;
    }
}
