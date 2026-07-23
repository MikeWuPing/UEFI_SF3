/** @file
  角色引擎 - 动画状态机、物理、碰撞、HP
  翻译自 bank_FF.asm 和 bank_00-0A.asm 中的角色相关函数
**/

#ifndef _SFC3_FIGHTER_H_
#define _SFC3_FIGHTER_H_

#include "nes_state.h"

/*
 * 角色 ID 范围为 0x00-0x08, 其中 0x05 不存在。
 * FIGHTER_COUNT (nes_types.h) = 8 表示实际角色数量,
 * FIGHTER_ID_COUNT 用于按 ID 索引的数组尺寸 (含 ID 5 空槽)。
 */
#define FIGHTER_ID_COUNT  (FIGHTER_VEGA + 1)  /* 9 */

/* 角色动作状态 */
#define FSTATE_IDLE     0
#define FSTATE_WALK     1
#define FSTATE_JUMP     2
#define FSTATE_CROUCH   3
#define FSTATE_ATTACK   4
#define FSTATE_HITSTUN  5
#define FSTATE_KO       6
#define FSTATE_ATTACK2  7   /* 重击 (原厂 state 0x28) */

/* 动画类型 */
#define ANIM_IDLE       0
#define ANIM_WALK       1
#define ANIM_JUMP       2
#define ANIM_CROUCH     3
#define ANIM_PUNCH      4
#define ANIM_KICK       5
#define ANIM_SPECIAL    6
#define ANIM_HIT        7
#define ANIM_KO         8
#define ANIM_MAX        9

/* 动画帧: 描述一帧中所有 sprite 的 tile 和偏移 */
typedef struct {
  UINT8   SpriteCount;
  UINT8   Sprites[16][4];  /* {tile, x_off, y_off, attr} */
  UINT8   Duration;         /* 持续帧数 */
  UINT8   NextFrame;        /* 下一帧索引 */
} FIGHTER_ANIM_FRAME;

/* 动画序列 */
typedef struct {
  UINT8               FrameCount;
  FIGHTER_ANIM_FRAME *Frames;
} FIGHTER_ANIM;

/* 碰撞框 */
typedef struct {
  INT8   XOffset, YOffset;
  UINT8  Width, Height;
} HITBOX;

/* 角色静态数据 */
typedef struct {
  UINT8          PaletteIndex;
  FIGHTER_ANIM  *Anims[ANIM_MAX];
} FIGHTER_DATA;

/* 角色数据表: 索引 = 角色 ID (0-8), ID 5 不存在 */
extern FIGHTER_DATA gFighterData[FIGHTER_ID_COUNT];

/* 角色 CHR bank 基偏移表 (tbl_C00B, 用于设置 sprite CHR bank) */
extern CONST UINT8 gFighterChrBase[FIGHTER_ID_COUNT];

/* 接口函数 */
VOID FighterUpdate(NES_STATE *State, UINT8 Player);
VOID FighterRenderSprites(NES_STATE *State, UINT8 Player);
BOOLEAN FighterCheckHit(NES_STATE *State, UINT8 Attacker, UINT8 Defender);
VOID FighterApplyDamage(NES_STATE *State, UINT8 Target, UINT8 Damage);

#endif /* _SFC3_FIGHTER_H_ */
