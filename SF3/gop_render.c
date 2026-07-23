/** @file
  GOP 图形输出 - 将 PPU 帧缓冲整数缩放后通过 Blt 输出到屏幕

  工作流程:
    1. GopInit   - 获取 GOP 协议, 计算缩放倍数与居中偏移, 分配后缓冲
    2. GopPresent - 将 PPU FrameBuffer (palette-indexed) 转换为 BGR 像素并
                    按 Scale×Scale 整数倍放大写入后缓冲, 最后一次 Blt 提交
    3. GopFree   - 释放后缓冲内存
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>
#include "gop_render.h"
#include "ppu.h"
#include "version.h"

/* 全局 GOP 渲染器实例 */
GOP_RENDERER gGopRenderer = { NULL, NULL, 0, 0, 0, 0, 0, FALSE };

/* 3x5 像素字体: 用于在边框区域渲染版本号
   每字符 5 行, 每行 3 bit (bit2=左, bit1=中, bit0=右)
   索引: '0'-'9'=0-9, '.'=10, 'v'=11 */
STATIC CONST UINT8 sVersionFont[12][5] = {
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
  { 0x0, 0x0, 0x0, 0x0, 0x2 },  /* . */
  { 0x0, 0x5, 0x5, 0x2, 0x2 },  /* v */
};

/**
  填充一个矩形区域 (用于版本号背后的衬底, 保证可读性)

  @param Buf     BackBuffer 指针
  @param ScrW    屏幕宽度 (像素)
  @param ScrH    屏幕高度 (像素)
  @param X0      矩形左上角 X
  @param Y0      矩形左上角 Y
  @param W       矩形宽
  @param H       矩形高
  @param Color   BGR 颜色值
**/
STATIC
VOID
FillRect (
  UINT32 *Buf,
  UINT32  ScrW,
  UINT32  ScrH,
  UINT32  X0,
  UINT32  Y0,
  UINT32  W,
  UINT32  H,
  UINT32  Color
  )
{
  UINT32 X, Y, Px, Py;

  for (Y = 0; Y < H; Y++) {
    Py = Y0 + Y;
    if (Py >= ScrH) {
      break;
    }
    for (X = 0; X < W; X++) {
      Px = X0 + X;
      if (Px < ScrW) {
        Buf[Py * ScrW + Px] = Color;
      }
    }
  }
}

/**
  在 BackBuffer 的指定像素位置渲染一个字符 (3x5 字形, 按 FS 整数倍放大)

  每个字形像素扩展为 FS x FS 的实心块, 让版本号在高分辨率下依然清晰。

  @param Buf     BackBuffer 指针
  @param ScrW    屏幕宽度 (像素)
  @param ScrH    屏幕高度 (像素)
  @param X0      字符左上角 X
  @param Y0      字符左上角 Y
  @param Ch      字符 ('0'-'9', '.', 'v')
  @param Color   BGR 颜色值
  @param FS      字形放大倍数 (>=1)
**/
STATIC
VOID
RenderChar (
  UINT32 *Buf,
  UINT32  ScrW,
  UINT32  ScrH,
  UINT32  X0,
  UINT32  Y0,
  CHAR8   Ch,
  UINT32  Color,
  UINT32  FS
  )
{
  UINT8 Idx;
  UINT8 Row, Col;
  UINT32 BX, BY, FX, FY;

  if (Ch >= '0' && Ch <= '9') {
    Idx = Ch - '0';
  } else if (Ch == '.') {
    Idx = 10;
  } else if (Ch == 'v' || Ch == 'V') {
    Idx = 11;
  } else {
    return;
  }

  if (FS < 1) {
    FS = 1;
  }

  for (Row = 0; Row < 5; Row++) {
    for (Col = 0; Col < 3; Col++) {
      if ((sVersionFont[Idx][Row] >> (2 - Col)) & 1) {
        /* 将单个字形像素扩展为 FS x FS 块 */
        for (FY = 0; FY < FS; FY++) {
          BY = Y0 + Row * FS + FY;
          if (BY >= ScrH) {
            break;
          }
          for (FX = 0; FX < FS; FX++) {
            BX = X0 + Col * FS + FX;
            if (BX < ScrW) {
              Buf[BY * ScrW + BX] = Color;
            }
          }
        }
      }
    }
  }
}

