#!/usr/bin/env python3
"""Deterministic C pin of unique vendor GPU/GMU/SMMU clock specifiers."""
import argparse
import json
from pathlib import Path
ROLES={'gpu':1,'gmu':2,'gpu_smmu':4}

def render(obj):
    if obj['schema']!=1 or obj['runtime_clock_ownership_proven'] or obj['safe_to_write_mmio']:
        raise ValueError('unsafe or unknown clock source')
    uniq={}
    for role,mask in ROLES.items():
        for x in obj['clock_groups'][role]['clocks']:
            args=x['specifier']
            if len(args)>1:raise ValueError('unexpected multi-cell clock')
            ph=int(x['provider_phandle'],16)
            key=(ph,len(args),args[0] if args else 0)
            if key in uniq:uniq[key]['mask']|=mask
            else:uniq[key]={'mask':mask,'label':x['name'],'provider':x['provider_path']}
    out=['/* Auto-generated from pinned PJD110 runtime FDT; no MMIO permission. */',
         '#ifndef WAFFLE_S9_CLOCK_IDS_H','#define WAFFLE_S9_CLOCK_IDS_H',
         '#include <stdint.h>',
         '#define WA_S9_CLOCK_COUNT '+str(len(uniq)),
         'struct wa_s9_clock_id { uint32_t phandle, specifier, role_mask; uint8_t arg_cells; };',
         'static const struct wa_s9_clock_id wa_s9_clock_ids[WA_S9_CLOCK_COUNT] = {']
    for (ph,width,val),v in sorted(uniq.items()):
        out.append('    {0x%08xu, %uu, %uu, %uu}, /* %s / %s */'%(ph,val,v['mask'],width,v['label'],v['provider']))
    out+=['};','#endif']
    return '\n'.join(out)+'\n'

def main():
    p=argparse.ArgumentParser();p.add_argument('json',type=Path);p.add_argument('header',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    src=json.loads(a.json.read_text(encoding='utf8')); generated=render(src)
    if a.check:
        if a.header.read_text(encoding='utf8')!=generated:raise SystemExit('clock header out of sync')
    else:a.header.write_text(generated,encoding='utf8')
if __name__=='__main__':main()
