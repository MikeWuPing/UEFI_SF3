/** @file
  键盘输入 → NES 手柄映射

  UEFI ConIn 协议只提供 key-down 事件（无 key-up），
  因此每帧轮询所有待处理按键，映射为 NES 手柄位掩码。
**/

#include "input.h"
#include <Library/UefiBootServicesTableLib.h>

/* 按键映射表条目 */
typedef struct {
  CHAR16  Unicode;    /* Unicode 字符 (0 = 不使用) */
  UINT16  ScanCode;   /* 扫描码 (0 = 不使用) */
  UINT8   NesButton;  /* NES 手柄位掩码 */
} KEY_MAP_ENTRY;

/*
  P1 按键映射表
  W/A/S/D = 方向, K = A, J = B, Enter = START, ESC = SELECT
*/
STATIC CONST KEY_MAP_ENTRY gP1KeyMap[] = {
  { L'w', 0x0000, NES_BTN_UP     },
  { L'W', 0x0000, NES_BTN_UP     },
  { L's', 0x0000, NES_BTN_DOWN   },
  { L'S', 0x0000, NES_BTN_DOWN   },
  { L'a', 0x0000, NES_BTN_LEFT   },
  { L'A', 0x0000, NES_BTN_LEFT   },
  { L'd', 0x0000, NES_BTN_RIGHT  },
  { L'D', 0x0000, NES_BTN_RIGHT  },
  { L'k', 0x0000, NES_BTN_A      },
  { L'K', 0x0000, NES_BTN_A      },
  { L'j', 0x0000, NES_BTN_B      },
  { L'J', 0x0000, NES_BTN_B      },
  { 0x0D, 0x0000, NES_BTN_START  },  /* Enter = CHAR_CARRIAGE_RETURN */
  { 0x00, 0x0017, NES_BTN_SELECT }   /* ESC = ScanCode 0x17 */
};

#define KEY_MAP_COUNT  (sizeof(gP1KeyMap) / sizeof(gP1KeyMap[0]))

/**
  初始化输入子系统
**/
VOID
InputInit (
  VOID
  )
{
  /* 清零手柄状态 */
  gState.Ram[ADDR_P1_JOYPAD] = 0;
  gState.Ram[ADDR_P1_JOYPAD_PREV] = 0;
}

/**
  每帧轮询键盘输入，映射为 NES P1 手柄状态

  @param[in,out] State  NES 状态结构体指针
**/
VOID
InputPoll (
  IN OUT NES_STATE  *State
  )
{
  EFI_INPUT_KEY  Key;
  UINT8          P1Input;
  UINTN          i;

  /* 保存上一帧手柄状态 */
  State->Ram[ADDR_P1_JOYPAD_PREV] = State->Ram[ADDR_P1_JOYPAD];

  P1Input = 0;

  /* 读取所有待处理按键 (非阻塞) */
  while (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) {
    for (i = 0; i < KEY_MAP_COUNT; i++) {
      if ((gP1KeyMap[i].Unicode != 0 && Key.UnicodeChar == gP1KeyMap[i].Unicode) ||
          (gP1KeyMap[i].ScanCode != 0 && Key.ScanCode == gP1KeyMap[i].ScanCode)) {
        P1Input |= gP1KeyMap[i].NesButton;
      }
    }
  }

  State->Ram[ADDR_P1_JOYPAD] = P1Input;
}
