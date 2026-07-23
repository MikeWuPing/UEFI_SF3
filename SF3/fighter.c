/** @file
  角色引擎实现

  翻译参考:
  - bank_FF.asm: NMI handler 中角色更新相关代码
  - bank_00-0A.asm: 各角色动画帧数据、招式逻辑
  - 搜索 ram_p1_fighter ($003B), ram_p1_hp ($0510), ram_damage ($040D) 的引用
**/

#include "fighter.h"
#include "fighter_sprite.h"
#include "ppu.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

/* OAM 布局: P1=sprites 0-15, P2=sprites 16-31, HUD=sprites 32-63 */
#define OAM_P1_BASE   0
#define OAM_P2_BASE   16
#define OAM_HUD_BASE  32

/* 每个角色的精灵 CHR bank 基偏移 (tbl_C00B, bank_FF.asm line 24)
   原厂 sub_EA21 用此值 + 动画帧偏移设置 $6002/$6003 sprite CHR bank */
CONST UINT8 gFighterChrBase[FIGHTER_ID_COUNT] = {
  0x00,  /* 0: Chun-Li  */
  0x5A,  /* 1: Ryu/Ken  */
  0x40,  /* 2: Guile    */
  0x2E,  /* 3: Blanka   */
  0x13,  /* 4: Dhalsim  */
  0x5A,  /* 5: (Ryu/Ken clone) */
  0x76,  /* 6: Balrog   */
  0x94,  /* 7: Sagat    */
  0x80,  /* 8: Vega     */
};

/* 精灵 tile 基偏移 (在角色 CHR bank 内, 均为 0 因为每个角色独占 bank)
   原厂从帧数据表读取 tile 号 (0x00-0x7F), P2 加 0x80 映射到第二个 2KB bank */
STATIC CONST UINT8 gFighterTileBase[FIGHTER_ID_COUNT] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/**
  角色帧更新 - 状态机推进、动画计时、物理计算

  @param State   NES 状态指针
  @param Player  0 = P1, 1 = P2
**/
VOID FighterUpdate(NES_STATE *State, UINT8 Player)
{
  UINT8  Input;
  UINT8  PrevInput;
  UINT8  Pressed;
  UINT8  FState;
  UINT8  Facing;
  UINT16 XAddr, YAddr, StateAddr, FaceAddr;
  INT16  X, Y;

  if (Player == 0) {
    Input     = State->Ram[0x00F4];
    PrevInput = State->Ram[0x00F6];
    XAddr     = 0x0400;
    YAddr     = 0x0401;
    StateAddr = 0x0406;
    FaceAddr  = 0x0404;
  } else {
    Input     = 0;  /* P2: AI 控制在 sub_D724 中处理 */
    PrevInput = 0;
    XAddr     = 0x0402;
    YAddr     = 0x0403;
    StateAddr = 0x0407;
    FaceAddr  = 0x0405;
  }

  Pressed = Input & ~PrevInput;
  FState  = State->Ram[StateAddr];
  Facing  = State->Ram[FaceAddr];
  X       = (INT16)State->Ram[XAddr];
  Y       = (INT16)State->Ram[YAddr];

  /* KO 状态不处理输入 */
  if (FState == FSTATE_KO) {
    return;
  }

  /* Hitstun 计时 (简化: 20 帧恢复) */
  if (FState == FSTATE_HITSTUN) {
    UINT8 Timer = State->Ram[0x0410 + Player];
    if (Timer > 0) {
      State->Ram[0x0410 + Player] = Timer - 1;
    } else {
      State->Ram[StateAddr] = FSTATE_IDLE;
    }
    return;
  }

  /* 攻击状态计时 (时长=该态烘焙总时长, 播完回站立) */
  if (FState == FSTATE_ATTACK || FState == FSTATE_ATTACK2) {
    UINT8 Timer = State->Ram[0x0412 + Player];
    if (Timer > 0) {
      State->Ram[0x0412 + Player] = Timer - 1;
    } else {
      State->Ram[StateAddr] = FSTATE_IDLE;
    }
    return;
  }

  /* 移动 */
  if (Input & NES_BTN_RIGHT) {
    if (X < 240) X += 2;
    State->Ram[StateAddr] = FSTATE_WALK;
  } else if (Input & NES_BTN_LEFT) {
    if (X > 16) X -= 2;
    State->Ram[StateAddr] = FSTATE_WALK;
  } else if (Input & NES_BTN_UP) {
    /* 跳跃 (简化: Y 上移后重力回落) */
    if (Y >= 180) {
      State->Ram[StateAddr] = FSTATE_JUMP;
      State->Ram[0x0414 + Player] = 12;  /* 跳跃帧计数 */
    }
  } else if (Input & NES_BTN_DOWN) {
    State->Ram[StateAddr] = FSTATE_CROUCH;
  } else {
    State->Ram[StateAddr] = FSTATE_IDLE;
  }

  /* 跳跃物理 */
  if (FState == FSTATE_JUMP) {
    UINT8 JumpTimer = State->Ram[0x0414 + Player];
    if (JumpTimer > 6) {
      Y -= 4;  /* 上升 */
    } else if (JumpTimer > 0) {
      Y += 4;  /* 下落 */
    }
    if (JumpTimer > 0) {
      State->Ram[0x0414 + Player] = JumpTimer - 1;
    }
    if (Y >= 200) {
      Y = 200;
      State->Ram[StateAddr] = FSTATE_IDLE;
    }
  }

  /* 攻击 (轻=A->原厂0x32, 重=B->0x28; 计时=烘焙总时长, 播完回站立) */
  if (Pressed & NES_BTN_A) {
    State->Ram[StateAddr] = FSTATE_ATTACK;
    State->Ram[0x0412 + Player] = FsdAttackDur(
      State->Ram[(Player == 0) ? 0x003B : 0x003C], FSTATE_ATTACK);
  } else if (Pressed & NES_BTN_B) {
    State->Ram[StateAddr] = FSTATE_ATTACK2;
    State->Ram[0x0412 + Player] = FsdAttackDur(
      State->Ram[(Player == 0) ? 0x003B : 0x003C], FSTATE_ATTACK2);
  }

  State->Ram[XAddr] = (UINT8)X;
  State->Ram[YAddr] = (UINT8)Y;

  (VOID)Facing;
}

