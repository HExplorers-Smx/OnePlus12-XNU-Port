/* SPDX-License-Identifier: MIT
 * Host-only fake UEFI Boot Services, no real device access.
 * Usage: mock probe.dtb [badmap|missing]
 */
#include <Uefi.h>
#include <Guid/Fdt.h>
#include <Protocol/SerialIo.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
EFI_GUID gFdtTableGuid={0xb1b621d5,0xf19c,0x41a5,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
EFI_GUID gEfiSerialIoProtocolGuid={0};
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
static uint8_t *test_fdt;
static int badmap;
BOOLEAN CompareGuid(const EFI_GUID *a,const EFI_GUID *b)
{ return a && b && memcmp(a,b,sizeof(*a))==0; }
VOID *AllocatePool(UINTN Size){return malloc((size_t)Size);}
VOID FreePool(VOID *Ptr){free(Ptr);}
UINTN Print(const wchar_t *format,...)
{
    /* EDK2 Print's %r is a firmware-only directive. In this mock, replace it
     * with a numeric status; all tests parse only WFLG_HEX lines. */
    wchar_t cleaned[1024];size_t i=0,j=0;
    while(format[i]&&j+2<sizeof(cleaned)/sizeof(cleaned[0])) {
        if(format[i]=='%'&&format[i+1]=='r'){
            cleaned[j++]='%';cleaned[j++]='u';i+=2;
        } else cleaned[j++]=format[i++];
    }
    cleaned[j]=0;
    va_list args;va_start(args,format);
    int ret=vwprintf(cleaned,args);
    va_end(args);
    fflush(stdout);
    return ret<0?0:(UINTN)ret;
}
static EFI_STATUS EFIAPI MockMap(UINTN *Bytes,EFI_MEMORY_DESCRIPTOR *Map,
                                  UINTN *Key,UINTN *Stride,UINT32 *Version)
{
    *Stride=sizeof(EFI_MEMORY_DESCRIPTOR);*Key=1;*Version=1;
    if(!Map){*Bytes=*Stride;return EFI_BUFFER_TOO_SMALL;}
    if(*Bytes<*Stride)return EFI_BUFFER_TOO_SMALL;
    memset(Map,0,sizeof(*Map));
    Map->Type=badmap?11:4;
    Map->PhysicalStart=(EFI_PHYSICAL_ADDRESS)((uintptr_t)test_fdt&~(uintptr_t)4095);
    Map->NumberOfPages=badmap?1:2048;
    *Bytes=*Stride;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI MockSerial(int a,const EFI_GUID *b,VOID *c,UINTN *count,EFI_HANDLE **handles)
{ (void)a;(void)b;(void)c;*count=0;*handles=NULL;return EFI_NOT_FOUND; }
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE,EFI_SYSTEM_TABLE *);
int main(int argc,char **argv)
{
    if(argc<2||argc>3){fputs("usage: mock <fdt> [badmap|missing]\n",stderr);return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 2;}
    long n=ftell(f);rewind(f);
    if(n<=0||n>8*1024*1024){fclose(f);return 2;}
    test_fdt=(uint8_t *)malloc((size_t)n);
    if(!test_fdt){fclose(f);return 2;}
    if(fread(test_fdt,1,(size_t)n,f)!=(size_t)n){fclose(f);return 2;}
    fclose(f);
    badmap=argc==3 && strcmp(argv[2],"badmap")==0;
    EFI_CONFIGURATION_TABLE table={0};
    table.VendorGuid=gFdtTableGuid;table.VendorTable=test_fdt;
    EFI_SYSTEM_TABLE st={1,&table};
    if(argc==3 && strcmp(argv[2],"missing")==0)st.NumberOfTableEntries=0;
    EFI_BOOT_SERVICES bs={MockMap,MockSerial};
    gST=&st;gBS=&bs;
    EFI_STATUS status=UefiMain(NULL,&st);
    free(test_fdt);
    /* Probe failure is a nonzero process status. */
    return status==EFI_SUCCESS?0:1;
}
