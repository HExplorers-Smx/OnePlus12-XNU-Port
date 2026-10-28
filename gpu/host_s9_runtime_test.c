/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s9_clock_runtime.h"
#include "waffle_s9_runtime_guard.h"
#include "waffle_a750_hw_s5.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct sim {unsigned claimed, released,deny_at,readback_fail,release_fail,epoch_live;uint64_t now;};
static int epoch(void *p,uint64_t e){struct sim *s=p;return s->epoch_live && s->now==e;}
static int claim(void *p,uint64_t e,const struct wa_s9_clock_id *id,uint64_t *tok){
    struct sim *s=p;unsigned i=s->claimed++;
    assert(e==s->now && id && tok && id->role_mask);
    if(s->deny_at==i+1)return 0;
    *tok=100u+i;
    return 1;
}
static int readback(void *p,uint64_t e,const struct wa_s9_clock_id *id,uint64_t tok){
    struct sim *s=p;assert(id && tok>=100 && e==s->now);
    return s->readback_fail==tok-99?0:1;
}
static int release_clock(void *p,uint64_t e,const struct wa_s9_clock_id *id,uint64_t tok){
    struct sim *s=p;assert(id && tok && e==s->now);
    s->released++;
    return s->release_fail==s->released?0:1;
}
static struct wa_s9_clock_ops callback(struct sim *sim) {
    struct wa_s9_clock_ops x={sim,claim,readback,release_clock,epoch};return x;
}
static int begin(struct wa_s9_gate *gate,const struct wa_s9_clock_ops *ops) {
    const struct waffle_a750_pins *p=&waffle_a750_s5_pins;
    return wa_s9_clock_claim(gate,ops,44,p->board,p->android_build,
                             p->runtime_fdt_sha256,p->gmu_firmware_sha256);
}
static struct wa_gpu_smmu_intent intent(void){
    struct wa_gpu_smmu_intent p={0};
    p.boot_epoch=44;p.ttbr0_pa=0x200000;p.owned_table_pa=0x200000;
    p.owned_table_bytes=0x3000;p.gmu_sid=5;p.secure_sid=2;p.gpu_sid=0;
    p.context_bank=9;p.asid=23;p.translation_granule=4096;
    p.independent_gpu_smmu=1;return p;
}
static struct wa_s9_smmu_readback good(void){
    struct wa_s9_smmu_readback r={0};
    r.epoch=44;r.owner_token=55;r.gpu_ttbr0_pa=0x300000;
    r.gmu_ttbr0_pa=0x200000;r.gpu_sid=0;r.gmu_sid=5;r.secure_sid=2;
    r.gpu_cb=8;r.gmu_cb=9;r.secure_cb=10;
    r.gpu_asid=22;r.gmu_asid=23;r.translation_granule=4096;
    r.independent_unit=r.bank_leases_exclusive=r.stream_routes_readback=1;
    r.fault_on_unmatched=r.cache_clean_verified=r.tlbi_completed=1;
    r.actlr_security_verified=r.secure_owner_intact=r.no_active_dma=1;
    return r;
}
int main(void){
    struct sim s={0};struct wa_s9_clock_ops ops;struct wa_s9_gate g={0};
    struct wa_gpu_smmu_intent i=intent();struct wa_s9_smmu_readback r=good();
    s.now=44;s.epoch_live=1;ops=callback(&s);
    assert(WA_S9_CLOCK_COUNT==9);
    assert(begin(&g,&ops)==1 && s.claimed==9 && g.held==9);
    assert(wa_s9_clock_live(&g,&ops)==1);
    assert(wa_s9_smmu_attest(&g,&r,44,0x300000,&i)==1);
    assert(wa_s9_runtime_ready(&g,&ops,&r,44,0x300000,&i)==WA_S9_CLOCK_OK);
    r.secure_cb=r.gmu_cb;assert(!wa_s9_smmu_attest(&g,&r,44,0x300000,&i));r=good();
    r.tlbi_completed=0;assert(!wa_s9_smmu_attest(&g,&r,44,0x300000,&i));r=good();
    r.secure_owner_intact=0;assert(!wa_s9_smmu_attest(&g,&r,44,0x300000,&i));r=good();
    r.gmu_ttbr0_pa=0x400000;assert(!wa_s9_smmu_attest(&g,&r,44,0x300000,&i));
    assert(wa_s9_clock_stop(&g,&ops,0)==-1 && g.phase==WA_S9_QUARANTINED);
    assert(s.released==0);
    memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));s.now=44;s.epoch_live=1;ops=callback(&s);
    s.deny_at=3;assert(begin(&g,&ops)==0 && s.released==2 && g.phase==WA_S9_EMPTY);
    memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));s.now=44;s.epoch_live=1;ops=callback(&s);
    s.readback_fail=2;assert(begin(&g,&ops)==-1 && g.phase==WA_S9_QUARANTINED && s.released==0);
    memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));s.now=44;s.epoch_live=1;ops=callback(&s);
    assert(begin(&g,&ops)==1);s.now=45;assert(wa_s9_clock_live(&g,&ops)==-1);
    memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));s.now=44;s.epoch_live=1;ops=callback(&s);
    assert(begin(&g,&ops)==1);s.release_fail=1;
    assert(wa_s9_clock_stop(&g,&ops,1)==-1 && g.phase==WA_S9_QUARANTINED && s.released==1);
    memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));s.now=44;s.epoch_live=1;ops=callback(&s);
    assert(begin(&g,&ops)==1);assert(wa_s9_clock_stop(&g,&ops,1)==1 && s.released==9);
    puts("S9 clock lease and SMMU attestation passed (mock native-owner, no MMIO)");return 0;
}
