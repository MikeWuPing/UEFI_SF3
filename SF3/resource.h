/** @file
  资源文件加载 - 从 UEFI FAT 文件系统读取游戏资源
**/

#ifndef _SFC3_RESOURCE_H_
#define _SFC3_RESOURCE_H_

#include <Uefi.h>
#include "nes_types.h"

#define CHR_ROM_SIZE  (512 * 1024)   /* 512KB */
#define PRG_ROM_SIZE  (128 * 1024)   /* 128KB */

typedef struct {
  UINT8   *ChrRom;         /* CHR 图块数据 (512KB) */
  UINT32   ChrRomSize;
  UINT8   *PrgRom;         /* PRG 数据表 (128KB) */
  UINT32   PrgRomSize;
  BOOLEAN  Loaded;
} GAME_RESOURCES;

extern GAME_RESOURCES gResources;

/**
  加载所有游戏资源文件

  @param[in] ImageHandle  当前应用 ImageHandle (用于定位文件系统)
  @retval EFI_SUCCESS     所有资源加载成功
**/
EFI_STATUS ResourceLoadAll(EFI_HANDLE ImageHandle);

/**
  释放资源内存
**/
VOID ResourceFree(VOID);

#endif /* _SFC3_RESOURCE_H_ */
