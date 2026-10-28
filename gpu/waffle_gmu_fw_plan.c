/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_gmu_fw_plan.h"

#define MAX_FIRMWARE_BYTES (64u * 1024u * 1024u)

static uint32_t le32(const uint8_t *s)
{
    return ((uint32_t)s[0]) | ((uint32_t)s[1] << 8) |
           ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}

static enum waffle_fw_region region(uint32_t address, uint32_t length)
{
    static const struct { uint32_t base, length; } candidates[] = {
        { 0x0u, 0x4000u }, { 0x4000u, 0xffc000u },
        { 0x10004000u, 0x4000u },
        { 0x60000000u, 0x20000000u },
        { 0xc0000000u, 0x20000000u }
    };
    size_t n;
    if (length == 0 || (address & 3u) || (length & 3u))
        return WAFFLE_FW_INVALID_REGION;
    for (n = 0; n < sizeof(candidates)/sizeof(candidates[0]); n++) {
        const uint32_t start=candidates[n].base;
        const uint32_t span=candidates[n].length;
        if (address>=start && length<=span && address-start<=span-length)
            return (enum waffle_fw_region)n;
    }
    return WAFFLE_FW_INVALID_REGION;
}

/* Two-pass scan: outputs are written only after the complete input passes.
 * Caller must hash and authenticate the exact matching firmware separately.
 */
enum waffle_fw_result waffle_gmu_firmware_plan(
    const uint8_t *raw, size_t bytes, struct waffle_fw_record *records,
    size_t max_records, size_t *record_count)
{
    unsigned pass;
    size_t count;
    if (record_count == NULL) return WAFFLE_FW_BAD_INPUT;
    *record_count=0;
    if (raw == NULL || records == NULL || max_records == 0 ||
        max_records > WAFFLE_FW_MAX_BLOCKS || bytes > MAX_FIRMWARE_BYTES)
        return WAFFLE_FW_BAD_INPUT;
    if (bytes<16) return WAFFLE_FW_TRUNCATED;

    for (pass=0;pass<2;pass++) {
        size_t offset=0;
        int payload_seen=0;
        count=0;
        while (offset<bytes) {
            struct waffle_fw_record rec;
            size_t previous;
            if (bytes-offset<16) return WAFFLE_FW_TRUNCATED;
            rec.gmu_virtual_address=le32(raw+offset);
            rec.data_bytes=le32(raw+offset+4);
            rec.metadata_type=le32(raw+offset+8);
            rec.metadata_value=le32(raw+offset+12);
            rec.payload_offset=offset+16;
            rec.region=WAFFLE_FW_INVALID_REGION;
            if ((size_t)rec.data_bytes>bytes-rec.payload_offset)
                return WAFFLE_FW_TRUNCATED;
            if (count==max_records || count==WAFFLE_FW_MAX_BLOCKS)
                return WAFFLE_FW_TOO_MANY;
            if (rec.data_bytes==0) {
                if (payload_seen) return WAFFLE_FW_BAD_ORDER;
                if (rec.metadata_type<1 || rec.metadata_type>7)
                    return WAFFLE_FW_BAD_INPUT;
                if (rec.metadata_type==WAFFLE_FW_PREALLOC ||
                    rec.metadata_type==WAFFLE_FW_PREALLOC_PERSIST) {
                    rec.region=region(rec.gmu_virtual_address,rec.metadata_value);
                    if (rec.region==WAFFLE_FW_INVALID_REGION)
                        return WAFFLE_FW_BAD_REGION;
                }
            } else {
                payload_seen=1;
                rec.region=region(rec.gmu_virtual_address,rec.data_bytes);
                if (rec.region==WAFFLE_FW_INVALID_REGION)
                    return WAFFLE_FW_BAD_REGION;
                for (previous=0;previous<offset;) {
                    const uint32_t a=le32(raw+previous);
                    const uint32_t n=le32(raw+previous+4);
                    if (n != 0 &&
                        (uint64_t)a < (uint64_t)rec.gmu_virtual_address+rec.data_bytes &&
                        (uint64_t)rec.gmu_virtual_address < (uint64_t)a+n)
                        return WAFFLE_FW_DATA_OVERLAP;
                    previous+=16u+(size_t)n;
                }
            }
            if (pass) records[count]=rec;
            count++;
            offset=rec.payload_offset+(size_t)rec.data_bytes;
        }
        if (!payload_seen) return WAFFLE_FW_EMPTY_DATA;
        if (pass) *record_count=count;
    }
    return WAFFLE_FW_OK;
}

/* SQE/AQE firmware headers contain one file dword excluded from DMA copy.
 * The caller must still validate file identity, version, SHA256 and policy.
 */
enum waffle_fw_result waffle_gpu_ucode_span(
    const uint8_t *raw, size_t bytes, size_t *payload_offset, size_t *payload_bytes)
{
    if (!payload_offset || !payload_bytes) return WAFFLE_FW_BAD_INPUT;
    *payload_offset=*payload_bytes=0;
    if (!raw || bytes<8 || (bytes&3u)) return WAFFLE_FW_BAD_INPUT;
    *payload_offset=4;
    *payload_bytes=bytes-4;
    return WAFFLE_FW_OK;
}
