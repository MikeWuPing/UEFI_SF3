/** @file
  Street Fighter III (NES) UEFI Shell 复刻 - 主入口
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include "version.h"
#include "nes_state.h"
#include "resource.h"
#include "ppu.h"
#include "gop_render.h"
#include "input.h"
#include "timer.h"
#include "game_state.h"

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS     Status;
  UINTN          Index;

  DEBUG((DEBUG_INFO, "[SFC3] APP_VERSION=%s\n", SFC3_VERSION_STRING));
  Print(L"Street Fighter III (NES Remake) v%s\n", SFC3_VERSION_STRING);

  NesStateInit(&gState);
  DEBUG((DEBUG_INFO, "[SFC3] NES State initialized, RAM=%u bytes, PPU FrameBuffer=%u bytes\n",
         (UINT32)sizeof(gState.Ram), (UINT32)sizeof(gState.Ppu.FrameBuffer)));

  ram_screen = 0x0C;
  ram_p1_fighter = FIGHTER_RYU_KEN;
  ram_p2_fighter = FIGHTER_CHUNLI;
  DEBUG((DEBUG_INFO, "[SFC3] Test: screen=0x%02X p1=%d p2=%d\n",
         ram_screen, ram_p1_fighter, ram_p2_fighter));

  /* 加载游戏资源 */
  Status = ResourceLoadAll(ImageHandle);
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Failed to load resources: %r\n", Status);
    Print(L"Make sure sfc3_chr.bin and sfc3_prg.bin are in the same directory.\n");
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
    return Status;
  }
  Print(L"Resources loaded: CHR=%dKB PRG=%dKB\n",
        gResources.ChrRomSize / 1024, gResources.PrgRomSize / 1024);

  /* 初始化 GOP */
  Status = GopInit();
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: GOP init failed: %r\n", Status);
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
    return Status;
  }

  /* 初始化输入 */
  InputInit();

  /* 初始化游戏状态机 (翻译自 Reset handler) */
  GameInit(&gState);

  Print(L"Entering game loop... (ESC=exit)\n");
  DEBUG((DEBUG_INFO, "[SFC3] Entering game loop\n"));

  while (!gState.Quit) {
    InputPoll(&gState);

    /* ESC/SELECT 退出 (Start 用于游戏内导航) */
    if (gState.Ram[ADDR_P1_JOYPAD] & NES_BTN_SELECT) {
      gState.Quit = TRUE;
    }

    /* 游戏状态机帧更新 (NMI handler 分发) */
    GameFrameUpdate(&gState);

    /* PPU 渲染 + GOP 输出 */
    PpuRenderFrame(&gState, gResources.ChrRom);
    GameRenderOverlay(&gState);
    GopPresent(&gState);
    FrameRateWait();
  }

  DEBUG((DEBUG_INFO, "[SFC3] Game loop ended after %d frames\n", gState.FrameCount));

  GopFree();
  ResourceFree();

  DEBUG((DEBUG_INFO, "[SFC3] Exiting.\n"));
  return EFI_SUCCESS;
}
