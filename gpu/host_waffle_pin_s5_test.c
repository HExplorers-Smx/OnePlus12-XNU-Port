#include "waffle_a750_hw_s5.h"
#include <stdio.h>
#include <assert.h>
int main(void) {
    assert(waffle_a750_s5_pins.gpu.cpu_phys == UINT64_C(0x03d00000));
    assert(waffle_a750_s5_pins.gmu.cpu_phys == UINT64_C(0x03d68000));
    assert(waffle_a750_s5_pins.gmu.bytes == 0x37000u);
    assert(waffle_a750_s5_pins.smmu.cpu_phys == UINT64_C(0x03da0000));
    assert(WAFFLE_A750_CHIP_ID == 0x43051401);
    assert(WAFFLE_A750_SID_GMU == 5 && WAFFLE_A750_SID_SECURE == 2);
    assert(WAFFLE_A750_GMU_SPI_0 == 304 && WAFFLE_A750_GMU_SPI_1 == 305);
    assert(waffle_a750_s5_match_snapshot("waffle", "PJD110_16.0.10.501(CN01)",
      "949c54944853d5e4f75ed75d7cfe40e0ec52fe5b4e5db866fd97a96c8f9c131a",
      "9685f62c42543befcaee68d25355e16bf55cdb0d573bddcc9f6633f59eec3f72"));
    assert(!waffle_a750_s5_match_snapshot("giulia", "PJD110_16.0.10.501(CN01)",
      waffle_a750_s5_pins.runtime_fdt_sha256,waffle_a750_s5_pins.gmu_firmware_sha256));
    assert(!waffle_a750_s5_match_snapshot("waffle","PJD110_17.0.1(CN01)",
      waffle_a750_s5_pins.runtime_fdt_sha256,waffle_a750_s5_pins.gmu_firmware_sha256));
    assert(!waffle_a750_s5_hardware_ready());
    puts("S5 real-board hardware pin tests passed");
    return 0;
}
