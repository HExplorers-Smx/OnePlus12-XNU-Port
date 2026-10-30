.PHONY: test gpu-test s3-test validate evidence package
test:
	python3 -m unittest discover -s tests -v
validate:
	python3 tools/validate_board.py boards/waffle.toml
evidence:
	python3 tools/check_evidence.py --board boards/waffle.toml --snapshot boards/observations/coloros16_501.json
package: test validate evidence
	python3 tools/make_board_bundle.py boards/waffle.toml output

# Works offline with a host compiler; tests prove only control flow, not hardware startup.
gpu-test:
	python3 -m unittest discover -s tests -p 'test_gpu.py' -v

# Offline S3 GPU and GMU planner regression. No hardware access.
s3-test:
	python3 -m unittest discover -s tests -p 'test_s3_graph.py' -v

# S4 HFI and power/SMMU transactions: compiles native code on the local host.
# Requires a C compiler. No OEM firmware and no device registers are used.
.PHONY: s4-test
s4-test:
	python3 -m unittest discover -s tests -p 'test_s4_native.py' -v

# S5: repository-safe tests. Private OEM evidence ZIP stays outside the repo.
.PHONY: s5-test
s5-test:
	python3 -m unittest discover -s tests -p 'test_s5_evidence.py' -v

# S6: Gen7 17-queue transport and HFI F2H_ALLOC/H2F_ACK runtime coordinator.
# No actual A750 power-up, MMIO access, XNU linking or hardware acceleration.
.PHONY: s6-test
s6-test:
	python3 -m unittest discover -s tests -p 'test_s6_native.py' -v

# S7: owned physical table pages, LPAE 4KiB descriptor construction and
# Gen7 F2H SCRATCH backing integration; host simulation ONLY.
.PHONY: s7-test
s7-test:
	python3 -m unittest discover -s tests -p 'test_s7_native.py' -v

# S8: preboot static SCRATCH, pinned power dependency inventory, and host
# lease/HFI integration. Does NOT run native device MMIO or flash partitions.
.PHONY: s8-test
s8-test:
	python3 -m unittest discover -s tests -p 'test_s8*.py' -v

# S9: pinned live-FDT clock-index audit, 9 unique clock lease resources,
# and fail-closed runtime SMMU readback checks. Host-only; no MMIO backend.
.PHONY: s9-test
s9-test:
	python3 tools/generate_s9_clock_header.py boards/gpu/waffle_a750_s9_named_clocks.json gpu/waffle_s9_clock_ids.h --check
	python3 -m unittest discover -s tests -p 'test_s9*.py' -v

# S10: portable WFLG boot/observation log plus EDK2 informational source.
# No EDK2 SDK is shipped, USB-C transport is NOT proven, no boot image.
.PHONY: s10-test
s10-test:
	python3 -m unittest discover -s tests -p 'test_s10_native.py' -v

# S11 standalone read-only UEFI diagnostic package (requires external EDK2).
# This only stages sources; never makes a phone boot.img or writes partitions.
.PHONY: s11-test s11-stage
s11-test:
	python3 -m unittest discover -s tests -p 'test_s11_native.py' -v
s11-stage:
	python3 tools/s11_stage_edk2.py --out output/edk2-packages

# S12: Native AArch64 EFI executable and HakiOS XNUP preflight (NOT a boot image).
.PHONY: s12-test s12-efi
s12-test:
	python3 -m unittest tests.test_s12_xnup -v
s12-efi:
	bash tools/s12_build_efi.sh
