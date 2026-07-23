/** @file
  游戏状态机实现

  翻译参考: Ref/asm/bank_FF.asm
  - Reset handler: vec_C704_RESET_handler ($C704)
  - NMI handler: vec_D69C_NMI_handler ($D69C)
  - sub_C759: 标题画面设置与等待循环
  - sub_C857: 角色选择设置与等待循环
  - sub_FA96: 调色板渐变 (每帧调用)
  - sub_C092: 标题画面 NMI 模式处理 (mode 4)
  - sub_C100: 角色选择 NMI 模式处理 (mode 3)
**/

#include "game_state.h"
#include "ppu.h"
#include "background.h"
#include "gop_render.h"
#include "fighter.h"
#include "fighter_sprite.h"
#include "fighter_sprite_data.h"
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>

/* 当前游戏阶段 (模拟原厂主线程流程状态) */
STATIC UINT8 gGamePhase = PHASE_TITLE;

/* 角色选择: 光标位置对应的角色 ID 表
   原厂 9 个角色排列为 3×3 网格 (跳过 ID 5)
   网格索引 0-8 → 角色 ID: 0,1,2,3,4,6,7,8,(0) */
STATIC CONST UINT8 gSelectGridToFighter[9] = {
  FIGHTER_CHUNLI, FIGHTER_RYU_KEN, FIGHTER_GUILE,
  FIGHTER_BLANKA, FIGHTER_DHALSIM, FIGHTER_BALROG,
  FIGHTER_SAGAT,  FIGHTER_VEGA,    FIGHTER_CHUNLI
};

/* 选角阶段的帧计数 (用于 VS 画面定时) */
STATIC UINT16 gPhaseTimer = 0;

/* 前向声明 */
STATIC VOID HudRenderFight(NES_STATE *State);

/* 3x5 像素数字字体 (每行 3 bit, 5 行) */
STATIC CONST UINT8 sDigitFont[10][5] = {
  { 0x7, 0x5, 0x5, 0x5, 0x7 },  /* 0 */
  { 0x2, 0x6, 0x2, 0x2, 0x7 },  /* 1 */
  { 0x7, 0x1, 0x7, 0x4, 0x7 },  /* 2 */
  { 0x7, 0x1, 0x7, 0x1, 0x7 },  /* 3 */
  { 0x5, 0x5, 0x7, 0x1, 0x1 },  /* 4 */
  { 0x7, 0x4, 0x7, 0x1, 0x7 },  /* 5 */
  { 0x7, 0x4, 0x7, 0x5, 0x7 },  /* 6 */
  { 0x7, 0x1, 0x1, 0x1, 0x1 },  /* 7 */
  { 0x7, 0x5, 0x7, 0x5, 0x7 },  /* 8 */
  { 0x7, 0x5, 0x7, 0x1, 0x7 },  /* 9 */
};

/**
  游戏初始化 - 翻译自 Reset handler $C704 + sub_C759

  清零 RAM, 初始化游戏状态, 进入标题画面。
  原厂流程: Reset → 清 RAM → 设 mapper → sub_C759 (标题画面)
**/
VOID
GameInit (
  NES_STATE *State
  )
{
  DEBUG((DEBUG_INFO, "[SFC3] GameInit: Reset handler $C704\n"));

  /* 清零 RAM (翻译自 $C719-$C72B) */
  ZeroMem(State->Ram, 0x800);

  /* 初始化游戏状态 (翻译自 $C743-$C756) */
  State->Ram[0x0078] = 0;    /* ram_0078: 高分 */
  State->Ram[0x0079] = 0;    /* ram_0079: 高分 */
  State->Ram[0x00C9] = 3;    /* ram_00C9: 初始信用配置 */

  gGamePhase = PHASE_TITLE;
  EnterTitleScreen(State);
}

/**
  进入标题画面 - 翻译自 sub_C759 ($C759)

  设置 mode 4, 绘制标题画面 (screen 0x0C), 初始化演示倒计时。
**/
VOID
EnterTitleScreen (
  NES_STATE *State
  )
{
  DEBUG((DEBUG_INFO, "[SFC3] EnterTitleScreen (sub_C759)\n"));

  gGamePhase = PHASE_TITLE;

  /* 翻译自 sub_C759 */
  State->Ram[0x000C] = 0x0A;   /* ram_screen_ctx = title context */
  State->Ram[0x000E] = 0x04;   /* ram_mode = 4 (标题画面 NMI handler) */
  State->Ram[0x00FC] = 0;      /* 调色板渐变 = 关闭 */

  /* 绘制标题画面 (翻译自 $C76E: LDX #$0C, JSR sub_E7E9) */
  State->Ram[0x0027] = 0x0C;   /* ram_screen = 标题画面 */
  sub_E7E9_draw_screen(State);

  /* 清空 OAM: 标题画面无精灵, 立即隐藏所有残留精灵
     (翻译自原厂切屏时 sub_FB8C_hide_all_sprites 的调用) */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  /* 标题难度光标 caret (tile 0x24) 的 sprite CHR 窗 = 2KB bank 0x84
     (翻译自 sub_EA21 标题 set9; 每帧 GameFrameUpdate 会把 ram_spr_chr_bank0 写回 $6002) */
  ram_spr_chr_bank0 = 0x84;
  ram_spr_chr_bank1 = 0x84;

  /* 标题精灵调色板 (翻译自 sub_C9BC / tbl_C9CE): sprite pal1 = 白, 使难度光标 caret 为白 */
  {
    STATIC CONST UINT8 tpal[15] = {
      0x30,0x30,0x30,0x0F, 0x30,0x30,0x30,0x0F, 0x06,0x17,0x28,0x0F, 0x11,0x15,0x30
    };
    UINT8 j;
    for (j = 0; j < 15; j++) {
      State->Ppu.Palette[0x11 + j] = tpal[j];
    }
  }

  /* 初始化演示倒计时 (翻译自 sub_C759 末尾) */
  State->Ram[0x00C6] = 0;      /* ram_demo_flag = OFF */
  State->Ram[0x00C7] = 10;     /* ram_timer_before_demo = 10 秒 */
  State->Ram[0x0010] = 60;     /* ram_frame_counter = 60 (1秒) */
  State->Ram[0x050D] = 0;      /* 清除 Start 按下标志 */

  gPhaseTimer = 0;

  DEBUG((DEBUG_INFO, "[SFC3] Title screen ready, demo timer=10\n"));
}

