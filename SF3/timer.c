/** @file
  帧率控制 - NES 60fps 帧率模拟
**/

#include "timer.h"
#include <Library/UefiBootServicesTableLib.h>

/**
  等待一帧时间，模拟 NES 60.0988 fps 帧率。
  每帧约 16639 微秒。
**/
VOID
FrameRateWait (
  VOID
  )
{
  /* NES 帧率: 60.0988 fps → 每帧 ~16639 微秒 */
  gBS->Stall(16639);
}
