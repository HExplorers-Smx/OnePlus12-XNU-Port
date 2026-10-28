"""S11 UEFI discovery and FDT validation: host-only, fail-closed test cases."""
from __future__ import annotations
import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]

class Board(ctypes.Structure):
    _fields_=[(x,ctypes.c_uint32) for x in (
        'totalsize','version','structure_bytes','nodes','properties','crc32',
        'model_waffle','compatible_pineapple')]
class Memory(ctypes.Structure):
    _fields_=[('physical_start',ctypes.c_uint64),('pages',ctypes.c_uint64),('memory_type',ctypes.c_uint32)]

def be(n):return struct.pack('>I',n)
def aligned(b:bytes,align=4): return b+b'\0'*((-len(b))%align)
def make_fdt(model=b'Qualcomm Technologies, Inc. Pineapple MTP,waffle\x00',
             compatible=b'qcom,pineapple-mtp\x00qcom,pineapple\x00'):
    strings=b'model\x00compatible\x00'
    root=be(1)+aligned(b'\x00')
    root+=be(3)+be(len(model))+be(0)+aligned(model)
    root+=be(3)+be(len(compatible))+be(6)+aligned(compatible)
    root+=be(2)+be(9)
    reserve=bytes(16)
    off_struct=40+len(reserve)
    off_strings=off_struct+len(root)
    total=off_strings+len(strings)
    hdr=b''.join(map(be,[0xd00dfeed,total,off_struct,off_strings,40,17,16,0,len(strings),len(root)]))
    return hdr+reserve+root+strings

class S11Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc: raise unittest.SkipTest('C compiler not available')
        cls.tmp=tempfile.TemporaryDirectory(prefix='op12-s11-')
        cls.libpath=Path(cls.tmp.name)/'fdt.so'
        cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-shared','-fPIC',
             '-I',str(ROOT/'boot'),str(ROOT/'boot/waffle_s11_fdt.c'),'-o',str(cls.libpath)]
        if os.environ.get('WAFFLE_SANITIZE')=='1':
            # ASan runtime preload is not reliable when loading a shared library
            # into Python. Separate native sanitizer runner tests this module.
            pass
        proc=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        if proc.returncode:raise AssertionError(proc.stderr)
        cls.lib=ctypes.CDLL(str(cls.libpath))
        cls.lib.wa_s11_fdt_inspect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(Board)]
        cls.lib.wa_s11_fdt_inspect.restype=ctypes.c_int
        cls.lib.wa_s11_memory_contains.argtypes=[ctypes.POINTER(Memory),ctypes.c_size_t,ctypes.c_uint64,ctypes.c_uint64]
        cls.lib.wa_s11_memory_contains.restype=ctypes.c_int
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def inspect(self,blob):
        result=Board()
        code=self.lib.wa_s11_fdt_inspect(blob,len(blob),ctypes.byref(result))
        return code,result
    def test_valid_waffle(self):
        rc,x=self.inspect(make_fdt())
        self.assertEqual(rc,0)
        self.assertEqual((x.nodes,x.properties,x.model_waffle,x.compatible_pineapple),(1,2,1,1))
    def test_reject_foreign_board(self):
        self.assertEqual(self.inspect(make_fdt(model=b'OnePlus Ace 5,giulia\0'))[0],-4)
        self.assertEqual(self.inspect(make_fdt(compatible=b'qcom,lahaina\0'))[0],-4)
    def test_bad_magic_truncation_and_excess_total(self):
        b=make_fdt()
        self.assertEqual(self.inspect(b'XXX'+b[3:])[0],-3)
        self.assertEqual(self.inspect(b[:30])[0],-1)
        self.assertEqual(self.inspect(b[:100])[0],-2)
        bad=bytearray(b);bad[4:8]=be(9*1024*1024)
        self.assertEqual(self.inspect(bytes(bad))[0],-2)
    def test_overlapping_blocks_and_invalid_nameoff(self):
        b=bytearray(make_fdt());b[12:16]=b[8:12]  # string block overlaps structures
        self.assertEqual(self.inspect(bytes(b))[0],-2)
        b=bytearray(make_fdt());b[64:68]=be(888) # first property length exceeds total
        self.assertNotEqual(self.inspect(bytes(b))[0],0)
    def test_no_privacy_raw_dump_required(self):
        b=make_fdt()+b'private-extra-not-part-of-FDT'
        rc,out=self.inspect(b)
        self.assertEqual(rc,0)
        self.assertEqual(out.totalsize,len(make_fdt()))
    def test_memory_map_must_own_entire_region(self):
        x=(Memory*2)(Memory(0x100000,3,4),Memory(0x300000,16,11))
        check=self.lib.wa_s11_memory_contains
        self.assertEqual(check(x,2,0x100050,4096),1)
        self.assertEqual(check(x,2,0x102000,4096),1)
        self.assertEqual(check(x,2,0x102000,4097),0)
        self.assertEqual(check(x,2,0x300000,4096),0)
        self.assertEqual(check(x,2,0xfffffffffffffff0,256),0)
    def test_stage_contains_buildable_source_set(self):
        spec=importlib.util.spec_from_file_location('stage_s11',ROOT/'tools/s11_stage_edk2.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        with tempfile.TemporaryDirectory() as d:
            result=mod.stage(Path(d))
            self.assertEqual(len(result),8)
            pkg=Path(d)/'WaffleS11Pkg'
            inf=(pkg/'WaffleS11Probe/WaffleS11Probe.inf').read_text()
            self.assertIn('waffle_s11_fdt.c',inf)
            self.assertIn('WaffleS11Pkg/WaffleS11Probe/WaffleS11Probe.inf',(pkg/'WaffleS11Pkg.dsc').read_text())
            self.assertFalse(any(x.suffix in ('.img','.mbn','.dtb','.zip','.key') for x in pkg.rglob('*')))
    def test_uefi_cross_target_syntax_with_host_shim(self):
        cc=shutil.which('clang')
        if not cc: raise unittest.SkipTest('Clang unavailable')
        common=[cc,'-target','aarch64-unknown-windows','-std=c11','-ffreestanding',
                '-Wall','-Wextra','-Werror','-I',str(ROOT/'boot')]
        uefi=common+['-fshort-wchar','-I',str(ROOT/'tests/fixtures/s11_edk2'),
                     '-fsyntax-only',str(ROOT/'uefi/WaffleS11Probe/WaffleS11Probe.c')]
        portable=common+['-fsyntax-only',str(ROOT/'boot/waffle_s11_fdt.c'),str(ROOT/'boot/waffle_s10_log.c')]
        for cmd in (uefi,portable):
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stderr)
    def test_real_private_fdt_when_available(self):
        evidence=os.getenv('ONEPLUS12_PRIVATE_EVIDENCE')
        if not evidence:raise unittest.SkipTest('Private owner evidence not passed; public CI cannot access it')
        with zipfile.ZipFile(evidence) as f:b=f.read('fdt.dtb')
        rc,board=self.inspect(b)
        self.assertEqual(rc,0)
        self.assertEqual(board.totalsize,919577)
        self.assertEqual(board.nodes,5009)
        self.assertEqual(board.properties,22265)
    def test_fdt_is_fail_closed_without_uefi_backend(self):
        c=(ROOT/'uefi/WaffleS11Probe/WaffleS11Probe.c').read_text()
        for banned in ('ExitBootServices(', 'SetVariable(', 'WriteBlocks(', 'IoWrite(',
                       'PciWrite(', 'fastboot flash', 'gpucc_enable('):
            self.assertNotIn(banned,c)
        self.assertIn('FdtMemoryContains',c)
        self.assertIn('WA_S10_TYPE_ERROR',c)
        self.assertIn('usb=unverified',c)

