/* SPDX-License-Identifier: MIT
 * Observed on PJD110 ColorOS 16.0.10.501(CN01), active A slot.
 * FDT properties are NOT authorization for XNU to take over Linux resources.
 * GPIO, clocks, power, SMMU, Gunyah and secure loader remain unimplemented.
 */
#ifndef WAFFLE_A750_HW_S5_H
#define WAFFLE_A750_HW_S5_H
#include <stdint.h>
#include <stddef.h>

enum {
    WAFFLE_A750_CHIP_ID = 0x43051401,
    WAFFLE_A750_GPU_SPI = 300,
    WAFFLE_A750_GMU_SPI_0 = 304,
    WAFFLE_A750_GMU_SPI_1 = 305,
    WAFFLE_A750_SID_USER = 0,
    WAFFLE_A750_SID_LPAC = 1,
    WAFFLE_A750_SID_SECURE = 2,
    WAFFLE_A750_SID_GMU = 5
};

struct waffle_a750_window {
    uint64_t cpu_phys;
    uint32_t bytes;
};

struct waffle_a750_pins {
    struct waffle_a750_window gpu, gmu, smmu;
    const char *board;
    const char *android_build;
    const char *runtime_fdt_sha256;
    const char *gmu_firmware_sha256;
};

extern const struct waffle_a750_pins waffle_a750_s5_pins;
/* Verifies metadata against the captured build. No firmware authentication. */
int waffle_a750_s5_match_snapshot(const char *board, const char *build,
                                  const char *fdt_sha256,
                                  const char *gmu_fw_sha256);
/* Will remain false until hardware backends are implemented and proven. */
int waffle_a750_s5_hardware_ready(void);
#endif
