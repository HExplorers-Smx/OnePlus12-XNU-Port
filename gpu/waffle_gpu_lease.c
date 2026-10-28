/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_gpu_lease.h"
#include <stddef.h>
#include <string.h>

int wa_gpu_smmu_intent_valid(const struct wa_gpu_smmu_intent *p) {
  uint64_t end;
  if (!p || !p->boot_epoch || p->translation_granule!=4096u ||
      p->independent_gpu_smmu!=1u || p->gmu_sid==p->secure_sid ||
      p->gmu_sid==p->gpu_sid || p->gmu_sid>=65536u ||
      p->context_bank>=256u || p->asid==0u || p->asid>65535u) return 0;
  if (!p->owned_table_pa || !p->ttbr0_pa || !p->owned_table_bytes ||
      (p->owned_table_pa&4095u) || (p->ttbr0_pa&4095u) ||
      (p->owned_table_bytes&4095u)) return 0;
  if (UINT64_MAX-p->owned_table_pa<p->owned_table_bytes) return 0;
  end=p->owned_table_pa+p->owned_table_bytes;
  if (p->ttbr0_pa<p->owned_table_pa || p->ttbr0_pa>end-4096u ||
      p->owned_table_bytes<4096u) return 0;
  return 1;
}
void wa_gpu_lease_init(struct wa_gpu_lease_runtime *s) {
  if (s) memset(s,0,sizeof(*s));
}
static int power_ops_valid(const struct wa_gpu_lease_ops *p) {
  return p && p->power_claim && p->power_readback && p->power_release;
}
int wa_gpu_claim_power(struct wa_gpu_lease_runtime *s,
                       const struct wa_gpu_lease_ops *p,uint64_t epoch) {
  uint64_t token=0;
  int rc;
  if (!s || !power_ops_valid(p) || !epoch || s->phase!=WA_LEASE_IDLE)
    return WA_LEASE_BLOCKED;
  rc=p->power_claim(p->context,epoch,&token);
  if (rc==0 && token==0) return WA_LEASE_BLOCKED;
  if (rc!=1 || !token) {s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;}
  s->boot_epoch=epoch;s->power_token=token;s->phase=WA_LEASE_POWER;
  if (p->power_readback(p->context,token)!=1) {
    s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;
  }
  return WA_LEASE_OK;
}
int wa_gpu_claim_smmu(struct wa_gpu_lease_runtime *s,
                      const struct wa_gpu_lease_ops *p,
                      const struct wa_gpu_smmu_intent *intent) {
  uint64_t token=0;
  int rc;
  if (!s || !p || !p->smmu_claim || !p->smmu_readback ||
      !p->power_release || s->phase!=WA_LEASE_POWER ||
      !wa_gpu_smmu_intent_valid(intent) || intent->boot_epoch!=s->boot_epoch)
    return WA_LEASE_BLOCKED;
  rc=p->smmu_claim(p->context,intent,s->power_token,&token);
  if (rc==0 && token==0) {
    /* SMMU refusal is a definite no-write: unwind proven power ownership. */
    if (p->power_release(p->context,s->power_token)!=1) {
      s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;
    }
    wa_gpu_lease_init(s);
    return WA_LEASE_BLOCKED;
  }
  if (rc!=1 || !token) {s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;}
  s->smmu_token=token;s->phase=WA_LEASE_SMMU;
  if (p->smmu_readback(p->context,token,intent)!=1) {
    s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;
  }
  return WA_LEASE_OK;
}
int wa_gpu_lease_shutdown(struct wa_gpu_lease_runtime *s,
                          const struct wa_gpu_lease_ops *p,
                          int verified_dma_stopped) {
  if (!s || !p || !p->power_release) return WA_LEASE_BLOCKED;
  if (s->phase==WA_LEASE_IDLE) return WA_LEASE_OK;
  if (s->phase==WA_LEASE_QUARANTINED) return WA_LEASE_UNKNOWN;
  if (verified_dma_stopped!=1) {
    s->phase=WA_LEASE_QUARANTINED;
    return WA_LEASE_UNKNOWN;
  }
  if (s->phase==WA_LEASE_SMMU) {
    if (!p->smmu_release || p->smmu_release(p->context,s->smmu_token)!=1) {
      s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;
    }
    s->smmu_token=0;s->phase=WA_LEASE_POWER;
  }
  if (s->phase==WA_LEASE_POWER) {
    if (p->power_release(p->context,s->power_token)!=1) {
      s->phase=WA_LEASE_QUARANTINED;return WA_LEASE_UNKNOWN;
    }
  }
  wa_gpu_lease_init(s);
  return WA_LEASE_OK;
}
int wa_gpu_lease_cold_reset(struct wa_gpu_lease_runtime *s,
                            int verified_hardware_reset,uint64_t new_epoch) {
  if (!s || verified_hardware_reset!=1 || !new_epoch ||
      new_epoch==s->boot_epoch) return WA_LEASE_BLOCKED;
  wa_gpu_lease_init(s);
  return WA_LEASE_OK;
}
