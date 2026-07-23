/** @file
  背景渲染实现 - 根据 ram_screen 加载 Nametable 和调色板

  翻译参考: Ref/asm/bank_FF.asm
  - sub_E7E9_draw_screen ($E7E9): 画面选择分发
  - sub_FBCE_draw_background ($FBCE): Nametable 数据写入 PPU
  - sub_FBB2_write_palette_to_ppu ($FBB2): 调色板写入 PPU

  原厂程序通过 PPU 地址/数据寄存器 ($2006/$2007) 逐字节写入 Nametable,
  C 版本直接以 CopyMem 批量拷贝到 PPU_REGS 结构体中, 由 PpuRenderFrame
  统一读取渲染, 语义等价但效率更高。
**/

#include "background.h"
#include "ppu.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

/* 数据表声明 (background_data.c 中定义) */
extern CONST UINT8  gScreenCount;
extern CONST UINT8  *gScreenNametables[];
extern CONST UINT8  *gScreenPalettes[];
extern CONST UINT16 gScreenNametableSizes[];
extern CONST UINT8  gScreenChrBanks[];

/**
  加载 Nametable - 翻译自 sub_FBCE_draw_background ($FBCE)

  原厂流程: 根据 ram_screen 索引数据表指针, 通过 $2006 设置 PPU 地址
  为 $2000 (Nametable 0 起始), 再通过 $2007 逐字节写入图块索引和属性表。
  C 版本将整块数据直接拷贝到 State->Ppu.Nametable[0], 渲染时由
  PpuRenderFrame 从该缓冲区读取, 最终像素输出完全一致。
**/
VOID
BackgroundLoadScreen (
  NES_STATE *State,
  UINT8     ScreenId
  )
{
  UINT16 Size;
  UINT16 CopySize;

  if (ScreenId >= gScreenCount) {
    DEBUG((DEBUG_WARN, "[SFC3] Invalid screen_id: %d\n", ScreenId));
    return;
  }

  Size = gScreenNametableSizes[ScreenId];
  if (gScreenNametables[ScreenId] != NULL && Size > 0) {
    /* 边界保护: 不超过 Nametable 容量 (0x400 = 1024 字节) */
    CopySize = (Size < PPU_NAMETABLE_SIZE) ? Size : PPU_NAMETABLE_SIZE;
    CopyMem(State->Ppu.Nametable[0], gScreenNametables[ScreenId], CopySize);
  }

  DEBUG((DEBUG_INFO, "[SFC3] Background loaded: screen=%d, size=%d\n", ScreenId, Size));
}

/**
  加载调色板 - 翻译自 sub_FBB2_write_palette_to_ppu ($FBB2)

  原厂流程: 通过 $2006 设置 PPU 地址为 $3F00, 再通过 $2007 连续写入
  32 字节的 NES 调色板索引值。C 版本直接拷贝到 State->Ppu.Palette,
  渲染时由 gNesPaletteBGR 转换表将 NES 色号映射为 32-bit BGR 输出。
**/
VOID
BackgroundLoadPalette (
  NES_STATE *State,
  UINT8     ScreenId
  )
{
  if (ScreenId >= gScreenCount) {
    return;
  }
  if (gScreenPalettes[ScreenId] != NULL) {
    CopyMem(State->Ppu.Palette, gScreenPalettes[ScreenId], PPU_PALETTE_SIZE);
  }
}

/**
  加载 CHR bank - 翻译自 sub_FBCE_draw_background 中的 bank 切换

  原厂流程: 数据表首字节为 CHR bank 号, 写入 Mapper 91 $6000,
  再 +1 写入 $6001, 选择连续的 2 个 2KB CHR bank 供背景渲染使用。
**/
VOID
BackgroundLoadChrBanks (
  NES_STATE *State,
  UINT8     ScreenId
  )
{
  UINT8 Bank;

  if (ScreenId >= gScreenCount) {
    return;
  }
  Bank = gScreenChrBanks[ScreenId];
  if (Bank != 0) {
    /* 翻译自 sub_FBCE: STA $6000 / ADC #$01 / STA $6001 */
    Mapper91Write(State, 0x6000, Bank);
    Mapper91Write(State, 0x6001, (UINT8)(Bank + 1));
  }
}
