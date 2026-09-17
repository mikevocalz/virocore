import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
import warnings
import zipfile

spec = importlib.util.spec_from_file_location('alignment', Path(__file__).with_name('verify-16kb-alignment.py'))
alignment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(alignment)


def elf(alignments=(0x4000,)):
    data = bytearray(64 + 56 * len(alignments))
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<H', data, 18, 183)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HH', data, 54, 56, len(alignments))
    for i, align in enumerate(alignments):
        struct.pack_into('<I', data, 64 + i * 56, 1)
        struct.pack_into('<Q', data, 64 + i * 56 + 48, align)
    return data


class AlignmentTests(unittest.TestCase):
    def archive_result(self, entries):
        # A sequence of pairs, unlike a mapping, can repeat a member name.
        members = entries.items() if hasattr(entries, 'items') else entries
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'renderer.aar'
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')  # zipfile warns on duplicate names
                with zipfile.ZipFile(file, 'w') as archive:
                    for name, data in members:
                        archive.writestr(name, data)
            with contextlib.redirect_stdout(io.StringIO()):
                return alignment.main([str(file)])

    def test_aligned(self):
        self.assertEqual(self.archive_result({'jni/arm64-v8a/libviro.so': elf()}), 0)

    def test_mixed_alignment_fails(self):
        self.assertEqual(self.archive_result({'jni/arm64-v8a/libviro.so': elf((0x4000, 0x1000))}), 1)

    def test_empty_or_wrong_abi_archive_fails(self):
        for entries in ({}, {'jni/armeabi-v7a/libviro.so': elf()}):
            self.assertEqual(self.archive_result(entries), 1)

    def test_corrupt_library_is_not_skipped(self):
        for data in (b'not ELF', elf()[:70], elf(())):
            self.assertEqual(self.archive_result({'jni/arm64-v8a/libviro.so': data}), 1)

    def test_wrong_class_machine_and_endianness_fail(self):
        for offset, value in ((4, 1), (5, 2), (18, 62)):
            data = elf(); data[offset] = value
            self.assertEqual(self.archive_result({'jni/arm64-v8a/libviro.so': data}), 1)

    def test_duplicate_entry_names_are_all_checked(self):
        # read(name) resolves through NameToInfo, which keeps only the last
        # member, so an under-aligned duplicate must fail in either order.
        name = 'jni/arm64-v8a/libviro.so'
        good, stale = elf(), elf((0x1000,))
        self.assertEqual(self.archive_result([(name, stale), (name, good)]), 1)
        self.assertEqual(self.archive_result([(name, good), (name, stale)]), 1)
        self.assertEqual(self.archive_result([(name, good), (name, good)]), 0)

    def test_prefab_and_bad_offset_alignment(self):
        self.assertEqual(self.archive_result({'prefab/modules/x/libs/android.arm64-v8a/libx.so': elf()}), 0)
        data = elf(); struct.pack_into('<Q', data, 72, 1)
        self.assertEqual(self.archive_result({'jni/arm64-v8a/libviro.so': data}), 1)


if __name__ == '__main__':
    unittest.main()
