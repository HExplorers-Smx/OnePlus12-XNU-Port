#!/usr/bin/env python3
"""Cross-check a normalized diagnostic snapshot against explicitly observed board facts."""
import argparse
import json
from pathlib import Path
from validate_board import load

class EvidenceMismatch(ValueError):pass

def crosscheck(board:dict, snapshot:dict)->dict:
    if snapshot.get('schema')!=1 or not snapshot.get('valid_root_snapshot'):
        raise EvidenceMismatch('not a normalized, successful Root report')
    checks={
        'model':(board['board']['model'],snapshot['model']),
        'coloros_build':(board['board']['coloros_build'],snapshot['build'].get('ro.build.display.id')),
        'kernel_release':(board['board']['kernel_release'],snapshot['kernel'].get('release')),
        'kernel_page_size':(board['storage']['page_size_bytes'],snapshot['kernel'].get('page_size_bytes')),
        'soc':(board['board']['soc'],snapshot['soc']),
        'platform':(board['board']['platform'],snapshot['platform']),
        'device_tree_model':(board['device_tree']['model'],snapshot['device_tree']['model']),
        'root_compatible':(board['device_tree']['compatible'],snapshot['device_tree']['compatible']),
        'display_width':(board['display']['width_px'],snapshot['display']['size_px'][0]),
        'display_height':(board['display']['height_px'],snapshot['display']['size_px'][1]),
        'gpu':(board['gpu']['model'],snapshot['gpu']['model']),
        'ufs':(board['storage']['controller'],snapshot['storage']['controller']),
    }
    for part,size in board['partition_size_bytes'].items():
        checks['partition/'+part]=(size,snapshot['partition_size_bytes'].get(part))
    mismatches={k:{'expected':e,'observed':o} for k,(e,o) in checks.items() if e!=o}
    return {'schema':1,'result':'MISMATCH' if mismatches else 'MATCH',
            'compared_fields':len(checks),'mismatches':mismatches,
            'not_flashable':True,
            'note':'Hardware metadata validation, not firmware/UEFI/XNU validation'}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--board',type=Path,default=Path('boards/waffle.toml'))
    ap.add_argument('--snapshot',type=Path,default=Path('boards/observations/coloros16_501.json'))
    ap.add_argument('--out',type=Path)
    args=ap.parse_args()
    result=crosscheck(load(args.board),json.loads(args.snapshot.read_text(encoding='utf-8')))
    output=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.out:
        args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(output,encoding='utf-8')
    print(output,end='')
    raise SystemExit(1 if result['mismatches'] else 0)
if __name__=='__main__':main()
