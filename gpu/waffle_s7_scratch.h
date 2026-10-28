/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_S7_SCRATCH_H
#define WAFFLE_S7_SCRATCH_H
#include "waffle_gpu_lease.h"
#include "waffle_hfi_alloc.h"
#include "waffle_smmu_pt.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host-only 4 KiB SCRATCH integration for Gen7 HFI kind 2. Firmware cannot
 * use this memory until a future XNU platform provider has installed and
 * audited the table roots, acquired exclusive SMMU leases, done TLBI and
 * published the required cache lines. None of that is implemented here.
 */
struct wa_s7_region {
  void *owner_token;
  unsigned char *host;
  uint64_t pa,gfx_iova,gmu_iova;
  uint32_t bytes;
};
struct wa_s7_memory_ops {
  void *context;
  /* 1=an exclusively owned 4K backing; zero=definitely none; negative=unknown */
  int (*claim)(void *,uint64_t boot_epoch,struct wa_s7_region *out);
  /* 1 only if the CPU zero/readback and DMA cache publication completed */
  int (*publish)(void *,const struct wa_s7_region *);
  /* 1 only if no device acquired the memory and ownership was reclaimed */
  int (*reclaim)(void *,const struct wa_s7_region *);
};
struct wa_s7_scratch {
  struct wa_pt_pool *gfx,*gmu;
  struct wa_gpu_lease_runtime *lease;
  struct wa_s7_memory_ops backend;
  struct wa_s7_region region;
  uint64_t epoch;
  unsigned claimed,published,quarantined;
};
int wa_s7_scratch_init(struct wa_s7_scratch *state,
                       struct wa_pt_pool *gfx,struct wa_pt_pool *gmu,
                       struct wa_gpu_lease_runtime *lease,uint64_t epoch,
                       const struct wa_s7_memory_ops *backend);
/* Used as the wa_hfi_provider in S6 only after transport is attested.
 * Not a GPU/GMU device-backend or real DMA mapping implementation. */
struct wa_hfi_provider wa_s7_hfi_provider(struct wa_s7_scratch *state);
#ifdef __cplusplus
}
#endif
#endif