/**
  角色精灵渲染 - 将当前动画帧写入 OAM

  每个角色用 3×4 = 12 个 8x8 精灵组成 (24×32 像素)。
  不同状态使用不同的 tile 偏移和布局:
  - IDLE: 标准站立, 每 30 帧微微起伏
  - WALK: 交替步态 (每 8 帧切换)
  - ATTACK: 出拳/踢腿伸展 (宽 4 列)
  - HITSTUN: 闪烁 (每 2 帧交替可见)
  - KO: 倒地 (水平布局)
  - JUMP: 紧凑姿态
  - CROUCH: 蹲下 (隐藏顶行)

  P1 使用 OAM[0..15], P2 使用 OAM[16..31]。

  @param State   NES 状态指针
  @param Player  0 = P1, 1 = P2
**/
VOID FighterRenderSprites(NES_STATE *State, UINT8 Player)
{
  UINT8  FighterId;
  UINT8  TileBase;
  UINT8  OamBase;
  UINT8  X, Y;
  UINT8  Facing;
  UINT8  FState;
  UINT8  Attr;
  UINT8  Row, Col;
  UINT8  OamIdx;
  UINT8  TileOffset;
  UINT8  Cols, Rows;
  UINT8  AnimFrame;
  UINT8  YOffset;
  BOOLEAN Visible;

  FighterId = (Player == 0) ?
    State->Ram[0x003B] : State->Ram[0x003C];

  if (FighterId >= FIGHTER_ID_COUNT) {
    return;
  }

  TileBase = gFighterTileBase[FighterId];
  OamBase  = (Player == 0) ? OAM_P1_BASE : OAM_P2_BASE;

  X      = (Player == 0) ? State->Ram[0x0400] : State->Ram[0x0402];
  Y      = (Player == 0) ? State->Ram[0x0401] : State->Ram[0x0403];
  Facing = (Player == 0) ? State->Ram[0x0404] : State->Ram[0x0405];
  FState = (Player == 0) ? State->Ram[0x0406] : State->Ram[0x0407];

  /* 属性: P1 = sprite palette 0, P2 = sprite palette 2 (翻译自 sub_E783)
     水平翻转 = Facing */
  Attr = (Player == 0) ? 0x00 : 0x02;
  if (Facing == 1) {
    Attr |= 0x40;  /* 水平翻转 */
  }

  /* 动画帧计数 (用于周期性动画) */
  AnimFrame = (UINT8)(State->FrameCount & 0xFF);

  /* 默认尺寸: 3 列 × 4 行 */
  Cols = 3;
  Rows = 4;
  YOffset = 0;
  Visible = TRUE;
  TileOffset = 0;

  switch (FState) {
    case FSTATE_IDLE:
      /* 微微起伏: 每 30 帧 Y 偏移 ±1 */
      YOffset = (AnimFrame % 60 < 30) ? 0 : 1;
      break;

    case FSTATE_WALK:
      /* 步态动画: 每 8 帧切换 tile 偏移 */
      TileOffset = (AnimFrame % 16 < 8) ? 0 : 12;
      break;

    case FSTATE_ATTACK:
      /* 攻击: 使用攻击帧 tile (偏移 +24), 宽 4 列 */
      Cols = 4;
      TileOffset = 24;
      break;

    case FSTATE_HITSTUN:
      /* 受击闪烁: 每 2 帧交替可见 */
      Visible = (AnimFrame & 2) != 0;
      TileOffset = 48;  /* 受击姿态 tile */
      break;

    case FSTATE_KO:
      /* KO 倒地: 水平布局 (4 列 × 2 行), 下移到地面 */
      Cols = 4;
      Rows = 2;
      YOffset = 16;  /* 下移 (倒地) */
      TileOffset = 60;  /* KO 姿态 tile */
      break;

    case FSTATE_JUMP:
      /* 跳跃: 紧凑 2×3 */
      Cols = 2;
      Rows = 3;
      TileOffset = 72;
      break;

    case FSTATE_CROUCH:
      /* 蹲下: 3×3 (隐藏顶行) */
      Rows = 3;
      YOffset = 8;  /* 下移 8px */
      TileOffset = 84;
      break;

    default:
      break;
  }

  if (!Visible) {
    /* 闪烁隐藏: 全部 sprite Y=0xFF */
    OamIdx = OamBase;
    while (OamIdx < OamBase + 16) {
      State->Ppu.Oam[OamIdx * 4 + 0] = 0xFF;
      OamIdx++;
    }
    return;
  }

  /* 渲染精灵网格 */
  OamIdx = OamBase;
  for (Row = 0; Row < Rows; Row++) {
    for (Col = 0; Col < Cols; Col++) {
      if (OamIdx >= OamBase + 16) break;
      if (OamIdx >= 64) break;

      /* OAM: Y, Tile, Attr, X
         原厂 P1 tile 范围 0x00-0x7F (bank $6002), P2 tile += 0x80 (bank $6003)
         翻译自 sub_E4B3 OAM builder: P2 时 ADC #$80

         简单顺序 tile 布局: 行优先, 每行 Cols 个 tile。
         原厂从 PRG 帧数据表读取精确 tile 号和 X/Y 偏移,
         此处为简化近似。 */
      {
        UINT8 TileNum = (UINT8)(TileBase + TileOffset + Row * Cols + Col);
        TileNum &= 0x7F;  /* 确保 P1 tile 在 0-127 范围内 */
        if (Player == 1) {
          TileNum |= 0x80;  /* P2: 映射到 $1800-$1FFF (第二个 2KB bank) */
        }
        State->Ppu.Oam[OamIdx * 4 + 0] = (UINT8)(Y - 32 + YOffset + Row * 8);
        State->Ppu.Oam[OamIdx * 4 + 1] = TileNum;
        State->Ppu.Oam[OamIdx * 4 + 2] = Attr;
      }
      if (Facing == 1) {
        State->Ppu.Oam[OamIdx * 4 + 3] = (UINT8)(X + (Cols - 1 - Col) * 8);
      } else {
        State->Ppu.Oam[OamIdx * 4 + 3] = (UINT8)(X + Col * 8);
      }
      OamIdx++;
    }
  }

  /* 剩余精灵隐藏 */
  while (OamIdx < OamBase + 16) {
    State->Ppu.Oam[OamIdx * 4 + 0] = 0xFF;
    OamIdx++;
  }
}

