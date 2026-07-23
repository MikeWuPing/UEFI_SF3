/** @file
  AI 系统 - P2 自动控制
  翻译自原厂 AI 决策逻辑
**/

#ifndef _SFC3_AI_H_
#define _SFC3_AI_H_

#include "nes_state.h"

#define ADDR_P2_JOYPAD 0x00F5

VOID AiUpdate(NES_STATE *State);

#endif
