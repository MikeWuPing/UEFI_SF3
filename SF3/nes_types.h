/** @file
  NES 基础类型定义与常量
**/

#ifndef _NES_TYPES_H_
#define _NES_TYPES_H_

#include <Uefi.h>

/* NES 手柄按键位定义 (与原厂 RAM 中格式一致) */
#define NES_BTN_A       0x01
#define NES_BTN_B       0x02
#define NES_BTN_SELECT  0x04
#define NES_BTN_START   0x08
#define NES_BTN_UP      0x10
#define NES_BTN_DOWN    0x20
#define NES_BTN_LEFT    0x40
#define NES_BTN_RIGHT   0x80

/* NES 屏幕尺寸 */
#define NES_SCREEN_WIDTH   256
#define NES_SCREEN_HEIGHT  240

/* PPU 常量 */
#define PPU_NAMETABLE_SIZE  0x400
#define PPU_PALETTE_SIZE    32
#define PPU_OAM_SIZE        256
#define PPU_TILE_SIZE       16

/* Mapper 91 寄存器地址 */
#define MAPPER91_CHR_BANK_0  0x6000
#define MAPPER91_CHR_BANK_1  0x6001
#define MAPPER91_CHR_BANK_2  0x6002
#define MAPPER91_CHR_BANK_3  0x6003
#define MAPPER91_PRG_BANK_0  0x7000
#define MAPPER91_PRG_BANK_1  0x7001
#define MAPPER91_IRQ_CTRL    0x7006

/* 游戏角色 ID */
#define FIGHTER_CHUNLI   0x00
#define FIGHTER_RYU_KEN  0x01
#define FIGHTER_GUILE    0x02
#define FIGHTER_BLANKA   0x03
#define FIGHTER_DHALSIM  0x04
#define FIGHTER_BALROG   0x06
#define FIGHTER_SAGAT    0x07
#define FIGHTER_VEGA     0x08
#define FIGHTER_COUNT    8

/* 游戏模式 (ram_000E) */
#define GAME_MODE_INIT     0x00
#define GAME_MODE_1        0x01
#define GAME_MODE_FIGHT_SETUP 0x02
#define GAME_MODE_3        0x03
#define GAME_MODE_4        0x04
#define GAME_MODE_FIGHT_MAIN  0x05
#define GAME_MODE_FIGHT_END   0x06

#endif