/**
  进入角色选择 - 翻译自 sub_C857 ($C857)

  设置 mode 3, 绘制角色选择画面 (screen 0x0B)。
**/
VOID
EnterCharacterSelect (
  NES_STATE *State
  )
{
  DEBUG((DEBUG_INFO, "[SFC3] EnterCharacterSelect (sub_C857)\n"));

  gGamePhase = PHASE_SELECT;

  /* 翻译自 sub_C857 */
  State->Ram[0x000C] = 0x09;   /* ram_screen_ctx = select context */
  State->Ram[0x000E] = 0x03;   /* ram_mode = 3 (角色选择 NMI handler) */

  /* 绘制角色选择画面 (翻译自: LDX #$0B, JSR sub_E7E9) */
  State->Ram[0x0027] = 0x0B;   /* ram_screen = 角色选择 */
  sub_E7E9_draw_screen(State);

  /* 清空 OAM: 角色选择画面无精灵 */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  /* 初始化选择状态 */
  State->Ram[0x00F4] = 0;      /* 光标位置 = 0 (春丽) */
  State->Ram[0x051D] = 0;      /* P1 未确认 */
  State->Ram[0x003B] = 0;      /* ram_p1_fighter 默认 */
  State->Ram[0x003C] = 1;      /* ram_p2_fighter 默认 */

  gPhaseTimer = 0;

  DEBUG((DEBUG_INFO, "[SFC3] Character select ready (screen=0x0B)\n"));
}

/**
  进入 VS 过场画面 - 翻译自 sub_F4AB ($F4AB)

  绘制 VS 画面 (screen 0x0A), 显示双方角色精灵对峙,
  加载角色调色板, 等待 2 秒后进入战斗。
**/
VOID
EnterVsScreen (
  NES_STATE *State
  )
{
  UINT8 P1Id, P2Id;
  UINT8 i;

  /* 翻译自 sub_F4AB 入口 */
  STATIC CONST UINT8 sSpritePaletteTable[9][4] = {
    { 0x0F, 0x12, 0x26, 0x36 },  /* 0: Chun-Li  */
    { 0x0F, 0x16, 0x26, 0x30 },  /* 1: Ryu/Ken  */
    { 0x0F, 0x18, 0x27, 0x36 },  /* 2: Guile    */
    { 0x0F, 0x06, 0x27, 0x37 },  /* 3: Blanka   */
    { 0x0F, 0x17, 0x27, 0x36 },  /* 4: Dhalsim  */
    { 0x0F, 0x16, 0x27, 0x37 },  /* 5: (unused) */
    { 0x0F, 0x1C, 0x2C, 0x3C },  /* 6: Balrog   */
    { 0x0F, 0x16, 0x26, 0x36 },  /* 7: Sagat    */
    { 0x0F, 0x15, 0x26, 0x36 },  /* 8: Vega     */
  };

  DEBUG((DEBUG_INFO, "[SFC3] EnterVsScreen (sub_F4AB)\n"));

  gGamePhase = PHASE_VS;
  State->Ram[0x000E] = 0x02;   /* mode 2 = 对战准备 */

  /* 绘制 VS 背景 (翻译自 sub_F4AB: LDX #$0A, JSR sub_E7E9) */
  State->Ram[0x0027] = 0x0A;   /* ram_screen = VS 画面 */
  sub_E7E9_draw_screen(State);

  /* Sprite CHR bank (翻译自 sub_EA21: tbl_C00B[fighter] + 动画帧偏移)
     P1 → $6002 (sprite $1000-$17FF), P2 → $6003 (sprite $1800-$1FFF) */
  P1Id = State->Ram[0x003B];
  P2Id = State->Ram[0x003C];
  /* VS 立绘 sprite CHR 窗 (翻译自 sub_EA21: 0xA6 + 肖像帧 chr_off; 同角色 P2 用 alt) */
  ram_spr_chr_bank0 = (P1Id < FIGHTER_ID_COUNT) ? (UINT8)(0xA6 + gVsChrOffP1[P1Id]) : 0;
  ram_spr_chr_bank1 = (P2Id < FIGHTER_ID_COUNT) ?
    (UINT8)(0xA6 + ((P1Id == P2Id) ? gVsChrOffAlt[P2Id] : gVsChrOffP1[P2Id])) : 0;

  /* 角色初始位置: P1 左侧, P2 右侧, 面对面站立
     翻译自 sub_CA69 → sub_CA4C 的位置设置 */
  State->Ram[0x0400] = 80;     /* P1 X */
  State->Ram[0x0401] = 200;    /* P1 Y (地面) */
  State->Ram[0x0402] = 168;    /* P2 X */
  State->Ram[0x0403] = 200;    /* P2 Y (地面) */
  State->Ram[0x0404] = 0;      /* P1 面朝右 */
  State->Ram[0x0405] = 1;      /* P2 面朝左 */
  State->Ram[0x0406] = FSTATE_IDLE;  /* P1 站立 */
  State->Ram[0x0407] = FSTATE_IDLE;  /* P2 站立 */

  /* 加载角色调色板 (翻译自 sub_F4AB: JSR sub_E783 × 2) */
  P1Id = State->Ram[0x003B];
  P2Id = State->Ram[0x003C];
  if (P1Id < 9) {
    for (i = 0; i < 4; i++) {
      State->Ppu.Palette[0x10 + i] = sSpritePaletteTable[P1Id][i];
    }
  }
  if (P2Id < 9) {
    for (i = 0; i < 4; i++) {
      State->Ppu.Palette[0x18 + i] = sSpritePaletteTable[P2Id][i];
    }
  }

  /* 启动调色板渐变 */
  PaletteFadeStart(State);

  gPhaseTimer = 120;  /* 2 秒 VS 画面 (60fps × 2) */

  DEBUG((DEBUG_INFO, "[SFC3] VS screen ready: P1=%d P2=%d, timer=120\n", P1Id, P2Id));
}