/**
  AABB 碰撞检测 - 判断攻击方 hitbox 是否命中防御方 hurtbox

  翻译自 ASM 中的碰撞判定函数。
  攻击方 hitbox: 前方 24px 宽 × 24px 高 (从角色中心偏移)
  防御方 hurtbox: 身体 16px 宽 × 32px 高

  @param State     NES 状态指针
  @param Attacker  攻击方 (0 = P1, 1 = P2)
  @param Defender  防御方 (0 = P1, 1 = P2)
  @retval TRUE     命中
  @retval FALSE    未命中
**/
BOOLEAN FighterCheckHit(NES_STATE *State, UINT8 Attacker, UINT8 Defender)
{
  UINT8 AtkX, AtkY, AtkFacing, AtkState;
  UINT8 DefX, DefY;
  INT16 HitX1, HitX2, HitY1, HitY2;
  INT16 HurtX1, HurtX2, HurtY1, HurtY2;

  /* 攻击方数据 */
  if (Attacker == 0) {
    AtkX = State->Ram[0x0400];
    AtkY = State->Ram[0x0401];
    AtkFacing = State->Ram[0x0404];
    AtkState = State->Ram[0x0406];
  } else {
    AtkX = State->Ram[0x0402];
    AtkY = State->Ram[0x0403];
    AtkFacing = State->Ram[0x0405];
    AtkState = State->Ram[0x0407];
  }

  /* 必须在攻击状态 */
  if (AtkState != FSTATE_ATTACK) {
    return FALSE;
  }

  /* 防御方数据 */
  if (Defender == 0) {
    DefX = State->Ram[0x0400];
    DefY = State->Ram[0x0401];
  } else {
    DefX = State->Ram[0x0402];
    DefY = State->Ram[0x0403];
  }

  /* 攻击 hitbox: 角色前方 24px, 垂直中心 ±12px */
  if (AtkFacing == 0) {  /* 面朝右 */
    HitX1 = (INT16)AtkX + 16;
    HitX2 = (INT16)AtkX + 40;
  } else {  /* 面朝左 */
    HitX1 = (INT16)AtkX - 16;
    HitX2 = (INT16)AtkX + 8;
  }
  HitY1 = (INT16)AtkY - 28;
  HitY2 = (INT16)AtkY - 4;

  /* 防御 hurtbox: 身体 16px 宽, 32px 高 */
  HurtX1 = (INT16)DefX;
  HurtX2 = (INT16)DefX + 16;
  HurtY1 = (INT16)DefY - 32;
  HurtY2 = (INT16)DefY;

  /* AABB 重叠检测 */
  if (HitX1 < HurtX2 && HitX2 > HurtX1 &&
      HitY1 < HurtY2 && HitY2 > HurtY1) {
    return TRUE;
  }
  return FALSE;
}

/**
  伤害结算 - 扣减 HP, 血条追赶值由 common_frame_update 处理

  翻译自 ASM 中 ram_damage ($040D) 相关处理。
  HP 地址: P1 = $0510, P2 = $0540。
  血条显示值: P1 = $0062, P2 = $0063 (缓慢追赶实际 HP)。

  @param State   NES 状态指针
  @param Target  受击方 (0 = P1, 1 = P2)
  @param Damage  伤害值
**/
VOID FighterApplyDamage(NES_STATE *State, UINT8 Target, UINT8 Damage)
{
  UINT16 HpAddr;
  UINT8  BarAddr;

  HpAddr  = (Target == 0) ? 0x0510 : 0x0540;
  BarAddr = (Target == 0) ? 0x0062 : 0x0063;

  if (State->Ram[HpAddr] > Damage) {
    State->Ram[HpAddr] -= Damage;
  } else {
    State->Ram[HpAddr] = 0;
  }

  (VOID)BarAddr;  /* 血条追赶在 common_frame_update 中处理 */

  DEBUG((DEBUG_INFO, "[SFC3] Damage: P%d -%d, HP=%d\n",
         Target + 1, Damage, State->Ram[HpAddr]));
}
