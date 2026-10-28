/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_S9_CLOCK_RUNTIME_H
#define WAFFLE_S9_CLOCK_RUNTIME_H
#include "waffle_s9_clock_ids.h"
#include "waffle_gpu_lease.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Hardware access is exclusively delegated to a separately authenticated
 * XNU runtime owner. No UEFI / Android provider may be passed through by
 * assumption. The actual runtime owner is NOT implemented in S9.
 */
enum wa_s9_phase { WA_S9_EMPTY, WA_S9_ACTIVE, WA_S9_QUARANTINED };
enum wa_s9_clock_status { WA_S9_CLOCK_OK=1, WA_S9_CLOCK_NO=0, WA_S9_CLOCK_UNCERTAIN=-1 };
struct wa_s9_clock_ops {
    void *context;
    /* 1 + nonzero token: claimed; 0 + zero token: conclusively unclaimed.
     * Anything else is ambiguous, and must quarantine. */
    int (*claim)(void *, uint64_t epoch, const struct wa_s9_clock_id *, uint64_t *token);
    /* Must read back ACTIVE actual clock, ownership and unchanged epoch.
     * Does not accept requested rate or FDT clock ID alone as proof. */
    int (*readback)(void *,uint64_t epoch,const struct wa_s9_clock_id *,uint64_t token);
    int (*release)(void *,uint64_t epoch,const struct wa_s9_clock_id *,uint64_t token);
    int (*epoch_live)(void *,uint64_t epoch);
};
struct wa_s9_gate {
    uint64_t epoch;
    uint64_t tokens[WA_S9_CLOCK_COUNT];
    enum wa_s9_phase phase;
    unsigned held;
};
/* Pins must match current build and the pre-captured FDT + GMU firmware. */
int wa_s9_clock_claim(struct wa_s9_gate *,const struct wa_s9_clock_ops *,uint64_t epoch,
                      const char *board,const char *build,const char *fdt_sha,
                      const char *gmu_sha);
/* Requires external serialization against reset, driver unload and SMMU
 * reconfiguration. Tokens are valid only within the current boot epoch. */
int wa_s9_clock_live(struct wa_s9_gate *,const struct wa_s9_clock_ops *);
int wa_s9_clock_stop(struct wa_s9_gate *,const struct wa_s9_clock_ops *,int dma_confirmed_stopped);

/* SMMUv2 readback evidence, supplied by a *real* native XNU owner.
 * Data about a DT-provided SID, an assumed unused bank or an offline page
 * table is not enough. This function NEVER writes SMMU registers. */
struct wa_s9_smmu_readback {
    uint64_t epoch, owner_token, gpu_ttbr0_pa, gmu_ttbr0_pa;
    uint32_t gpu_sid,gmu_sid,secure_sid, gpu_cb,gmu_cb,secure_cb;
    uint32_t gpu_asid,gmu_asid;
    uint32_t translation_granule;
    unsigned independent_unit, bank_leases_exclusive, stream_routes_readback;
    unsigned fault_on_unmatched, cache_clean_verified, tlbi_completed;
    unsigned actlr_security_verified, secure_owner_intact, no_active_dma;
};
/* Returns NO without side effects; standalone SMMU attest, not a hardware
 * lease or claim. No automatic GPU-ready declaration is possible. */
int wa_s9_smmu_attest(const struct wa_s9_gate *,const struct wa_s9_smmu_readback *,
                      uint64_t epoch,uint64_t expected_gpu_root,
                      const struct wa_gpu_smmu_intent *gmu_intent);
/* Single serialized precondition for S8 runtime attest callback. Caller MUST
 * hold its native XNU owner lock across this check and hardware use.
 * A stale clock epoch quarantines S9; a bad SMMU snapshot blocks without
 * pretending to revoke somebody else's SMMU context bank. */
int wa_s9_runtime_ready(struct wa_s9_gate *,const struct wa_s9_clock_ops *,
                        const struct wa_s9_smmu_readback *,uint64_t epoch,
                        uint64_t expected_gpu_root,
                        const struct wa_gpu_smmu_intent *gmu_intent);
#ifdef __cplusplus
}
#endif
#endif