/**
  进入战斗场景 - 翻译自 sub_C076 入口 + 对战设置

  加载竞技场背景 (screen 0x00), 初始化角色位置、HP、计时器,
  启动调色板渐变。
**/
VOID
EnterFightScene (
  NES_STATE *State
  )
{
  DEBUG((DEBUG_INFO, "[SFC3] EnterFightScene: P1=%d P2=%d\n",
         State->Ram[0x003B], State->Ram[0x003C]));

  gGamePhase = PHASE_FIGHT;

  /* 设置战斗模式 (翻译自 sub_C076 入口) */
  State->Ram[0x000E] = 0x01;   /* mode 1 = 战斗中 */
  State->Ram[0x000C] = 0x01;   /* screen context = fight */

  /* 加载竞技场背景 (翻译自: LDX #$00, JSR sub_E7E9) */
  State->Ram[0x0027] = 0x00;   /* screen 0x00 = 第一关竞技场 */
  sub_E7E9_draw_screen(State);

  /* 竞技场逐扫描线 CHR 分段 (含地板实心垫 0xC4/0xC5) 已由 sub_E7E9 经
     gScreenBgSegments[0x00] 装载, 此处无需再设。 */

  /* 初始化 HP (翻译自对战设置: HP 初始值 = 0x40 = 64) */
  State->Ram[0x0510] = 0x40;   /* P1 HP */
  State->Ram[0x0540] = 0x40;   /* P2 HP */
  State->Ram[0x0062] = 0x40;   /* P1 血条显示值 */
  State->Ram[0x0063] = 0x40;   /* P2 血条显示值 */

  /* 初始化回合计时器 (翻译自: 99 秒) */
  State->Ram[0x003E] = 99;     /* 回合倒计时 = 99 秒 */
  State->Ram[0x003F] = 60;     /* 帧计数 = 60 (1秒) */

  /* 角色初始位置 (翻译自对战入场位置)
     NES 画面 256×240, 地面约 Y=200
     P1 在左侧 X=60, P2 在右侧 X=180 */
  State->Ram[0x0400] = 60;     /* P1 X */
  State->Ram[0x0401] = 200;    /* P1 Y (地面) */
  State->Ram[0x0402] = 180;    /* P2 X */
  State->Ram[0x0403] = 200;    /* P2 Y (地面) */
  State->Ram[0x0404] = 0;      /* P1 面朝右 */
  State->Ram[0x0405] = 1;      /* P2 面朝左 */
  State->Ram[0x0406] = 0;      /* P1 状态 = IDLE */
  State->Ram[0x0407] = 0;      /* P2 状态 = IDLE */

  /* 战斗状态标志 */
  State->Ram[0x0072] = 0;      /* ram_fight_state = 进行中 */

  /* Sprite CHR bank 初始化 (翻译自 sub_EA21: tbl_C00B[fighter] + 帧偏移)
     每个角色有独立的 CHR bank, 包含该角色所有动画图块。
     P1 → $6002 ($1000-$17FF), P2 → $6003 ($1800-$1FFF) */
  {
    UINT8 P1F = State->Ram[0x003B];
    UINT8 P2F = State->Ram[0x003C];
    ram_spr_chr_bank0 = (P1F < FIGHTER_ID_COUNT) ? gFighterChrBase[P1F] : 0;
    ram_spr_chr_bank1 = (P2F < FIGHTER_ID_COUNT) ? gFighterChrBase[P2F] : 0;
  }

  /* Sprite 调色板 (翻译自 sub_E783 + tbl_E7A0_spr_colors)
     P1 写入 PPU Palette[0x10-0x13] (sprite palette 0)
     P2 写入 PPU Palette[0x18-0x1B] (sprite palette 2) */
  {
    STATIC CONST UINT8 sSpritePaletteTable[9][4] = {
      { 0x0F, 0x12, 0x26, 0x36 },  /* 0: Chun-Li  */
      { 0x0F, 0x16, 0x26, 0x30 },  /* 1: Ryu/Ken  */
      { 0x0F, 0x18, 0x27, 0x36 },  /* 2: Guile    */
      { 0x0F, 0x06, 0x27, 0x37 },  /* 3: Blanka   */
      { 0x0F, 0x17, 0x27, 0x36 },  /* 4: Dhalsim  */
      { 0x0F, 0x16, 0x27, 0x37 },  /* 5: (unused) */
      { 0x0F, 0x1C, 0x2C, 0x3C },  /* 6: Balrog   */
      { 0x0F, 0x16, 0x26, 0x36 },  /* 7: Sagat    */
      { 0x0F, 0x15, 0x26, 0x36 },  /* 8: Vega     */
    };
    UINT8 P1Id = State->Ram[0x003B];
    UINT8 P2Id = State->Ram[0x003C];
    UINT8 i;

    if (P1Id < 9) {
      for (i = 0; i < 4; i++) {
        State->Ppu.Palette[0x10 + i] = sSpritePaletteTable[P1Id][i];
      }
    }
    if (P2Id < 9) {
      for (i = 0; i < 4; i++) {
        State->Ppu.Palette[0x18 + i] = sSpritePaletteTable[P2Id][i];
      }
    }
  }

  /* PPUCTRL: 战斗模式使用 8x8 精灵 (翻译自 sub_CAF6: AND #$DF 清除 bit5) */
  ram_ppuctrl_shadow &= 0xDF;
  PpuWriteCtrl(State, ram_ppuctrl_shadow);

  /* 启动调色板渐变 (从黑到亮) */
  PaletteFadeStart(State);

  gPhaseTimer = 0;

  DEBUG((DEBUG_INFO, "[SFC3] Fight scene ready (arena=0x00, timer=99)\n"));
}

