#!/usr/bin/env python3
"""Produce a metadata-only bundle, never boot/partition images."""
import json
import sys
from pathlib import Path
from validate_board import load

def build(boardfile: Path, outdir: Path) -> Path:
    data=load(boardfile)
    outdir.mkdir(parents=True,exist_ok=True)
    out=outdir/'waffle-board-baseline.json'
    out.write_text(json.dumps({'schema':1,'kind':'hardware-metadata-only',
        'not_flashable':True,'source':'PJD110 read-only DeviceArchive v1.0 report (2026-10-27)',
        'data':data},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return out
if __name__=='__main__':
    print(build(Path(sys.argv[1] if len(sys.argv)>1 else 'boards/waffle.toml'),
        Path(sys.argv[2] if len(sys.argv)>2 else 'output')))
