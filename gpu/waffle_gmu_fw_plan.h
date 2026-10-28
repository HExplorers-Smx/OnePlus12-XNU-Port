/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_GMU_FW_PLAN_H
#define WAFFLE_GMU_FW_PLAN_H
#include <stddef.h>
#include <stdint.h>

/* Gen7 candidate memory ranges; validate on a matched PJD110 firmware/DTB.
 * Virtual GMU addresses below are NOT host physical or bus DMA addresses.
 * No output from this parser authorizes firmware launch or XNU attachment.
 */
enum waffle_fw_region {
    WAFFLE_FW_ITCM, WAFFLE_FW_ICACHE, WAFFLE_FW_DTCM,
    WAFFLE_FW_KERNEL, WAFFLE_FW_KERNEL_EXT, WAFFLE_FW_INVALID_REGION
};
enum waffle_fw_result {
    WAFFLE_FW_OK, WAFFLE_FW_BAD_INPUT, WAFFLE_FW_TRUNCATED,
    WAFFLE_FW_TOO_MANY, WAFFLE_FW_BAD_REGION,
    WAFFLE_FW_BAD_ORDER, WAFFLE_FW_DATA_OVERLAP, WAFFLE_FW_EMPTY_DATA
};
enum { WAFFLE_FW_MAX_BLOCKS = 64, WAFFLE_FW_PREALLOC=1, WAFFLE_FW_PREALLOC_PERSIST=7 };
struct waffle_fw_record {
    uint32_t gmu_virtual_address;
    uint32_t data_bytes;
    uint32_t metadata_type;
    uint32_t metadata_value;
    size_t payload_offset;
    enum waffle_fw_region region;
};

enum waffle_fw_result waffle_gmu_firmware_plan(
    const uint8_t *raw, size_t bytes, struct waffle_fw_record *records,
    size_t max_records, size_t *record_count);

enum waffle_fw_result waffle_gpu_ucode_span(
    const uint8_t *raw, size_t bytes, size_t *payload_offset, size_t *payload_bytes);

#endif