/**
  每帧更新 - 翻译自 NMI handler 分发 + 主线程轮询

  先执行公共帧更新 (调色板渐变), 再按 ram_000E 分发到 NMI 模式处理,
  最后检查高层阶段转换 (模拟原厂主线程的轮询逻辑)。
**/
VOID
GameFrameUpdate (
  NES_STATE *State
  )
{
  UINT8 Mode;

  Mode = State->Ram[0x000E];

  /* 翻译自 NMI handler $D6A7-$D6AE: 每帧写入 sprite CHR bank 到 mapper 寄存器
     原厂 NMI 无条件执行 LDA ram_0025 / STA $6002 / LDA ram_0026 / STA $6003,
     不区分游戏模式。这确保 sprite pattern table ($1000) 始终映射到正确的 CHR bank。 */
  Mapper91Write(State, 0x6002, ram_spr_chr_bank0);
  Mapper91Write(State, 0x6003, ram_spr_chr_bank1);

  /* 公共帧更新: 调色板渐变 (翻译自 NMI $D6EA: JSR sub_FA96) */
  sub_FA96_common_frame_update(State);

  /* NMI 模式分发 (翻译自 NMI $D6E1-$D719) */
  switch (Mode) {
    case 0x01:
      sub_D724_mode1(State);
      break;
    case 0x02:
    case 0x05:
    case 0x06:
      sub_C076_fight(State, Mode);
      break;
    case 0x03:
      sub_C100_mode3(State);
      break;
    case 0x04:
      sub_C092_mode4(State);
      break;
    default:
      break;
  }

  /* 高层阶段转换检查 (模拟原厂主线程轮询) */
  switch (gGamePhase) {
    case PHASE_TITLE:
      /* 翻译自 sub_C759 等待循环: 检查 Start 按下或演示计时器到期 */
      if (State->Ram[0x050D] != 0) {
        /* Start 按下 → 进入角色选择 (翻译自 loc_C7C1) */
        DEBUG((DEBUG_INFO, "[SFC3] Start pressed -> character select\n"));
        State->Ram[0x00CA] = State->Ram[0x00C9];
        State->Ram[0x00CB] = State->Ram[0x00C9] + 2;  /* 初始信用 */
        EnterCharacterSelect(State);
      } else if (State->Ram[0x00C6] != 0) {
        /* 演示计时器到期 → 演示对战 (暂时跳过, 回到标题) */
        DEBUG((DEBUG_INFO, "[SFC3] Demo timer expired (demo fight skipped)\n"));
        EnterTitleScreen(State);
      }
      break;

    case PHASE_SELECT:
      /* 翻译自 sub_C857 等待循环: 检查选择确认 */
      if (State->Ram[0x051D] != 0) {
        /* 选择确认 → 进入 VS 过场 (翻译自 loc_C8ED → sub_F4AB) */
        DEBUG((DEBUG_INFO, "[SFC3] Fighter selected: P1=%d P2=%d\n",
               State->Ram[0x003B], State->Ram[0x003C]));
        EnterVsScreen(State);
      }
      break;

    case PHASE_VS:
      /* VS 过场: 画两角色立绘 (翻译自 sub_F52C: 哑角色9 metasprite, P1 左上/P2 右下镜像) */
      SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);
      {
        UINT8 n = 0;
        UINT8 P1Id = State->Ram[0x003B];
        UINT8 P2Id = State->Ram[0x003C];
        if (P1Id < FIGHTER_ID_COUNT) {
          VsPortraitRender(State, 0, 0, &n, 48, 48, 0, gVsRecP1[P1Id], gVsCntP1[P1Id]);
        }
        if (P2Id < FIGHTER_ID_COUNT) {
          if (P1Id == P2Id) {
            VsPortraitRender(State, 1, n, &n, 200, 128, 1, gVsRecAlt[P2Id], gVsCntAlt[P2Id]);
          } else {
            VsPortraitRender(State, 1, n, &n, 200, 128, 1, gVsRecP1[P2Id], gVsCntP1[P2Id]);
          }
        }
      }
      /* 定时 → 进入战斗 (翻译自 sub_CA69: 等待 ram_0010==0) */
      if (gPhaseTimer > 0) {
        gPhaseTimer--;
      } else {
        EnterFightScene(State);
      }
      break;

    case PHASE_FIGHT:
      /* 战斗中: 检查 KO 或计时器到期 */
      if (State->Ram[0x0072] != 0) {
        /* 战斗结束 → 结算 */
        DEBUG((DEBUG_INFO, "[SFC3] Fight ended, fight_state=%d\n",
               State->Ram[0x0072]));
        gGamePhase = PHASE_RESULT;
        gPhaseTimer = 180;  /* 3 秒结算画面 */
        State->Ram[0x000E] = 0x06;  /* mode 6 = 回合结束 */
      }
      break;

    case PHASE_RESULT:
      /* 结算: 清空 OAM, 定时后回标题 */
      SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);
      if (gPhaseTimer > 0) {
        gPhaseTimer--;
      } else {
        DEBUG((DEBUG_INFO, "[SFC3] Result done -> title\n"));
        EnterTitleScreen(State);
      }
      break;

    default:
      break;
  }

  State->FrameCount++;
}

/**
  调色板渐变系统 - 翻译自 sub_FA96 ($FA96)

  5 级 × 8 帧的调色板从黑到亮渐变。
  ram_00FC: 渐变状态 (0=关, 1=初始化, 2=渐变中, $FF=完成)
  ram_00FD: 子帧计时器 (每 8 帧一步)
  ram_00FE: 亮度偏移 (0=最暗, 每步 +$10, 到 $50 完成)
  RAM $06E0: 目标调色板 (32 字节)
**/
VOID
sub_FA96_common_frame_update (
  NES_STATE *State
  )
{
  UINT8  FadeState;
  UINT8  Offset;
  UINT8  Target;
  UINT8  Value;
  UINTN  i;

  FadeState = State->Ram[0x00FC];

  /* 渐变未激活或已完成 */
  if (FadeState == 0 || FadeState == 0xFF) {
    return;
  }

  /* 初始化渐变
     原厂 sub_FA96 是 fade-to-black (用于切屏前淡出旧画面)。
     C 版本架构不同: 先加载新画面再启动渐变, 因此需要反向实现为
     fade-in (从黑到亮)。偏移从 0x40 递减到 0, 画面从全黑渐亮。 */
  if (FadeState == 1) {
    State->Ram[0x00FE] = 0x40; /* 亮度偏移 = 0x40 (全黑) */
    State->Ram[0x00FC] = 2;    /* 进入渐变状态 */
    State->Ram[0x00FD] = 1;    /* 立即执行第一步 */
  }

  /* 子帧计时 (翻译自 $FAB0: DEC ram_00FD, 每 8 帧一步) */
  if (State->Ram[0x00FD] > 0) {
    State->Ram[0x00FD]--;
  }
  if (State->Ram[0x00FD] != 0) {
    return;  /* 还没到下一步 */
  }
  State->Ram[0x00FD] = 8;  /* 重载: 8 帧一步 */

  /* 写入当前亮度的调色板: Value = Target - Offset
     Offset 从 0x40 (全黑) 递减到 0 (全亮) */
  Offset = State->Ram[0x00FE];
  for (i = 0; i < PPU_PALETTE_SIZE; i++) {
    Target = State->Ram[RAM_FADE_TARGET + i];
    if (Target >= Offset) {
      Value = Target - Offset;
    } else {
      Value = 0x0F;  /* 钳制到黑色 */
    }
    State->Ppu.Palette[i] = Value;
  }

  /* 递减偏移 (fade-in: 从暗到亮) */
  if (State->Ram[0x00FE] >= 0x10) {
    State->Ram[0x00FE] -= 0x10;
  } else {
    /* 偏移归零, 渐变完成 */
    State->Ram[0x00FE] = 0;
    State->Ram[0x00FC] = 0xFF;  /* 渐变完成 */
    /* 确保最终调色板完全准确 */
    for (i = 0; i < PPU_PALETTE_SIZE; i++) {
      State->Ppu.Palette[i] = State->Ram[RAM_FADE_TARGET + i];
    }
  }
}

/**
  血条追赶效果 - 翻译自 sub_EA80 ($EA80)

  每帧将显示值 (ram_0062/0063) 向实际 HP (0510/0540) 追赶 1 点。
  原厂每 4 帧追赶 1 点, 此处简化为每 2 帧。
**/
STATIC
VOID
HpBarChase (
  NES_STATE *State
  )
{
  /* 每 2 帧执行一次 */
  if ((State->FrameCount & 1) != 0) {
    return;
  }

  /* P1 血条追赶 */
  if (State->Ram[0x0062] > State->Ram[0x0510]) {
    State->Ram[0x0062]--;
  }
  /* P2 血条追赶 */
  if (State->Ram[0x0063] > State->Ram[0x0540]) {
    State->Ram[0x0063]--;
  }
}

