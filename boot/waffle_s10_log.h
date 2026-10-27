/* SPDX-License-Identifier: MIT */
#ifndef WAFFLE_S10_LOG_H
#define WAFFLE_S10_LOG_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Versioned, transport-neutral read-only diagnostic frames. The transport
 * MAY be UART, a memory buffer or USB ONLY after a real backend is proven.
 * Wire layout: MAGIC[4], version[1], type[1], flags[2], seq[4], size[2],
 * reserved[2], crc32[4], payload[size]. All integers little-endian.
 * CRC32/ISO-HDLC covers bytes 0..15 followed by payload (excludes CRC field).
 */
enum { WA_S10_HDR=20, WA_S10_PAYLOAD_MAX=512, WA_S10_FRAME_MAX=532,
       WA_S10_VERSION=1, WA_S10_TYPE_BOOT=1, WA_S10_TYPE_OBSERVATION=2,
       WA_S10_TYPE_ERROR=3, WA_S10_TYPE_HANDOFF=4 };
enum wa_s10_code { WA_S10_OK=0, WA_S10_INVALID=-1, WA_S10_TRUNCATED=-2,
                   WA_S10_CHECKSUM=-3, WA_S10_UNSUPPORTED=-4 };
uint32_t wa_s10_crc32(const uint8_t *bytes,size_t size);
int wa_s10_encode(uint8_t type,uint32_t seq,const uint8_t *payload,size_t count,
                  uint8_t *out,size_t capacity,size_t *used);
int wa_s10_decode(const uint8_t *input,size_t length,uint8_t *type,
                  uint32_t *seq,const uint8_t **payload,size_t *count,size_t *consumed);
#ifdef __cplusplus
}
#endif
#endif
