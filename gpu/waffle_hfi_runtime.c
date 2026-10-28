/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_hfi_runtime.h"
#include <string.h>

void wa_s6_init(struct wa_s6_runtime *state) {
  if(state)memset(state,0,sizeof(*state));
}
static int lease_ok(const struct wa_s6_ops *ops,uint64_t epoch) {
  const struct wa_gpu_lease_runtime *l=ops?ops->lease:0;
  return l && l->phase==WA_LEASE_SMMU && l->boot_epoch==epoch &&
         l->power_token && l->smmu_token;
}
static int quarantine(struct wa_s6_runtime *state) {
  state->quarantined=1;
  return WA_S6_QUARANTINED;
}
int wa_s6_attach(struct wa_s6_runtime *state,const struct wa_s6_ops *ops,
                 uint64_t epoch) {
  uint64_t token=0;
  int p;
  if(!state || !ops || !epoch) return WA_S6_BLOCKED;
  if(state->quarantined)return WA_S6_QUARANTINED;
  if(state->attached || !lease_ok(ops,epoch) || !ops->attest_gmu_hfi ||
     !ops->allocator.reserve || !ops->allocator.publish ||
     !ops->allocator.release || ops->io.base_iova!=WA_RING_IOVA ||
     wa_ring_check(&ops->io,0)!=WA_RING_OK ||
     wa_ring_check(&ops->io,1)!=WA_RING_OK ||
     wa_ring_check(&ops->io,2)!=WA_RING_OK) return WA_S6_BLOCKED;
  p=ops->attest_gmu_hfi(ops->attest_context,epoch,&token);
  if(p<0 || (p==1 && !token) || (p==0 && token))return quarantine(state);
  if(p!=1)return WA_S6_BLOCKED;
  /* Only after checking this is the SAME authenticated boot. */
  state->epoch=epoch;state->transport_token=token;state->attached=1;
  return WA_S6_PROCESSED;
}
int wa_s6_pump_one(struct wa_s6_runtime *state,const struct wa_s6_ops *ops) {
  uint32_t frame[WA_RING_MAX_PACKET],reply[WA_HFI_V1_WORDS];
  size_t count=0,reply_words=0;
  uint64_t token=0;
  int rc;
  if(!state || !ops)return WA_S6_BLOCKED;
  if(state->quarantined)return WA_S6_QUARANTINED;
  if(!state->attached || !lease_ok(ops,state->epoch) ||
     !ops->attest_gmu_hfi || !state->transport_token ||
     ops->io.base_iova!=WA_RING_IOVA) return quarantine(state);
  rc=ops->attest_gmu_hfi(ops->attest_context,state->epoch,&token);
  if(rc!=1 || token!=state->transport_token)return quarantine(state);
  rc=wa_ring_receive(&ops->io,1,frame,WA_RING_MAX_PACKET,&count);
  if(rc==WA_RING_EMPTY)
    rc=wa_ring_receive(&ops->io,2,frame,WA_RING_MAX_PACKET,&count);
  if(rc==WA_RING_EMPTY)return WA_S6_EMPTY;
  if(rc!=WA_RING_OK)return quarantine(state);
  /* Only F2H memory alloc request (20/type CMD) is implemented at S6. */
  if((frame[0]&255u)!=WA_HFI_ALLOC_ID || ((frame[0]>>16)&15u)!=0u) {
    ++state->rejected;
    return quarantine(state);
  }
  rc=wa_hfi_process_alloc(&state->allocator,&ops->allocator,
                          frame,count,reply,WA_HFI_V1_WORDS,&reply_words);
  if(rc!=WA_HFI_OK || !reply_words) {
    ++state->rejected;
    return quarantine(state);
  }
  /* A failed enqueue may leave allocations DMA-visible: NEVER release here. */
  if(wa_ring_send(&ops->io,reply,reply_words)!=WA_RING_OK)
    return quarantine(state);
  ++state->handled;
  return WA_S6_PROCESSED;
}