/**
  启动调色板渐变 - 翻译自 sub_FAF4 ($FAF4)

  将当前背景调色板复制到目标缓冲区, 然后启动渐变。
**/
VOID
PaletteFadeStart (
  NES_STATE *State
  )
{
  /* 复制当前调色板到目标缓冲区 */
  CopyMem(&State->Ram[RAM_FADE_TARGET], State->Ppu.Palette, PPU_PALETTE_SIZE);
  /* 启动渐变 */
  State->Ram[0x00FC] = 1;  /* 渐变状态 = 初始化 */
  State->Ram[0x00FD] = 1;  /* 立即开始 */
}

/**
  模式 4 - 标题画面 NMI 处理 - 翻译自 sub_C092 ($C092)

  每帧执行:
  1. 帧计数递减, 每秒递减演示倒计时
  2. 检测任意按键 → 重置演示倒计时
  3. 检测 Start → 设置 ram_050D 标志 (通知主线程)
**/
VOID
sub_C092_mode4 (
  NES_STATE *State
  )
{
  UINT8 P1Input;
  UINT8 P1Prev;
  UINT8 Pressed;

  /* 每帧清空 OAM: 标题画面无精灵, 防止残留 OAM 数据渲染为色块
     (翻译自 NMI handler 中 OAM DMA 前清零 $0200-$02FF 的行为) */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  /* 帧计数递减 (翻译自 sub_C092 中的计时逻辑) */
  if (State->Ram[0x0010] > 0) {
    State->Ram[0x0010]--;
  }

  /* 每秒递减演示倒计时 (翻译自: 当 ram_0010 == 0 时重载为 60 并 DEC ram_00C7) */
  if (State->Ram[0x0010] == 0) {
    State->Ram[0x0010] = 60;  /* 重载 60 帧 = 1 秒 */
    if (State->Ram[0x00C7] > 0) {
      State->Ram[0x00C7]--;
      if (State->Ram[0x00C7] == 0) {
        /* 演示计时器到期 → 设置演示标志 */
        State->Ram[0x00C6] = 1;
        DEBUG((DEBUG_INFO, "[SFC3] Demo timer expired, demo flag set\n"));
      }
    }
  }

  /* 读取 P1 输入 */
  P1Input = State->Ram[0x00F4];   /* 当前帧 */
  P1Prev  = State->Ram[0x00F6];   /* 上一帧 */
  Pressed = P1Input & ~P1Prev;     /* 新按下的按键 */

  /* 任意按键 → 重置演示倒计时 (翻译自 sub_C092 中的按键检测) */
  if (P1Input != 0) {
    State->Ram[0x00C7] = 10;  /* 重置为 10 秒 */
    State->Ram[0x00C6] = 0;   /* 清除演示标志 */
  }

  /* Start 按下 → 设置标志通知主线程 (翻译自: LDA ram_00B4 / CMP #$07) */
  if (Pressed & NES_BTN_START) {
    State->Ram[0x050D] = 1;
    DEBUG((DEBUG_INFO, "[SFC3] Start button detected on title screen\n"));
  }

  /* 难度左右切换 (翻译自 sub_C092 bank_FF.asm 177-202: ram_00C9 = 0..7 环绕) */
  if (Pressed & NES_BTN_RIGHT) {
    State->Ram[0x00C9] = (UINT8)((State->Ram[0x00C9] + 1) & 0x07);
  } else if (Pressed & NES_BTN_LEFT) {
    State->Ram[0x00C9] = (UINT8)((State->Ram[0x00C9] + 7) & 0x07);
  }

  /* 难度光标精灵 (翻译自 sub_C092 + 标题 init sub_C759: 对象$10 → OAM[0],
     Y=0xC2, tile=0x24 (caret, $6002=0x84), attr=0x01 (sprite pal1=白), X=0x60+8*diff; 无闪烁) */
  State->Ppu.Oam[0] = 0xC2;
  State->Ppu.Oam[1] = 0x24;
  State->Ppu.Oam[2] = 0x01;
  State->Ppu.Oam[3] = (UINT8)(0x60 + ((State->Ram[0x00C9] & 0x07) << 3));
}

/**
  模式 3 - 角色选择 NMI 处理 - 翻译自 sub_C100 ($C100)

  每帧执行:
  1. 读取方向键移动光标 (3×3 网格)
  2. A 键确认选择
  3. 设置 ram_p1_fighter / ram_p2_fighter
**/
VOID
sub_C100_mode3 (
  NES_STATE *State
  )
{
  UINT8 P1Input;
  UINT8 P1Prev;
  UINT8 Pressed;
  UINT8 Cursor;

  /* 每帧清空 OAM: 角色选择画面无精灵, 防止残留 OAM 数据渲染为色块 */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  /* 读取 P1 输入 */
  P1Input = State->Ram[0x00F4];
  P1Prev  = State->Ram[0x00F6];
  Pressed = P1Input & ~P1Prev;

  /* 光标位置存储在 ram_002E (避免与手柄 RAM $00F4 冲突) */
  Cursor = State->Ram[0x002E];

  /* 方向键移动光标 (3×3 网格) */
  if (Pressed & NES_BTN_RIGHT) {
    if ((Cursor % 3) < 2) {
      Cursor++;
    }
  }
  if (Pressed & NES_BTN_LEFT) {
    if ((Cursor % 3) > 0) {
      Cursor--;
    }
  }
  if (Pressed & NES_BTN_DOWN) {
    if (Cursor < 6) {
      Cursor += 3;
    }
  }
  if (Pressed & NES_BTN_UP) {
    if (Cursor >= 3) {
      Cursor -= 3;
    }
  }

  /* 限制范围 */
  if (Cursor > 8) {
    Cursor = 8;
  }

  State->Ram[0x002E] = Cursor;

  /* A 键确认选择 (翻译自 sub_C100 中的确认逻辑) */
  if (Pressed & NES_BTN_A) {
    /* 映射光标位置到角色 ID */
    State->Ram[0x003B] = gSelectGridToFighter[Cursor];  /* P1 角色 */
    /* P2 角色: 简单 AI 选择 (原厂根据难度选择) */
    State->Ram[0x003C] = gSelectGridToFighter[(Cursor + 4) % 9];  /* P2 角色 */
    State->Ram[0x051D] = 1;  /* 确认标志 */
    DEBUG((DEBUG_INFO, "[SFC3] Select: cursor=%d, P1=%d, P2=%d\n",
           Cursor, State->Ram[0x003B], State->Ram[0x003C]));
  }
}

