/* Local synthetic-only tests; does not contain vendor firmware. */
#include "waffle_gmu_fw_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void word(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static size_t metadata(uint8_t *dst, uint32_t addr, uint32_t type, uint32_t value)
{
    word(dst,addr); word(dst+4,0); word(dst+8,type); word(dst+12,value);
    return 16;
}
static size_t block(uint8_t *dst, uint32_t addr, uint32_t bytes)
{
    word(dst,addr); word(dst+4,bytes); word(dst+8,0);word(dst+12,0);
    memset(dst+16,0x5a,bytes);
    return (size_t)bytes+16;
}
int main(void)
{
    uint8_t fw[100];
    struct waffle_fw_record out[8];
    size_t count=90, bytes=0, offset=99, length=99;
    memset(fw,0,sizeof(fw));
    bytes+=metadata(fw+bytes,0x60000000,WAFFLE_FW_PREALLOC_PERSIST,0x1000);
    bytes+=block(fw+bytes,0,4);
    bytes+=block(fw+bytes,0x4000,4);
    assert(waffle_gmu_firmware_plan(fw,bytes,out,8,&count)==WAFFLE_FW_OK);
    assert(count==3 && out[0].region==WAFFLE_FW_KERNEL);
    assert(out[1].region==WAFFLE_FW_ITCM && out[2].region==WAFFLE_FW_ICACHE);
    assert(out[0].payload_offset==16 && out[2].payload_offset==52);
    assert(waffle_gpu_ucode_span(fw,bytes,&offset,&length)==WAFFLE_FW_OK);
    assert(offset==4 && length==bytes-4);

    /* Overlap rejected without output modifications. */
    word(fw+36,0);
    memset(out,0xac,sizeof(out));count=91;
    assert(waffle_gmu_firmware_plan(fw,bytes,out,8,&count)==WAFFLE_FW_DATA_OVERLAP);
    assert(count==0 && out[0].gmu_virtual_address==0xacacacacu);
    word(fw+36,0x4000);

    count=91;
    assert(waffle_gmu_firmware_plan(fw,bytes-1,out,8,&count)==WAFFLE_FW_TRUNCATED);
    assert(count==0);
    count=91;
    assert(waffle_gmu_firmware_plan(fw,bytes,out,2,&count)==WAFFLE_FW_TOO_MANY);
    assert(count==0);
    word(fw+36,0xfffffffcu);
    count=91;
    assert(waffle_gmu_firmware_plan(fw,bytes,out,8,&count)==WAFFLE_FW_BAD_REGION);
    assert(count==0);
    word(fw+36,0x4000);

    /* Metadata following data is rejected. */
    bytes+=metadata(fw+bytes,0x60001000,WAFFLE_FW_PREALLOC,0x1000);
    count=91;
    assert(waffle_gmu_firmware_plan(fw,bytes,out,8,&count)==WAFFLE_FW_BAD_ORDER);
    assert(count==0);

    assert(waffle_gpu_ucode_span(fw,7,&offset,&length)==WAFFLE_FW_BAD_INPUT);
    assert(offset==0 && length==0);
    printf("waffle synthetic GMU firmware planner passed\n");
    return 0;
}