/**
  在 BackBuffer 边框区域渲染版本号字符串

  版本号格式 "v0.1.0.9", 渲染在游戏区域右下方的黑色边框中。
  每个字符 3x5 像素, 字符间距 1 像素, 总宽度约 4*8-1 = 31 像素。

  @param Buf   BackBuffer 指针
  @param ScrW  屏幕宽度
  @param ScrH  屏幕高度
  @param OffX  游戏区域 X 偏移
  @param OffY  游戏区域 Y 偏移
**/
STATIC
VOID
RenderVersionString (
  UINT32 *Buf,
  UINT32  ScrW,
  UINT32  ScrH,
  UINT32  OffX,
  UINT32  OffY
  )
{
  /* 版本字符串: "v" + VERSION_STRING (宽字符 → 逐字符提取 ASCII) */
  CHAR8  VerStr[16];
  UINTN  Len, i;
  UINT32 X, Y;
  UINT32 Color;
  UINT32 BgColor;
  UINT32 FS;          /* 字形放大倍数 */
  UINT32 CharAdv;     /* 每字符水平步进 (像素) */
  UINT32 TextW;       /* 文本总宽 (像素) */
  UINT32 TextH;       /* 文本总高 (像素) */
  UINT32 Pad;         /* 衬底边距 */

  /* 从宽字符版本字符串提取 ASCII (version.h 中为 L"0.1.0.9") */
  {
    CONST CHAR16 *WVer = SFC3_VERSION_STRING;
    Len = 0;
    VerStr[Len++] = 'v';
    for (i = 0; WVer[i] != 0 && Len < 15; i++) {
      VerStr[Len++] = (CHAR8)(WVer[i] & 0x7F);
    }
    VerStr[Len] = 0;
  }

  /* 字形随游戏画面缩放一起放大, 至少 2 倍, 保证肉眼可读 */
  FS = gGopRenderer.Scale;
  if (FS < 2) {
    FS = 2;
  }

  CharAdv = 4 * FS;          /* 字形 3 列 + 1 列字距, 均按 FS 放大 */
  TextW   = (UINT32)Len * CharAdv - FS;  /* 去掉末尾字距 */
  TextH   = 5 * FS;
  Pad     = FS + 1;

  /* 颜色: 浅灰白, 配黑色衬底, 在黑边与画面上都清晰 */
  Color   = 0x00E0E0E0;  /* RGB(224,224,224) */
  BgColor = 0x00000000;  /* 黑色衬底 */

  /* 默认位置: 游戏区域右下角外侧, 右对齐 */
  Y = OffY + NES_SCREEN_HEIGHT * gGopRenderer.Scale + Pad;
  if (OffX + NES_SCREEN_WIDTH * gGopRenderer.Scale > TextW + Pad * 2) {
    X = OffX + NES_SCREEN_WIDTH * gGopRenderer.Scale - TextW - Pad;
  } else {
    X = Pad;
  }

  /* 如果下方空间不够, 改放左上角 */
  if (Y + TextH + Pad >= ScrH) {
    Y = Pad;
    X = Pad;
  }

  /* 先画黑色衬底矩形, 再画文字 (避免与游戏画面混色) */
  if (X > Pad) {
    FillRect(Buf, ScrW, ScrH, X - Pad, Y - Pad, TextW + Pad * 2, TextH + Pad * 2, BgColor);
  } else {
    FillRect(Buf, ScrW, ScrH, 0, Y - Pad, X + TextW + Pad * 2, TextH + Pad * 2, BgColor);
  }

  for (i = 0; i < Len; i++) {
    RenderChar(Buf, ScrW, ScrH, X + (UINT32)i * CharAdv, Y, VerStr[i], Color, FS);
  }
}

