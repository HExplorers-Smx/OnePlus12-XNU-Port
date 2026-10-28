"""S8 preboot static 4K mapping and HFI host tests, not hardware proof."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SRC=('waffle_s8_preboot.c','waffle_a750_hw_s5.c','waffle_smmu_pt.c',
     'waffle_gpu_lease.c','waffle_hfi_alloc.c','waffle_hfi_ring.c',
     'waffle_hfi_runtime.c','host_s8_preboot_test.c')

class S8Native(unittest.TestCase):
    def test_static_scratch_runtime_bridge(self):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:self.skipTest('No native C compiler installed')
        with tempfile.TemporaryDirectory(prefix='waffle-s8-') as tmp:
            cmd=[compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pedantic']
            if os.getenv('WAFFLE_SANITIZE')=='1':
                cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            cmd+=list(map(str,(ROOT/'gpu'/f for f in SRC)))+['-o',str(Path(tmp)/'s8')]
            result=subprocess.run(cmd,capture_output=True,text=True,timeout=40)
            self.assertEqual(result.returncode,0,result.stderr)
            check=subprocess.run([str(Path(tmp)/'s8')],capture_output=True,text=True,timeout=40)
            self.assertEqual(check.returncode,0,check.stderr or check.stdout)
            self.assertIn('host checks passed',check.stdout)

    def test_hardware_ownership_not_faked(self):
        data=(ROOT/'gpu/waffle_s8_preboot.c').read_text()
        self.assertIn('attest_runtime',data)
        self.assertIn('wa_pt_seal',data)
        for forbidden in ('/dev/mem','fastboot flash','/dev/block/','writel(', 'ioremap('):
            self.assertNotIn(forbidden,data)
        self.assertIn('return 0;', (ROOT/'gpu/waffle_a750_hw_s5.c').read_text())

if __name__=='__main__':unittest.main()