/**
  模式 1 - 战斗中 NMI 处理 - 翻译自 sub_D724 ($D724)

  战斗中的每帧逻辑: 计时器递减、P1 输入移动、P2 简单 AI、
  角色朝向更新、KO 检测。
  当前为基础框架, 后续 Task 18-19 中完善动画与碰撞。
**/
VOID
sub_D724_mode1 (
  NES_STATE *State
  )
{
  UINT8 P1Input;
  UINT8 P1X, P2X;

  /* ---- 血条追赶效果 (翻译自 sub_EA80) ---- */
  HpBarChase(State);

  /* ---- 每帧清空 OAM (隐藏所有精灵), 然后由渲染函数填充 ---- */
  SetMem(State->Ppu.Oam, PPU_OAM_SIZE, 0xFF);

  /* ---- 回合计时器 (翻译自 sub_D724 计时逻辑) ---- */
  if (State->Ram[0x003F] > 0) {
    State->Ram[0x003F]--;
  }
  if (State->Ram[0x003F] == 0) {
    State->Ram[0x003F] = 60;  /* 重载 60 帧 = 1 秒 */
    if (State->Ram[0x003E] > 0) {
      State->Ram[0x003E]--;
    }
    if (State->Ram[0x003E] == 0) {
      State->Ram[0x0072] = (State->Ram[0x0510] >= State->Ram[0x0540]) ? 1 : 2;
      return;
    }
  }

  P1X = State->Ram[0x0400];
  P2X = State->Ram[0x0402];

  /* ---- P2 CPU: 在渲染前作为"替代输入源"决定 状态/位置 (简化 sub_DA24),
     故不再被 FighterUpdate 覆盖。硬直/攻击中只递减计时, 收招回站立 (done-latch 近似)。 ---- */
  {
    UINT8 P2St = State->Ram[0x0407];
    if (P2St == FSTATE_HITSTUN) {
      if (State->Ram[0x0411] > 0) {
        State->Ram[0x0411]--;
      } else {
        State->Ram[0x0407] = FSTATE_IDLE;
      }
    } else if (P2St == FSTATE_ATTACK || P2St == FSTATE_ATTACK2) {
      if (State->Ram[0x0413] > 0) {
        State->Ram[0x0413]--;
      } else {
        State->Ram[0x0407] = FSTATE_IDLE;
      }
    } else if (P2St != FSTATE_KO) {
      UINT8 Dist = (P2X > P1X) ? (UINT8)(P2X - P1X) : (UINT8)(P1X - P2X);
      UINT8 AiRand = (UINT8)((State->FrameCount * 7 + 13) & 0xFF);
      if (Dist > 48) {
        /* 远: 走近 */
        P2X = (P2X > P1X) ? (UINT8)(P2X - 1) : (UINT8)(P2X + 1);
        State->Ram[0x0407] = FSTATE_WALK;
      } else if (Dist < 28) {
        /* 近: 多半攻击, 偶尔后退/待机 */
        if (State->Ram[0x0413] == 0 && (AiRand & 3) == 0) {
          State->Ram[0x0407] = (AiRand & 4) ? FSTATE_ATTACK2 : FSTATE_ATTACK;
          State->Ram[0x0413] = FsdAttackDur(State->Ram[0x003C], State->Ram[0x0407]);
        } else if ((AiRand & 7) == 0) {
          P2X = (P2X > P1X) ? (UINT8)(P2X + 2) : (UINT8)(P2X - 2);
          State->Ram[0x0407] = FSTATE_WALK;
        } else {
          State->Ram[0x0407] = FSTATE_IDLE;
        }
      } else {
        /* 中: 接近, 偶发攻击 */
        if (State->Ram[0x0413] == 0 && (AiRand & 7) == 0) {
          State->Ram[0x0407] = FSTATE_ATTACK;
          State->Ram[0x0413] = FsdAttackDur(State->Ram[0x003C], FSTATE_ATTACK);
        } else {
          P2X = (P2X > P1X) ? (UINT8)(P2X - 1) : (UINT8)(P2X + 1);
          State->Ram[0x0407] = FSTATE_WALK;
        }
      }
      if (P2X > 240) P2X = 240;
      if (P2X < 16) P2X = 16;
      State->Ram[0x0402] = P2X;
    }
  }

  /* ---- P1 人类输入 → 状态 (含攻击/硬直计时与跳跃物理) ---- */
  FighterUpdate(State, 0);

  /* ---- 朝向: 双方面对面 (每帧按位置; 简化, 不含原厂 sub_EF47 的位移补偿) ---- */
  P1X = State->Ram[0x0400];
  P2X = State->Ram[0x0402];
  State->Ram[0x0404] = (P1X < P2X) ? 0 : 1;
  State->Ram[0x0405] = (P2X < P1X) ? 0 : 1;

  /* ---- 推挤: 重叠时各让 1px (简化 sub_EFFD) ---- */
  {
    INT16 dx = (INT16)P1X - (INT16)P2X;
    if (dx < 0) dx = -dx;
    if (dx < 24 && State->Ram[0x0406] != FSTATE_KO && State->Ram[0x0407] != FSTATE_KO) {
      if (P1X < P2X) {
        if (P1X > 8) State->Ram[0x0400]--;
        if (P2X < 240) State->Ram[0x0402]++;
      } else {
        if (P2X > 8) State->Ram[0x0402]--;
        if (P1X < 240) State->Ram[0x0400]++;
      }
    }
  }

  /* ---- 角色精灵渲染 + 每帧 sprite CHR 窗口 (sub_EA21) ---- */
  {
    UINT8 Next0, Next1, Fid0, Fid1;
    UINT8 F0, F1;

    FighterRenderRecord(State, 0, 0, &Next0, &Fid0);
    FighterRenderRecord(State, 1, Next0, &Next1, &Fid1);

    F0 = State->Ram[0x003B];
    F1 = State->Ram[0x003C];
    if (F0 < FIGHTER_ID_COUNT && Fid0 < SFC3_FSD_MAX_FRAMES) {
      Mapper91Write(State, 0x6002, (UINT8)(gFighterChrBase[F0] + gFsdChrOff[F0][Fid0]));
    }
    if (F1 < FIGHTER_ID_COUNT && Fid1 < SFC3_FSD_MAX_FRAMES) {
      Mapper91Write(State, 0x6003, (UINT8)(gFighterChrBase[F1] + gFsdChrOff[F1][Fid1]));
    }
    (VOID)Next1;
  }

  /* ---- 命中检测 (简化: 攻击计时经过 10 时判定一次; 效果下帧体现) ---- */
  if (State->Ram[0x0412] == 10 && FighterCheckHit(State, 0, 1)) {
    FighterApplyDamage(State, 1, 6);
    State->Ram[0x0407] = FSTATE_HITSTUN;
    State->Ram[0x0411] = 20;
    if (State->Ram[0x0402] > State->Ram[0x0400]) {
      State->Ram[0x0402] += 8;
      if (State->Ram[0x0402] > 240) State->Ram[0x0402] = 240;
    } else if (State->Ram[0x0402] > 8) {
      State->Ram[0x0402] -= 8;
    }
  }
  if (State->Ram[0x0413] == 10 && FighterCheckHit(State, 1, 0)) {
    FighterApplyDamage(State, 0, 6);
    State->Ram[0x0406] = FSTATE_HITSTUN;
    State->Ram[0x0410] = 20;
    if (State->Ram[0x0400] > State->Ram[0x0402]) {
      State->Ram[0x0400] += 8;
      if (State->Ram[0x0400] > 240) State->Ram[0x0400] = 240;
    } else if (State->Ram[0x0400] > 8) {
      State->Ram[0x0400] -= 8;
    }
  }

  /* ---- KO 检测 ---- */
  if (State->Ram[0x0510] == 0) {
    State->Ram[0x0072] = 2;
    State->Ram[0x0406] = FSTATE_KO;
  } else if (State->Ram[0x0540] == 0) {
    State->Ram[0x0072] = 1;
    State->Ram[0x0407] = FSTATE_KO;
  }

  (VOID)P1Input;
}

