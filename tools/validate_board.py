#!/usr/bin/env python3
"""Validate documented OnePlus 12 metadata; does NOT generate boot/flash images."""
import json
import sys
import tomllib
from pathlib import Path

def validate(config: dict) -> dict:
    board=config['board']
    assert board['name'] == 'waffle' and board['model'] == 'PJD110'
    assert board['soc'] == 'SM8650' and board['platform'] == 'pineapple'
    assert board['arch'] == 'arm64'
    dt=config['device_tree']
    assert 'waffle' in dt['model']
    assert 'qcom,pineapple' in dt['compatible']
    disp=config['display']
    assert disp['width_px'] > 0 and disp['height_px'] > 0
    assert disp['max_reported_refresh_hz'] <= 240
    assert config['storage']['ab_slots'] == ['a','b']
    parts=config['partition_size_bytes']
    for k in ('boot', 'init_boot', 'vendor_boot', 'dtbo', 'vbmeta', 'abl', 'recovery'):
        a=parts[k+'_a']; b=parts[k+'_b']
        assert isinstance(a,int) and a>0 and a==b, f'invalid AB pair: {k}'
    assert parts['super'] >= 16*1024*1024
    for key, val in config['port_status'].items():
        assert val in ('not_started','in_progress','host_tested','device_tested')
    assert config['gpu']['xnu_driver_status'] in ('unimplemented','experimental','device_tested')
    # No low-level addresses; their presence would give an unsupported illusion of safe flashing.
    forbidden={'mmio_base','load_addr','dram_base','partition_start_lba','firmware_key','private_key'}
    for section in config.values():
        if isinstance(section,dict):
            assert not forbidden.intersection(section), 'unverified sensitive boot value present'
    return config

def load(path: Path) -> dict:
    with path.open('rb') as file:
        return validate(tomllib.load(file))

if __name__=='__main__':
    file=Path(sys.argv[1] if len(sys.argv)>1 else 'boards/waffle.toml')
    print(json.dumps({'board':load(file)['board'],'validation':'PASS','note':'Not a flashable image'},indent=2,ensure_ascii=False))
