/** @file
  背景渲染系统 - 翻译自 sub_E7E9_draw_screen + sub_FBCE_draw_background

  根据 ram_screen ($0027) 的值加载对应的 Nametable 和调色板数据到 PPU 状态中。
  原厂程序在 bank_FF.asm 的 sub_E7E9_draw_screen ($E7E9) 中根据画面 ID 选择
  数据表, 再调用 sub_FBCE_draw_background ($FBCE) 将 Nametable 写入 PPU,
  通过 sub_FBB2_write_palette_to_ppu ($FBB2) 写入调色板。C 版本将这三步
  合并为 BackgroundLoadScreen / BackgroundLoadPalette 两个函数。
**/

#ifndef _SFC3_BACKGROUND_H_
#define _SFC3_BACKGROUND_H_

#include "nes_state.h"

/**
  加载指定画面 ID 的 Nametable 数据到 PPU 状态。

  翻译自 sub_FBCE_draw_background ($FBCE): 将背景图块索引和属性表
  数据拷贝到 PPU Nametable 内存区域。

  @param State    NES 状态指针
  @param ScreenId 画面 ID (ram_screen, $0027)
**/
VOID BackgroundLoadScreen(NES_STATE *State, UINT8 ScreenId);

/**
  加载指定画面 ID 的调色板数据到 PPU 状态。

  翻译自 sub_FBB2_write_palette_to_ppu ($FBB2): 将 32 字色调色板
  (4 组背景 + 4 组精灵) 写入 PPU 调色板 RAM ($3F00-$3F1F)。

  @param State    NES 状态指针
  @param ScreenId 画面 ID (ram_screen, $0027)
**/
VOID BackgroundLoadPalette(NES_STATE *State, UINT8 ScreenId);

/**
  加载指定画面 ID 的 CHR bank 到 Mapper 91 寄存器。

  翻译自 sub_FBCE_draw_background 中的 CHR bank 切换逻辑:
  将 bank 值写入 $6000, bank+1 写入 $6001。

  @param State    NES 状态指针
  @param ScreenId 画面 ID (ram_screen, $0027)
**/
VOID BackgroundLoadChrBanks(NES_STATE *State, UINT8 ScreenId);

/* 逐扫描线背景 CHR 分段表 (16 槽, 下标=画面 ID; NULL=整屏用入口 bank) */
extern CONST BG_SEG * CONST gScreenBgSegments[16];

#endif /* _SFC3_BACKGROUND_H_ */
