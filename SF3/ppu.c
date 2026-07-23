/** @file
  NES PPU 模拟实现 - 寄存器写入、Mapper 91 bank 切换、tile 解码、帧渲染

  本模块模拟 NES PPU 的核心行为：CPU 侧寄存器读写（$2000-$2007, $4014）、
  Mapper 91 (JY Company / HK-SF3 盗版 mapper) 的 CHR/PRG bank 切换、
  以及每帧的 Nametable 背景渲染与 OAM Sprite 渲染。所有像素输出到
  palette-indexed FrameBuffer，后续由 gop_render 模块转换为 GOP BGR 输出。
**/

#include "ppu.h"
#include "resource.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

/* NES 系统调色板 → 显示 RGB 预计算表 (UINT32 = 0x00RRGGBB, 与 GopPresent/BLT 字节序一致;
   注释早期误标 BBGGRR)。基准 = 标准 NES 表, 其中 10 个索引用参考截图 compare/2.png(选择)
   /4.png(战斗) 的实测像素覆盖, 使选择+战斗的大块配色 (navy 墙/深红毯/品红帷幕/蓝绿象+柱/
   金饰+金地/黑选择底/橄榄/肤色) 贴近参考观感; 其余回退标准值 (含蓝描边 0x11/白 0x30 本就正确)。
   仅改 索引→显示RGB 映射, 不改游戏数据 (调色板 RAM 索引仍来自 ROM)。 */
CONST UINT32 gNesPaletteBGR[64] = {
  0x007C7C7C, 0x000000FC, 0x000000BC, 0x004428BC,  /* 00-03 标准 */
  0x00940084, 0x00600C18, 0x00A81000, 0x00D49068,  /* 04 标准,05 采样深红毯,06 标准,07 采样肤色 */
  0x001E2B00, 0x00007800, 0x00006800, 0x00005800,  /* 08 采样橄榄,09-0B 标准 */
  0x00002B55, 0x00000000, 0x00000000, 0x00000000,  /* 0C 采样navy,0D-0E 标准,0F 采样黑 */
  0x00BCBCBC, 0x004060F8, 0x004040FC, 0x007828FC,  /* 10-13 标准 (11=蓝描边) */
  0x00A800E4, 0x00A83758, 0x00C01040, 0x00B81800,  /* 14 标准,15 采样品红帷幕,16-17 标准 */
  0x00883800, 0x00589800, 0x0000A800, 0x0000A844,  /* 18-1B 标准 */
  0x0000648B, 0x00000000, 0x00000000, 0x00000000,  /* 1C 采样蓝绿象,1D-1F 标准 */
  0x00FCFCFC, 0x007888FC, 0x00A078F8, 0x00B878FC,  /* 20 白,21-23 标准 */
  0x00F858F8, 0x00F858C0, 0x00F86078, 0x00D4A023,  /* 24-26 标准,27 采样金地 */
  0x00B1A912, 0x0088B800, 0x0050D800, 0x0030E058,  /* 28 采样金饰,29 标准,2A 血条绿,2B 标准 */
  0x00177CA0, 0x00787878, 0x00000000, 0x00000000,  /* 2C 采样蓝柱,2D-2F 标准 */
  0x00FCFCFC, 0x00B8C8FC, 0x00C8C8FC, 0x00D8B8FC,  /* 30 白,31-33 标准 */
  0x00F8B8F8, 0x00F8A4C0, 0x00F0B0B8, 0x00FCC8A8,  /* 34-37 标准 */
  0x00F8D878, 0x00D8F878, 0x00B8F878, 0x00A8FCA0,  /* 38-3B 标准 */
  0x0098F8C8, 0x00D8D8D8, 0x00000000, 0x00000000,  /* 3C-3F 标准 */
};

/**
  PPU $2000 (PPUCTRL) 写入

  Bit 2 控制 VRAM 地址增量：1=32（垂直滚动），0=1（水平滚动）。
  Bit 0-1 选择基础 Nametable 地址，Bit 3-4 Sprite 模式等。
**/
VOID PpuWriteCtrl(NES_STATE *State, UINT8 Val)
{
  State->Ppu.Ctrl = Val;
  State->Ppu.VramInc = (Val & 0x04) ? 32 : 1;
}

/**
  PPU $2001 (PPUMASK) 写入 - 渲染使能控制
**/
VOID PpuWriteMask(NES_STATE *State, UINT8 Val)
{
  State->Ppu.Mask = Val;
}

