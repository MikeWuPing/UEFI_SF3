/** @file
  角色精灵渲染 - 忠实翻译自 bank_FF.asm sub_E4E8 ($E4E8) 的精灵发射循环。

  原厂 sub_E4E8 对当前帧记录的每个精灵三元组 [xd, yd, tile] 执行:
    xd: ASL; ROL ram_005E; LSR  -> 把 xd.bit7 滚入属性累加器, A = xd & 0x7F
    OAM_X = screenX +/- (xd & 0x7F)   (朝右加, 朝左减; 16-bit, 高字节!=0 = 屏幕外裁剪)
    yd: ASL; ROL ram_005E; LSR  -> 把 yd.bit7 滚入累加器, A = yd & 0x7F
    OAM_Y = screenY + (yd & 0x7F)     (16-bit, 高字节!=0 = 裁剪)
    tile: P2(ram_0050 & 0x80) -> tile+0x80, attr = ram_005E+2;  P1 -> tile, attr = ram_005E
    attr &= 0x03; 朝左分支再 ORA 0x40 (水平翻转)
  ram_005E & 3 == (xd.bit7 << 1) | yd.bit7  (即每精灵 2-bit 调色板选择)。
  本函数对原始字节 1:1 复现上述位逻辑 (acc 每角色清零, 忽略原厂跨帧的 1 像素脏位)。

  屏幕外-X 分支原厂只做 1 次 ROL (怪癖), 此处照抄: 该路径不再滚入 yd.bit7。
  OAM 采用共享竞技场: 由调用方传入起始槽, 返回下一空闲槽 (单帧可 >16 精灵)。
**/

#include "fighter_sprite.h"
#include "fighter.h"
#include "fighter_sprite_data.h"
#include "ppu.h"
#include <Library/BaseMemoryLib.h>

/* 角色锚点 -> 屏幕坐标的呈现层偏移 (记录 yd 自锚点向下; 标定使脚部落在地面, 截图校) */
#define FSD_ANCHOR_Y  40
#define FSD_ANCHOR_X  0

/* 每角色 RAM 地址 (与 fighter.c 一致) */
#define P_X0     0x0400
#define P_Y0     0x0401
#define P_FACE0  0x0404
#define P_STATE0 0x0406
#define P_X1     0x0402
#define P_Y1     0x0403
#define P_FACE1  0x0405
#define P_STATE1 0x0407

/* 动画计时器 (本应用单实例, 用静态变量保存每角色游标) */
STATIC UINT8 sTick[2]     = { 0, 0 };
STATIC UINT8 sIdx[2]      = { 0, 0 };
STATIC UINT8 sLastState[2] = { 0xFF, 0xFF };

