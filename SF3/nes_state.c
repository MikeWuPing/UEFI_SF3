/** @file
  NES 状态初始化
**/

#include "nes_state.h"
#include <Library/BaseMemoryLib.h>

NES_STATE gState;

VOID
NesStateInit (
  NES_STATE *State
  )
{
  ZeroMem(State, sizeof(NES_STATE));

  State->Ppu.Ctrl = 0x00;
  State->Ppu.Mask = 0x00;
  State->Ppu.Status = 0x00;
  State->Ppu.VramInc = 1;

  State->ChrBanks[0] = 0;
  State->ChrBanks[1] = 1;
  State->ChrBanks[2] = 2;
  State->ChrBanks[3] = 3;
  State->BgSegs = NULL;     /* 不分裂; sub_E7E9 按画面设置分段表 */
  State->PrgBanks[0] = 0;
  State->PrgBanks[1] = 0;

  /* 隐藏全部精灵 (翻译自 sub_FB8C: 所有 OAM Y = 0xFF) */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  State->Quit = FALSE;
  State->FrameCount = 0;
}
