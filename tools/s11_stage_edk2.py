#!/usr/bin/env python3
"""Prepare a self-contained EDK2 diagnostics package; never produces a boot.img.

No network, phone, disk writes, or GitHub Actions. EDK2 itself must be supplied
by the builder; the package builds a UEFI application only on an EDK2 platform.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
SOURCES={
    "uefi/WaffleS11Pkg/WaffleS11Pkg.dsc":"WaffleS11Pkg.dsc",
    "uefi/WaffleS11Pkg/WaffleS11Pkg.dec":"WaffleS11Pkg.dec",
    "uefi/WaffleS11Probe/WaffleS11Probe.c":"WaffleS11Probe/WaffleS11Probe.c",
    "uefi/WaffleS11Probe/WaffleS11Probe.inf":"WaffleS11Probe/WaffleS11Probe.inf",
    "boot/waffle_s10_log.c":"WaffleS11Probe/waffle_s10_log.c",
    "boot/waffle_s10_log.h":"WaffleS11Probe/waffle_s10_log.h",
    "boot/waffle_s11_fdt.c":"WaffleS11Probe/waffle_s11_fdt.c",
    "boot/waffle_s11_fdt.h":"WaffleS11Probe/waffle_s11_fdt.h",
}
def stage(output:Path)->dict:
    output=output.resolve()
    if ROOT in output.parents or output==ROOT:
        raise ValueError("Refusing to stage inside source repository")
    pkg=output/"WaffleS11Pkg"
    pkg.mkdir(parents=True,exist_ok=True)
    result={}
    for source,dest in SOURCES.items():
        data=(ROOT/source).read_bytes()
        target=pkg/dest
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
        result[dest]={"sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data)}
    (pkg/"MANIFEST.json").write_text(json.dumps({"stage":"S11","flashable":False,"files":result},indent=2)+"\n")
    return result
if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--out",type=Path,required=True)
    args=ap.parse_args()
    print(json.dumps({"staged":len(stage(args.out)),"path":str(args.out.resolve())}))
