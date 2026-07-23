/** @file
  NES PPU 模拟 - 寄存器写入、帧渲染、Mapper 91 bank 切换
**/

#ifndef _SFC3_PPU_H_
#define _SFC3_PPU_H_

#include "nes_state.h"

/* PPU 寄存器写入 (模拟 $2000-$2007) */
VOID PpuWriteCtrl(NES_STATE *State, UINT8 Val);      /* $2000 */
VOID PpuWriteMask(NES_STATE *State, UINT8 Val);      /* $2001 */
VOID PpuWriteScroll(NES_STATE *State, UINT8 Val);    /* $2005 */
VOID PpuWriteAddr(NES_STATE *State, UINT8 Val);      /* $2006 */
VOID PpuWriteData(NES_STATE *State, UINT8 Val);      /* $2007 */
UINT8 PpuReadData(NES_STATE *State);                 /* $2007 */
UINT8 PpuReadStatus(NES_STATE *State);               /* $2002 */
VOID PpuOamDma(NES_STATE *State, UINT8 Page);        /* $4014 */

/* Mapper 91 */
VOID Mapper91Write(NES_STATE *State, UINT16 Addr, UINT8 Val);
UINT8 ChrRead(NES_STATE *State, UINT16 PpuAddr);

/* 帧渲染 */
VOID PpuRenderFrame(NES_STATE *State, UINT8 *ChrRom);

/* NES 系统调色板 → BGR 转换表 (64 色, 供 gop_render 使用) */
extern CONST UINT32 gNesPaletteBGR[64];

#endif /* _SFC3_PPU_H_ */
