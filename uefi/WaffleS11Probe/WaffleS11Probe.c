/** @file
  SPDX-License-Identifier: MIT
  Read-only UEFI application. NOT a UEFI platform port, USB transport,
  XNU bootloader, or flashable image. No ExitBootServices or MMIO.
**/
#include <Uefi.h>
#include <Guid/Fdt.h>
#include <Protocol/SerialIo.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include "waffle_s10_log.h"
#include "waffle_s11_fdt.h"

/* Print a transport-independent WFLG frame as ASCII hex on the UEFI console.
 * This does NOT establish any serial or USB-C data path to a host. */
static VOID EmitLog(UINT8 Type,UINT32 Sequence,const CHAR8 *Message)
{
  UINT8 Frame[WA_S10_FRAME_MAX];
  size_t Used=0;
  UINTN TextLength=0;
  while (Message[TextLength] && TextLength<WA_S10_PAYLOAD_MAX) ++TextLength;
  if (wa_s10_encode(Type,Sequence,(const uint8_t *)Message,
                    (size_t)TextLength,Frame,sizeof(Frame),&Used)!=WA_S10_OK)
    return;
  Print(L"WFLG_HEX:");
  for (UINTN Index=0;Index<(UINTN)Used;Index++) Print(L"%02x",(UINT32)Frame[Index]);
  Print(L"\r\n");
}

/* Check that every byte of the offered FDT lies in a single mapped UEFI
 * descriptor. This is a conservative bound, not a proof of firmware trust. */
static BOOLEAN FdtMemoryContains(EFI_PHYSICAL_ADDRESS Ptr,UINTN Length)
{
  EFI_STATUS Status;
  UINTN Bytes=0,Key=0,Stride=0;
  UINT32 Version=0;
  VOID *Map=NULL;
  BOOLEAN Match=FALSE;
  Status=gBS->GetMemoryMap(&Bytes,NULL,&Key,&Stride,&Version);
  if(Status!=EFI_BUFFER_TOO_SMALL || Bytes>1024*1024 || Stride==0)
    return FALSE;
  if(Stride>4096u || Bytes>1024u*1024u-2u*Stride) return FALSE;
  Bytes+=2*Stride;
  Map=AllocatePool(Bytes);
  if(!Map) return FALSE;
  Status=gBS->GetMemoryMap(&Bytes,(EFI_MEMORY_DESCRIPTOR *)Map,&Key,&Stride,&Version);
  if(!EFI_ERROR(Status) && Stride>=sizeof(EFI_MEMORY_DESCRIPTOR)) {
    for(UINTN Offset=0;Offset<=Bytes && Stride<=Bytes-Offset;Offset+=Stride) {
      EFI_MEMORY_DESCRIPTOR *Entry=(EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map+Offset);
      struct wa_s11_memory_region Region={Entry->PhysicalStart,Entry->NumberOfPages,Entry->Type};
      if(wa_s11_memory_contains(&Region,1,(uint64_t)Ptr,(uint64_t)Length)) {
        Match=TRUE;
        break;
      }
    }
  }
  FreePool(Map);
  return Match;
}

static EFI_STATUS GetLiveFdt(struct wa_s11_identity *Identity)
{
  if (!gST || !gST->ConfigurationTable || !Identity) return EFI_NOT_FOUND;
  for (UINTN I=0;I<gST->NumberOfTableEntries;I++) {
    EFI_CONFIGURATION_TABLE *Entry=&gST->ConfigurationTable[I];
    if(!CompareGuid(&Entry->VendorGuid,&gFdtTableGuid) || !Entry->VendorTable)
      continue;
    EFI_PHYSICAL_ADDRESS Ptr=(EFI_PHYSICAL_ADDRESS)(UINTN)Entry->VendorTable;
    if(!FdtMemoryContains(Ptr,40u)) return EFI_SECURITY_VIOLATION;
    const UINT8 *Data=(const UINT8 *)Entry->VendorTable;
    UINTN Total=((UINTN)Data[4]<<24)|((UINTN)Data[5]<<16)|
                ((UINTN)Data[6]<<8)|Data[7];
    if(Total<40u || Total>WA_S11_MAX_DTB || !FdtMemoryContains(Ptr,Total))
      return EFI_SECURITY_VIOLATION;
    if(wa_s11_fdt_inspect(Data,(size_t)Total,Identity)!=WA_S11_OK)
      return EFI_SECURITY_VIOLATION;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
                           IN EFI_SYSTEM_TABLE *SystemTable)
{
  struct wa_s11_identity Identity={0};
  EFI_STATUS Status;
  EFI_HANDLE *Handles=NULL;
  UINTN HandleCount=0;
  (VOID)ImageHandle;
  (VOID)SystemTable;
  Print(L"WaffleS11: UEFI read-only board identification; NOT XNU/iOS boot.\r\n");
  Status=GetLiveFdt(&Identity);
  if(EFI_ERROR(Status)) {
    Print(L"WaffleS11: current-boot waffle FDT not established: %r\r\n",Status);
    EmitLog(WA_S10_TYPE_ERROR,1,"fdt=unverified;uefi=probe;usb=unverified;xnu=not-started");
    return Status;
  }
  Print(L"WaffleS11: waffle model/compatible present; DTB=%u bytes, nodes=%u properties=%u\r\n",
        Identity.totalsize,Identity.nodes,Identity.properties);
  Print(L"WaffleS11: CRC32=%08x (NOT a security/firmware signature)\r\n",Identity.crc32);
  EmitLog(WA_S10_TYPE_BOOT,1,"fdt=waffle;uefi=probe;usb=unverified;xnu=not-started");
  Status=gBS->LocateHandleBuffer(ByProtocol,&gEfiSerialIoProtocolGuid,
                                  NULL,&HandleCount,&Handles);
  if(!EFI_ERROR(Status)) {
    Print(L"WaffleS11: SerialIo handles=%u; this is NOT USB-C proof\r\n",(UINT32)HandleCount);
    if(Handles) FreePool(Handles);
  } else {
    Print(L"WaffleS11: SerialIo unavailable: %r\r\n",Status);
  }
  Print(L"WaffleS11: exit without ExitBootServices, disk writes, or MMIO.\r\n");
  return EFI_SUCCESS;
}