/**
  PPU $2005 滚动寄存器写入 - 双次写入锁存

  第一次写入设置水平滚动 (ScrollX)，第二次设置垂直滚动 (ScrollY)。
  通过 ScrollLatch 标志区分两次写入。
**/
VOID PpuWriteScroll(NES_STATE *State, UINT8 Val)
{
  if (State->Ppu.ScrollLatch == 0) {
    State->Ppu.ScrollX = Val;
    State->Ppu.ScrollLatch = 1;
  } else {
    State->Ppu.ScrollY = Val;
    State->Ppu.ScrollLatch = 0;
  }
}

/**
  PPU $2006 (PPUADDR) 写入 - 双次写入设置 14-bit VRAM 地址

  第一次写入高字节（& 0x3F 屏蔽到 14-bit），第二次写入低字节。
**/
VOID PpuWriteAddr(NES_STATE *State, UINT8 Val)
{
  if (State->Ppu.AddrLatch == 0) {
    State->Ppu.VramAddr = (UINT16)((Val & 0x3F) << 8);
    State->Ppu.AddrLatch = 1;
  } else {
    State->Ppu.VramAddr = (UINT16)((State->Ppu.VramAddr & 0xFF00) | Val);
    State->Ppu.AddrLatch = 0;
  }
}

/**
  PPU $2007 (PPUDATA) 写入

  根据当前 VramAddr 写入调色板 RAM (0x3F00-0x3F1F) 或
  Nametable (0x2000-0x2FFF，含垂直镜像)。写入后 VramAddr 按
  VramInc (1 或 32) 递增。
**/
VOID PpuWriteData(NES_STATE *State, UINT8 Val)
{
  UINT16 Addr = State->Ppu.VramAddr;

  if (Addr >= 0x3F00 && Addr < 0x3F20) {
    /* 调色板 RAM 写入 */
    UINT16 PalIdx = (Addr - 0x3F00) & 0x1F;
    State->Ppu.Palette[PalIdx] = Val & 0x3F;
  } else if (Addr >= 0x2000 && Addr < 0x3000) {
    /* Nametable 写入 - 垂直镜像: 0x2000-0x23FF = NT0, 0x2400-0x27FF = NT1,
       0x2800-0x2BFF mirrors NT0, 0x2C00-0x2FFF mirrors NT1 */
    UINT16 Offset = (Addr - 0x2000) & 0x07FF;
    if (Offset < PPU_NAMETABLE_SIZE) {
      State->Ppu.Nametable[0][Offset] = Val;
    } else {
      State->Ppu.Nametable[1][Offset - PPU_NAMETABLE_SIZE] = Val;
    }
  }

  State->Ppu.VramAddr += State->Ppu.VramInc;
  State->Ppu.VramAddr &= 0x3FFF;
}

/**
  PPU $2007 (PPUDATA) 读取

  从调色板 RAM 或 Nametable 读取数据，读取后 VramAddr 递增。
  注意：真实 NES 中非调色板区域有 1 次读取延迟（内部缓冲区），
  此处简化为直接读取。
**/
UINT8 PpuReadData(NES_STATE *State)
{
  UINT16 Addr = State->Ppu.VramAddr;
  UINT8 Result = 0;

  if (Addr >= 0x3F00 && Addr < 0x3F20) {
    UINT16 PalIdx = (Addr - 0x3F00) & 0x1F;
    Result = State->Ppu.Palette[PalIdx] & 0x3F;
  } else if (Addr >= 0x2000 && Addr < 0x3000) {
    UINT16 Offset = (Addr - 0x2000) & 0x07FF;
    if (Offset < PPU_NAMETABLE_SIZE) {
      Result = State->Ppu.Nametable[0][Offset];
    } else {
      Result = State->Ppu.Nametable[1][Offset - PPU_NAMETABLE_SIZE];
    }
  }

  State->Ppu.VramAddr += State->Ppu.VramInc;
  State->Ppu.VramAddr &= 0x3FFF;
  return Result;
}

/**
  PPU $2002 (PPUSTATUS) 读取

  返回状态寄存器，同时清除 VBlank 标志 (bit 7) 并重置
  AddrLatch 和 ScrollLatch（真实硬件行为）。
**/
UINT8 PpuReadStatus(NES_STATE *State)
{
  UINT8 Result = State->Ppu.Status;
  State->Ppu.Status &= 0x7F;  /* 清除 VBlank 标志 */
  State->Ppu.AddrLatch = 0;
  State->Ppu.ScrollLatch = 0;
  return Result;
}

