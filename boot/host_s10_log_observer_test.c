/* SPDX-License-Identifier: MIT */
#include "waffle_s10_log.h"
#include "waffle_s10_observer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    uint8_t frame[WA_S10_FRAME_MAX];size_t used=7,count=0,consumed=0;
    const uint8_t bytes[]="UEFI: read-only probe";
    uint8_t kind=0;uint32_t sequence=0;const uint8_t *payload=NULL;
    assert(wa_s10_encode(WA_S10_TYPE_BOOT,13,bytes,sizeof(bytes)-1,
                        frame,sizeof(frame),&used)==WA_S10_OK);
    assert(used==WA_S10_HDR+sizeof(bytes)-1);
    assert(wa_s10_decode(frame,used,&kind,&sequence,&payload,&count,&consumed)==WA_S10_OK);
    assert(kind==WA_S10_TYPE_BOOT&&sequence==13&&count==sizeof(bytes)-1);
    assert(consumed==used&&!memcmp(payload,bytes,count));
    assert(wa_s10_decode(frame,used-1,&kind,&sequence,&payload,&count,&consumed)==WA_S10_TRUNCATED);
    assert(wa_s10_encode(99,0,bytes,sizeof(bytes)-1,frame,sizeof(frame),&used)==WA_S10_INVALID);
    assert(wa_s10_encode(WA_S10_TYPE_BOOT,1,bytes,513,frame,sizeof(frame),&used)==WA_S10_INVALID);
    assert(wa_s10_encode(WA_S10_TYPE_BOOT,13,bytes,sizeof(bytes)-1,
                        frame,sizeof(frame),&used)==WA_S10_OK);
    frame[WA_S10_HDR]^=1;
    assert(wa_s10_decode(frame,used,&kind,&sequence,&payload,&count,&consumed)==WA_S10_CHECKSUM);
    frame[WA_S10_HDR]^=1;
    frame[14]=1;
    assert(wa_s10_decode(frame,used,&kind,&sequence,&payload,&count,&consumed)==WA_S10_UNSUPPORTED);
    frame[14]=0;
    assert(wa_s10_decode(frame,used,&kind,&sequence,&payload,&count,&consumed)==WA_S10_OK);
    /* Other than controlled transport payload we never call MMIO APIs. */
    struct wa_s10_observer state;
    wa_s10_observer_init(&state,42);
    struct wa_s10_observation o={0};
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_BLOCKED);
    o.boot_epoch=42;o.current_boot=1;o.read_only=1;
    o.exclusive_power=1;o.exclusive_smmu=1;o.clocks_enabled_confirmed=1;
    o.mmio_mapped_by_owner=1;o.runtime_owner_confirmed=1;o.access_complete=1;
    o.power_owner_token=10;o.smmu_owner_token=20;o.reported_device=WA_S10_GPUCC;
    o.observation_serial=1;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_READY);
    o.observation_serial=1;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_QUARANTINE);
    o.observation_serial=2;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_BLOCKED);
    wa_s10_observer_init(&state,42);
    o.access_complete=0;o.observation_serial=3;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_BLOCKED);
    o.access_complete=1;o.boot_epoch=43;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_QUARANTINE);
    wa_s10_observer_init(&state,42);
    o.boot_epoch=42;o.reported_device=WA_S10_SMMU;o.observation_serial=7;
    assert(wa_s10_observer_accept(&state,&o)==WA_S10_READY);
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");assert(f);
        assert(fwrite(frame,1,used,f)==used);assert(!fclose(f));
    }
    puts("S10 host log + read-only observer PASS; no MMIO or USB claimed");
    return 0;
}
