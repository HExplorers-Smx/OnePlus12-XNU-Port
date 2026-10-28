/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_S8_PREBOOT_H
#define WAFFLE_S8_PREBOOT_H
#include "waffle_smmu_pt.h"
#include "waffle_s7_scratch.h"
#include "waffle_hfi_alloc.h"
#include "waffle_hfi_runtime.h"
#include "waffle_gpu_lease.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Static SCRATCH staging before a real SMMU context is enabled.
 * This solves the S7 lifetime conflict: the firmware asks for SCRATCH AFTER
 * GMU starts, when changing sealed tables without BBM+TLBI is forbidden.
 * The 4K page, both IOVAs and table roots are provisioned and published
 * BEFORE GMU is released. The HFI reply merely describes the owned mapping.
 *
 * There is NO SMMU register writer here. XNU callbacks must implement actual
 * readback, context leases, cache clean, barriers and TLB synchronization.
 * No fallback to guessed clock votes / context banks / mapped MMIO.
 */
enum wa_s8_phase {
  WA_S8_EMPTY, WA_S8_PREPARED, WA_S8_ACTIVE, WA_S8_QUARANTINED
};
enum wa_s8_status { WA_S8_OK=1, WA_S8_REFUSED=0, WA_S8_UNCERTAIN=-1 };
struct wa_s8_runtime_ops {
  void *context;
  /* 1: exclusive owned 4K CPU+PA allocation, 0: no allocation, negative:
   * ambiguous. Reusing the 4K at a different epoch is forbidden. */
  int (*claim)(void *,uint64_t epoch,struct wa_s7_region *);
  /* A successful publish means zero/readback, required CPU cache clean and
   * all required DMA-visible barriers were confirmed by the platform. */
  int (*publish_backing)(void *,const struct wa_s7_region *);
  /* Only called prior to ANY table change. A false/uncertain reclaim is fatal. */
  int (*reclaim_unmapped)(void *,const struct wa_s7_region *);
  /* Publishes ALL owned 4 KiB table pages to memory visible by GPU SMMU.
   * Must not run after controller activation. */
  int (*publish_tables)(void *,const struct wa_pt_pool *,uint64_t root_pa);
  /* Returns nonzero ownership token ONLY after both SID0 (GPU) and SID5
   * (GMU) CONTEXT BANKS have independent verified 4 KiB translations,
   * TTBRs, fault-on-unmatched, TLBI complete, verified ACTLR/security and
   * no conflict with SID2 secure owner. The callback is NOT a shortcut
   * from FDT properties or UEFI observation reports. */
  int (*attest_runtime)(void *,uint64_t epoch,uint64_t gpu_root,
                        uint64_t gmu_root,uint64_t power_token,
                        uint64_t smmu_token,uint64_t *runtime_token);
};
struct wa_s8_preboot {
  struct wa_s8_runtime_ops ops;
  struct wa_s7_region region;
  struct wa_pt_pool *gpu_tables,*gmu_tables;
  struct wa_gpu_lease_runtime *lease;
  uint64_t epoch,gpu_root,gmu_root,runtime_token;
  enum wa_s8_phase phase;
  unsigned claimed,backing_published,reply_count,offer_assigned;
};
/* snapshot must match pinned build AND independent XNU runtime trust policy.
 * Metadata matching is necessary but never sufficient for hardware rights. */
int wa_s8_init(struct wa_s8_preboot *s,struct wa_pt_pool *gpu,
               struct wa_pt_pool *gmu,struct wa_gpu_lease_runtime *lease,
               uint64_t epoch,const struct wa_s8_runtime_ops *ops,
               const char *board,const char *build,const char *fdt_sha,
               const char *gmu_fw_sha);
/* Only modifies host-owned memory/page tables. On error after mapping or
 * publishing, quarantines and keeps DMA-facing resources, never frees. */
int wa_s8_prepare(struct wa_s8_preboot *s);
/* Uses existing S4 lease coordinator and independent, runtime-owned GPU+GMU
 * attestations. No device writes occur in this module, unless a trusted
 * caller explicitly supplies its own power/SMMU operation providers. */
int wa_s8_activate(struct wa_s8_preboot *s,
                   const struct wa_gpu_lease_ops *ops,
                   const struct wa_gpu_smmu_intent *gmu_intent);
/* Use this as S6 allocator: can only respond to a pre-mapped kind-2 4K
 * SCRATCH. No allocation/page-table edits are allowed at HFI runtime. */
struct wa_hfi_provider wa_s8_hfi_provider(struct wa_s8_preboot *s);
/* The S6 allocator caches duplicate requests without invoking reserve().
 * Thus S8 must attest BOTH on first and cached HFI replies. These wrapper
 * entry points require caller serialization against owner revocation and
 * device reset, including SMMU TLB/cache publication and the queue barrier.
 * Without that EXTERNAL lock, live hardware use is not allowed. */
int wa_s8_attach_s6(struct wa_s8_preboot *s,struct wa_s6_runtime *runtime,
                    const struct wa_s6_ops *ops,uint64_t epoch);
int wa_s8_pump_s6(struct wa_s8_preboot *s,struct wa_s6_runtime *runtime,
                  const struct wa_s6_ops *ops);
#ifdef __cplusplus
}
#endif
#endif