/**
  初始化 GOP 渲染器: 定位协议、计算缩放、分配后缓冲。

  @retval EFI_SUCCESS       初始化成功
  @retval EFI_NOT_FOUND     未找到 GOP 协议
  @retval EFI_OUT_OF_RESOURCES 内存分配失败
**/
EFI_STATUS
GopInit (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      ScaleW;
  UINT32      ScaleH;

  /* 定位 GOP 协议 */
  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&gGopRenderer.Gop
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[SFC3] GOP: LocateProtocol failed: %r\n", Status));
    return Status;
  }

  /* 读取当前分辨率 */
  gGopRenderer.ScreenW = gGopRenderer.Gop->Mode->Info->HorizontalResolution;
  gGopRenderer.ScreenH = gGopRenderer.Gop->Mode->Info->VerticalResolution;

  DEBUG ((DEBUG_INFO, "[SFC3] GOP: Resolution %ux%u\n",
          gGopRenderer.ScreenW, gGopRenderer.ScreenH));

  /* 计算整数缩放倍数: min(ScreenW/256, ScreenH/240), 最小为 1 */
  ScaleW = gGopRenderer.ScreenW / NES_SCREEN_WIDTH;
  ScaleH = gGopRenderer.ScreenH / NES_SCREEN_HEIGHT;
  gGopRenderer.Scale = (ScaleW < ScaleH) ? ScaleW : ScaleH;
  if (gGopRenderer.Scale < 1) {
    gGopRenderer.Scale = 1;
  }

  /* 计算居中偏移 */
  gGopRenderer.OffsetX = (gGopRenderer.ScreenW - NES_SCREEN_WIDTH * gGopRenderer.Scale) / 2;
  gGopRenderer.OffsetY = (gGopRenderer.ScreenH - NES_SCREEN_HEIGHT * gGopRenderer.Scale) / 2;

  /* 分配后缓冲: 整个屏幕大小, 每个像素 4 字节 (BGR + Reserved) */
  gGopRenderer.BackBuffer = (UINT32 *)AllocateZeroPool (
                            gGopRenderer.ScreenW * gGopRenderer.ScreenH * sizeof (UINT32)
                            );
  if (gGopRenderer.BackBuffer == NULL) {
    DEBUG ((DEBUG_ERROR, "[SFC3] GOP: BackBuffer alloc failed (%u bytes)\n",
            gGopRenderer.ScreenW * gGopRenderer.ScreenH * (UINT32)sizeof (UINT32)));
    return EFI_OUT_OF_RESOURCES;
  }

  gGopRenderer.Initialized = TRUE;

  DEBUG ((DEBUG_INFO, "[SFC3] GOP: Init OK, Scale=%u, Offset=(%u,%u), BackBuffer=%u bytes\n",
          gGopRenderer.Scale, gGopRenderer.OffsetX, gGopRenderer.OffsetY,
          gGopRenderer.ScreenW * gGopRenderer.ScreenH * (UINT32)sizeof (UINT32)));

  return EFI_SUCCESS;
}

/**
  将 PPU FrameBuffer 渲染到屏幕。

  逐像素读取 NES palette index, 查 gNesPaletteBGR 表转换为 BGR,
  按整数缩放写入后缓冲对应 Scale×Scale 像素块, 最终一次 Blt 提交全屏。

  @param[in] State  NES 状态结构体指针 (含 Ppu.FrameBuffer)
**/
VOID
GopPresent (
  IN NES_STATE  *State
  )
{
  UINT32   X, Y, SX, SY;
  UINT32   S;
  UINT32   OffX, OffY;
  UINT32   ScrW;
  UINT8    NesColor;
  UINT32   BgrPixel;
  UINT32  *Buf;
  UINT32   BufRowStart;
  UINT32   BufIdx;

  if (!gGopRenderer.Initialized) {
    return;
  }

  S    = gGopRenderer.Scale;
  OffX = gGopRenderer.OffsetX;
  OffY = gGopRenderer.OffsetY;
  ScrW = gGopRenderer.ScreenW;
  Buf  = gGopRenderer.BackBuffer;

  /* 清屏: 全部设为黑色 (0x00000000) */
  SetMem32 (Buf, ScrW * gGopRenderer.ScreenH * sizeof (UINT32), 0x00000000);

  /* 逐 NES 像素渲染 */
  for (Y = 0; Y < NES_SCREEN_HEIGHT; Y++) {
    for (X = 0; X < NES_SCREEN_WIDTH; X++) {
      NesColor = State->Ppu.FrameBuffer[Y * NES_SCREEN_WIDTH + X];
      BgrPixel = gNesPaletteBGR[NesColor & 0x3F];

      /* 将 Scale×Scale 块写入后缓冲 */
      BufRowStart = (OffY + Y * S) * ScrW + (OffX + X * S);
      for (SY = 0; SY < S; SY++) {
        BufIdx = BufRowStart + SY * ScrW;
        for (SX = 0; SX < S; SX++) {
          Buf[BufIdx + SX] = BgrPixel;
        }
      }
    }
  }

  /* 在边框区域渲染版本号 (防止静默编译错误: 用户可直观确认运行版本) */
  RenderVersionString (Buf, ScrW, gGopRenderer.ScreenH, OffX, OffY);

  /* 一次 Blt 提交整个后缓冲到屏幕 */
  gGopRenderer.Gop->Blt (
                      gGopRenderer.Gop,
                      (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Buf,
                      EfiBltBufferToVideo,
                      0, 0,
                      0, 0,
                      gGopRenderer.ScreenW, gGopRenderer.ScreenH,
                      gGopRenderer.ScreenW * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                      );
}

/**
  释放 GOP 渲染器资源。
**/
VOID
GopFree (
  VOID
  )
{
  if (gGopRenderer.BackBuffer != NULL) {
    FreePool (gGopRenderer.BackBuffer);
    gGopRenderer.BackBuffer = NULL;
  }

  gGopRenderer.Initialized = FALSE;
  DEBUG ((DEBUG_INFO, "[SFC3] GOP: Freed.\n"));
}
