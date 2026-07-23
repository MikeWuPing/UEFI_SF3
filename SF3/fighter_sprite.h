/** @file
  角色精灵渲染 (忠实翻译自 bank_FF.asm sub_E4E8 的精灵发射循环)

  消费 gen_fighter_sprite_data.py 从 ROM 烘焙的记录表: 每帧原始 (xd,yd,tile)
  三元组 + 每帧 sprite CHR 偏移 + 每 FState 的动画循环。按当前 FState 推进动画
  计时器选出 frame_id, 再把该帧的三元组 1:1 复现 sub_E4E8 的位逻辑写入 OAM。
**/

#ifndef _SFC3_FIGHTER_SPRITE_H_
#define _SFC3_FIGHTER_SPRITE_H_

#include "nes_state.h"

/**
  推进一名角色的动画并渲染其精灵到 OAM (共享竞技场, 从 OamStart 起分配)。

  @param State       NES 状态
  @param Player      0 = P1, 1 = P2
  @param OamStart    本次分配起始 OAM 索引 (0..63)
  @param OamNext     输出: 下一个空闲 OAM 索引
  @param FrameIdOut  输出: 本帧选用的 frame_id (供设置 sprite CHR 窗口)
**/
VOID
FighterRenderRecord (
  NES_STATE *State,
  UINT8      Player,
  UINT8      OamStart,
  UINT8     *OamNext,
  UINT8     *FrameIdOut
  );

/** 返回某 FState 烘焙时间线总时长 (各帧 hold 之和), 用作攻击/硬直计时。 */
UINT8 FsdAttackDur(UINT8 Fighter, UINT8 FState);

/**
  VS 对决立绘渲染 (翻译自 sub_F52C + sub_E4E8, 哑角色9 的 metasprite)。
  无 anchor/无动画: 直接以 (Ox,Oy) 为原点发射 Rec 的 Cnt 个三元组; facing 控制镜像/+0x80/+2。
  调用方需已设好对应玩家的 sprite CHR 窗 ($6002/$6003 = 0xA6 + chr_off)。
*/
VOID
VsPortraitRender (
  NES_STATE   *State,
  UINT8        Player,
  UINT8        OamStart,
  UINT8       *OamNext,
  INT16        Ox,
  INT16        Oy,
  UINT8        Facing,
  CONST UINT8  Rec[][3],
  UINT8        Cnt
  );

#endif
