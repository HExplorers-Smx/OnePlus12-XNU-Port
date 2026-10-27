import copy
import importlib.util
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate_board import load,validate
from make_board_bundle import build

class BoardTests(unittest.TestCase):
    def test_valid_board(self):
        x=load(ROOT/'boards/waffle.toml')
        self.assertEqual(x['device_tree']['model'].split(',')[-1], 'waffle')
        self.assertEqual(x['gpu']['model'],'Adreno750v2')
    def test_mismatched_partition(self):
        x=copy.deepcopy(load(ROOT/'boards/waffle.toml'))
        x['partition_size_bytes']['boot_b']=1024
        with self.assertRaises(AssertionError):validate(x)
    def test_unknown_device(self):
        x=copy.deepcopy(load(ROOT/'boards/waffle.toml'))
        x['board']['model']='PKG110'
        with self.assertRaises(AssertionError):validate(x)
    def test_bundle(self):
        import tempfile,json
        with tempfile.TemporaryDirectory() as t:
            y=json.loads(build(ROOT/'boards/waffle.toml',Path(t)).read_text())
            self.assertTrue(y['not_flashable'])
            self.assertEqual(y['kind'],'hardware-metadata-only')
if __name__=='__main__':unittest.main()
