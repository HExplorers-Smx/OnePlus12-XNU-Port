#ifndef WAFFLE_GPU_PLATFORM_H
#define WAFFLE_GPU_PLATFORM_H
#include "waffle_gpu_gate.h"
#include <stdint.h>

/* Provider callbacks must return exact readiness evidence on verified success.
 * negative return: uncertain hardware state -> quarantine; zero: no proof yet.
 * No callbacks have implementations in this project. A real XNU port must
 * supply all appropriate board-specific implementations before use.
 */
struct wa_gpu_platform_ops {
  int (*verify_board_and_resources)(void *ctx);      /* passive DT/MMIO/IRQ inspection */
  int (*verify_signed_firmware)(void *ctx);           /* hashes and authentication */
  int (*claim_power_and_confirm)(void *ctx);          /* dedicated power/clock ownership */
  int (*claim_smmu_and_confirm)(void *ctx);           /* GPU SMMU context/stream owner */
  int (*start_gmu_and_confirm_hfi)(void *ctx);        /* real GMU firmware + HFI ack */
  int (*submit_and_confirm_fence)(void *ctx);        /* command + observed completion */
  int (*display_scanout_and_validate)(void *ctx);    /* pixel match + IOSurface lifetime */
  int (*publish_metal_and_verify_frame)(void *ctx);  /* iOS frontend + render-server frame */
};

/* Execute precisely one unverified stage, never skipping ahead.
 * Returns 1 if stage advanced; 0 if provider unavailable or proof missing;
 * returns -1 and quarantines if callback errors or returns invalid evidence.
 */
int wa_gpu_platform_step(struct wa_gpu_gate *gate,
                         const struct wa_gpu_platform_ops *ops, void *ctx);
#endif
