/* SPDX-License-Identifier: MIT */
#include "waffle_s13_macho.h"
static uint32_t u32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t u64(const uint8_t *p){ return (uint64_t)u32(p)|((uint64_t)u32(p+4)<<32); }
static void clear(uint8_t *p, uint64_t n){while(n--)*p++=0;}
static void copy(uint8_t *d,const uint8_t *s,uint64_t n){while(n--)*d++=*s++;}
static int range(uint64_t offset,uint64_t count,uint64_t limit){
    return offset<=limit && count<=limit-offset;
}
static uint64_t up16k(uint64_t n){return (n+16383u)&~(uint64_t)16383u;}
int wa_s13_macho_plan(const uint8_t *f, size_t n, struct wa_s13_plan *out){
    if(!out)return WA_S13_BAD;
    struct wa_s13_plan a={0};*out=a;
    if(!f||n<32||n>WA_S13_MAX_IMAGE||u32(f)!=0xfeedfacfu||u32(f+4)!=0x0100000cu)
        return WA_S13_BAD;
    /* MH_EXECUTE/MH_PRELOAD only; never accept object/dylib or fat binaries. */
    uint32_t type=u32(f+12),cmds=u32(f+16),cmdsize=u32(f+20);
    if((type!=2u&&type!=5u)||!cmds||cmds>WA_S13_MAX_COMMANDS||
       !range(32,cmdsize,n)||cmdsize<8u*cmds)return WA_S13_BAD;
    uint64_t min=UINT64_MAX,max=0; unsigned text=0;
    size_t at=32;
    for(uint32_t i=0;i<cmds;i++){
        if(!range(at,8,32u+cmdsize))return WA_S13_BOUNDS;
        uint32_t op=u32(f+at),sz=u32(f+at+4);
        if(sz<8||sz%4||!range(at,sz,32u+cmdsize))return WA_S13_BOUNDS;
        if(op==0x19){ /* LC_SEGMENT_64 */
            if(sz<72||a.segments>=WA_S13_SEGMENTS)return WA_S13_BAD;
            uint32_t sects=u32(f+at+64);
            if(sects>(sz-72u)/80u)return WA_S13_BOUNDS;
            uint64_t vm=u64(f+at+24),vsize=u64(f+at+32);
            uint64_t off=u64(f+at+40),size=u64(f+at+48);
            uint32_t maxprot=u32(f+at+56),prot=u32(f+at+60);
            if(!vsize||vsize>WA_S13_MAX_SPAN||size>vsize||!range(off,size,n)||
               vm>UINT64_MAX-vsize||(maxprot&~7u)||(prot&~7u)||(prot&~maxprot))
                return WA_S13_BOUNDS;
            /* Reject file-backed __PAGEZERO and unaligned virtual memory segments. */
            if(vm&(WA_S13_ALIGN-1u))return WA_S13_UNSUPPORTED;
            if(size&&((off&(4095u))!=(vm&4095u)))return WA_S13_UNSUPPORTED;
            if((prot&4u)&&size)text++;
            struct wa_s13_segment *s=&a.seg[a.segments++];
            s->vmaddr=vm;s->vmsize=vsize;s->fileoff=off;s->filesize=size;s->initprot=prot;
            for(uint32_t j=0;j<a.segments-1;j++){
                const struct wa_s13_segment *t=&a.seg[j];
                if(vm<t->vmaddr+t->vmsize && t->vmaddr<vm+vsize)
                    return WA_S13_OVERLAP;
            }
            if(vm<min)min=vm;
            if(vm+vsize>max)max=vm+vsize;
        }else if(op==0x80000034u){ /* chained fixups: must be handled before kernel execution */
            if(sz<16||!range(u32(f+at+8),u32(f+at+12),n))return WA_S13_BOUNDS;
            a.has_fixups=1;
        }else if(op==0x5u){ /* LC_UNIXTHREAD: ARM_THREAD_STATE64 */
            if(sz>=16u+272u&&u32(f+at+8)==6u&&u32(f+at+12)>=68u){
                /* thread_state64: x0..x28, fp, lr, sp, pc */
                a.entry_vmaddr=u64(f+at+16+32u*8u);
            }
        }else if(op==0x80000028u){ /* LC_MAIN: ordinary app, unsupported as kernel */
            return WA_S13_UNSUPPORTED;
        }
        at+=sz;
    }
    if(at!=32u+cmdsize||!a.segments||!text)return WA_S13_NO_TEXT;
    if(max<min||max-min>WA_S13_MAX_SPAN||!range(0,max-min,UINT64_MAX-16384u))
        return WA_S13_BOUNDS;
    if(a.entry_vmaddr){
        int found=0;
        for(unsigned i=0;i<a.segments;i++){
            const struct wa_s13_segment *s=&a.seg[i];
            if((s->initprot&4u)&&a.entry_vmaddr>=s->vmaddr&&
               a.entry_vmaddr-s->vmaddr<s->vmsize)found=1;
        }
        if(!found)return WA_S13_BAD;
    }
    a.vm_base=min;
    a.vm_size=up16k(max-min);
    *out=a;return WA_S13_OK;
}
int wa_s13_macho_stage(const uint8_t *file,size_t n,const struct wa_s13_plan *p,
                      uint8_t *dest,size_t capacity){
    if(!file||!p||!dest||!p->segments||p->segments>WA_S13_SEGMENTS||
       !p->vm_size||p->vm_size>WA_S13_MAX_SPAN||p->vm_size>capacity)return WA_S13_BAD;
    /* Independently verify the caller's plan against the file: no forged ranges. */
    struct wa_s13_plan verified;
    if(wa_s13_macho_plan(file,n,&verified))return WA_S13_BAD;
    if(verified.segments!=p->segments||verified.vm_base!=p->vm_base||
       verified.vm_size!=p->vm_size)return WA_S13_BAD;
    for(unsigned i=0;i<p->segments;i++){
        const struct wa_s13_segment *s=&p->seg[i],*v=&verified.seg[i];
        if(s->vmaddr!=v->vmaddr||s->vmsize!=v->vmsize||
           s->fileoff!=v->fileoff||s->filesize!=v->filesize||
           s->initprot!=v->initprot)return WA_S13_BAD;
    }
    /* Refuse overlap with source buffer; even exact aliasing corrupts the image. */
    uintptr_t a=(uintptr_t)file,b=(uintptr_t)dest;
    if(a>UINTPTR_MAX-n||b>UINTPTR_MAX-capacity)return WA_S13_BOUNDS;
    if(a<b+capacity&&b<a+n)return WA_S13_OVERLAP;
    clear(dest,p->vm_size);
    for(unsigned i=0;i<p->segments;i++){
        const struct wa_s13_segment *s=&p->seg[i];
        uint64_t at=s->vmaddr-p->vm_base;
        if(!range(at,s->vmsize,p->vm_size))return WA_S13_BOUNDS;
        copy(dest+at,file+s->fileoff,s->filesize);
    }
    return WA_S13_OK;
}
int wa_s13_bootargs_draft(struct wa_s13_boot_args *out,const struct wa_s13_plan *plan,
                          uint64_t physbase,uint64_t capacity,
                          const char *cmdline,size_t cmdline_bytes){
    if(!out||!plan||!plan->segments||!plan->vm_size||
       plan->vm_size>capacity||!capacity||physbase>UINT64_MAX-capacity||
       !cmdline||!cmdline_bytes||cmdline_bytes>=WA_S13_BOOT_LINE||
       cmdline[cmdline_bytes-1]!=0)return WA_S13_BAD;
    struct wa_s13_boot_args a={0};
    /* Only metadata under the iOS26 xnu-12377 boot_args layout. */
    a.revision=2;a.version=2;
    a.virtBase=plan->vm_base;a.physBase=physbase;
    a.memSize=capacity;a.memSizeActual=capacity;
    a.topOfKernelData=physbase+plan->vm_size;
    for(size_t i=0;i<cmdline_bytes;i++)a.commandLine[i]=cmdline[i];
    *out=a;return WA_S13_OK;
}