/**
  OAM DMA ($4014) - 从 CPU RAM 页复制 256 字节到 OAM

  CPU 写入 $4014 = Page 后，将 Ram[Page*0x100..Page*0x100+255]
  整体传输到 PPU OAM 缓冲区，用于 Sprite 显示。
**/
VOID PpuOamDma(NES_STATE *State, UINT8 Page)
{
  UINT16 SrcAddr = (UINT16)(Page * 0x100);
  if (SrcAddr + 256 <= 0x800) {
    CopyMem(State->Ppu.Oam, &State->Ram[SrcAddr], 256);
  }
}

/**
  Mapper 91 寄存器写入

  JY Company / HK-SF3 盗版 mapper 的 bank 切换寄存器：
  $6000-$6003: CHR bank 选择 (4 x 2KB)，bit5-0 有效 (0-63)
  $7000-$7001: PRG bank 选择 (2 x 8KB)
  $7006:       IRQ 控制/确认 (本项目忽略)

  注意：不能用 switch(Addr & 0x7003) 因为 $7006 & 0x7003 = 0x7002
  会产生错误映射。
**/
VOID Mapper91Write(NES_STATE *State, UINT16 Addr, UINT8 Val)
{
  if (Addr >= 0x6000 && Addr <= 0x6003) {
    State->ChrBanks[Addr - 0x6000] = Val;  /* 8-bit bank (0-255), 512KB CHR = 256×2KB */
  } else if (Addr == 0x7000) {
    State->PrgBanks[0] = Val;
  } else if (Addr == 0x7001) {
    State->PrgBanks[1] = Val;
  } else if (Addr == 0x7006) {
    /* IRQ acknowledge - 忽略（声音相关，本项目不实现） */
  }
}

/**
  CHR ROM 字节读取 - 通过 Mapper 91 的 4x2KB CHR bank 映射

  PPU 地址空间 $0000-$1FFF 被分为 4 个 2KB 窗口：
  $0000-$07FF → ChrBanks[0]
  $0800-$0FFF → ChrBanks[1]
  $1000-$17FF → ChrBanks[2]
  $1800-$1FFF → ChrBanks[3]

  每个 bank 为 2KB = 2048 字节，bank 号 0-63 对应 CHR ROM 偏移。
**/
UINT8 ChrRead(NES_STATE *State, UINT16 PpuAddr)
{
  UINT16 BankIdx;
  UINT16 Offset;
  UINT32 ChrOffset;
  UINT8 BankNum;

  PpuAddr &= 0x1FFF;
  BankIdx = PpuAddr / 0x0800;      /* 0-3: 选择 2KB bank */
  Offset = PpuAddr % 0x0800;       /* bank 内偏移 */
  BankNum = State->ChrBanks[BankIdx];

  /* 计算 CHR ROM 中的绝对偏移 */
  ChrOffset = (UINT32)BankNum * 0x0800 + Offset;

  if (gResources.ChrRom != NULL && ChrOffset < gResources.ChrRomSize) {
    return gResources.ChrRom[ChrOffset];
  }
  return 0;
}

