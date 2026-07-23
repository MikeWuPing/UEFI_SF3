/** @file
  游戏状态机 - 翻译自 NMI handler 的分发逻辑与主线程流程

  原厂程序使用双上下文架构:
  - 主线程 (Reset handler 循环): 高层游戏流程 (画面切换、对战设置)
  - NMI handler (60Hz): 每帧逻辑 (动画、输入、调色板渐变)

  C 版本将两者合并为 GameFrameUpdate 中的分层状态机:
  - GamePhase: 高层流程 (标题→选角→VS→战斗→结算)
  - ram_000E: NMI 模式分发 (每帧动画/输入处理)
**/

#ifndef _SFC3_GAME_STATE_H_
#define _SFC3_GAME_STATE_H_

#include "nes_state.h"

/* 高层游戏阶段 (模拟原厂主线程流程) */
#define PHASE_TITLE       0   /* 标题画面, 等待 Start */
#define PHASE_SELECT      1   /* 角色选择 */
#define PHASE_VS          2   /* VS 过场 */
#define PHASE_FIGHT       3   /* 战斗中 */
#define PHASE_RESULT      4   /* 回合结算 */
#define PHASE_ENDING      5   /* 结局动画 */

/* 游戏初始化 (翻译自 Reset handler $C704 + sub_C759) */
VOID GameInit(NES_STATE *State);

/* 每帧更新 (翻译自 NMI handler 分发 + 主线程轮询) */
VOID GameFrameUpdate(NES_STATE *State);

/* PPU 渲染后叠加层 (HUD 等) */
VOID GameRenderOverlay(NES_STATE *State);

/* 调色板渐变系统 (翻译自 sub_FA96) */
VOID sub_FA96_common_frame_update(NES_STATE *State);
VOID PaletteFadeStart(NES_STATE *State);

/* NMI 模式处理函数 (从 ASM bank_FF 翻译) */
VOID sub_D724_mode1(NES_STATE *State);       /* 战斗中 */
VOID sub_C076_fight(NES_STATE *State, UINT8 Mode);  /* 对战过渡 */
VOID sub_C100_mode3(NES_STATE *State);       /* 角色选择 */
VOID sub_C092_mode4(NES_STATE *State);       /* 标题画面 */

/* 画面切换 (翻译自 sub_E7E9_draw_screen) */
VOID sub_E7E9_draw_screen(NES_STATE *State);

/* 阶段切换辅助 */
VOID EnterTitleScreen(NES_STATE *State);
VOID EnterCharacterSelect(NES_STATE *State);
VOID EnterVsScreen(NES_STATE *State);
VOID EnterFightScene(NES_STATE *State);

#endif /* _SFC3_GAME_STATE_H_ */
