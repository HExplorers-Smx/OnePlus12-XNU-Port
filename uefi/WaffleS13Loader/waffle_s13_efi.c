/* SPDX-License-Identifier: MIT
 * Freestanding AArch64 PE/COFF UEFI application (no EDK2 required to compile).
 * Stage S13: verify waffle FDT + XNUP, parse segments and stage to owned
 * temporary UEFI pages. ALL pages are freed before returning.
 * No disk writes, MMIO, ExitBootServices, or kernel execution.
 * Does NOT transfer control to XNU. Deliberate strict preflight barrier.
 */
#include "waffle_efi_min.h"
#include "waffle_s11_fdt.h"
#include "waffle_s12_xnup.h"
#include "waffle_s13_macho.h"
static const wa_guid FDT_GUID={0xb1b621d5,0xf19c,0x41a5,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
static const wa_guid LOADED_GUID={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static int guid_equal(const wa_guid *a,const wa_guid *b) {
    if(a->a!=b->a||a->b!=b->b||a->c!=b->c) return 0;
    for(unsigned i=0;i<8;i++) if(a->d[i]!=b->d[i])return 0;
    return 1;
}
static uint32_t be32(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void line(wa_system_table *s,const wa_char16 *message){
    if(s&&s->out&&s->out->output_string)s->out->output_string(s->out,message);
}
/* Runtime bounds checking prior to FDT/LoadOptions dereference: the pointer
 * and bytes must lie inside the *same* ordinary RAM descriptor. */
static int valid_memory(wa_system_table *s,uint64_t address,uint64_t bytes) {
    if(!s||!s->bs||!s->bs->get_memory_map||!s->bs->allocate_pool||
       !s->bs->free_pool||!bytes||address>UINT64_MAX-bytes)return 0;
    uint64_t size=0,key=0,stride=0;uint32_t version=0;
    wa_efi_status st=s->bs->get_memory_map(&size,0,&key,&stride,&version);
    if(st!=WA_EFI_BUFFER_TOO_SMALL||size>1048576||stride<sizeof(wa_memory_descriptor)||stride>4096)
        return 0;
    if(size>1048576-2*stride)return 0;
    size+=2*stride;
    void *map=0;
    if(WA_EFI_ERROR(s->bs->allocate_pool(2,size,&map))||!map)return 0;
    st=s->bs->get_memory_map(&size,map,&key,&stride,&version);
    int found=0;
    if(!WA_EFI_ERROR(st)&&stride>=sizeof(wa_memory_descriptor)) {
        for(uint64_t off=0;off<=size&&stride<=size-off;off+=stride) {
            const wa_memory_descriptor *e=(const void *)((uint8_t *)map+off);
            const uint64_t allowed=((1ull<<0)|(1ull<<1)|(1ull<<2)|(1ull<<3)|
                                    (1ull<<4)|(1ull<<5)|(1ull<<6)|(1ull<<7)|(1ull<<9));
            if(e->type>=63||(allowed&(1ull<<e->type))==0||!e->pages||
               e->pages>UINT64_MAX/4096u)continue;
            uint64_t n=e->pages*4096u;
            if(e->physical>UINT64_MAX-n)continue;
            if(address>=e->physical&&address-e->physical<=n&&bytes<=n-(address-e->physical)){
                found=1;break;
            }
        }
    }
    s->bs->free_pool(map);
    return found;
}
wa_efi_status EFIAPI efi_main(wa_efi_handle image,wa_system_table *s){
    if(!s||!s->bs||!s->tables||s->count>4096)return WA_EFI_SECURITY;
    line(s,(const wa_char16 *)u"Waffle S13: Mach-O temporary loader (NO XNU EXECUTION)\r\n");
    struct wa_s11_identity identity={0};int ok=0;
    for(uint64_t i=0;i<s->count;i++){
        if(!guid_equal(&s->tables[i].guid,&FDT_GUID)||!s->tables[i].table)continue;
        uint64_t p=(uint64_t)(uintptr_t)s->tables[i].table;
        if(!valid_memory(s,p,40))break;
        const uint8_t *f=(const uint8_t *)(uintptr_t)p;
        uint32_t bytes=be32(f+4);
        if(bytes<40||bytes>WA_S11_MAX_DTB||!valid_memory(s,p,bytes))break;
        if(wa_s11_fdt_inspect(f,bytes,&identity)==WA_S11_OK)ok=1;
        break;
    }
    if(!ok){line(s,(const wa_char16 *)u"S13 FAIL: unverified waffle FDT\r\n");return WA_EFI_SECURITY;}
    wa_loaded_image *li=0;
    if(!s->bs->handle_protocol||WA_EFI_ERROR(s->bs->handle_protocol(image,&LOADED_GUID,(void **)&li))||!li){
        line(s,(const wa_char16 *)u"S13 FAIL: no loaded image protocol\r\n");return WA_EFI_NOT_FOUND;
    }
    if(!li->options||li->options_bytes<32||li->options_bytes>WA_S12_MAX_PAYLOAD||
       !valid_memory(s,(uint64_t)(uintptr_t)li->options,li->options_bytes)){
        line(s,(const wa_char16 *)u"S13 FAIL: missing or unowned XNUP load options\r\n");return WA_EFI_NOT_FOUND;
    }
    struct wa_s12_view view={0};
    int rc=wa_s12_xnup_parse((const uint8_t *)li->options,li->options_bytes,&view);
    if(rc!=WA_S12_OK){
        line(s,(const wa_char16 *)u"S13 FAIL: XNUP invalid or kernel not ARM64 Mach-O\r\n");return WA_EFI_SECURITY;
    }
    struct wa_s13_plan plan={0};
    const uint8_t *kernel=(const uint8_t *)li->options+view.kernel_offset;
    int stage_rc=wa_s13_macho_plan(kernel,view.kernel_bytes,&plan);
    if(stage_rc!=WA_S13_OK){
        line(s,(const wa_char16 *)u"S13 FAIL: invalid/unsupported Mach-O load commands\r\n");
        return WA_EFI_SECURITY;
    }
    const char *cmd="cpus=1";
    size_t cmdbytes=7;
    if(view.cmdline_bytes){
        cmd=(const char *)li->options+view.cmdline_offset;
        cmdbytes=view.cmdline_bytes;
    }
    if(!s->bs->allocate_pages||!s->bs->free_pages||
       plan.vm_size>WA_S13_MAX_SPAN ||
       plan.vm_size>UINT64_MAX-4096-sizeof(struct wa_s13_boot_args))
        return WA_EFI_SECURITY;
    /* AllocateAnyPages=0, EfiLoaderData=2. 4 KiB page allocation, not XNU handoff. */
    uint64_t bootoff=(plan.vm_size+4095u)&~4095ull;
    uint64_t total=bootoff+sizeof(struct wa_s13_boot_args);
    uint64_t pages=(total+4095u)/4096u;
    uint64_t physical=0;
    if(!pages||WA_EFI_ERROR(s->bs->allocate_pages(0,2,pages,&physical))||!physical){
        line(s,(const wa_char16 *)u"S13 FAIL: temporary UEFI allocation refused\r\n");
        return WA_EFI_SECURITY;
    }
    /* Construct metadata, stage only into pages returned by Boot Services.
     * The output is intentionally ephemeral: freeing it prevents mistaken boots. */
    struct wa_s13_boot_args draft;
    int stage=wa_s13_bootargs_draft(&draft,&plan,physical,pages*4096u,cmd,cmdbytes);
    if(stage==WA_S13_OK)
        stage=wa_s13_macho_stage(kernel,view.kernel_bytes,&plan,
                                (uint8_t *)(uintptr_t)physical,(size_t)bootoff);
    if(stage==WA_S13_OK){
        uint8_t *dst=(uint8_t *)(uintptr_t)(physical+bootoff);
        const uint8_t *src=(const uint8_t *)&draft;
        for(size_t i=0;i<sizeof(draft);i++)dst[i]=src[i];
    }
    wa_efi_status freed=s->bs->free_pages(physical,pages);
    if(stage!=WA_S13_OK||WA_EFI_ERROR(freed)){
        line(s,(const wa_char16 *)u"S13 FAIL: staging or free-pages failed\r\n");
        return WA_EFI_SECURITY;
    }
    line(s,(const wa_char16 *)u"S13 PASS: Mach-O segments staged, BSS zeroed, draft boot_args built\r\n");
    line(s,(const wa_char16 *)u"S13 STOP: temp memory freed; no ExitBootServices or XNU jump\r\n");
    return WA_EFI_SUCCESS;
}