/**
  解码单个 8x8 图块到 FrameBuffer

  从 CHR ROM 读取 16 字节 tile 数据（2 bitplane x 8 行），
  将 2-bit 颜色索引经调色板转换后写入 FrameBuffer。

  @param State     NES 状态
  @param ChrRom    CHR ROM 指针 (保留参数，实际通过 ChrRead 访问)
  @param TileNum   图块编号 (0-1023 per 8KB bank)
  @param BaseAddr  PPU 基地址 (0x0000 或 0x1000)
  @param PalIdx    调色板索引基址 (0x3F00 + PalIdx*4)
  @param DestX     目标 X 坐标 (像素)
  @param DestY     目标 Y 坐标 (像素)
  @param FlipH     水平翻转
  @param FlipV     垂直翻转
**/
STATIC
VOID
DecodeTile(
  NES_STATE  *State,
  UINT8      *ChrRom,
  UINT16     TileNum,
  UINT16     BaseAddr,
  UINT8      PalIdx,
  UINT16     DestX,
  UINT16     DestY,
  BOOLEAN    FlipH,
  BOOLEAN    FlipV
  )
{
  UINT16 TileAddr;
  UINT8  Row, Col;
  UINT8  Bp0, Bp1;
  UINT8  PixIdx, PalColor;
  UINT16 Px, Py;

  (VOID)ChrRom;  /* 通过 ChrRead 访问，此参数保留 */

  TileAddr = BaseAddr + TileNum * PPU_TILE_SIZE;

  for (Row = 0; Row < 8; Row++) {
    Bp0 = ChrRead(State, (UINT16)(TileAddr + Row));
    Bp1 = ChrRead(State, (UINT16)(TileAddr + Row + 8));

    Py = DestY + (FlipV ? (7 - Row) : Row);
    if (Py >= NES_SCREEN_HEIGHT) {
      continue;
    }

    for (Col = 0; Col < 8; Col++) {
      /* 从 bitplane 提取 2-bit 像素值 (bit 7 = 最左像素) */
      PixIdx = (UINT8)(((Bp1 >> (7 - Col)) & 1) << 1) |
               (UINT8)((Bp0 >> (7 - Col)) & 1);

      if (PixIdx == 0) {
        continue;  /* 透明像素 */
      }

      /* 查调色板获取 NES 系统色号 */
      PalColor = State->Ppu.Palette[(PalIdx + PixIdx) & 0x1F];

      Px = DestX + (FlipH ? (7 - Col) : Col);
      if (Px >= NES_SCREEN_WIDTH) {
        continue;
      }

      State->Ppu.FrameBuffer[Py * NES_SCREEN_WIDTH + Px] = PalColor;
    }
  }
}

