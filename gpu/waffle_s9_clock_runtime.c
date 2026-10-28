/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s9_clock_runtime.h"
#include "waffle_a750_hw_s5.h"
#include <stddef.h>

static int ops_valid(const struct wa_s9_clock_ops *o) {
    return o && o->claim && o->readback && o->release && o->epoch_live;
}
static int quarantine(struct wa_s9_gate *s) {
    if(s) s->phase=WA_S9_QUARANTINED;
    return WA_S9_CLOCK_UNCERTAIN;
}
/* Only safe for resources known to have been stopped or never exposed to DMA.
 * The first uncertain release aborts remaining releases and quarantines. */
static int release_reverse(struct wa_s9_gate *s,const struct wa_s9_clock_ops *ops) {
    while(s->held) {
        unsigned n=s->held-1;
        if(!s->tokens[n] || ops->release(ops->context,s->epoch,
               &wa_s9_clock_ids[n],s->tokens[n])!=1)
            return quarantine(s);
        s->tokens[n]=0;
        s->held=n;
    }
    s->phase=WA_S9_EMPTY;
    s->epoch=0;
    return WA_S9_CLOCK_OK;
}
int wa_s9_clock_claim(struct wa_s9_gate *s,const struct wa_s9_clock_ops *o,
                     uint64_t epoch,const char *board,const char *build,
                     const char *fdt,const char *firmware) {
    unsigned i;
    if(!s || !ops_valid(o) || !epoch || s->phase!=WA_S9_EMPTY || s->held ||
       !waffle_a750_s5_match_snapshot(board,build,fdt,firmware) ||
       o->epoch_live(o->context,epoch)!=1)return WA_S9_CLOCK_NO;
    s->epoch=epoch;
    for(i=0;i<WA_S9_CLOCK_COUNT;i++) {
        uint64_t token=0;
        int rc=o->claim(o->context,epoch,&wa_s9_clock_ids[i],&token);
        if(rc==0 && token==0) {
            int e=release_reverse(s,o);
            return e==WA_S9_CLOCK_OK?WA_S9_CLOCK_NO:WA_S9_CLOCK_UNCERTAIN;
        }
        if(rc!=1 || token==0) return quarantine(s);
        s->tokens[i]=token;
        s->held=i+1;
        /* Bad readback is ambiguous: do not release a voted clock. */
        if(o->readback(o->context,epoch,&wa_s9_clock_ids[i],token)!=1 ||
           o->epoch_live(o->context,epoch)!=1)
            return quarantine(s);
    }
    s->phase=WA_S9_ACTIVE;
    return WA_S9_CLOCK_OK;
}
int wa_s9_clock_live(struct wa_s9_gate *s,const struct wa_s9_clock_ops *o) {
    unsigned i;
    if(!s || !ops_valid(o) || s->phase!=WA_S9_ACTIVE ||
       s->held!=WA_S9_CLOCK_COUNT || !s->epoch)
        return WA_S9_CLOCK_NO;
    if(o->epoch_live(o->context,s->epoch)!=1)return quarantine(s);
    for(i=0;i<WA_S9_CLOCK_COUNT;i++) {
        if(!s->tokens[i] || o->readback(o->context,s->epoch,
           &wa_s9_clock_ids[i],s->tokens[i])!=1)
            return quarantine(s);
    }
    return WA_S9_CLOCK_OK;
}
int wa_s9_clock_stop(struct wa_s9_gate *s,const struct wa_s9_clock_ops *o,
                     int verified_dma_stopped) {
    if(!s || !ops_valid(o))return WA_S9_CLOCK_NO;
    if(s->phase==WA_S9_QUARANTINED)return WA_S9_CLOCK_UNCERTAIN;
    if(s->phase==WA_S9_EMPTY && !s->held)return WA_S9_CLOCK_OK;
    if(s->phase!=WA_S9_ACTIVE || s->held!=WA_S9_CLOCK_COUNT ||
       verified_dma_stopped!=1 || o->epoch_live(o->context,s->epoch)!=1)
        return quarantine(s);
    return release_reverse(s,o);
}
int wa_s9_smmu_attest(const struct wa_s9_gate *s,
                      const struct wa_s9_smmu_readback *hw,
                      uint64_t epoch,uint64_t gpu_root,
                      const struct wa_gpu_smmu_intent *gmu_intent) {
    if(!s || !hw || !gmu_intent || !epoch ||
       s->phase!=WA_S9_ACTIVE || s->held!=WA_S9_CLOCK_COUNT ||
       s->epoch!=epoch || hw->epoch!=epoch ||
       !hw->owner_token ||
       !wa_gpu_smmu_intent_valid(gmu_intent) ||
       gmu_intent->boot_epoch!=epoch ||
       hw->gpu_sid!=WAFFLE_A750_SID_USER ||
       hw->gmu_sid!=WAFFLE_A750_SID_GMU ||
       hw->secure_sid!=WAFFLE_A750_SID_SECURE ||
       hw->gpu_cb>=256 || hw->gmu_cb>=256 || hw->secure_cb>=256 ||
       hw->gpu_cb==hw->gmu_cb || hw->gpu_cb==hw->secure_cb ||
       hw->gmu_cb==hw->secure_cb ||
       hw->gmu_cb!=gmu_intent->context_bank ||
       hw->gmu_asid!=gmu_intent->asid ||
       !hw->gpu_asid || hw->gpu_asid==hw->gmu_asid ||
       hw->gpu_ttbr0_pa!=gpu_root || !gpu_root ||
       hw->gmu_ttbr0_pa!=gmu_intent->ttbr0_pa ||
       hw->translation_granule!=4096 ||
       !hw->independent_unit || !hw->bank_leases_exclusive ||
       !hw->stream_routes_readback || !hw->fault_on_unmatched ||
       !hw->cache_clean_verified || !hw->tlbi_completed ||
       !hw->actlr_security_verified || !hw->secure_owner_intact ||
       !hw->no_active_dma)
        return WA_S9_CLOCK_NO;
    return WA_S9_CLOCK_OK;
}

int wa_s9_runtime_ready(struct wa_s9_gate *s,const struct wa_s9_clock_ops *ops,
                        const struct wa_s9_smmu_readback *hw,uint64_t epoch,
                        uint64_t gpu_root,const struct wa_gpu_smmu_intent *intent) {
    int live=wa_s9_clock_live(s,ops);
    if(live!=WA_S9_CLOCK_OK)return live;
    return wa_s9_smmu_attest(s,hw,epoch,gpu_root,intent);
}
