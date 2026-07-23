/** @file
  AI 系统实现

  基础 AI 策略:
  1. 远距离: 向 P1 方向移动
  2. 近距离: 交替使用 A/B 攻击
  3. 偶尔跳跃或后退

  后续将从原厂 ASM 中翻译真实 AI 决策逻辑。
  搜索方法: 在 bank_FF.asm 中搜索引用 ram_p2_fighter ($003C)
  和 P2 手柄 RAM ($00F5) 的函数。
**/

#include "ai.h"
#include "nes_types.h"
#include <Library/DebugLib.h>

VOID AiUpdate(NES_STATE *State)
{
  UINT8 AiInput;
  UINT32 Phase;

  AiInput = 0;
  Phase = State->FrameCount % 120;  /* 2 秒周期 */

  /* 基础 AI: 根据帧计数切换行为 */
  if (Phase < 40) {
    /* 接近: 向右移动 (假设 P1 在右侧) */
    AiInput |= NES_BTN_RIGHT;
  } else if (Phase < 60) {
    /* 攻击 A */
    AiInput |= NES_BTN_A;
  } else if (Phase < 80) {
    /* 接近 */
    AiInput |= NES_BTN_RIGHT;
  } else if (Phase < 100) {
    /* 攻击 B */
    AiInput |= NES_BTN_B;
  } else {
    /* 偶尔跳跃 */
    AiInput |= NES_BTN_UP;
  }

  State->Ram[ADDR_P2_JOYPAD] = AiInput;
}
