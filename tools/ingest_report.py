#!/usr/bin/env python3
"""Normalize a DeviceArchive v1.0 TXT report into an allowlisted board baseline.

No raw report is bundled into a public repository; names, serials, IMEIs and keys are never copied.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path

class ReportError(ValueError): pass

ALLOWED_BUILD_KEYS = {
    'ro.product.model','ro.product.device','ro.board.platform','ro.soc.model',
    'ro.build.version.release','ro.build.display.id','ro.build.version.security_patch',
    'ro.boot.slot_suffix','ro.boot.dynamic_partitions','ro.boot.verifiedbootstate'
}

def sections(text: str) -> dict[str,list[str]]:
    result={}; current=None
    for line in text.splitlines():
        if line.startswith('## '):
            current=line[3:].strip(); result[current]=[]
        elif current is not None:result[current].append(line.strip())
    return result

def parse_report(text: str) -> dict:
    s=sections(text)
    if 'BUILD_AND_BOOT' not in s or 'DEVICE_TREE' not in s:
        raise ReportError('not a DeviceArchive full report')
    raw={}
    for line in s['BUILD_AND_BOOT']:
        if ': ' in line:
            k,v=line.split(': ',1)
            if k in ALLOWED_BUILD_KEYS:raw[k]=v
    if raw.get('ro.product.model')!='PJD110' or raw.get('ro.soc.model')!='SM8650':
        raise ReportError('report is not verified OnePlus 12 PJD110 / SM8650')
    root_ok = re.search(r'^snapshot\.root_ok:\s*(true|false)',text,re.M)
    if not root_ok or root_ok.group(1)!='true':
        raise ReportError('report did not successfully collect Root data')
    compatibles=[]; dtmodel=None
    dt=s['DEVICE_TREE']
    for index,line in enumerate(dt):
        if line.endswith('/compatible'):
            for value in dt[index+1:]:
                if not value: break
                if value.startswith('/'): break
                if re.fullmatch(r'[a-z0-9,._+-]{1,128}',value,re.I):compatibles.append(value)
        elif line.endswith('/model') and index+1<len(dt):
            dtmodel=dt[index+1]
    if not dtmodel or 'waffle' not in dtmodel:
        raise ReportError('model property does not identify waffle')
    display=s.get('DISPLAY',[])
    size=next((m for line in display if (m:=re.fullmatch(r'Physical size: (\d+)x(\d+)',line))),None)
    if size is None: raise ReportError('missing display dimensions')
    display_interfaces={}; last=None
    for line in display:
        if line.startswith('device: /sys/class/drm/card0-'):
            last=line.removeprefix('device: /sys/class/drm/card0-')
            display_interfaces[last]=[]
        elif last and re.fullmatch(r'(modes: )?\d+x\d+x\d+cmd',line):
            mode=line.removeprefix('modes: ')
            if mode not in display_interfaces[last]:display_interfaces[last].append(mode)
        elif last and line=='status: disconnected':
            display_interfaces.pop(last,None);last=None
    gpu={}
    for line in s.get('GPU',[]):
        if '/gpu_model:' in line:
            gpu['model']=line.split(':',1)[1].strip()
    devices=[]
    for line in s.get('TOUCH_AND_INPUT',[]):
        m=re.fullmatch(r'/sys/class/input/(input\d+)/name: (.{1,80})',line)
        if m and m.group(2) in {'touchpanel','gpio-keys','pmic_pwrkey','oplus,hall_tri_state_key'}:
            devices.append(m.group(2))
    sizes={}
    for line in s.get('PARTITION_SIZES_BYTES',[]):
        m=re.fullmatch(r'((?:boot|init_boot|vendor_boot|dtbo|vbmeta|abl|recovery)_[ab]|super): (\d+)',line)
        if m:sizes[line.split(':')[0]]=int(m.group(2))
    parts=[p for p in s.get('PARTITIONS_ALL_NAMES',[]) if re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',p)]
    # Exclude serials and names that could be arbitrary strings; only safely retain a count
    kernel={}
    for line in s.get('KERNEL',[]):
        if line.startswith('kernel.release: '):kernel['release']=line.split(': ',1)[1]
        elif line.startswith('kernel.page_size: '):kernel['page_size_bytes']=int(line.split(': ',1)[1])
    result={'schema':1,'device':'OnePlus 12','model':'PJD110','device_codename':'waffle',
            'soc':'SM8650','platform':'pineapple',
            'build':raw,'kernel':kernel,'device_tree':{'model':dtmodel,'compatible':compatibles},
            'display':{'size_px':[int(size.group(1)),int(size.group(2))],
                       'modes':{k:v for k,v in display_interfaces.items() if k.startswith('DSI-')}},
            'gpu':gpu,'important_input_nodes':sorted(set(devices)),
            'partition_count':len(parts),'partition_size_bytes':sizes,
            'storage':{'controller':'ufshcd' if any('ufshcd' in x for x in s.get('STORAGE',[])) else 'unknown'},
            'valid_root_snapshot':True,'image_flashability':'none','verified_real_device':True}
    if not result['gpu'].get('model'):
        raise ReportError('missing GPU model')
    return result

def main():
    ap=argparse.ArgumentParser(description='Create privacy-preserving report baseline from local phone TXT')
    ap.add_argument('report',type=Path)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    data=args.report.read_bytes()
    res=parse_report(data.decode('utf-8','replace'))
    # SHA alone may allow correlation with a publicly posted unique device report; omit by default.
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(res,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"Normalized {res['model']} report: {res['partition_count']} partitions -> {args.out}")

if __name__=='__main__':main()
