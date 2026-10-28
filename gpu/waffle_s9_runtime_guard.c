/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s9_runtime_guard.h"
#include "waffle_a750_hw_s5.h"
#include <string.h>

void wa_s9_guard_init(struct wa_s9_guard *g,struct wa_s8_preboot *s8) {
  if(!g)return;
  memset(g,0,sizeof(*g));
  g->s8=s8;
  if(s8)g->epoch=s8->epoch;
}
static int isolate(struct wa_s9_guard *g) {
  if(g) {
    g->quarantined=1;
    if(g->s8)g->s8->phase=WA_S8_QUARANTINED;
    /* Intentionally retain power / SMMU / backing resources. */
  }
  return WA_S9_UNCERTAIN;
}
static int context_ok(const struct wa_s9_smmu_context *s,
                      uint32_t expected_sid,uint64_t root) {
  return s && s->sid==expected_sid && s->granule==4096u &&
    s->bank<256u && s->asid!=0u && s->asid<=65535u &&
    s->ttbr_pa==root && s->enabled==1u && s->tlbi_complete==1u &&
    s->page_tables_published==1u && s->fault_on_unmatched==1u &&
    s->normal_nc_data==1u;
}
static int verified(const struct wa_s9_guard *g,
                    const struct wa_s9_observation *o) {
  const struct wa_s8_preboot *s;
  if(!g || !(s=g->s8) || !o || !g->epoch || !o->serial ||
     g->epoch!=o->epoch || s->phase!=WA_S8_ACTIVE ||
     s->runtime_token==0 || !s->lease ||
     s->lease->phase!=WA_LEASE_SMMU ||
     s->lease->boot_epoch!=o->epoch ||
     s->lease->power_token!=o->power_token ||
     s->lease->smmu_token!=o->smmu_token ||
     s->runtime_token!=o->runtime_token ||
     !s->gpu_tables || !s->gmu_tables ||
     !s->gpu_tables->sealed || !s->gmu_tables->sealed ||
     wa_pt_audit(s->gpu_tables,0)!=WA_PT_OK ||
     wa_pt_audit(s->gmu_tables,0)!=WA_PT_OK ||
     o->exclusive_power_aop_clock_lease!=1u ||
     o->exclusive_smmu_lease!=1u ||
     o->power_clock_readback_good!=1u ||
     o->sid2_reserved_for_secure!=1u ||
     o->no_unmatched_stream_mappings!=1u ||
     o->dma_quiescence_policy_known!=1u ||
     !context_ok(&o->gpu,WAFFLE_A750_SID_USER,s->gpu_root) ||
     !context_ok(&o->gmu,WAFFLE_A750_SID_GMU,s->gmu_root) ||
     o->gpu.bank==o->gmu.bank || o->gpu.asid==o->gmu.asid)
    return 0;
  return 1;
}
int wa_s9_observe(struct wa_s9_guard *g,const struct wa_s9_probe_ops *ops) {
  struct wa_s9_observation o;
  int rc;
  if(!g || g->quarantined)return WA_S9_UNCERTAIN;
  if(!ops || !ops->observe || !g->s8 || !g->epoch ||
     g->s8->phase!=WA_S8_ACTIVE)
    return g->admitted?isolate(g):WA_S9_BLOCKED;
  memset(&o,0,sizeof(o));
  rc=ops->observe(ops->context,g->epoch,&o);
  if(rc==0 && !g->admitted)return WA_S9_BLOCKED;
  if(rc!=1 || !verified(g,&o) || (g->last_serial && o.serial<g->last_serial))
    return isolate(g);
  g->admitted=1;
  g->last_serial=o.serial;
  return WA_S9_OK;
}
int wa_s9_pump_s6(struct wa_s9_guard *g,const struct wa_s9_probe_ops *obs,
                  struct wa_s6_runtime *runtime,const struct wa_s6_ops *ops) {
  int rc;
  if(!g || !runtime || !ops)return WA_S9_BLOCKED;
  if(wa_s9_observe(g,obs)!=WA_S9_OK)return g->quarantined?WA_S9_UNCERTAIN:WA_S9_BLOCKED;
  rc=wa_s8_pump_s6(g->s8,runtime,ops);
  if(rc==WA_S6_QUARANTINED)return isolate(g);
  if(wa_s9_observe(g,obs)!=WA_S9_OK)return g->quarantined?WA_S9_UNCERTAIN:WA_S9_BLOCKED;
  return rc;
}