/**
  战斗 HUD 渲染 - 直接在 FrameBuffer 上绘制 HP 条和计时器

  翻译自原厂 sub_EA80 (HP bar 追赶逻辑) + NMI 中的 HUD sprite 绘制。
  简化实现: 直接写像素到 FrameBuffer, 不经过 OAM/Tile 系统。
**/
STATIC
VOID
HudRenderFight (
  NES_STATE *State
  )
{
  UINT16 X, Y;
  UINT8  P1Hp, P2Hp, Timer;
  UINT8  BarWidth;
  UINT8  ColorBg   = 0x0C;  /* navy HUD 顶条 (匹配参考 compare/4; 0x0F 留给选择/标题作黑) */
  UINT8  ColorFill = 0x2A;  /* 绿色 HP 填充 */
  UINT8  ColorEmpty = 0x00; /* 深灰 HP 空槽 */
  UINT8  ColorBorder = 0x30; /* 白色边框 */
  UINT8  ColorText = 0x30;  /* 白色文字 */

  /* 使用血条显示值 (缓慢追赶), 而非实际 HP (翻译自 sub_EA80) */
  P1Hp  = State->Ram[0x0062];  /* ram_p1_bar_hp */
  P2Hp  = State->Ram[0x0063];  /* ram_p2_bar_hp */
  Timer = State->Ram[0x003E];

  /* 绘制 HUD 背景条 (顶部 16 像素) */
  SetMem(&State->Ppu.FrameBuffer[0], 16 * NES_SCREEN_WIDTH, ColorBg);

  /* P1 HP 条 (左: x=4..99, y=4..11) */
  BarWidth = (UINT8)((UINT16)P1Hp * 96 / 64);
  if (BarWidth > 96) BarWidth = 96;
  for (Y = 4; Y < 12; Y++) {
    for (X = 4; X < 100; X++) {
      if (Y == 4 || Y == 11 || X == 4 || X == 99) {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorBorder;
      } else if (X - 5 < BarWidth) {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorFill;
      } else {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorEmpty;
      }
    }
  }

  /* P2 HP 条 (右: x=156..251, y=4..11) */
  BarWidth = (UINT8)((UINT16)P2Hp * 96 / 64);
  if (BarWidth > 96) BarWidth = 96;
  for (Y = 4; Y < 12; Y++) {
    for (X = 156; X < 252; X++) {
      if (Y == 4 || Y == 11 || X == 156 || X == 251) {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorBorder;
      } else if (251 - X < BarWidth) {  /* P2 条从右向左缩 */
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorFill;
      } else {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = ColorEmpty;
      }
    }
  }

  /* 计时器 (中间: 两位数字, 4x6 像素 each, 位于 x=120..135, y=3..13) */
  {
    UINT8 Tens = Timer / 10;
    UINT8 Ones = Timer % 10;
    UINT8 Row, Col;
    UINT8 Px, Py;

    if (Tens > 9) Tens = 9;
    if (Ones > 9) Ones = 9;

    /* 十位 */
    for (Row = 0; Row < 5; Row++) {
      for (Col = 0; Col < 3; Col++) {
        if ((sDigitFont[Tens][Row] >> (2 - Col)) & 1) {
          for (Py = 0; Py < 2; Py++) {
            for (Px = 0; Px < 2; Px++) {
              UINT16 Fx = 118 + Col * 2 + Px;
              UINT16 Fy = 3 + Row * 2 + Py;
              if (Fx < NES_SCREEN_WIDTH && Fy < NES_SCREEN_HEIGHT) {
                State->Ppu.FrameBuffer[Fy * NES_SCREEN_WIDTH + Fx] = ColorText;
              }
            }
          }
        }
      }
    }
    /* 个位 */
    for (Row = 0; Row < 5; Row++) {
      for (Col = 0; Col < 3; Col++) {
        if ((sDigitFont[Ones][Row] >> (2 - Col)) & 1) {
          for (Py = 0; Py < 2; Py++) {
            for (Px = 0; Px < 2; Px++) {
              UINT16 Fx = 128 + Col * 2 + Px;
              UINT16 Fy = 3 + Row * 2 + Py;
              if (Fx < NES_SCREEN_WIDTH && Fy < NES_SCREEN_HEIGHT) {
                State->Ppu.FrameBuffer[Fy * NES_SCREEN_WIDTH + Fx] = ColorText;
              }
            }
          }
        }
      }
    }
  }
}

/**
  战斗过渡模式 (2/5/6) - 翻译自 sub_C076 ($C076)

  Mode 2: 对战准备 (角色入场动画)
  Mode 5: 对战中 (主要游戏逻辑)
  Mode 6: 回合结束 (KO 动画, 胜负判定)
**/
VOID
sub_C076_fight (
  NES_STATE *State,
  UINT8      Mode
  )
{
  (VOID)State;
  (VOID)Mode;
}

