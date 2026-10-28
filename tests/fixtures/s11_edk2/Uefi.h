/* SPDX-License-Identifier: MIT - host-only UEFI type declarations for syntax checks.
   NOT a substitute for EDK2 headers or an EFI firmware. */
#ifndef TEST_S11_UEFI_H
#define TEST_S11_UEFI_H
#include <stddef.h>
#include <stdint.h>
#define IN
#define OUT
#define EFIAPI
#define VOID void
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef int BOOLEAN;
typedef char CHAR8;
typedef void *EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef UINTN EFI_STATUS;
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFI_NOT_FOUND ((EFI_STATUS)14)
#define EFI_BUFFER_TOO_SMALL ((EFI_STATUS)5)
#define EFI_SECURITY_VIOLATION ((EFI_STATUS)26)
#define EFI_ERROR(s) ((s)!=EFI_SUCCESS)
#define ByProtocol 0
typedef struct { UINT32 Data1; uint16_t Data2,Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { EFI_GUID VendorGuid; VOID *VendorTable; } EFI_CONFIGURATION_TABLE;
typedef struct { UINT32 Type; EFI_PHYSICAL_ADDRESS PhysicalStart; UINT64 VirtualStart,NumberOfPages,Attribute; } EFI_MEMORY_DESCRIPTOR;
typedef struct { UINTN NumberOfTableEntries; EFI_CONFIGURATION_TABLE *ConfigurationTable; } EFI_SYSTEM_TABLE;
typedef struct {
 EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *,EFI_MEMORY_DESCRIPTOR *,UINTN *,UINTN *,UINT32 *);
 EFI_STATUS (EFIAPI *LocateHandleBuffer)(int,const EFI_GUID *,VOID *,UINTN *,EFI_HANDLE **);
} EFI_BOOT_SERVICES;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
#endif
