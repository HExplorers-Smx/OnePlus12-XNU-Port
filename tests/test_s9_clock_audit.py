"""Pinned ColorOS GPU clock index evidence without leaking FDT into git."""
from pathlib import Path
import sys
import unittest
import os
import struct

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from s9_clock_audit import decode_clock_names, audit, selected_properties, NAMES, EXPECTED
from qualify_evidence_s5 import read_zip_bundle, capture_properties

def prop(values):return struct.pack(f'>{len(values)}I',*values)
def fixture():
    providers={0x2c:(EXPECTED[0][0],1),0x4e:(EXPECTED[2][0],1),
               0x3f:(EXPECTED[3][0],0)}
    node={'clock-names':b'\0'.join(x.encode('utf-8') for x in NAMES)+b'\0',
          'clocks':prop([0x2c,37,0x2c,38,0x4e,0,0x3f,0])}
    return node,providers

class S9ClockAudit(unittest.TestCase):
    def test_four_named_entries_plus_terminal_zero(self):
        node,providers=fixture()
        r=decode_clock_names(node,providers)
        self.assertEqual(len(r['named']),4)
        self.assertEqual(r['named'][3]['kind'],'power-controller-reference-unverified')
        self.assertFalse(r['safe_to_vote_or_write'])
    def test_zero_in_middle_rejected(self):
        node,providers=fixture()
        node['clocks']=prop([0x2c,37,0x2c,38,0,0x4e,0,0x3f])
        with self.assertRaises(ValueError):decode_clock_names(node,providers)
    def test_drift_and_wrong_provider_rejected(self):
        node,providers=fixture()
        node['clock-names']=b'gpu_cc_ahb\0'
        with self.assertRaises(ValueError):decode_clock_names(node,providers)
        node,providers=fixture()
        providers[0x3f]=('/soc/wrong@c300000',0)
        with self.assertRaises(ValueError):decode_clock_names(node,providers)
    def test_unexpected_extra_cells_rejected(self):
        node,providers=fixture()
        node['clocks']+=prop([10])
        with self.assertRaises(ValueError):decode_clock_names(node,providers)
    def test_public_evidence_is_sanitized(self):
        import json
        data=json.loads((ROOT/'boards/gpu/waffle_s9_clock_index.json').read_text())
        self.assertFalse(data['hardware_control_available'])
        self.assertEqual([x['name'] for x in data['clock_audit']['named']],list(NAMES))
        self.assertNotIn('firmware/',str(data))
    def test_real_capture_when_provided(self):
        zpath=os.environ.get('ONEPLUS12_PRIVATE_EVIDENCE')
        if not zpath:self.skipTest('private evidence not installed in public repository')
        bundle=read_zip_bundle(Path(zpath))
        props=capture_properties(bundle['capture.txt'])
        r=audit(bundle['fdt.dtb'],props['prop.ro.build.display.id'])
        self.assertEqual(len(r['clock_audit']['named']),4)
        self.assertTrue(r['clock_audit']['trailing_phandle_zero'])
        with self.assertRaises(ValueError):audit(bundle['fdt.dtb']+b'changed',props['prop.ro.build.display.id'])
        with self.assertRaises(ValueError):audit(bundle['fdt.dtb'],'PJD110_17.0.0.0(CN01)')

if __name__=='__main__':unittest.main()
