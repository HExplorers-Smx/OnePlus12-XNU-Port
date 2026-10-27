/** @file
  SPDX-License-Identifier: MIT

  Minimal UEFI-only read-only bring-up diagnostic. A console message or an
  EFI_SERIAL_IO_PROTOCOL DOES NOT imply USB-C debug output on OnePlus 12.
  Deliberately performs no MMIO, no fastboot, no partition access, no XNU
  handoff and no EFI variable writes. Enumeration is informative only.
**/
#include <Uefi.h>
#include <Protocol/SerialIo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
                            IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  (VOID)ImageHandle;
  (VOID)SystemTable;
  Print(L"WaffleS10: UEFI entered; this is NOT an XNU/iOS boot test.\r\n");
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSerialIoProtocolGuid,
                                   NULL, &HandleCount, &Handles);
  if (!EFI_ERROR(Status)) {
    Print(L"WaffleS10: SerialIo handles=%u (not verified USB-C)\r\n",
          (UINT32)HandleCount);
    if (Handles != NULL) FreePool(Handles);
  } else if (Status == EFI_NOT_FOUND) {
    Print(L"WaffleS10: no UEFI SerialIo present\r\n");
  } else {
    Print(L"WaffleS10: SerialIo enumeration failed, EFI_STATUS=%r\r\n",Status);
  }
  Print(L"WaffleS10: read-only probe complete; no device registers touched.\r\n");
  return EFI_SUCCESS;
}
