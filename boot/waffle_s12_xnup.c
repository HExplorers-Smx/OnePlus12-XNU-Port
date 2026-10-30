/* SPDX-License-Identifier: MIT */
#include "waffle_s12_xnup.h"
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int eq(const uint8_t *p,const char *s,size_t n) {
    size_t i=0;
    for(;i<n;i++) {
        uint8_t wanted=(uint8_t)*s;
        if(p[i]!=wanted) return 0;
        if(wanted) s++;
    }
    return *s==0;
}
int wa_s12_xnup_parse(const uint8_t *b,size_t actual,struct wa_s12_view *out) {
    struct wa_s12_view v={0};
    if(!out) return WA_S12_INVALID;
    *out=v;
    if(!b || actual<32 || actual>WA_S12_MAX_PAYLOAD || le32(b)!=0x50554e58u) return WA_S12_INVALID;
    uint32_t body=le32(b+4);
    if(body<24 || (size_t)body!=actual-8) return WA_S12_BOUNDS;
    size_t at=8;
    int saw_end=0,saw_kernel=0,saw_cmd=0,saw_usb=0;
    while(at<actual) {
        if(actual-at<24) return WA_S12_BOUNDS;
        const uint8_t *name=b+at;
        uint32_t size=le32(name+16),reserved=le32(name+20);
        at+=24;
        if(!name[0]) {
            if(size || reserved) return WA_S12_INVALID;
            for(size_t i=1;i<16;i++) if(name[i]) return WA_S12_INVALID;
            if(at!=actual) return WA_S12_BOUNDS;
            saw_end=1;
            break;
        }
        if(reserved || size>WA_S12_MAX_PAYLOAD || (unsigned)v.entries>=WA_S12_MAX_ENTRIES)
            return WA_S12_INVALID;
        int terminated=0;
        for(size_t i=0;i<16;i++) if(!name[i]){terminated=1;break;}
        if(!terminated) return WA_S12_INVALID;
        size_t padded=(size_t)size+((16u-(size&15u))&15u);
        if(padded>actual-at) return WA_S12_BOUNDS;
        for(size_t i=size;i<padded;i++) if(b[at+i]) return WA_S12_INVALID;
        if(eq(name,"kernel",16)) {
            if(saw_kernel) return WA_S12_DUPLICATE;
            saw_kernel=1;
            v.kernel_offset=at;v.kernel_bytes=size;
        } else if(eq(name,"cmdline",16)) {
            if(saw_cmd) return WA_S12_DUPLICATE;
            saw_cmd=1;v.cmdline_offset=at;v.cmdline_bytes=size;
            if(!size || b[at+size-1]!=0) return WA_S12_INVALID;
        } else if(eq(name,"usb",16)) {
            if(saw_usb) return WA_S12_DUPLICATE;
            saw_usb=1;
            if(size!=2 || b[at]!='1' || b[at+1]!=0) return WA_S12_INVALID;
            v.usb=1;
        }
        v.entries++;
        at+=padded;
    }
    if(!saw_end) return WA_S12_BOUNDS;
    if(!saw_kernel || !v.kernel_bytes) return WA_S12_NO_KERNEL;
    const uint8_t *k=b+v.kernel_offset;
    /* Fat Mach-O, compressed kernels and iOS images are NOT accepted silently.
     * A proper LC_SEGMENT_64/entrypoint/fixup validator is still required before boot. */
    if(v.kernel_bytes<32 || le32(k)!=0xfeedfacfu || le32(k+4)!=0x0100000cu)
        return WA_S12_NOT_MACHO;
    /* A header with impossible load-command size cannot be staged. */
    uint32_t ncmds=le32(k+16), sizeofcmds=le32(k+20);
    if(ncmds==0 || ncmds>65535 || sizeofcmds>v.kernel_bytes-32 ||
       sizeofcmds/8<ncmds) return WA_S12_NOT_MACHO;
    *out=v;
    return WA_S12_OK;
}
