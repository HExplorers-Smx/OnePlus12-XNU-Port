#!/usr/bin/env python3
"""Offline decoder for transport-neutral WFLG v1 read-only bring-up frames.

No access to USB, firmware, device memory or network; this reads a saved file.
Does not interpret a frame as evidence of a working GPU/USB/XNU session.
"""
import argparse
import binascii
import json
from pathlib import Path

MAGIC = b"WFLG"
HEADER = 20
MAX_PAYLOAD = 512
TYPES = {1: "boot", 2: "observation", 3: "error", 4: "handoff"}


def decode(data: bytes, *, strict=True) -> dict:
    if len(data) > 32 * 1024 * 1024:
        raise ValueError("log is larger than 32 MiB")
    offset = 0
    frames = []
    dropped = 0
    while offset < len(data):
        if data[offset:offset + 4] != MAGIC:
            if strict:
                raise ValueError(f"bad magic at {offset}")
            offset += 1
            dropped += 1
            continue
        if len(data) - offset < HEADER:
            raise ValueError(f"truncated frame header at {offset}")
        h = data[offset:offset + HEADER]
        version, kind = h[4], h[5]
        flags = int.from_bytes(h[6:8], "little")
        serial = int.from_bytes(h[8:12], "little")
        count = int.from_bytes(h[12:14], "little")
        reserved = int.from_bytes(h[14:16], "little")
        crc = int.from_bytes(h[16:20], "little")
        if version != 1 or kind not in TYPES or flags or reserved or count > MAX_PAYLOAD:
            raise ValueError(f"unsupported/invalid header at {offset}")
        if len(data) - offset < HEADER + count:
            raise ValueError(f"truncated payload at {offset}")
        payload = data[offset + HEADER:offset + HEADER + count]
        calculated = binascii.crc32(h[:16] + payload) & 0xffffffff
        if calculated != crc:
            raise ValueError(f"CRC mismatch at {offset}")
        frames.append({"offset": offset, "sequence": serial, "type": TYPES[kind],
                       "payload_utf8": payload.decode("utf-8", errors="replace"),
                       "payload_sha256": __import__('hashlib').sha256(payload).hexdigest()})
        offset += HEADER + count
    return {"format": "WFLG-v1", "frames": frames, "discarded_prefix_bytes": dropped,
            "transport_proven": False, "hardware_started_proven": False}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("--recover-prefix", action="store_true", help="discard non-WFLG bytes before frames")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    result = decode(args.input.read_bytes(), strict=not args.recover_prefix)
    rendered = json.dumps(result, indent=2, ensure_ascii=False)
    if args.out:
        args.out.write_text(rendered + "\n", encoding="utf-8")
    else:
        print(rendered)


if __name__ == "__main__":
    main()
