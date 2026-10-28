/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_S9_RUNTIME_GUARD_H
#define WAFFLE_S9_RUNTIME_GUARD_H
#include "waffle_s8_preboot.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Snapshot must be supplied by a REAL XNU runtime observer holding independent
 * GPU power/AOP and SMMU owner leases. It cannot be derived from FDT, static
 * clock indices, UEFI tables or a host mock. There is NO MMIO writer here.
 */
struct wa_s9_smmu_context {
  uint32_t sid,bank,asid,granule;
  uint64_t ttbr_pa;
  unsigned enabled,tlbi_complete,page_tables_published,
           fault_on_unmatched,normal_nc_data;
};
struct wa_s9_observation {
  uint64_t epoch,serial,power_token,smmu_token,runtime_token;
  unsigned exclusive_power_aop_clock_lease,exclusive_smmu_lease,
           power_clock_readback_good,sid2_reserved_for_secure,
           no_unmatched_stream_mappings,dma_quiescence_policy_known;
  struct wa_s9_smmu_context gpu,gmu;
};
struct wa_s9_probe_ops {
  void *context;
  /* 1 means snapshot from the CURRENT XNU runtime, 0 means unavailable,
   * other numbers mean ambiguous (may have already touched hardware). */
  int (*observe)(void *,uint64_t epoch,struct wa_s9_observation *out);
};
struct wa_s9_guard {
  struct wa_s8_preboot *s8;
  uint64_t epoch,last_serial;
  unsigned admitted,quarantined;
};
enum wa_s9_status { WA_S9_OK=1,WA_S9_BLOCKED=0,WA_S9_UNCERTAIN=-1 };
void wa_s9_guard_init(struct wa_s9_guard *,struct wa_s8_preboot *);
/* No status is a hardware success without an actual, independent observer.
 * Mismatched/ambiguous state after starting always quarantines. */
int wa_s9_observe(struct wa_s9_guard *,const struct wa_s9_probe_ops *);
/* Must be invoked under caller's XNU owner mutex. Re-check before AND after
 * accessing the HFI ring, including when the allocator reuses cached replies.
 * If post-check fails, a reply might be DMA-visible: quarantine, never free.
 */
int wa_s9_pump_s6(struct wa_s9_guard *,const struct wa_s9_probe_ops *,
                  struct wa_s6_runtime *,const struct wa_s6_ops *);
#ifdef __cplusplus
}
#endif
#endif
