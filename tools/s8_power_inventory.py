#!/usr/bin/env python3
"""Extract sanitized SM8650 GPU power/clock dependencies from owner's FDT.

Read-only: no root required on build host, no MMIO, no device writes. No
user/device identifiers or OEM binary blobs in output. A provider unresolved
by the runtime tree makes the output BLOCKED, not a guessed clock value.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile
from inspect_firmware import inspect_bytes, FormatError
from qualify_evidence_s5 import (read_zip_bundle, capture_properties, BUILD,
                                 EvidenceError)

FDT_PIN='949c54944853d5e4f75ed75d7cfe40e0ec52fe5b4e5db866fd97a96c8f9c131a'
PATHS={'gpu':'/soc/qcom,kgsl-3d0@3d00000',
       'gmu':'/soc/qcom,gmu@3d69000',
       'gpu_smmu':'/soc/kgsl-smmu@3da0000'}
PERMITTED=('clocks','power-domains','mboxes','resets','vdd-supply',
           'vddcx-supply','iommus')


def inventory(fdt:bytes, *, board_build:str=BUILD) -> dict:
    if board_build != BUILD or hashlib.sha256(fdt).hexdigest()!=FDT_PIN:
        raise EvidenceError('unsupported ColorOS/FDT: never borrow another phone clock tree')
    parsed=inspect_bytes(fdt)
    if parsed['format']!='fdt' or not parsed['fdt']['root_model'] or \
       'waffle' not in parsed['fdt']['root_model'][0]:
        raise EvidenceError('not the verified active waffle FDT')
    graph=parsed['fdt']['gpu_graph']
    if graph.get('duplicate_phandles'):
        raise EvidenceError('duplicate phandles cannot be used as a runtime source')
    results={}
    complete=True
    for role,path in PATHS.items():
        matches=[n for n in graph['nodes'] if n['path']==path]
        if len(matches)!=1:raise EvidenceError('missing or duplicated '+role)
        node=matches[0]
        result={}
        for key in PERMITTED:
            if key not in node['links']:continue
            link=node['links'][key]
            value={'resolution':link['status']}
            if key.endswith('-supply'):
                value['provider_path']=link.get('provider_path')
                if not value['provider_path']:complete=False
            else:
                value['specifiers']=[{
                    'provider_path':it.get('provider_path'),
                    'provider_phandle':it.get('provider_phandle'),
                    'argument_cells':it.get('argument_cells')
                } for it in link.get('items',[])]
                if any(x.get('provider_path') is None for x in value['specifiers']):
                    complete=False
            if link['status']!='syntactically-resolved-no-ownership':complete=False
            result[key]=value
        results[role]=result
    return {
        'schema':1,'project':'waffle-xnu-gpu-s8',
        'build':BUILD,'fdt_sha256':FDT_PIN,
        'source':'owner-supplied-active-runtime-fdt',
        'dependencies':results,
        'all_phandles_resolved':complete,
        'hardware_ownership_confirmed':False,
        'safe_to_issue_power_votes':False,
        'runtime_provider_required':True,
        'gpu_clock_tail_has_null_phandle':any(
            x.get('provider_phandle')=='0x0' for x in
            results.get('gpu',{}).get('clocks',{}).get('specifiers',[])),
        'clock_null_trailer_requires_index_audit':not complete,
        'notice':'GPU clocks contains a trailing null phandle 0; named clocks need index audit before calling it a missing provider. Specifier cells are not votes or access grants.'
    }


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('private_zip',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    try:
        files=read_zip_bundle(a.private_zip)
        props=capture_properties(files['capture.txt'])
        result=inventory(files['fdt.dtb'],board_build=props['prop.ro.build.display.id'])
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        print(f'FDT provenance OK; all GPU providers resolved: {result["all_phandles_resolved"]}')
        return 0
    except (OSError,ValueError,FormatError,zipfile.BadZipFile) as e:
        print(f'S8 inventory rejected: {e}',file=sys.stderr)
        return 1

if __name__=='__main__':sys.exit(main())
