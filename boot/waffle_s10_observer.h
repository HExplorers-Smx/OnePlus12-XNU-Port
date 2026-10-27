/* SPDX-License-Identifier: MIT */
#ifndef WAFFLE_S10_OBSERVER_H
#define WAFFLE_S10_OBSERVER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* NO RAW MMIO here. This contract accepts an external observer's VERIFIED
 * read-only GPUCC/SMMU snapshot. S9 FDT addresses are not read permissions.
 * Not even passing this predicate means hardware was accessed by XNU.
 */
enum { WA_S10_GPUCC=1, WA_S10_SMMU=2 };
enum { WA_S10_BLOCKED=0, WA_S10_READY=1, WA_S10_QUARANTINE=-1 };
struct wa_s10_observation {
    uint64_t boot_epoch,observation_serial,power_owner_token,smmu_owner_token;
    uint32_t reported_device,measured_value;
    unsigned current_boot,read_only,exclusive_power,exclusive_smmu,
             clocks_enabled_confirmed,mmio_mapped_by_owner,
             runtime_owner_confirmed,access_complete;
};
struct wa_s10_observer {
    uint64_t boot_epoch,last_serial;
    unsigned quarantined,accepted;
};
void wa_s10_observer_init(struct wa_s10_observer *s,uint64_t epoch);
/* -1 quarantines on ambiguous owner or potentially active MMIO access; 0
 * indicates an unproven state. Never implicitly claims controller ownership. */
int wa_s10_observer_accept(struct wa_s10_observer *s,
                           const struct wa_s10_observation *o);
#ifdef __cplusplus
}
#endif
#endif
