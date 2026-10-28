/* SPDX-License-Identifier: MIT */
#include "waffle_s11_fdt.h"
#include <stdint.h>

static int same_bytes(const uint8_t *a,const char *b,size_t n)
{ for(size_t i=0;i<n;i++) if(a[i]!=(uint8_t)b[i]) return 0; return 1; }
static int same_name(const char *a,const char *b)
{ while(*a && *b) if(*a++!=*b++) return 0; return *a==*b; }

static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int range32(uint32_t off,uint32_t length,uint32_t total)
{ return off<=total && length<=total-off; }
static int string_at(const uint8_t *p,size_t length,size_t start)
{
    if(start>=length) return 0;
    for(size_t i=start;i<length;i++) if(!p[i]) return 1;
    return 0;
}
static int root_waffle(const uint8_t *p,size_t n)
{
    /* Qualcomm's live /model: "Qualcomm Technologies, Inc. Pineapple MTP,waffle". */
    const char token[]=",waffle";
    if (!n || p[n-1] != 0) return 0;
    for(size_t i=0;i+sizeof(token)<=n;i++)
        if(same_bytes(p+i,token,sizeof(token)-1) && p[i+sizeof(token)-1]==0)
            return 1;
    return 0;
}
static int root_pineapple(const uint8_t *p,size_t n)
{
    const char prefix[]="qcom,pineapple";
    size_t i=0;
    while(i<n) {
        size_t begin=i;
        while(i<n && p[i]) i++;
        if(i==n) return 0;
        if(i-begin>=sizeof(prefix)-1 && same_bytes(p+begin,prefix,sizeof(prefix)-1))
            return 1;
        ++i;
    }
    return 0;
}
int wa_s11_memory_contains(const struct wa_s11_memory_region *regions,
                           size_t count,uint64_t address,uint64_t size)
{
    if(!regions || !size || size>WA_S11_MAX_DTB || address>UINT64_MAX-size)
        return 0;
    if(count>4096u) return 0;
    for(size_t i=0;i<count;i++) {
        const uint64_t start=regions[i].physical_start;
        const uint64_t pages=regions[i].pages;
        /* Valid UEFI types for RAM/firmware-installed DTB. Exclude MMIO,
         * ACPI NVS, unusable memory, and all unrecognised type values. */
        const uint32_t t=regions[i].memory_type;
        if(!(t==0 || t==1 || t==2 || t==3 || t==4 || t==5 || t==6 || t==7 || t==9))
            continue;
        if(!pages || pages>UINT64_MAX/4096u) continue;
        const uint64_t bytes=pages*4096u;
        if(start>UINT64_MAX-bytes) continue;
        if(address>=start && address-start<=bytes && size<=bytes-(address-start))
            return 1;
    }
    return 0;
}
int wa_s11_fdt_inspect(const uint8_t *b,size_t available,
                      struct wa_s11_identity *out)
{
    struct wa_s11_identity tmp={0};
    if(!out) return WA_S11_NO_FDT;
    *out=(struct wa_s11_identity){0};
    if(!b || available<40) return WA_S11_NO_FDT;
    if(be32(b)!=0xd00dfeedu) return WA_S11_FORMAT;
    uint32_t total=be32(b+4), off_struct=be32(b+8), off_strings=be32(b+12);
    uint32_t off_mem=be32(b+16), version=be32(b+20), last=be32(b+24);
    uint32_t strings=be32(b+32), structs=be32(b+36);
    if(total<40 || total>WA_S11_MAX_DTB || total>available) return WA_S11_BOUNDS;
    if(version<17 || version>17 || last>version) return WA_S11_FORMAT;
    if((off_struct&3)||(off_strings&3)||(off_mem&7)|| off_mem<40 ||
       !range32(off_mem,16,total) || !range32(off_struct,structs,total) ||
       !range32(off_strings,strings,total) || !structs || !strings ||
       off_struct<40 || off_strings<40) return WA_S11_BOUNDS;
    /* The string and structure blocks may share surrounding padding but must
     * never intersect; overlapping ranges can reinterpret structural bytes. */
    if(off_struct<off_strings+strings && off_strings<off_struct+structs)
        return WA_S11_BOUNDS;
    /* A proper terminating empty 64-bit reservation-map record is required. */
    int mem_end=0;
    for(uint32_t i=off_mem;i+16u<=total;i+=16u) {
        if(i>=off_struct || i>=off_strings) break;
        uint32_t a=be32(b+i)|be32(b+i+4);
        uint32_t s=be32(b+i+8)|be32(b+i+12);
        if(!a&&!s){mem_end=1;break;}
    }
    if(!mem_end) return WA_S11_FORMAT;
    uint32_t at=off_struct, end=off_struct+structs;
    const uint8_t *s=b+off_strings;
    unsigned depth=0, found_end=0;
    tmp.totalsize=total;tmp.version=version;tmp.structure_bytes=structs;
    while(at+4u<=end) {
        uint32_t token=be32(b+at);at+=4u;
        if(token==1) {
            if(++depth>96u) return WA_S11_COMPLEXITY;
            ++tmp.nodes;
            uint32_t name_start=at;
            while(at<end && b[at]) at++;
            if(at==end) return WA_S11_BOUNDS;
            at++;
            uint32_t pad=(4u-(at&3u))&3u;
            if(at>end || pad>end-at) return WA_S11_BOUNDS;
            at+=pad;
            (void)name_start;
        } else if(token==2) {
            if(!depth) return WA_S11_FORMAT;
            depth--;
        } else if(token==3) {
            if(!depth || at>end || end-at<8u) return WA_S11_FORMAT;
            uint32_t bytes=be32(b+at),nameoff=be32(b+at+4);at+=8;
            if(bytes>end-at || !string_at(s,strings,nameoff)) return WA_S11_BOUNDS;
            if(depth==1) {
                const char *name=(const char *)(s+nameoff);
                if(same_name(name,"model")) tmp.model_waffle=root_waffle(b+at,bytes);
                if(same_name(name,"compatible")) tmp.compatible_pineapple=root_pineapple(b+at,bytes);
            }
            tmp.properties++;
            at+=bytes;
            uint32_t pad=(4u-(at&3u))&3u;
            if(pad>end-at) return WA_S11_BOUNDS;
            at+=pad;
        } else if(token==4) {
            /* NOP is permitted. */
        } else if(token==9) {
            if(depth || at!=end) return WA_S11_FORMAT;
            found_end=1;break;
        } else return WA_S11_FORMAT;
        if(tmp.nodes>65536u || tmp.properties>262144u) return WA_S11_COMPLEXITY;
    }
    if(!found_end) return WA_S11_FORMAT;
    if(!tmp.model_waffle || !tmp.compatible_pineapple) return WA_S11_BOARD;
    /* Simple accidental-corruption marker; manifest SHA-256 is offline. */
    uint32_t crc=0xffffffffu;
    for(uint32_t i=0;i<total;i++) {
        crc^=b[i];
        for(unsigned j=0;j<8;j++) crc=(crc>>1)^(0xedb88320u&((uint32_t)-(int)(crc&1u)));
    }
    tmp.crc32=~crc;
    *out=tmp;
    return WA_S11_OK;
}
