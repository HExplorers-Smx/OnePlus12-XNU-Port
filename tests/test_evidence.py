import copy
import json
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate_board import load
from check_evidence import crosscheck,EvidenceMismatch

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.board=load(ROOT/'boards/waffle.toml')
        self.snap=json.loads((ROOT/'boards/observations/coloros16_501.json').read_text())
    def test_real_report_alignment(self):
        d=crosscheck(self.board,self.snap)
        self.assertEqual(d['result'],'MATCH',d['mismatches'])
        self.assertGreaterEqual(d['compared_fields'],15)
    def test_reject_mutated_gpu(self):
        bad=copy.deepcopy(self.snap)
        bad['gpu']['model']='Adreno730'
        d=crosscheck(self.board,bad)
        self.assertIn('gpu',d['mismatches'])
    def test_bad_root(self):
        self.snap['valid_root_snapshot']=False
        with self.assertRaises(EvidenceMismatch):crosscheck(self.board,self.snap)
if __name__=='__main__':unittest.main()
