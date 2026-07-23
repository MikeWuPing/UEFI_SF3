/** @file
  资源文件加载实现
**/

#include "resource.h"
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

GAME_RESOURCES gResources;

STATIC
EFI_STATUS
LoadFile (
  IN  EFI_HANDLE  ImageHandle,
  IN  CHAR16      *FileName,
  OUT UINT8       **Buffer,
  OUT UINT32      *FileSize
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FsProtocol;
  EFI_FILE_PROTOCOL                *RootDir;
  EFI_FILE_PROTOCOL                *File;
  EFI_FILE_INFO                    *FileInfo;
  UINTN                            InfoSize;
  UINTN                            ReadSize;

  /* 通过 LoadedImageProtocol 获取映像所在设备句柄 */
  Status = gBS->HandleProtocol(
    ImageHandle,
    &gEfiLoadedImageProtocolGuid,
    (VOID **)&LoadedImage
  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] LoadedImage protocol not found: %r\n", Status));
    return Status;
  }

  /* 从设备句柄获取 Simple File System Protocol */
  Status = gBS->HandleProtocol(
    LoadedImage->DeviceHandle,
    &gEfiSimpleFileSystemProtocolGuid,
    (VOID **)&FsProtocol
  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] FileSystem protocol not found: %r\n", Status));
    return Status;
  }

  /* 打开根目录 */
  Status = FsProtocol->OpenVolume(FsProtocol, &RootDir);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] OpenVolume failed: %r\n", Status));
    return Status;
  }

  /* 打开目标文件 */
  Status = RootDir->Open(RootDir, &File, FileName, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] Open %s failed: %r\n", FileName, Status));
    RootDir->Close(RootDir);
    return Status;
  }

  /* 获取文件大小 */
  InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  FileInfo = AllocatePool(InfoSize);
  if (FileInfo == NULL) {
    File->Close(File);
    RootDir->Close(RootDir);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] GetInfo failed: %r\n", Status));
    FreePool(FileInfo);
    File->Close(File);
    RootDir->Close(RootDir);
    return Status;
  }

  *FileSize = (UINT32)FileInfo->FileSize;
  FreePool(FileInfo);

  /* 分配缓冲区并读取 */
  *Buffer = AllocatePool(*FileSize);
  if (*Buffer == NULL) {
    File->Close(File);
    RootDir->Close(RootDir);
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = *FileSize;
  Status = File->Read(File, &ReadSize, *Buffer);
  File->Close(File);
  RootDir->Close(RootDir);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "[SFC3] Read %s failed: %r\n", FileName, Status));
    FreePool(*Buffer);
    *Buffer = NULL;
    return Status;
  }

  DEBUG((DEBUG_INFO, "[SFC3] Loaded %s: %d bytes\n", FileName, *FileSize));
  return EFI_SUCCESS;
}

EFI_STATUS
ResourceLoadAll (
  EFI_HANDLE ImageHandle
  )
{
  EFI_STATUS Status;

  /* 加载 CHR ROM */
  Status = LoadFile(ImageHandle, L"sfc3_chr.bin",
                    &gResources.ChrRom, &gResources.ChrRomSize);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (gResources.ChrRomSize != CHR_ROM_SIZE) {
    DEBUG((DEBUG_ERROR, "[SFC3] CHR size mismatch: %d != %d\n",
           gResources.ChrRomSize, CHR_ROM_SIZE));
    return EFI_INVALID_PARAMETER;
  }

  /* 加载 PRG ROM (数据表) */
  Status = LoadFile(ImageHandle, L"sfc3_prg.bin",
                    &gResources.PrgRom, &gResources.PrgRomSize);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (gResources.PrgRomSize != PRG_ROM_SIZE) {
    DEBUG((DEBUG_ERROR, "[SFC3] PRG size mismatch: %d != %d\n",
           gResources.PrgRomSize, PRG_ROM_SIZE));
    return EFI_INVALID_PARAMETER;
  }

  gResources.Loaded = TRUE;
  DEBUG((DEBUG_INFO, "[SFC3] All resources loaded: CHR=%dKB PRG=%dKB\n",
         gResources.ChrRomSize / 1024, gResources.PrgRomSize / 1024));
  return EFI_SUCCESS;
}

VOID
ResourceFree (
  VOID
  )
{
  if (gResources.ChrRom != NULL) {
    FreePool(gResources.ChrRom);
    gResources.ChrRom = NULL;
  }
  if (gResources.PrgRom != NULL) {
    FreePool(gResources.PrgRom);
    gResources.PrgRom = NULL;
  }
  gResources.Loaded = FALSE;
}
