# S12 — HakiOS wire-format XNUP parser and native ARM64 UEFI preflight

This is **working source and a genuine PE/COFF `.efi` application**. It is not
an XNU/iOS boot, firmware image, Qualcomm UEFI firmware, or a working GPU driver.

## What is implemented

- Compatible XNUP records (HakiOS's `mkpayload.py` layout): 8-byte header,
  24-byte records, 16-byte data alignment, zero-name terminator.
- A bounded, no-allocation C payload parser; checks magic, body length, record
  boundaries, terminator, reserved fields, duplicate essential records,
  alignment padding, `kernel` presence, and ARM64 Mach-O header invariants.
- Local XNUP packer (`tools/s12_pack_xnup.py`) for a user-provided open-source
  Mach-O image; refuses missing/invalid binaries. Does not fetch Apple content.
- Actual ARM64 PE32+ UEFI application (`WaffleS12Preflight.efi`) that queries
  the UEFI configuration table for FDT, verifies the FDT's full RAM ownership
  and exact waffle/pineapple identity, then obtains XNUP bytes through the
  standard UEFI LoadedImage protocol LoadOptions buffer.
- Fail-closed return on every invalid condition; success reports validated
  headers **without** jumping to the Mach-O kernel.
- A mock UEFI entry-point test that exercises the same production C code,
  including non-waffle board, unowned FDT memory and missing XNUP.

## Build and test (on a host with clang + lld-link)

```sh
bash tools/s12_build_efi.sh
python3 -m unittest tests.test_s12_xnup -v
python3 -m unittest discover -s tests -v
```

The builder outputs `output/s12/WaffleS12Preflight.efi`, an AArch64
UEFI_APPLICATION, not an Android boot image. **Do not `fastboot flash` or
`fastboot boot` this standalone EFI file**: the phone bootloader expects an
Android boot image with device-specific firmware and board configuration.

## Integration contract

The upstream `xnuloader` receives an XNUP blob from the UEFI boot shim in
reserved RAM. The S12 EFI app instead takes the same wire format from a
UEFI LoadedImage LoadOptions buffer provided by a future stage-1 runner;
**there is no existing runner or SM8650 UEFI here yet**. This keeps the
interface testable without assuming the Xiaomi 15's payload base, DRAM layout,
USB controller, GPU, or recovery method.

Before adding XNU handoff, the port needs authenticated memory reservations,
Mach-O segment loader/chained fixups and PAC handling as applicable, confirmed
GIC and GOP handoff, a real USB diagnostic channel, verified watchdog/recovery,
plus an independently validated board-specific UEFI build.

The current Mach-O check tests only minimal 64-bit ARM64 header invariants;
it **must not** be treated as a loader/verifier for executable kernel segments.

## Provenance

XNUP wire format described in upstream:
`https://github.com/superboyyy/hakios-arm64e-dada/blob/main/base/xnuloader/mkpayload.py`

UEFI struct layout independently declared from the UEFI specification,
with public HakiOS `efi.h` as a cross-check. No upstream proprietary images,
firmware, Apple binaries, or third-party source code is bundled here.

No GitHub Actions run has been requested or started by this milestone.
