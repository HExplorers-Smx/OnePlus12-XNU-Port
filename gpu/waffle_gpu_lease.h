/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_GPU_LEASE_H
#define WAFFLE_GPU_LEASE_H
#include <stdint.h>
/*
 * Fail-closed transaction coordinator for future SM8650 GPU power and SMMU
 * providers. Does NOT implement a register backend. Bootloader/UEFI ownership
 * cannot be reused as an XNU runtime lease. All arguments must come from a
 * verified boot snapshot of THIS waffle build and are presently unavailable.
 */
#ifdef __cplusplus
extern "C" {
#endif

enum wa_gpu_lease_phase { WA_LEASE_IDLE, WA_LEASE_POWER,
                          WA_LEASE_SMMU, WA_LEASE_QUARANTINED };
enum wa_gpu_lease_result { WA_LEASE_OK=1, WA_LEASE_BLOCKED=0,
                            WA_LEASE_UNKNOWN=-1 };
struct wa_gpu_smmu_intent {
  uint64_t boot_epoch;
  uint64_t ttbr0_pa, owned_table_pa, owned_table_bytes;
  uint32_t gmu_sid, secure_sid, gpu_sid, context_bank, asid;
  uint32_t translation_granule; /* exactly 4096 for Gen7 GPU SMMU tables */
  uint32_t independent_gpu_smmu;/* independently verified, not apps-SMMU */
};
struct wa_gpu_lease_runtime {
  enum wa_gpu_lease_phase phase;
  uint64_t boot_epoch, power_token, smmu_token;
};
struct wa_gpu_lease_ops {
  void *context;
  /* Return 1 only with an EXCLUSIVE lease and nonzero token.
     Return 0 only if no resource was acquired. Negative = uncertain. */
  int (*power_claim)(void *context,uint64_t boot_epoch,uint64_t *token);
  /* Returns 1 only if all required power/clock readbacks and reset
     conditions are VERIFIED under the supplied lease. */
  int (*power_readback)(void *context,uint64_t token);
  int (*smmu_claim)(void *context,const struct wa_gpu_smmu_intent *,
                    uint64_t power_token,uint64_t *smmu_token);
  /* Must attest to SID, bank, TTBR, 4 KiB TCR and TLBI, plus exclusive
     ownership. No implicit choice of an available context bank is allowed. */
  int (*smmu_readback)(void *context,uint64_t smmu_token,
                       const struct wa_gpu_smmu_intent *intent);
  /* Return 1 ONLY after confirmed, full release. */
  int (*smmu_release)(void *context,uint64_t smmu_token);
  int (*power_release)(void *context,uint64_t power_token);
};

int wa_gpu_smmu_intent_valid(const struct wa_gpu_smmu_intent *intent);
void wa_gpu_lease_init(struct wa_gpu_lease_runtime *runtime);
int wa_gpu_claim_power(struct wa_gpu_lease_runtime *runtime,
                       const struct wa_gpu_lease_ops *ops,uint64_t epoch);
int wa_gpu_claim_smmu(struct wa_gpu_lease_runtime *runtime,
                      const struct wa_gpu_lease_ops *ops,
                      const struct wa_gpu_smmu_intent *intent);
/* Stop requires an independent confirmation that ALL DMA and command queues
 * are stopped, not a clean CPU copy. Unknown stop/release => quarantine. */
int wa_gpu_lease_shutdown(struct wa_gpu_lease_runtime *runtime,
                          const struct wa_gpu_lease_ops *ops,
                          int verified_dma_stopped);
/* Only hardware cold reset, verified by a trusted platform owner, permits
 * resetting local book-keeping. An ordinary Android warm reboot is not proof. */
int wa_gpu_lease_cold_reset(struct wa_gpu_lease_runtime *runtime,
                            int verified_hardware_reset,uint64_t new_epoch);
#ifdef __cplusplus
}
#endif
#endif
