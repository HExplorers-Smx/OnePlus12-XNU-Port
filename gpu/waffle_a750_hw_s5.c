/* SPDX-License-Identifier: MIT */
#include "waffle_a750_hw_s5.h"
#include <string.h>

const struct waffle_a750_pins waffle_a750_s5_pins = {
    {UINT64_C(0x03d00000), UINT32_C(0x00040000)},
    {UINT64_C(0x03d68000), UINT32_C(0x00037000)},
    {UINT64_C(0x03da0000), UINT32_C(0x00040000)},
    "waffle",
    "PJD110_16.0.10.501(CN01)",
    "949c54944853d5e4f75ed75d7cfe40e0ec52fe5b4e5db866fd97a96c8f9c131a",
    "9685f62c42543befcaee68d25355e16bf55cdb0d573bddcc9f6633f59eec3f72"
};

int waffle_a750_s5_match_snapshot(const char *board, const char *build,
                                  const char *fdt_sha256,
                                  const char *gmu_fw_sha256)
{
    if (!board || !build || !fdt_sha256 || !gmu_fw_sha256) return 0;
    return strcmp(board, waffle_a750_s5_pins.board)==0 &&
           strcmp(build, waffle_a750_s5_pins.android_build)==0 &&
           strcmp(fdt_sha256, waffle_a750_s5_pins.runtime_fdt_sha256)==0 &&
           strcmp(gmu_fw_sha256, waffle_a750_s5_pins.gmu_firmware_sha256)==0;
}

int waffle_a750_s5_hardware_ready(void)
{
    /* Not enabled: address and firmware pins do not grant hardware ownership. */
    return 0;
}
