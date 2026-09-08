/**
 * @file    modbus_app.c
 * @brief   FreeModbus 从站寄存器回调（保持/输入/线圈/离散，仅保持寄存器实映射）
 *
 * 设计要点：
 *   - 保持寄存器：运行期参数（debounce）的可读写映射，复用 param_apply()
 *     （freertos.c 定义，param.h 声明，单一校验源=param.h 范围宏 —— 防双源漂移）。
 *   - 写路径两遍：先整体范围校验（不落盘）→ 全合法才逐一 param_apply；
 *     任一越界返回 MB_ENOREG（FreeModbus 映射异常码 0x02），参数全保留旧值。
 *   - 读路径：实时读 debounce_get_param() 快照回填（反映任何时刻真实运行值）。
 *   - 输入/线圈/离散：未实例化 → 恒 MB_ENOREG（主机访问得地址异常，非静默 0）。
 *
 * 成熟方案核对（R27/R32）：
 *   - quanghona/Freemodbus-CMSIS-RTOS2-STM32 的 user_mb_app.c 回调范本；
 *   - 错误映射 mb.c:prveMBError2Exception 语义（见实现注释）。
 */
#include "mb.h"             /* eMBRegHoldingCB / eMBRegisterMode / eMBErrorCode */
#include "modbus_app.h"
#include "debounce.h"       /* debounce_get_param / debounce_set_params / DB_WIN_LEN */
#include "param.h"          /* PARAM_ID_* / PARAM_* 范围宏（单一校验源） */

/* ------------------------------------------------------------------ */
/* 静态：保持寄存器快照缓冲（仅读路径转储用）                          */
/* ------------------------------------------------------------------ */
static USHORT usRegHolding[MODBUS_REG_HOLDING_NUM];

/* ------------------------------------------------------------------ */
/* 寄存器偏移 → 参数 ID（1 基址 usAddress → 偏移 = usAddress-1）        */
/* ------------------------------------------------------------------ */
static UCHAR reg_off_to_pid(USHORT off)
{
  switch (off)
  {
    case 0u: return PARAM_ID_TH;
    case 1u: return PARAM_ID_ALARM_CNT;
    case 2u: return PARAM_ID_RELEASE_CNT;
    case 3u: return PARAM_ID_WIN_LEN;
    case 4u: return PARAM_ID_WIN_ANOM;
    default: return 0xFFu;   /* 不可达（越界已在外层拦截） */
  }
}

/* ------------------------------------------------------------------ */
/* 范围校验（与 param_apply 同源范围宏；只判不落盘）                    */
/* ------------------------------------------------------------------ */
static bool reg_is_valid(UCHAR pid, USHORT val)
{
  switch (pid)
  {
    case PARAM_ID_TH:         /* ×100 定点 1.0~20.0 */
      return val >= PARAM_TH_MIN_x100 && val <= PARAM_TH_MAX_x100;
    case PARAM_ID_ALARM_CNT:
    case PARAM_ID_RELEASE_CNT:
    case PARAM_ID_WIN_ANOM:
      return val >= PARAM_FRAME_MIN && val <= PARAM_FRAME_MAX;
    case PARAM_ID_WIN_LEN:    /* 滑窗固定=100（位图容量耦合，拒改） */
      return val == PARAM_WIN_LEN_FIX;
    default:
      return false;
  }
}

/* ------------------------------------------------------------------ */
/* FreeModbus 保持寄存器回调（唯一实映射）                              */
/* ------------------------------------------------------------------ */
eMBErrorCode eMBRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress,
                             USHORT usNRegs, eMBRegisterMode eMode)
{
  /* usAddress 为 1 基址；段 [usAddress, usAddress+usNRegs-1] 必须落在 1..N 内 */
  if (usAddress < 1u || (usAddress + usNRegs - 1u) > MODBUS_REG_HOLDING_NUM)
  {
    return MB_ENOREG;   /* 地址越界 → 异常 0x02 */
  }

  if (eMode == MB_REG_READ)
  {
    /* 实时快照（shadow set_params 后立即读得到新值） */
    debounce_param_t p = debounce_get_param();
    usRegHolding[0] = (USHORT)(p.threshold * 100.0f + 0.5f);  /* ×100 定点，就近取整 */
    usRegHolding[1] = p.consec_anom_alarm;
    usRegHolding[2] = p.consec_norm_release;
    usRegHolding[3] = (USHORT)DB_WIN_LEN;   /* 固定=100 */
    usRegHolding[4] = p.win_anom_cnt;

    for (USHORT i = 0u; i < usNRegs; i++)
    {
      USHORT v = usRegHolding[usAddress - 1u + i];
      pucRegBuffer[i*2u]     = (UCHAR)(v >> 8);   /* 大端（Modbus 规定） */
      pucRegBuffer[i*2u+1u]  = (UCHAR)(v & 0xFF);
    }
  }
  else if (eMode == MB_REG_WRITE)
  {
    /* 第一遍：整体范围校验（不落盘） */
    for (USHORT i = 0u; i < usNRegs; i++)
    {
      USHORT val = (USHORT)((pucRegBuffer[i*2u] << 8) | pucRegBuffer[i*2u+1u]);
      UCHAR  pid = reg_off_to_pid((USHORT)(usAddress - 1u + i));
      if ((pid == 0xFFu) || !reg_is_valid(pid, val))
      {
        return MB_ENOREG;   /* 任一越界 → 整体拒绝，全保留旧值 */
      }
    }
    /* 第二遍：全部合法才逐一生效（param_apply 内部再校验，双保险一致） */
    for (USHORT i = 0u; i < usNRegs; i++)
    {
      USHORT val = (USHORT)((pucRegBuffer[i*2u] << 8) | pucRegBuffer[i*2u+1u]);
      UCHAR  pid = reg_off_to_pid((USHORT)(usAddress - 1u + i));
      (void)param_apply(pid, val);   /* 已过校验，正常恒 true */
    }
  }
  else
  {
    return MB_ENOREG;   /* 未知模式兜底 */
  }

  return MB_ENOERR;
}

/* ------------------------------------------------------------------ */
/* 输入寄存器 / 线圈 / 离散：未实例化 → 恒地址异常（主机非静默）       */
/* ------------------------------------------------------------------ */
eMBErrorCode eMBRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress,
                           USHORT usNRegs)
{
  (void)pucRegBuffer; (void)usAddress; (void)usNRegs;
  return MB_ENOREG;
}

eMBErrorCode eMBRegCoilsCB(UCHAR *pucRegBuffer, USHORT usAddress,
                           USHORT usNCoils, eMBRegisterMode eMode)
{
  (void)pucRegBuffer; (void)usAddress; (void)usNCoils; (void)eMode;
  return MB_ENOREG;
}

eMBErrorCode eMBRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress,
                              USHORT usNDiscrete)
{
  (void)pucRegBuffer; (void)usAddress; (void)usNDiscrete;
  return MB_ENOREG;
}