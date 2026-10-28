"""S9 HFI before/after runtime guard; explicit joint-header compatibility."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]

class S9Guard(unittest.TestCase):
    def test_dual_context_hfi_runtime_guard(self):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc:self.skipTest('No local C compiler')
        sources=('waffle_s9_runtime_guard.c','waffle_s9_clock_runtime.c',
                 'waffle_s8_preboot.c','waffle_a750_hw_s5.c','waffle_smmu_pt.c',
                 'waffle_gpu_lease.c','waffle_hfi_alloc.c','waffle_hfi_ring.c',
                 'waffle_hfi_runtime.c','host_s9_runtime_guard_test.c')
        with tempfile.TemporaryDirectory(prefix='s9-guard-') as d:
            cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pedantic',
                 '-include',str(ROOT/'gpu/waffle_s9_clock_runtime.h')]
            if os.getenv('WAFFLE_SANITIZE')=='1':
                cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            cmd += [str(ROOT/'gpu'/p) for p in sources]
            cmd += ['-o',str(Path(d)/'guard')]
            p=subprocess.run(cmd,text=True,capture_output=True,timeout=60)
            self.assertEqual(p.returncode,0,p.stderr)
            result=subprocess.run([str(Path(d)/'guard')],text=True,capture_output=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('host checks passed',result.stdout)
    def test_no_direct_hardware_access(self):
        content=(ROOT/'gpu/waffle_s9_runtime_guard.c').read_text()
        for forbid in ('/dev/mem','/dev/block/','writel(', 'ioremap(', 'fastboot flash'):
            self.assertNotIn(forbid,content)
        self.assertIn('return 0;', (ROOT/'gpu/waffle_a750_hw_s5.c').read_text())

if __name__=='__main__':unittest.main()
