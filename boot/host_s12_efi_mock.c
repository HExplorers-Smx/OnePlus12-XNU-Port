/* SPDX-License-Identifier: MIT
 * Execute actual S12 EFI entry point under a controlled mock UEFI table.
 * Read-only: synthetic UEFI memory map, real or synthetic device tree.
 */
#include "waffle_efi_min.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern wa_efi_status efi_main(wa_efi_handle,wa_system_table *);
static uint8_t *fdt,*payload;
static size_t fdt_bytes,payload_bytes;
static wa_memory_descriptor memory[2];
static unsigned fdt_ok=1,options_ok=1;
static wa_loaded_image loaded;
static const wa_guid FDT_GUID={0xb1b621d5,0xf19c,0x41a5,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
static const wa_guid LOADED_GUID={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static wa_efi_status output(wa_text_out *unused,const wa_char16 *p){
    (void)unused;
    while(*p){putchar((char)(*p&0x7f));p++;}
    return 0;
}
static wa_efi_status get_memory_map(uint64_t *n,void *map,uint64_t *key,uint64_t *stride,uint32_t *version){
    *stride=sizeof(memory[0]);*version=1;*key=1;
    if(!map || *n<sizeof(memory)){*n=sizeof(memory);return WA_EFI_BUFFER_TOO_SMALL;}
    memcpy(map,memory,sizeof(memory));*n=sizeof(memory);return 0;
}
static wa_efi_status alloc_pool(int type,uint64_t n,void **p){
    (void)type;
    *p=malloc((size_t)n);
    return *p?0:(WA_EFI_ERR|9);
}
static wa_efi_status free_pool(void *p){free(p);return 0;}
static wa_efi_status handle_protocol(wa_efi_handle image,const wa_guid *guid,void **out){
    (void)image;
    if(memcmp(guid,&LOADED_GUID,sizeof(*guid)))return WA_EFI_NOT_FOUND;
    *out=&loaded;return 0;
}
static uint8_t *read_file(const char *p,size_t *len){
    FILE *f=fopen(p,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);
    if(n<=0 || n>128*1024*1024){fclose(f);return NULL;}
    rewind(f);uint8_t *b=malloc((size_t)n);if(!b){fclose(f);return NULL;}
    if(fread(b,1,(size_t)n,f)!=(size_t)n){fclose(f);free(b);return NULL;}
    fclose(f);*len=(size_t)n;return b;
}
static wa_memory_descriptor region(void *p,size_t len){
    uintptr_t start=(uintptr_t)p&~(uintptr_t)4095;
    uintptr_t end=((uintptr_t)p+len+4095)&~(uintptr_t)4095;
    wa_memory_descriptor m={0};m.type=2;m.physical=start;m.pages=(end-start)/4096;
    return m;
}
int main(int argc,char **argv){
    if(argc<3)return 2;
    fdt=read_file(argv[1],&fdt_bytes);payload=read_file(argv[2],&payload_bytes);
    if(!fdt||!payload)return 2;
    if(argc>3&&strcmp(argv[3],"badmap")==0)fdt_ok=0;
    if(argc>3&&strcmp(argv[3],"nooptions")==0)options_ok=0;
    memory[0]=region(fdt,fdt_bytes);memory[1]=region(payload,payload_bytes);
    if(!fdt_ok){memory[0].type=11;memory[1].type=11;}
    loaded.options=options_ok?payload:NULL;loaded.options_bytes=options_ok?(uint32_t)payload_bytes:0;
    wa_text_out console={0};console.output_string=output;
    wa_boot_services bs={0};
    bs.get_memory_map=get_memory_map;bs.allocate_pool=alloc_pool;
    bs.free_pool=free_pool;bs.handle_protocol=handle_protocol;
    struct {wa_guid guid;void *table;} configs[]={ { {0}, NULL} };
    memcpy(&configs[0].guid,&FDT_GUID,sizeof(FDT_GUID));configs[0].table=fdt;
    wa_system_table st={0};st.out=&console;st.bs=&bs;
    /* Avoid relying on anonymous structure pointer type compatibility. */
    st.tables=(void *)configs;st.count=1;
    wa_efi_status result=efi_main((void *)1,&st);
    free(payload);free(fdt);
    return WA_EFI_ERROR(result)?1:0;
}
