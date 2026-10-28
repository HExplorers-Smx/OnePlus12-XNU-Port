/* Auto-generated from pinned PJD110 runtime FDT; no MMIO permission. */
#ifndef WAFFLE_S9_CLOCK_IDS_H
#define WAFFLE_S9_CLOCK_IDS_H
#include <stdint.h>
#define WA_S9_CLOCK_COUNT 9
struct wa_s9_clock_id { uint32_t phandle, specifier, role_mask; uint8_t arg_cells; };
static const struct wa_s9_clock_id wa_s9_clock_ids[WA_S9_CLOCK_COUNT] = {
    {0x0000002cu, 14u, 2u, 1u}, /* axi_clk / /soc/clock-controller@100000 */
    {0x0000002cu, 37u, 7u, 1u}, /* gcc_gpu_memnoc_gfx / /soc/clock-controller@100000 */
    {0x0000002cu, 38u, 5u, 1u}, /* gcc_gpu_snoc_dvm_gfx / /soc/clock-controller@100000 */
    {0x0000003fu, 0u, 1u, 0u}, /* apb_pclk / /soc/power-controller@c300000 */
    {0x0000004eu, 0u, 7u, 1u}, /* gpu_cc_ahb / /soc/clock-controller@3d90000 */
    {0x0000004eu, 4u, 2u, 1u}, /* gmu_clk / /soc/clock-controller@3d90000 */
    {0x0000004eu, 6u, 2u, 1u}, /* cxo_clk / /soc/clock-controller@3d90000 */
    {0x0000004eu, 18u, 4u, 1u}, /* gpu_cc_hlos1_vote_gpu_smmu / /soc/clock-controller@3d90000 */
    {0x0000004eu, 21u, 2u, 1u}, /* hub_clk / /soc/clock-controller@3d90000 */
};
#endif
