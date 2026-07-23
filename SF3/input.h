/** @file
  键盘输入 → NES 手柄映射
**/

#ifndef _SFC3_INPUT_H_
#define _SFC3_INPUT_H_

#include "nes_state.h"

/* 手柄 RAM 地址 (P1) */
#define ADDR_P1_JOYPAD      0x00F4
#define ADDR_P1_JOYPAD_PREV 0x00F6

VOID InputInit(VOID);
VOID InputPoll(NES_STATE *State);

#endif