VOID
FighterRenderRecord (
  NES_STATE *State,
  UINT8      Player,
  UINT8      OamStart,
  UINT8     *OamNext,
  UINT8     *FrameIdOut
  )
{
  UINT8  FighterId;
  UINT8  FState;
  UINT8  Facing;
  UINT8  IsP2;
  INT16  ScreenX;
  INT16  ScreenY;
  UINT8  PosX, PosY;
  CONST SFC3_FSD_LOOP *Loop;
  UINT8  N, Hold;
  UINT8  LoopFlag;
  UINT8  Frame;
  UINT32 P, Base;
  UINT8  Count, i;
  UINT8  Acc;
  UINT8  Oam;
  UINT8 *OamBuf;

  FighterId = (Player == 0) ? State->Ram[0x003B] : State->Ram[0x003C];
  if (FighterId >= FIGHTER_ID_COUNT) {
    *OamNext = OamStart;
    *FrameIdOut = 0;
    return;
  }

  if (Player == 0) {
    PosX   = State->Ram[P_X0];
    PosY   = State->Ram[P_Y0];
    Facing = State->Ram[P_FACE0];
    FState = State->Ram[P_STATE0];
  } else {
    PosX   = State->Ram[P_X1];
    PosY   = State->Ram[P_Y1];
    Facing = State->Ram[P_FACE1];
    FState = State->Ram[P_STATE1];
  }
  IsP2 = (Player == 1) ? 1 : 0;
  if (FState >= SFC3_FSD_FSTATE) {
    FState = FSTATE_IDLE;
  }

  /* 状态切换时重置动画游标 */
  if (sLastState[Player] != FState) {
    sLastState[Player] = FState;
    sIdx[Player] = 0;
    sTick[Player] = 0;
  }

  /* 选帧: 按当前 FState 的 循环/单次 序列推进计时器
     loop (idle/walk): 循环; 单次 (attack/hit/ko): 推进一次后停留末帧 (收招/击倒保持) */
  Loop = &gFsdAnim[FighterId][FState];
  N = Loop->count;
  LoopFlag = (UINT8)(Loop->flags & 1);
  if (N == 0) {
    Frame = 0;
    Hold  = 1;
  } else {
    if (sIdx[Player] >= N) {
      sIdx[Player] = (UINT8)(N - 1);
    }
    Frame = Loop->ent[sIdx[Player]][0];
    Hold  = Loop->ent[sIdx[Player]][1];
    if (Hold == 0) {
      Hold = 1;
    }
    sTick[Player]++;
    if (sTick[Player] >= Hold) {
      sTick[Player] = 0;
      if (LoopFlag != 0) {
        sIdx[Player] = (UINT8)((sIdx[Player] + 1) % N);
      } else if (sIdx[Player] < (UINT8)(N - 1)) {
        sIdx[Player] = (UINT8)(sIdx[Player] + 1);
      }
      /* 否则停留末帧 */
    }
  }
  if (Frame >= gFsdNFrames[FighterId]) {
    Frame = 0;
  }
  *FrameIdOut = Frame;

  /* 屏幕坐标 (16-bit 以支持裁剪) */
  /* 水平锚点校正符号随朝向翻转: 镜像分支 xd 取负, bbox 偏移反向, 故朝左用 +anchorX */
  if (Facing == 1) {
    ScreenX = (INT16)PosX + (INT16)gFsdAnchorX[FighterId];
  } else {
    ScreenX = (INT16)PosX - (INT16)gFsdAnchorX[FighterId];
  }
  ScreenY = (INT16)PosY - (INT16)gFsdAnchorY[FighterId];

  /* 取该帧记录字节流 */
  Base  = gFsdFrameBase[FighterId] + gFsdFrameOff[FighterId][Frame];
  Count = gFsdFrameData[Base];
  P     = Base + 1;

  OamBuf = State->Ppu.Oam;
  Oam    = OamStart;
  Acc    = 0;  /* ram_005E 累加器, 每角色清零 */

  for (i = 0; i < Count; i++) {
    UINT8  Xd, Yd, Tile;
    UINT8  XdLow, YdLow;
    UINT8  C;
    INT16  X16;
    INT16  Y16;
    UINT8  AttrV, Attr;

    if (P + 3 > (UINT32)sizeof (gFsdFrameData)) {
      break;  /* 越界保护 */
    }
    Xd = gFsdFrameData[P];

    /* xd: ASL; ROL acc; LSR  =>  C = xd.bit7; acc 左移并入 C; XdLow = xd & 0x7F */
    C = (UINT8)((Xd >> 7) & 1);
    Acc = (UINT8)(((Acc << 1) | C) & 0xFF);
    XdLow = (UINT8)(Xd & 0x7F);

    /* OAM_X = screenX +/- XdLow (朝左减); 16-bit 裁剪 */
    if (Facing == 1) {
      X16 = ScreenX - (INT16)XdLow;
    } else {
      X16 = ScreenX + (INT16)XdLow;
    }
    if (X16 < 0 || X16 > 255) {
      /* 屏幕外-X: 原厂此路径只滚入 xd.bit7, 跳过 yd 的 ROL; 消耗整组三元组 */
      P += 3;
      continue;
    }

    Yd = gFsdFrameData[P + 1];
    /* yd: ASL; ROL acc; LSR */
    C = (UINT8)((Yd >> 7) & 1);
    Acc = (UINT8)(((Acc << 1) | C) & 0xFF);
    YdLow = (UINT8)(Yd & 0x7F);

    /* OAM_Y = screenY + YdLow; 16-bit 裁剪 */
    Y16 = ScreenY + (INT16)YdLow;
    if (Y16 < 0 || Y16 > 255) {
      P += 3;
      continue;
    }

    Tile = gFsdFrameData[P + 2];
    P += 3;

    /* tile 与属性: P2 -> tile+0x80, attr = acc+2;  P1 -> tile, attr = acc */
    if (IsP2) {
      Tile = (UINT8)((Tile + 0x80) & 0xFF);
      AttrV = (UINT8)((Acc + 2) & 0xFF);
    } else {
      AttrV = Acc;
    }
    Attr = (UINT8)(AttrV & 0x03);
    if (Facing == 1) {
      Attr |= 0x40;  /* 朝左: 水平翻转 (mirrored 分支 ORA #$40) */
    }

    if (Oam >= 64) {
      break;  /* 竞技场满 (原厂 CMP ram_0045) */
    }
    OamBuf[Oam * 4 + 0] = (UINT8)(Y16 & 0xFF);   /* OAM Y (ppu.c 渲染时 +1) */
    OamBuf[Oam * 4 + 1] = Tile;
    OamBuf[Oam * 4 + 2] = Attr;
    OamBuf[Oam * 4 + 3] = (UINT8)(X16 & 0xFF);   /* OAM X */
    Oam++;
  }

  *OamNext = Oam;
}