class S11UefiEntrypointTests(unittest.TestCase):
    def test_uefi_main_executes_and_outputs_verified_wflg(self):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc:raise unittest.SkipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'uefi-mock'
            cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                 '-I',str(ROOT/'tests/fixtures/s11_edk2'),'-I',str(ROOT/'boot'),
                 str(ROOT/'uefi/WaffleS11Probe/WaffleS11Probe.c'),
                 str(ROOT/'boot/waffle_s11_fdt.c'),str(ROOT/'boot/waffle_s10_log.c'),
                 str(ROOT/'boot/host_s11_uefi_mock.c'),'-o',str(exe)]
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stderr)
            spec=importlib.util.spec_from_file_location('s10_decoder',ROOT/'tools/s10_decode_log.py')
            mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
            for kind,device,expected_exit,expected_type in (
                ('success',make_fdt(),0,'boot'),('foreign',make_fdt(model=b'OnePlus Ace 5,giulia\0'),1,'error')):
                file=Path(d)/(kind+'.dtb');file.write_bytes(device)
                r=subprocess.run([str(exe),str(file)],capture_output=True,text=True,timeout=30)
                self.assertEqual(r.returncode,expected_exit,r.stderr)
                line=next((a.strip() for a in r.stdout.splitlines() if a.startswith('WFLG_HEX:')),None)
                self.assertIsNotNone(line,r.stdout)
                decoded=mod.decode(bytes.fromhex(line.removeprefix('WFLG_HEX:')))
                self.assertEqual(decoded['frames'][0]['type'],expected_type)
                self.assertIn('usb=unverified',decoded['frames'][0]['payload_utf8'])
                self.assertIn('xnu=not-started',decoded['frames'][0]['payload_utf8'])
                if expected_exit==0:self.assertIn('waffle model/compatible',r.stdout)
            file=Path(d)/'success.dtb'
            for failure in ('badmap','missing'):
                r=subprocess.run([str(exe),str(file),failure],capture_output=True,text=True,timeout=30)
                self.assertEqual(r.returncode,1,r.stdout)
                self.assertIn('WFLG_HEX:',r.stdout)

class S11QuotaTests(unittest.TestCase):
    def test_all_repo_workflows_are_manual_only(self):
        folder=ROOT/'.github/workflows'
        for p in folder.glob('*.yml'):
            text=p.read_text()
            self.assertIn('  workflow_dispatch:',text,p)
            for prohibited in ('  push:', '  pull_request:', '  pull_request_target:',
                               '  schedule:', '  workflow_run:', '  repository_dispatch:'):
                self.assertNotIn(prohibited,text,p)

if __name__=='__main__':unittest.main()
