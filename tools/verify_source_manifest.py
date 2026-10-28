"""Verify repository text/source inventory against SOURCE_SHA256.json.

This is a reproducibility tool; checksums are not an authenticity signature.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"SOURCE_SHA256.json"}
IGNORED = {"__pycache__", ".git", "output", "build", "out", ".pytest_cache"}


def source_files():
    return {
        p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in ROOT.rglob("*")
        if p.is_file() and not p.is_symlink()
        and not (set(p.relative_to(ROOT).parts) & IGNORED)
        and p.relative_to(ROOT).as_posix() not in EXCLUDED
        and p.suffix not in {".pyc", ".pyo"}
    }


def main():
    manifest = json.loads((ROOT / "SOURCE_SHA256.json").read_text(encoding="utf-8"))
    observed = source_files()
    expected = manifest["files"]
    if expected != observed:
        changed = sorted(k for k in expected.keys() & observed.keys()
                         if expected[k] != observed[k])
        missing = sorted(expected.keys() - observed.keys())
        untracked = sorted(observed.keys() - expected.keys())
        raise SystemExit(f"FAIL changed={changed} missing={missing} untracked={untracked}")
    print(f"OK S{manifest['stage'].lstrip('S')} source hashes: {len(observed)} files")


if __name__ == "__main__":
    main()
