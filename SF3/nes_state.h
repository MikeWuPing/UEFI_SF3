/** @file
  NES 状态结构体 - 映射 NES 硬件 RAM/PPU/Mapper 状态
**/

#ifndef _NES_STATE_H_
#define _NES_STATE_H_

#include "nes_types.h"

/* 逐扫描线背景 CHR 分段 (原厂 mapper-91 IRQ 程序 tbl_C016 的 C 表示) */
typedef struct {
  UINT8 StartRow;  /* 起始 tile 行 (0..29); ==30 为哨兵(表尾) */
  UINT8 Bank0;     /* 该段 $6000 窗口 (PPU $0000-07FF) */
  UINT8 Bank1;     /* 该段 $6001 窗口 (PPU $0800-0FFF) */
} BG_SEG;

typedef struct {
  UINT8   Ctrl;
  UINT8   Mask;
  UINT8   Status;
  UINT8   ScrollX;
  UINT8   ScrollY;
  UINT8   ScrollLatch;
  UINT16  VramAddr;
  UINT8   AddrLatch;
  UINT8   VramInc;
  UINT8   Nametable[2][PPU_NAMETABLE_SIZE];
  UINT8   Palette[PPU_PALETTE_SIZE];
  UINT8   Oam[PPU_OAM_SIZE];
  UINT8   OamAddr;
  UINT8   FrameBuffer[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT];
} PPU_REGS;

typedef struct {
  UINT8     Ram[0x800];
  PPU_REGS  Ppu;
  UINT8     ChrBanks[4];
  UINT8     PrgBanks[2];
  /* 逐扫描线背景 CHR 分段表 (摘自 tbl_C016); NULL = 整屏用入口 bank */
  CONST BG_SEG *BgSegs;
  BOOLEAN   Quit;
  UINT32    FrameCount;
} NES_STATE;

extern NES_STATE gState;

/* RAM 语义化访问宏 */
#define ram_mode                gState.Ram[0x000E]
#define ram_frame_counter       gState.Ram[0x0010]
#define ram_screen              gState.Ram[0x0027]
#define ram_screen_ctx          gState.Ram[0x000C]
#define ram_p1_fighter          gState.Ram[0x003B]
#define ram_p2_fighter          gState.Ram[0x003C]
#define ram_game_time           gState.Ram[0x003E]
#define ram_fight_state         gState.Ram[0x0072]
#define ram_p1_bar_hp           gState.Ram[0x0062]
#define ram_p2_bar_hp           gState.Ram[0x0063]
#define ram_demo_flag           gState.Ram[0x00C6]
#define ram_timer_before_demo   gState.Ram[0x00C7]
#define ram_credits             gState.Ram[0x00CB]
#define ram_fade_state          gState.Ram[0x00FC]
#define ram_fade_timer          gState.Ram[0x00FD]
#define ram_fade_offset         gState.Ram[0x00FE]
#define ram_damage              gState.Ram[0x040D]
#define ram_p1_hp               gState.Ram[0x0510]
#define ram_p2_hp               gState.Ram[0x0540]
#define ram_btn_pressed         gState.Ram[0x050D]
#define ram_select_confirmed    gState.Ram[0x051D]

/* PPU 控制/状态寄存器 (NMI 中写入 $2000/$2001) */
#define ram_ppuctrl_shadow      gState.Ram[0x0015]  /* PPUCTRL 影子 ($88=NMI+SP@$1000+BG@$0000) */
#define ram_ppumask_shadow      gState.Ram[0x0016]  /* PPUMASK 影子 ($1E=渲染使能) */
#define ram_spr_chr_bank0       gState.Ram[0x0025]  /* Sprite CHR bank → $6002 */
#define ram_spr_chr_bank1       gState.Ram[0x0026]  /* Sprite CHR bank → $6003 */

/* 调色板淡入目标缓冲区 (32 字节, 位于 RAM $06E0) */
#define RAM_FADE_TARGET         0x06E0

/* 角色位置与状态 (战斗中使用) */
#define ram_p1_x                gState.Ram[0x0400]
#define ram_p1_y                gState.Ram[0x0401]
#define ram_p2_x                gState.Ram[0x0402]
#define ram_p2_y                gState.Ram[0x0403]
#define ram_p1_facing           gState.Ram[0x0404]  /* 0=右, 1=左 */
#define ram_p2_facing           gState.Ram[0x0405]
#define ram_p1_state            gState.Ram[0x0406]
#define ram_p2_state            gState.Ram[0x0407]
#define ram_round_timer         gState.Ram[0x003E]  /* 回合倒计时 (秒) */
#define ram_round_frame_cnt     gState.Ram[0x003F]  /* 帧计数 (60=1秒) */

VOID NesStateInit(NES_STATE *State);

#endif