/**
  VS 对决立绘渲染 - 翻译自 sub_F52C + sub_E4E8 (哑角色 id 9 的 metasprite)。

  与 FighterRenderRecord 相同的三元组位逻辑, 但无 anchor/无动画: 以 (Ox,Oy) 为原点。
  facing=1 (P2) -> X = Ox - (xd&0x7F), tile += 0x80 ($6003 窗), attr = (acc+2)&3 | 0x40。
  PPUCTRL bit3=1 (SpBase=$1000), 故 tile 0x80+ 读 $1800 窗 = ChrBanks[3] = $6003。
**/
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
  )
{
  UINT8  i;
  UINT8  acc;
  UINT8  oam;
  UINT8  xd, yd, tile;
  UINT8  c, xdl, ydl;
  UINT8  attrv, attr;
  INT16  X16, Y16;
  UINT8 *OamBuf;

  OamBuf = State->Ppu.Oam;
  oam = OamStart;
  acc = 0;
  for (i = 0; i < Cnt; i++) {
    xd   = Rec[i][0];
    yd   = Rec[i][1];
    tile = Rec[i][2];

    c = (UINT8)((xd >> 7) & 1);
    acc = (UINT8)(((acc << 1) | c) & 0xFF);
    xdl = (UINT8)(xd & 0x7F);
    if (Facing == 1) {
      X16 = Ox - (INT16)xdl;
    } else {
      X16 = Ox + (INT16)xdl;
    }
    if (X16 < 0 || X16 > 255) {
      continue;
    }

    c = (UINT8)((yd >> 7) & 1);
    acc = (UINT8)(((acc << 1) | c) & 0xFF);
    ydl = (UINT8)(yd & 0x7F);
    Y16 = Oy + (INT16)ydl;
    if (Y16 < 0 || Y16 > 255) {
      continue;
    }

    if (Player == 1) {
      tile = (UINT8)((tile + 0x80) & 0xFF);
      attrv = (UINT8)((acc + 2) & 0xFF);
    } else {
      attrv = acc;
    }
    attr = (UINT8)(attrv & 0x03);
    if (Facing == 1) {
      attr |= 0x40;  /* 水平翻转 */
    }

    if (oam >= 64) {
      break;
    }
    OamBuf[oam * 4 + 0] = (UINT8)(Y16 & 0xFF);
    OamBuf[oam * 4 + 1] = tile;
    OamBuf[oam * 4 + 2] = attr;
    OamBuf[oam * 4 + 3] = (UINT8)(X16 & 0xFF);
    oam++;
  }

  *OamNext = oam;
}

/**
  返回某 FState 烘焙时间线的总时长 (各帧 hold 之和)。

  用作攻击/硬直的呈现层计时: 单次序列播完 (计时归零) 即回站立,
  等价于原厂 sub_E4B3 终止符 $FF/$F8 置 ram_0300|$40 后下一 tick 的收招 latch。
**/
UINT8
FsdAttackDur (
  UINT8 Fighter,
  UINT8 FState
  )
{
  UINT8 i, n, sum;
  CONST SFC3_FSD_LOOP *L;

  if (Fighter >= FIGHTER_ID_COUNT || FState >= SFC3_FSD_FSTATE) {
    return 12;
  }
  L = &gFsdAnim[Fighter][FState];
  n = L->count;
  sum = 0;
  for (i = 0; i < n; i++) {
    UINT8 h = L->ent[i][1];
    sum = (UINT8)(sum + (h != 0 ? h : 1));
    if (sum > 200) {
      break;
    }
  }
  return (sum != 0) ? sum : 12;
}
