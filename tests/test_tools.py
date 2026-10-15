import importlib.util
import pathlib
import unittest
import os
import subprocess
import tempfile
import struct

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('check_memory', TOOLS / 'check-memory.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class MemoryChecks(unittest.TestCase):
    def setUp(self):
        self.symbols = dict(__flash_binary_end=0x100c0000, __bss_end__=0x20020000,
                            __HeapLimit=0x2003c000, __StackBottom=0x20041800,
                            __StackTop=0x20042000, main=0x10000400)
    def check(self, **kwargs):
        return module.validate(self.symbols, 2097152, 1048576, 1048576, **kwargs)
    def test_valid(self):
        self.assertEqual(self.check(stack_reserve=24576), (786432, 131072, 114688, 24576))
    def test_firmware_overlap(self):
        self.symbols['__flash_binary_end'] = 0x10100004
        with self.assertRaisesRegex(ValueError, 'overlaps'): self.check()
    def test_heap_collision(self):
        self.symbols['__HeapLimit'] = 0x20042000
        with self.assertRaisesRegex(ValueError, 'SRAM'): self.check()
    def test_stack_reserve(self):
        with self.assertRaisesRegex(ValueError, 'STACK_RESERVE'): self.check(stack_reserve=32768)
    def test_missing_symbol(self):
        del self.symbols['__flash_binary_end']
        with self.assertRaisesRegex(ValueError, 'missing linker'): self.check()
    def test_ram_entry(self):
        self.symbols['main'] = 0x20000100
        with self.assertRaisesRegex(ValueError, 'XIP'): self.check()
        self.check(copy_to_ram=True)
    def test_volume_overrun_and_alignment(self):
        with self.assertRaisesRegex(ValueError, 'capacity'):
            module.validate(self.symbols, 1048576, 1048576, 1048576)
        with self.assertRaisesRegex(ValueError, 'multiples'):
            module.validate(self.symbols, 2097152, 1048577, 1048575)

class LayoutChecks(unittest.TestCase):
    def test_mismatch_and_empty_probes_fail(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            for name, size in [('host', 8), ('target', 4)]:
                src = work / (name + '.c')
                src.write_text(f'char layout_long[{size}];\n')
                subprocess.run(['cc', '-c', str(src), '-o', str(work / (name + '.o'))], check=True)
            cmd = ['python3', str(TOOLS / 'check-layout.py'), '--host-object', str(work / 'host.o'),
                   '--target-object', str(work / 'target.o'), '--target-nm', 'nm']
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('host=8, target=4', result.stderr)
            cmd[cmd.index('--target-object') + 1] = str(work / 'host.o')
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
            src = work / 'empty.c'; src.write_text('char unrelated[4];\n')
            subprocess.run(['cc', '-c', str(src), '-o', str(work / 'empty.o')], check=True)
            for flag in ['--host-object', '--target-object']:
                cmd[cmd.index(flag) + 1] = str(work / 'empty.o')
            self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)

class VolumeChecks(unittest.TestCase):
    def test_names_spaces_and_nested_destinations(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            src = work / 'source with spaces'; src.write_bytes(b'volume fixture\n')
            manifest = work / 'fixture.txt'
            manifest.write_text(f'label TESTVOL\noffset 0x180000\nsize 524288\nflash no\n'
                                f'file\t{src}\tnested/renamed file.txt\nempty\tnested/scores\ndir\tsave\n')
            subprocess.run(['sh', str(TOOLS / 'mkvolume.sh'), '--manifest', str(manifest),
                            '--out', str(work)], check=True, stdout=subprocess.DEVNULL)
            output = work / 'extracted'
            subprocess.run(['mcopy', '-i', str(work / 'fixture-volume.img'),
                            '::nested/renamed file.txt', str(output)], check=True)
            self.assertEqual(output.read_bytes(), src.read_bytes())
            subprocess.run(['mdir', '-i', str(work / 'fixture-volume.img'), '::nested/scores'],
                           check=True, stdout=subprocess.DEVNULL)

    def test_exact_flash_geometry_and_custom_root(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            manifest = work / 'exact.txt'
            manifest.write_text('label EXACT\noffset 0x17B000\nsize 544768\n'
                                'root_entries 256\nflash no\n')
            command = ['sh', str(TOOLS / 'mkvolume.sh'), '--manifest', str(manifest),
                       '--out', str(work)]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            image = work / 'exact-volume.img'; boot = image.read_bytes()[:512]
            sector = struct.unpack_from('<H', boot, 11)[0]
            total = struct.unpack_from('<H', boot, 19)[0]
            root = struct.unpack_from('<H', boot, 17)[0]
            self.assertEqual(total * sector, image.stat().st_size)
            self.assertEqual(root, 256)
            reserved = struct.unpack_from('<H', boot, 14)[0]
            fat_sectors = struct.unpack_from('<H', boot, 22)[0]
            data_sectors = total - reserved - boot[16] * fat_sectors - root * 32 // sector
            # Exercise the final data clusters through mtools, without bypassing
            # its geometry checks. The previous geometry failed near fullness.
            payload = bytes(range(256)) * ((data_sectors - 1) * sector // 256)
            source = work / 'payload'; source.write_bytes(payload)
            subprocess.run(['mcopy', '-i', str(image), str(source), '::last'], check=True)
            output = work / 'readback'
            subprocess.run(['mcopy', '-i', str(image), '::last', str(output)], check=True)
            self.assertEqual(output.read_bytes(), payload)
            subprocess.run(['fsck.fat', '-n', str(image)], check=True, stdout=subprocess.DEVNULL)
            for invalid in ('0', '17', 'garbage'):
                manifest.write_text('flash no\nroot_entries ' + invalid + '\n')
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('root_entries', result.stderr)

class ProgramStateChecks(unittest.TestCase):
    def test_program_only_tagging_and_rejected_sections(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            program = work / 'program.c'
            program.write_text('int initialized=27; int zeroed; const int immutable=19;\n')
            sources = work / 'sources.txt'; sources.write_text(str(program) + '\n')
            output = work / 'program.o'
            launcher = ['python3', str(TOOLS / 'tag-program-state.py'), '--sources', str(sources),
                        '--objcopy', 'objcopy', '--objdump', 'objdump', '--']
            command = ['cc', '-fdata-sections', '-fno-common', '-c', str(program), '-o', str(output)]
            subprocess.run(launcher + command, check=True)
            sections = subprocess.check_output(['objdump', '-h', str(output)], text=True)
            self.assertIn('.rp2040_program_data', sections)
            self.assertIn('.rp2040_program_bss', sections)
            self.assertIn('.rodata', sections)
            sources.write_text(str(work / 'different.c') + '\n')
            subprocess.run(launcher + command, check=True)
            self.assertNotIn('.rp2040_program', subprocess.check_output(['objdump', '-h', str(output)], text=True))
            sources.write_text(str(program) + '\n')
            bad = subprocess.run(launcher + ['cc', '-fcommon', '-c', str(program), '-o', str(output)], capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('-fno-common', bad.stderr)
            program.write_text('int retained __attribute__((section(".retained")))=27;\n')
            bad = subprocess.run(launcher + command, capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('unsupported mutable section', bad.stderr)

if __name__ == '__main__': unittest.main()
