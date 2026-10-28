/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_gpu_lease.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct sample { int claim_power, power_readback, claim_smmu, smmu_readback,
                smmu_release, power_release;
                int p_acquire,p_probe,s_acquire,s_probe,s_release,p_release; };
static int pc(void *ctx,uint64_t epoch,uint64_t *token) {
  struct sample *s=ctx;assert(epoch==42);++s->p_acquire;
  if(s->claim_power==1) *token=123;
  return s->claim_power;
}
static int pr(void *ctx,uint64_t token) {struct sample*s=ctx;assert(token==123);++s->p_probe;return s->power_readback;}
static int sc(void *ctx,const struct wa_gpu_smmu_intent *i,uint64_t power,uint64_t *tok) {
  struct sample*s=ctx;assert(i->gmu_sid==5 && power==123);++s->s_acquire;
  if(s->claim_smmu==1) *tok=456;
  return s->claim_smmu;
}
static int sr(void *ctx,uint64_t t,const struct wa_gpu_smmu_intent*i) {
  struct sample*s=ctx;assert(t==456 && i->boot_epoch==42);++s->s_probe;return s->smmu_readback;
}
static int sl(void *ctx,uint64_t t) {struct sample*s=ctx;assert(t==456);++s->s_release;return s->smmu_release;}
static int pl(void *ctx,uint64_t t) {struct sample*s=ctx;assert(t==123);++s->p_release;return s->power_release;}
static struct wa_gpu_smmu_intent intent(void) {
  struct wa_gpu_smmu_intent t={42,0x80001000u,0x80000000u,0x4000u,5,2,0,7,1,4096,1};
  return t;
}
static void defaults(struct sample *s) {
  memset(s,0,sizeof(*s));s->claim_power=s->power_readback=1;
  s->claim_smmu=s->smmu_readback=s->smmu_release=s->power_release=1;
}
int main(void) {
  struct wa_gpu_smmu_intent t=intent();
  struct wa_gpu_lease_runtime state;
  struct sample f;
  struct wa_gpu_lease_ops ops={&f,pc,pr,sc,sr,sl,pl};
  assert(wa_gpu_smmu_intent_valid(&t));
  t.gmu_sid=t.secure_sid; assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.independent_gpu_smmu=0;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.translation_granule=16384;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.ttbr0_pa=0x7ffff000u;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.ttbr0_pa=0x80004000u;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.owned_table_pa=UINT64_MAX-4095u;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.asid=0;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();t.gmu_sid=t.gpu_sid;assert(!wa_gpu_smmu_intent_valid(&t));
  t=intent();
  defaults(&f);wa_gpu_lease_init(&state);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_BLOCKED);
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_OK);
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_BLOCKED);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_OK);
  assert(state.phase==WA_LEASE_SMMU);
  assert(wa_gpu_lease_shutdown(&state,&ops,1)==WA_LEASE_OK);
  assert(state.phase==WA_LEASE_IDLE && f.s_release==1 && f.p_release==1);
  defaults(&f);wa_gpu_lease_init(&state);
  f.claim_smmu=0;
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_OK);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_BLOCKED);
  assert(state.phase==WA_LEASE_IDLE && f.p_release==1);
  defaults(&f);wa_gpu_lease_init(&state);
  f.smmu_readback=0;
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_OK);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_UNKNOWN);
  assert(state.phase==WA_LEASE_QUARANTINED && f.p_release==0);
  assert(wa_gpu_lease_cold_reset(&state,0,43)==WA_LEASE_BLOCKED);
  assert(wa_gpu_lease_cold_reset(&state,1,43)==WA_LEASE_OK);
  defaults(&f);wa_gpu_lease_init(&state);
  f.power_readback=-1;
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_UNKNOWN);
  assert(state.phase==WA_LEASE_QUARANTINED && f.p_release==0);
  defaults(&f);wa_gpu_lease_init(&state);
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_OK);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_OK);
  assert(wa_gpu_lease_shutdown(&state,&ops,0)==WA_LEASE_UNKNOWN);
  assert(state.phase==WA_LEASE_QUARANTINED && f.s_release==0);
  defaults(&f);wa_gpu_lease_init(&state);
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_OK);
  assert(wa_gpu_claim_smmu(&state,&ops,&t)==WA_LEASE_OK);
  f.smmu_release=0;
  assert(wa_gpu_lease_shutdown(&state,&ops,1)==WA_LEASE_UNKNOWN);
  assert(state.phase==WA_LEASE_QUARANTINED && f.p_release==0);
  defaults(&f);wa_gpu_lease_init(&state);f.claim_power=-1;
  assert(wa_gpu_claim_power(&state,&ops,42)==WA_LEASE_UNKNOWN);
  assert(state.phase==WA_LEASE_QUARANTINED);
  puts("S4 power/SMMU lease ownership and uncertain DMA rollback tests passed");
  return 0;
}
