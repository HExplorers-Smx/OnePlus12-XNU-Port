"""S13: actually stage synthetic Mach-O segments and validate EFI allocation path."""
from __future__ import annotations
import ctypes, importlib.util, os, shutil, struct, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('s12',ROOT/'tools/s12_pack_xnup.py')
s12=importlib.util.module_from_spec(spec);spec.loader.exec_module(s12)
BASE=0xffffff8000000000
HEADER=struct.Struct('<IIIIIIII')
SEG=struct.Struct('<II16sQQQQIIII')

def kernel(*,overlap=False,truncated=False,wrong_entry=False,fixups=False):
    vm0=BASE; vm1=BASE+(0x0 if overlap else 0x4000)
    cmds=[]
    for name,vm,offset,bytesize,prot in [(b'__TEXT',vm0,0x1000,4,5),(b'__DATA',vm1,0x2000,4,3)]:
        cmds.append(SEG.pack(0x19,72,name.ljust(16,b'\0'),vm,0x4000,offset,bytesize,prot,prot,0,0))
    if fixups: cmds.append(struct.pack('<IIII',0x80000034,16,0,0))
    # LC_UNIXTHREAD: ARM_THREAD_STATE64 x29+fp/lr/sp/pc; just enough for PC.
    state=bytearray(272);struct.pack_into('<Q',state,32*8,BASE+(0x8000 if wrong_entry else 0))
    cmds.append(struct.pack('<IIII',5,16+len(state),6,68)+state)
    commands=b''.join(cmds)
    k=bytearray(0x3000)
    k[:32]=HEADER.pack(0xfeedfacf,0x0100000c,0,2,len(cmds),len(commands),0,0)
    k[32:32+len(commands)]=commands
    k[0x1000:0x1004]=b'XNU!';k[0x2000:0x2004]=b'DATA'
    return bytes(k[:-1] if truncated else k)

class Seg(ctypes.Structure):
    _fields_=[('vmaddr',ctypes.c_uint64),('vmsize',ctypes.c_uint64),
              ('fileoff',ctypes.c_uint64),('filesize',ctypes.c_uint64),('initprot',ctypes.c_uint32)]
class Plan(ctypes.Structure):
    _fields_=[('seg',Seg*64),('vm_base',ctypes.c_uint64),('vm_size',ctypes.c_uint64),
              ('entry_vmaddr',ctypes.c_uint64),('segments',ctypes.c_uint32),('has_fixups',ctypes.c_uint32)]
class BootVideo(ctypes.Structure):
    _fields_=[(x,ctypes.c_uint64) for x in ('v_baseAddr','v_display','v_rowBytes','v_width','v_height','v_depth')]
class BootArgs(ctypes.Structure):
    _fields_=[('revision',ctypes.c_uint16),('version',ctypes.c_uint16),
              ('virtBase',ctypes.c_uint64),('physBase',ctypes.c_uint64),('memSize',ctypes.c_uint64),
              ('topOfKernelData',ctypes.c_uint64),('video',BootVideo),('machineType',ctypes.c_uint32),
              ('deviceTreeP',ctypes.c_uint64),('deviceTreeLength',ctypes.c_uint32),
              ('commandLine',ctypes.c_char*1024),('bootFlags',ctypes.c_uint64),('memSizeActual',ctypes.c_uint64)]
