/** @file
  GOP 图形输出 - 将 PPU 帧缓冲缩放后 Blt 到屏幕
**/

#ifndef _SFC3_GOP_RENDER_H_
#define _SFC3_GOP_RENDER_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include "nes_state.h"

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT32  *BackBuffer;
  UINT32   ScreenW, ScreenH;
  UINT32   Scale;        /* 整数缩放倍数 */
  UINT32   OffsetX, OffsetY;  /* 居中偏移 */
  BOOLEAN  Initialized;
} GOP_RENDERER;

extern GOP_RENDERER gGopRenderer;

EFI_STATUS GopInit(VOID);
VOID GopPresent(NES_STATE *State);
VOID GopFree(VOID);

#endif
