"""S8 inventory enforces device provenance and preserves missing providers."""
from pathlib import Path
import json
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from s8_power_inventory import inventory,FDT_PIN
from qualify_evidence_s5 import EvidenceError

ROOT=Path(__file__).resolve().parents[1]

class S8PowerInventory(unittest.TestCase):
    def test_bad_evidence_fails_closed(self):
        with self.assertRaises(EvidenceError):inventory(b'not FDT')
        with self.assertRaises(EvidenceError):inventory(b'not FDT',board_build='PJD110_17')

    def test_resolved_inventory_safe_to_share(self):
        path=ROOT/'boards/gpu/waffle_a750_power_deps_s8.json'
        data=json.loads(path.read_text())
        self.assertEqual(data['fdt_sha256'],FDT_PIN)
        self.assertFalse(data['all_phandles_resolved'])
        self.assertFalse(data['safe_to_issue_power_votes'])
        self.assertFalse(data['hardware_ownership_confirmed'])
        self.assertTrue(data['gpu_clock_tail_has_null_phandle'])
        self.assertTrue(data['clock_null_trailer_requires_index_audit'])
        self.assertEqual(data['dependencies']['gmu']['iommus']['specifiers'][0]['argument_cells'],[5,0])
        self.assertIn('/soc/clock-controller@3d90000',
                      [i['provider_path'] for i in data['dependencies']['gmu']['clocks']['specifiers']])

if __name__=='__main__':unittest.main()
