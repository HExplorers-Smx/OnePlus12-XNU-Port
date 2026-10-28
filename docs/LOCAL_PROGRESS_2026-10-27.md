# OnePlus 12 · Local Stage S1 (2026-10-27)

This iteration intentionally does NOT run GitHub Actions: the user's available minutes are exhausted. The workflow in the XNU repository has been changed to manual-only at commit `8325aa5`.

## Implemented offline

- Read-only `boot.img` v3/v4, `vendor_boot.img` v3/v4, DTB and DTBO metadata reader (`tools/inspect_firmware.py`).
- Sanitize DeviceArchive v1.0 reports into an allowlisted, non-personal snapshot (`tools/ingest_report.py`).
- Compare normalized snapshot and PJD110/waffle board facts, fail on inconsistencies (`tools/check_evidence.py`).
- Versioned observation `boards/observations/coloros16_501.json` derived from the user's successful Root report: 147 partitions; no raw partition or unique user identifiers.
- Synthetic fixture tests for Android headers, FDT parsing, DTBO indexes, malformed input, privacy and hardware identity.

## Verification

- `python3 -m unittest discover -s tests -v`: 21 tests PASS on local Python.
- `python3 tools/check_evidence.py`: MATCH on 27 board fields.
- `python3 tools/validate_board.py`: PASS.
- The Android app baseline Java CompareSelfTest passes locally, separate from XNU tests.
- No actual OnePlus 12 vendor image has been parsed in this iteration. No UEFI/XNU compile or device boot has been performed. Test fixtures are synthetic.

## Follow-up direction

1. Find and verify the exact PJD110_16.0.10.501(CN01) full firmware. Record sources, sizes, and SHA-256 before using images.
2. Feed matched boot/vendor_boot/dtbo/DTB into the local inspector. Correlate the real panel, touch, UFS, SMMU and reserved-memory resources with kernel sources.
3. Develop waffle-specific UEFI source/config, pin all upstream revisions; compile on Linux/WSL offline before hardware testing.
4. Confirm safe temporary-boot support and verified recovery procedure. Never flash UEFI, XBL or boot partitions on guesswork.
5. Fully publish DeviceArchive Android sources after static checks. Keep APK signing material outside Git and public ZIPs.