class TestMachO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='wa-s13-')
        cls.cc=shutil.which('clang') or shutil.which('gcc')
        if not cls.cc:raise unittest.SkipTest('compiler needed')
        path=Path(cls.tmp.name)/'macho.so'
        r=subprocess.run([cls.cc,'-std=c11','-O1','-g','-shared','-fPIC','-Wall','-Wextra','-Werror',
             str(ROOT/'boot/waffle_s13_macho.c'),'-o',str(path)],capture_output=True,text=True)
        assert r.returncode==0,r.stderr
        cls.lib=ctypes.CDLL(str(path))
        cls.plan=cls.lib.wa_s13_macho_plan
        cls.plan.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(Plan)]
        cls.plan.restype=ctypes.c_int
        cls.stage=cls.lib.wa_s13_macho_stage
        cls.stage.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(Plan),ctypes.c_void_p,ctypes.c_size_t]
        cls.stage.restype=ctypes.c_int
        cls.bootargs=cls.lib.wa_s13_bootargs_draft
        cls.bootargs.argtypes=[ctypes.POINTER(BootArgs),ctypes.POINTER(Plan),ctypes.c_uint64,ctypes.c_uint64,ctypes.c_char_p,ctypes.c_size_t]
        cls.bootargs.restype=ctypes.c_int
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def check(self,k):
        plan=Plan();rc=self.plan(k,len(k),ctypes.byref(plan));return rc,plan
    def test_segment_copy_bss_and_entry(self):
        k=kernel();rc,p=self.check(k);self.assertEqual(rc,0)
        self.assertEqual((p.vm_base,p.vm_size,p.segments,p.entry_vmaddr),(BASE,0x8000,2,BASE))
        buf=ctypes.create_string_buffer(b'\xa5'*p.vm_size)
        self.assertEqual(self.stage(k,len(k),ctypes.byref(p),buf,p.vm_size),0)
        self.assertEqual(buf.raw[:6],b'XNU!\0\0')
        self.assertEqual(buf.raw[0x4000:0x4006],b'DATA\0\0')
        self.assertEqual(buf.raw[0x2000],0)
    def test_plan_mutation_rejected(self):
        k=kernel();_,p=self.check(k)
        p.seg[0].filesize=5000
        buf=ctypes.create_string_buffer(p.vm_size)
        self.assertNotEqual(self.stage(k,len(k),ctypes.byref(p),buf,p.vm_size),0)
    def test_overlap_rejected(self):
        self.assertEqual(self.check(kernel(overlap=True))[0],-4)
    def test_wrong_entry_rejected(self):
        self.assertNotEqual(self.check(kernel(wrong_entry=True))[0],0)
    def test_truncated_file_rejected(self):
        k=kernel(truncated=True);rc,p=self.check(k)
        self.assertEqual(rc,0) # last byte is padding and not used; this is legal.
        damaged=bytearray(kernel());struct.pack_into('<Q',damaged,32+40,0x4000)
        self.assertNotEqual(self.check(bytes(damaged))[0],0)
    def test_unsupported_and_wrong_prot(self):
        k=bytearray(kernel());struct.pack_into('<I',k,12,6)
        self.assertNotEqual(self.check(bytes(k))[0],0)
        k=bytearray(kernel());struct.pack_into('<I',k,32+60,8)
        self.assertNotEqual(self.check(bytes(k))[0],0)
    def test_fixup_flag_only_not_patched(self):
        rc,p=self.check(kernel(fixups=True))
        self.assertEqual(rc,0);self.assertEqual(p.has_fixups,1)
    def test_bootargs_draft_iOS26_only(self):
        rc,p=self.check(kernel());self.assertEqual(rc,0)
        a=BootArgs();self.assertEqual(self.bootargs(ctypes.byref(a),ctypes.byref(p),0x10000000,0xa000,b'cpus=1\0',7),0)
        self.assertEqual(a.virtBase,BASE);self.assertEqual(a.topOfKernelData,0x10008000)
        self.assertEqual(a.commandLine,b'cpus=1')
        self.assertNotEqual(self.bootargs(ctypes.byref(a),ctypes.byref(p),0x10000000,0x100,b'cpus=1\0',7),0)
    def test_uefi_mock_memory_flow_and_rejections(self):
        from tests.test_s11_native import make_fdt
        with tempfile.TemporaryDirectory(prefix='wa-s13-efi-') as t:
            t=Path(t); exe=t/'mock'
            sources=[ROOT/'uefi/WaffleS13Loader/waffle_s13_efi.c',ROOT/'boot/waffle_s13_macho.c',
                ROOT/'boot/waffle_s12_xnup.c',ROOT/'boot/waffle_s11_fdt.c',ROOT/'boot/host_s13_efi_mock.c']
            cmd=[self.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                  '-I'+str(ROOT/'uefi/WaffleS13Loader'),'-I'+str(ROOT/'boot'),*map(str,sources),'-o',str(exe)]
            if os.environ.get('WAFFLE_SANITIZE')=='1':cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            res=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(res.returncode,0,res.stderr)
            f=t/'waffle.dtb';f.write_bytes(make_fdt());p=t/'payload.xnup';p.write_bytes(s12.make({'kernel':kernel()},'cpus=1'))
            for mode,code,expected in [(None,0,'S13 PASS'),('badmap',1,'S13 FAIL'),
                                        ('nooptions',1,'S13 FAIL'),('noalloc',1,'S13 FAIL')]:
                run=subprocess.run([str(exe),str(f),str(p)]+([mode] if mode else []),capture_output=True,text=True)
                self.assertEqual(run.returncode,code,run.stdout+run.stderr)
                self.assertIn(expected,run.stdout)
    def test_arm64_efi_binary(self):
        res=subprocess.run(['bash',str(ROOT/'tools/s13_build_efi.sh')],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(res.returncode,0,res.stderr)
        b=(ROOT/'output/s13/WaffleS13EphemeralStage.efi').read_bytes()
        self.assertEqual(b[:2],b'MZ')
        off=struct.unpack_from('<I',b,0x3c)[0]
        self.assertEqual(b[off:off+4],b'PE\0\0')
        self.assertEqual(struct.unpack_from('<H',b,off+4)[0],0xaa64)
        self.assertEqual(struct.unpack_from('<H',b,off+24+68)[0],10)
if __name__=='__main__':unittest.main()
