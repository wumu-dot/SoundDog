/*
 * FreeModbus Libary: CMSIS-RTOS2 Port
 * Copyright (C) 2013 Armink <armink.ztl@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: portevent.c,v 1.60 2013/08/13 15:07:05 Armink $
 *            portevent.c,v 1.60 2022/07/17          quanghona <lyhonquang@gmail.com> CMSIS-RTOS2 port $
 */

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "cmsis_os2.h"

/* ----------------------- Variables ----------------------------------------*/
static osEventFlagsId_t     xEventId;

/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortEventInit( void )
{
    xEventId = osEventFlagsNew(NULL);
    return xEventId != NULL;
}

BOOL
xMBPortEventPost( eMBEventType eEvent )
{
    if (xEventId != NULL)
    {
        return (osEventFlagsSet(xEventId, eEvent) & 0x80000000) == 0;
    }
    else
    {
        return FALSE;
    }
}

BOOL
xMBPortEventGet( eMBEventType * eEvent )
{
    uint32_t receivedEvent;
    const uint32_t EVENTS = EV_READY | EV_FRAME_RECEIVED | EV_EXECUTE | EV_FRAME_SENT;

    /* P2-10（审查缺陷）：原实现 osFlagsWaitAny + 单 bit switch，多个 flag 同时
     * 置位时 receivedEvent 是位掩码 → 落 default 返回 FALSE；而 osEventFlagsWait
     * 默认行为会自动清除已返回的 flag → 事件静默丢失且无痕。eMBPoll 因此漏处理。
     *
     * 修复：用 osFlagsNoClear 不自动清位，按位优先级逐个分发，每分发一个 bit
     * 显式 Clear 掉它，未处理的 bit 保留在事件组中、下次调用继续分发 → 不丢事件。
     * 权重顺序：EV_FRAME_SENT 最低，EV_READY 最高（协议栈状态机要求优先处理）。 */
    receivedEvent = osEventFlagsWait(xEventId, EVENTS, osFlagsNoClear, osWaitForever);

    if ((receivedEvent & EV_READY) != 0u) {
        *eEvent = EV_READY;
        (void)osEventFlagsClear(xEventId, EV_READY);
        return TRUE;
    }
    if ((receivedEvent & EV_FRAME_RECEIVED) != 0u) {
        *eEvent = EV_FRAME_RECEIVED;
        (void)osEventFlagsClear(xEventId, EV_FRAME_RECEIVED);
        return TRUE;
    }
    if ((receivedEvent & EV_EXECUTE) != 0u) {
        *eEvent = EV_EXECUTE;
        (void)osEventFlagsClear(xEventId, EV_EXECUTE);
        return TRUE;
    }
    if ((receivedEvent & EV_FRAME_SENT) != 0u) {
        *eEvent = EV_FRAME_SENT;
        (void)osEventFlagsClear(xEventId, EV_FRAME_SENT);
        return TRUE;
    }
    return FALSE;
}
