/* SPDX-License-Identifier: MIT */
#include "waffle_s10_log.h"
#define WA_MAGIC0 'W'
#define WA_MAGIC1 'F'
#define WA_MAGIC2 'L'
#define WA_MAGIC3 'G'
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p,uint32_t v) { put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p)|((uint32_t)get16(p+2)<<16); }
static uint32_t step(uint32_t c,uint8_t b)
{
    c^=b;
    for(unsigned i=0;i<8;i++) c=(c>>1)^(0xedb88320u&((uint32_t)-(int)(c&1u)));
    return c;
}
uint32_t wa_s10_crc32(const uint8_t *bytes,size_t size)
{
    uint32_t c=0xffffffffu;
    if(!bytes && size) return 0;
    for(size_t i=0;i<size;i++) c=step(c,bytes[i]);
    return ~c;
}
static uint32_t frame_crc(const uint8_t *bytes,size_t payload_size)
{
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<16;i++) c=step(c,bytes[i]);
    for(size_t i=0;i<payload_size;i++) c=step(c,bytes[WA_S10_HDR+i]);
    return ~c;
}
static int type_ok(uint8_t t) { return t>=WA_S10_TYPE_BOOT && t<=WA_S10_TYPE_HANDOFF; }
int wa_s10_encode(uint8_t type,uint32_t seq,const uint8_t *payload,size_t count,
                  uint8_t *out,size_t capacity,size_t *used)
{
    if(!used) return WA_S10_INVALID;
    *used=0;
    if(!out || !type_ok(type) || (count&&!payload) || count>WA_S10_PAYLOAD_MAX)
        return WA_S10_INVALID;
    if(capacity<WA_S10_HDR+count) return WA_S10_TRUNCATED;
    out[0]=WA_MAGIC0;out[1]=WA_MAGIC1;out[2]=WA_MAGIC2;out[3]=WA_MAGIC3;
    out[4]=WA_S10_VERSION;out[5]=type;put16(out+6,0);
    put32(out+8,seq);put16(out+12,(uint16_t)count);put16(out+14,0);
    /* A caller may pass payload=out+20, so do not corrupt payload first. */
    for(size_t i=0;i<count;i++) out[WA_S10_HDR+i]=payload[i];
    put32(out+16,frame_crc(out,count));
    *used=WA_S10_HDR+count;
    return WA_S10_OK;
}
int wa_s10_decode(const uint8_t *input,size_t length,uint8_t *type,
                  uint32_t *seq,const uint8_t **payload,size_t *count,size_t *consumed)
{
    if(consumed) *consumed=0;
    if(count) *count=0;
    if(payload) *payload=NULL;
    if(!input||!type||!seq||!payload||!count||!consumed) return WA_S10_INVALID;
    if(length<WA_S10_HDR) return WA_S10_TRUNCATED;
    if(input[0]!=WA_MAGIC0||input[1]!=WA_MAGIC1||input[2]!=WA_MAGIC2||input[3]!=WA_MAGIC3)
        return WA_S10_INVALID;
    if(input[4]!=WA_S10_VERSION||!type_ok(input[5])||get16(input+6)||get16(input+14))
        return WA_S10_UNSUPPORTED;
    size_t n=get16(input+12);
    if(n>WA_S10_PAYLOAD_MAX) return WA_S10_INVALID;
    if(length<WA_S10_HDR+n) return WA_S10_TRUNCATED;
    if(get32(input+16)!=frame_crc(input,n)) return WA_S10_CHECKSUM;
    *type=input[5];*seq=get32(input+8);*payload=input+WA_S10_HDR;
    *count=n;*consumed=WA_S10_HDR+n;
    return WA_S10_OK;
}