/**
  PPU 帧渲染 - 背景 Nametable + Sprite

  按 NES PPU 渲染顺序：先绘制背景 (Nametable 0 的 32x30 图块)，
  再绘制 Sprite (OAM 64 个，高优先级覆盖低优先级，即索引小的在后绘制)。
  渲染结果为 palette-indexed 帧缓冲，由 gop_render 转换为 BGR 输出。

  背景图块使用 PPU 地址 $1000 的 pattern table (由 PPUCTRL bit 4 决定)，
  Sprite 图块使用 $0000 或 $1000 (由 PPUCTRL bit 3 决定)。
**/
VOID PpuRenderFrame(NES_STATE *State, UINT8 *ChrRom)
{
  UINT16 TileRow, TileCol;
  UINT16 BgBase;
  UINT16 SpBase;
  INT32  i;
  UINT8  AttrTable[64];  /* 属性表缓存 (8x8 个 2x2 属性块) */
  CONST BG_SEG *_bgSeg  = State->BgSegs;   /* 逐扫描线分段表; NULL=整屏 */
  UINT8  _bgSegIdx = 0;
  UINT8  _bgE0 = State->ChrBanks[0];       /* 入口 bank, 每行恢复用 */
  UINT8  _bgE1 = State->ChrBanks[1];

  /* 确定背景和 Sprite 的 pattern table 基地址 */
  BgBase = (State->Ppu.Ctrl & 0x10) ? 0x1000 : 0x0000;
  SpBase = (State->Ppu.Ctrl & 0x08) ? 0x1000 : 0x0000;

  /* 清空帧缓冲 (背景色 = 调色板[0]) */
  SetMem(State->Ppu.FrameBuffer, NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT,
         State->Ppu.Palette[0]);

  /* 缓存属性表 (Nametable offset 0x3C0-0x3FF) */
  CopyMem(AttrTable, &State->Ppu.Nametable[0][0x3C0], 64);

  /* 渲染背景: 32 列 x 30 行 = 960 个 tile */
  for (TileRow = 0; TileRow < 30; TileRow++) {
    UINT8 _bgS0 = _bgE0;
    UINT8 _bgS1 = _bgE1;
    if (_bgSeg != NULL) {
      /* 推进到覆盖当前行的段 (哨兵 StartRow==30 保证 _bgSegIdx+1 不越界) */
      while (_bgSeg[_bgSegIdx + 1].StartRow <= TileRow) {
        _bgSegIdx++;
      }
      _bgS0 = _bgSeg[_bgSegIdx].Bank0;
      _bgS1 = _bgSeg[_bgSegIdx].Bank1;
    }
    State->ChrBanks[0] = _bgS0;
    State->ChrBanks[1] = _bgS1;
    for (TileCol = 0; TileCol < 32; TileCol++) {
      UINT8  TileNum;
      UINT16 NamIdx;
      UINT8  AttrByte;
      UINT8  PalNum;
      UINT16 DestX, DestY;

      NamIdx = TileRow * 32 + TileCol;
      TileNum = State->Ppu.Nametable[0][NamIdx];

      /* 从属性表获取调色板号 (每 2x2 tile 块共享一个 2-bit 调色板) */
      AttrByte = AttrTable[(TileRow / 4) * 8 + (TileCol / 4)];
      if ((TileRow & 2) && (TileCol & 2)) {
        PalNum = (AttrByte >> 6) & 0x03;
      } else if (TileRow & 2) {
        PalNum = (AttrByte >> 4) & 0x03;
      } else if (TileCol & 2) {
        PalNum = (AttrByte >> 2) & 0x03;
      } else {
        PalNum = AttrByte & 0x03;
      }

      /* 应用背景滚动偏移 (有符号运算, 负值 → 大 UINT16 → 被 DecodeTile 边界裁剪) */
      DestX = (UINT16)(INT16)(TileCol * 8 - State->Ppu.ScrollX);
      DestY = (UINT16)(INT16)(TileRow * 8 - State->Ppu.ScrollY);

      /* 背景调色板从 PPU Palette[0x00] 开始, PalNum*4 为偏移 */
      DecodeTile(State, ChrRom, TileNum, BgBase,
                 PalNum * 4, DestX, DestY, FALSE, FALSE);
    }
    State->ChrBanks[0] = _bgE0;
    State->ChrBanks[1] = _bgE1;
  }

  /* 渲染 Sprite: 从 OAM 索引 63 到 0 绘制 (索引越小优先级越高，后绘制覆盖先绘制) */
  {
    BOOLEAN Sprite8x16 = (State->Ppu.Ctrl & 0x20) != 0;

    for (i = 63; i >= 0; i--) {
      UINT8   OamY, OamTile, OamAttr, OamX;
      UINT32  SpriteY;
      BOOLEAN FlipH, FlipV;
      UINT8   PalNum;

      OamY    = State->Ppu.Oam[i * 4 + 0];
      OamTile = State->Ppu.Oam[i * 4 + 1];
      OamAttr = State->Ppu.Oam[i * 4 + 2];
      OamX    = State->Ppu.Oam[i * 4 + 3];

      if (OamY == 0xFF) {
        continue;  /* Y=0xFF 表示 sprite 隐藏 */
      }

      SpriteY = (UINT32)OamY + 1;  /* NES OAM Y 坐标偏移 +1 */
      FlipH = (OamAttr & 0x40) != 0;
      FlipV = (OamAttr & 0x80) != 0;
      PalNum = OamAttr & 0x03;

      if (Sprite8x16) {
        /* 8x16 模式: Tile bit 0 选择 CHR pattern table, Tile & 0xFE 为 tile 号 */
        UINT16 ChrBank = (OamTile & 0x01) ? 0x1000 : 0x0000;
        UINT8  TopTile = OamTile & 0xFE;
        UINT8  BotTile = TopTile + 1;

        if (FlipV) {
          /* 垂直翻转时上下 tile 互换 */
          DecodeTile(State, ChrRom, BotTile, ChrBank,
                     (UINT8)(0x10 + PalNum * 4),
                     (UINT16)OamX, (UINT16)SpriteY,
                     FlipH, TRUE);
          DecodeTile(State, ChrRom, TopTile, ChrBank,
                     (UINT8)(0x10 + PalNum * 4),
                     (UINT16)OamX, (UINT16)(SpriteY + 8),
                     FlipH, TRUE);
        } else {
          DecodeTile(State, ChrRom, TopTile, ChrBank,
                     (UINT8)(0x10 + PalNum * 4),
                     (UINT16)OamX, (UINT16)SpriteY,
                     FlipH, FALSE);
          DecodeTile(State, ChrRom, BotTile, ChrBank,
                     (UINT8)(0x10 + PalNum * 4),
                     (UINT16)OamX, (UINT16)(SpriteY + 8),
                     FlipH, FALSE);
        }
      } else {
        /* 8x8 模式: PPUCTRL bit 3 选择 Sprite pattern table */
        DecodeTile(State, ChrRom, OamTile, SpBase,
                   (UINT8)(0x10 + PalNum * 4),
                   (UINT16)OamX, (UINT16)SpriteY,
                   FlipH, FlipV);
      }
    }
  }
}