/**
  画面切换 - 翻译自 sub_E7E9_draw_screen ($E7E9)

  根据 ram_screen ($0027) 的值选择并绘制对应的背景画面。
**/
VOID
sub_E7E9_draw_screen (
  NES_STATE *State
  )
{
  UINT8 ScreenId;

  ScreenId = State->Ram[0x0027];
  DEBUG((DEBUG_INFO, "[SFC3] draw_screen: screen_id=0x%02X\n", ScreenId));

  /* CHR bank 切换 (翻译自 sub_FBCE 首字节 → $6000/$6001) */
  BackgroundLoadChrBanks(State, ScreenId);

  /* 按画面装载逐扫描线背景 CHR 分段表 (摘自 tbl_C016); NULL = 整屏用入口 bank */
  State->BgSegs = (ScreenId < 16) ? gScreenBgSegments[ScreenId] : NULL;

  /* 加载 Nametable 和调色板 (翻译自 $E7E9 → $FBCE / $FBB2 调用链) */
  BackgroundLoadScreen(State, ScreenId);
  BackgroundLoadPalette(State, ScreenId);

  /* 设置 PPU 控制寄存器 (翻译自 sub_FCD0: LDA ram_0015 / STA $2000)
     ram_0015 = 0x88: NMI 使能(bit7) + Sprite pattern $1000(bit3) + BG pattern $0000(bit4=0)
     原厂初始化位于 $FB7A: LDA #$88 / STA ram_0015 */
  ram_ppuctrl_shadow = 0x88;
  PpuWriteCtrl(State, 0x88);
  ram_ppumask_shadow = 0x1E;
  PpuWriteMask(State, 0x1E);  /* 背景+精灵渲染使能 */

  DEBUG((DEBUG_INFO, "[SFC3] screen=%02X CHR=%02X/%02X/%02X/%02X Ctrl=%02X\n",
         ScreenId, State->ChrBanks[0], State->ChrBanks[1],
         State->ChrBanks[2], State->ChrBanks[3], State->Ppu.Ctrl));
}

/**
  VS 画面叠加渲染 - 绘制 "VS" 大字和角色名标识

  在 PpuRenderFrame 之后调用, 直接在 FrameBuffer 上绘制。
**/
STATIC
VOID
VsRenderOverlay (
  NES_STATE *State
  )
{
  /* "V" 字母 5x7 像素点阵 */
  STATIC CONST UINT8 sLetterV[7] = {
    0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04
  };
  /* "S" 字母 5x7 像素点阵 */
  STATIC CONST UINT8 sLetterS[7] = {
    0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E
  };
  UINT8 Row, Col;
  UINT8 Px, Py;
  UINT16 Fx, Fy;
  UINT8  Color = 0x28;  /* 亮灰/白色 VS 文字 */

  /* 绘制 "V" (center-left: x=110, y=100, 3x scale) */
  for (Row = 0; Row < 7; Row++) {
    for (Col = 0; Col < 5; Col++) {
      if ((sLetterV[Row] >> (4 - Col)) & 1) {
        for (Py = 0; Py < 3; Py++) {
          for (Px = 0; Px < 3; Px++) {
            Fx = 108 + Col * 3 + Px;
            Fy = 95 + Row * 3 + Py;
            if (Fx < NES_SCREEN_WIDTH && Fy < NES_SCREEN_HEIGHT) {
              State->Ppu.FrameBuffer[Fy * NES_SCREEN_WIDTH + Fx] = Color;
            }
          }
        }
      }
    }
  }

  /* 绘制 "S" (center-right: x=132, y=100, 3x scale) */
  for (Row = 0; Row < 7; Row++) {
    for (Col = 0; Col < 5; Col++) {
      if ((sLetterS[Row] >> (4 - Col)) & 1) {
        for (Py = 0; Py < 3; Py++) {
          for (Px = 0; Px < 3; Px++) {
            Fx = 132 + Col * 3 + Px;
            Fy = 95 + Row * 3 + Py;
            if (Fx < NES_SCREEN_WIDTH && Fy < NES_SCREEN_HEIGHT) {
              State->Ppu.FrameBuffer[Fy * NES_SCREEN_WIDTH + Fx] = Color;
            }
          }
        }
      }
    }
  }
}

/**
  回合结算叠加渲染 - 显示 "P1 WINS" 或 "P2 WINS"

  翻译自原厂回合结束画面 (mode 6) 的胜负显示。
**/
STATIC
VOID
ResultRenderOverlay (
  NES_STATE *State
  )
{
  /* "P1 WINS" / "P2 WINS" 用 3x5 像素字体 × 2 倍缩放 */
  /* W=0x1F, I=0x04, N=0x15, S=0x0E, P=0x1E, 1=0x02, 2=0x06 */
  STATIC CONST UINT8 sWinFont[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
  UINT8 Winner;
  UINT8 Color = 0x30;  /* 白色 */
  UINT16 X, Y;

  (VOID)sWinFont;

  /* 判定胜者: ram_0072 = 1 → P1 胜, 2 → P2 胜 */
  Winner = State->Ram[0x0072];

  /* 绘制中央高亮条 */
  for (Y = 108; Y < 132; Y++) {
    for (X = 60; X < 196; X++) {
      if (X < NES_SCREEN_WIDTH && Y < NES_SCREEN_HEIGHT) {
        State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X] = 0x0F;
      }
    }
  }

  /* 绘制 "P1" 或 "P2" 文字 (简化: 用数字字体) */
  {
    UINT8 Digit = (Winner == 1) ? 1 : 2;
    UINT8 Row, Col;
    UINT8 Px, Py;
    /* "P" + digit 在中央 */
    for (Row = 0; Row < 5; Row++) {
      for (Col = 0; Col < 3; Col++) {
        if ((sDigitFont[Digit][Row] >> (2 - Col)) & 1) {
          for (Py = 0; Py < 3; Py++) {
            for (Px = 0; Px < 3; Px++) {
              UINT16 Fx = 118 + Col * 3 + Px;
              UINT16 Fy = 110 + Row * 3 + Py;
              if (Fx < NES_SCREEN_WIDTH && Fy < NES_SCREEN_HEIGHT) {
                State->Ppu.FrameBuffer[Fy * NES_SCREEN_WIDTH + Fx] = Color;
              }
            }
          }
        }
      }
    }
  }

  /* 绘制 "WINS" 提示 (简化: 在数字下方画一条横线) */
  for (X = 110; X < 146; X++) {
    if (X < NES_SCREEN_WIDTH) {
      State->Ppu.FrameBuffer[128 * NES_SCREEN_WIDTH + X] = Color;
    }
  }
}

/**
  渲染叠加层 - 在 PpuRenderFrame 之后调用

  根据当前游戏阶段绘制 HUD 等叠加内容。
**/
VOID
GameRenderOverlay (
  NES_STATE *State
  )
{
  if (gGamePhase == PHASE_FIGHT) {
    HudRenderFight(State);
  } else if (gGamePhase == PHASE_VS) {
    VsRenderOverlay(State);
  } else if (gGamePhase == PHASE_RESULT) {
    ResultRenderOverlay(State);
  }
}
