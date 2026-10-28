#!/usr/bin/env python3
"""Pinned OnePlus12/waffle OEM runtime FDT clock topology (READ ONLY).

Index clock-names against decoded phandle specifiers, refusing any non-zero
trailing cells. A single all-zero vendor trailer is recorded but never voted.
A provider phandle is not an XNU power/clock ownership token.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from qualify_evidence_s5 import read_zip_bundle, capture_properties, EvidenceError, BUILD
from inspect_firmware import inspect_bytes
from s8_power_inventory import FDT_PIN

CLOCK_PATHS = {
 'gpu': '/soc/qcom,kgsl-3d0@3d00000',
 'gmu': '/soc/qcom,gmu@3d69000',
 'gpu_smmu': '/soc/kgsl-smmu@3da0000',
}
MAX_RAW=8*1024*1024

class ClockError(ValueError): pass

def cells(raw:bytes)->list[int]:
    if len(raw)%4 or len(raw)>65536: raise ClockError('invalid word property')
    return list(struct.unpack(f'>{len(raw)//4}I',raw))

def names(raw:bytes)->list[str]:
    if not raw or not raw.endswith(b'\0') or len(raw)>4096:
        raise ClockError('clock-names is not a NUL-terminated string list')
    names=raw[:-1].split(b'\0')
    if any(not n or len(n)>96 for n in names): raise ClockError('invalid clock name')
    try: return [n.decode('ascii') for n in names]
    except UnicodeDecodeError as e: raise ClockError('non-ASCII clock name') from e

def fdt_nodes(blob:bytes) -> dict[str,dict[str,bytes]]:
    """Strict bounded property walker, no raw bytes in report."""
    if not (40<=len(blob)<=MAX_RAW) or blob[:4]!=bytes.fromhex('d00dfeed'):
        raise ClockError('not an FDT')
    magic,total,so,st,ro,version,last,boot,stbytes,sobytes=struct.unpack_from('>10I',blob)
    if version not in (16,17) or last>version or total>len(blob) or total<40:
        raise ClockError('invalid FDT header')
    if so+sobytes>total or st+stbytes>total or ro+16>total:
        raise ClockError('invalid FDT section bounds')
    structure=blob[so:so+sobytes]; strings=blob[st:st+stbytes]
    at=0; paths=[]; out={}; ended=False
    while at+4<=len(structure):
        op=struct.unpack_from('>I',structure,at)[0];at+=4
        if op==1:
            end=structure.find(b'\0',at)
            if end<at or end-at>200:raise ClockError('invalid FDT node')
            component=structure[at:end].decode('utf8','replace')
            at=(end+4)&~3
            if not paths and component:raise ClockError('bad root node')
            path='/' if not paths else paths[-1].rstrip('/')+'/'+component
            paths.append(path)
            if len(paths)>64 or len(out)>=20000 or path in out:raise ClockError('FDT node capacity/duplicate')
            out[path]={}
        elif op==2:
            if not paths:raise ClockError('unbalanced FDT')
            paths.pop()
        elif op==3:
            if not paths or at+8>len(structure):raise ClockError('invalid FDT property header')
            size,off=struct.unpack_from('>2I',structure,at);at+=8
            if size>1_000_000 or off>=len(strings) or at+size>len(structure):raise ClockError('property outside FDT')
            end=strings.find(b'\0',off)
            if end<off or end-off>200:raise ClockError('invalid property string')
            prop=strings[off:end].decode('ascii','replace')
            if prop in out[paths[-1]]:raise ClockError('duplicate FDT property')
            out[paths[-1]][prop]=structure[at:at+size]
            at=(at+size+3)&~3
        elif op==4:continue
        elif op==9:
            if paths:raise ClockError('unterminated nodes')
            ended=True;break
        else:raise ClockError('invalid FDT opcode')
    if not ended:raise ClockError('missing FDT end marker')
    return out

def analyze_nodes(nodes:dict[str,dict[str,bytes]]) -> dict:
    owners={}
    for path,p in nodes.items():
        val=p.get('phandle',p.get('linux,phandle'))
        if val is None:continue
        word=cells(val)
        if len(word)!=1 or word[0] in (0,0xffffffff) or word[0] in owners:
            raise ClockError('invalid or duplicate phandle')
        owners[word[0]]=path
    result={}
    for role,path in CLOCK_PATHS.items():
        p=nodes.get(path)
        if p is None or not p.get('clocks') or not p.get('clock-names'):
            raise ClockError('missing clock properties: '+role)
        clocks=cells(p['clocks']); labels=names(p['clock-names'])
        if len(labels)>32:raise ClockError('too many clocks')
        at=0;entries=[]
        for label in labels:
            if at>=len(clocks):raise ClockError('clock-names exceeds specifier count')
            ph=clocks[at];at+=1
            provider=owners.get(ph)
            if not ph or not provider:raise ClockError('missing named clock provider: '+label)
            width_raw=nodes[provider].get('#clock-cells')
            if width_raw is None:raise ClockError('missing provider clock cell width: '+provider)
            width=cells(width_raw)
            if len(width)!=1 or width[0]>4 or at+width[0]>len(clocks):
                raise ClockError('invalid provider clock width')
            args=clocks[at:at+width[0]];at+=width[0]
            entries.append({'name':label,'index':len(entries),
                'provider_path':provider,'provider_phandle':f'0x{ph:x}',
                'specifier':args,'verified_named_clock':True,
                'runtime_lease_proven':False})
        trailing=clocks[at:]
        if trailing not in ([],[0]):
            raise ClockError('non-zero or excessive unlabelled clock specifiers')
        result[role]={'clocks':entries,'zero_trailer_cells':len(trailing),
            'usable_for_runtime_votes':False}
    return {'schema':1,'target':'PJD110/waffle','build':BUILD,
        'fdt_sha256':FDT_PIN, 'clock_groups':result,
        'clock_identity_from_applied_fdt':True,
        'runtime_clock_ownership_proven':False,
        'gpucc_power_owner_proven':False,
        'safe_to_write_mmio':False,
        'notice':'phandles and clock IDs are OEM FDT indices; never convert to rates or direct MMIO write instructions'}

def inventory_fdt(raw:bytes,build:str=BUILD)->dict:
    if build!=BUILD or hashlib.sha256(raw).hexdigest()!=FDT_PIN:
        raise ClockError('wrong build or pinned FDT digest')
    parsed=inspect_bytes(raw)
    if parsed['format']!='fdt' or 'waffle' not in ' '.join(parsed['fdt']['root_model']):
        raise ClockError('not an active waffle FDT')
    return analyze_nodes(fdt_nodes(raw))

def main(argv=None)->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('private_evidence_zip',type=Path)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args(argv)
    try:
        bundle=read_zip_bundle(a.private_evidence_zip)
        props=capture_properties(bundle['capture.txt'])
        report=inventory_fdt(bundle['fdt.dtb'],props['prop.ro.build.display.id'])
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
        print('S9 pinned OEM named clock inventory valid; 0 GPU runtime writes permitted')
        return 0
    except (ClockError,EvidenceError,OSError,KeyError,ValueError) as e:
        print('S9 inventory rejected:',str(e),file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
