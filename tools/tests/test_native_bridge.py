"""Inert PE fixtures for the native bridge inspector; no runner execution."""
import struct
import unittest

from tools.inspect_native_bridge import (InvalidImage, PEImage, call_graph, locate_table,
                                         rip_refs_in_range)


def image():
    data = bytearray(0x800)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHI', data, 0x84, 0x8664, 2, 123)
    struct.pack_into('<H', data, 0x94, 0xF0)
    struct.pack_into('<H', data, 0x98, 0x20B)
    struct.pack_into('<Q', data, 0xB0, 0x140000000)
    struct.pack_into('<I', data, 0xD0, 0x3000)
    for i, (rva, raw, flags) in enumerate(((0x1000, 0x200, 0x60000020),
                                          (0x2000, 0x400, 0x40000040))):
        off = 0x188 + i * 40
        struct.pack_into('<IIII', data, off + 8, 0x200, rva, 0x200, raw)
        struct.pack_into('<I', data, off + 36, flags)
    struct.pack_into('<Q', data, 0x418, 4)
    # Overlapping guest ranges are valid in real images; only starts must be strictly ordered.
    for i in range(4):
        struct.pack_into('<IIB7sQ', data, 0x420 + i * 24,
                         0x10000 + i * 0x10, 0x10020 + i * 0x10, i % 2,
                         bytes(7), 0x140001000 + i * 16)
    return data


def runtime_image():
    """Image with .pdata for three functions and one RIP-relative reference."""
    data = image()
    # DataDirectory[3] (exception): rva 0x2080, 36 bytes = three RUNTIME_FUNCTION rows.
    struct.pack_into('<II', data, 0x120, 0x2080, 36)
    for i, (begin, end) in enumerate(((0x1010, 0x1020), (0x1020, 0x1030), (0x1040, 0x1050))):
        struct.pack_into('<III', data, 0x480 + i * 12, begin, end, 0x20C0)
    data[0x4C0] = 0x01  # UNWIND_INFO version 1, flags 0: no EH/UH handler
    # mov eax, [rip+disp] at 0x1014, disp at 0x1016, target VA = base+0x2500
    data[0x214:0x21A] = b'\x8b\x05' + struct.pack('<i', (0x140002500) - (0x14000101A))
    # call 0x1010 from 0x1040
    data[0x240:0x245] = b'\xe8' + struct.pack('<i', 0x1010 - (0x1040 + 5))
    return data


def find(data):
    return locate_table(PEImage(bytes(data)), seeds=(0x10010, 0x10020), min_entries=3)


class NativeBridgeInspectorTests(unittest.TestCase):
    def test_exact_table_and_file_translation(self):
        data = image()
        table = find(data)
        self.assertEqual(table.raw, 0x420)
        self.assertEqual(len(table.entries), 4)
        self.assertEqual(PEImage(data).rva(table.raw, 96), 0x2020)
        self.assertEqual(PEImage(data).code_offset(0x140001010), 0x210)

    def test_count_must_match_complete_table(self):
        data = image()
        struct.pack_into('<Q', data, 0x418, 3)
        with self.assertRaises(InvalidImage):
            find(data)

    def test_corrupt_entry_cannot_silently_shorten_table(self):
        for index in range(4):
            with self.subTest(index=index):
                data = image()
                data[0x420 + index * 24 + 9] = 1
                with self.assertRaises(InvalidImage):
                    find(data)

    def test_native_targets_must_be_executable_and_file_backed(self):
        for target in (0x140002000, 0x140001300, 0x140009000, 0):
            with self.subTest(target=target):
                data = image()
                struct.pack_into('<Q', data, 0x430, target)
                with self.assertRaises(InvalidImage):
                    find(data)

    def test_requires_all_seed_entries(self):
        with self.assertRaises(InvalidImage):
            locate_table(PEImage(image()), seeds=(0x10010, 0x20000), min_entries=3)

    def test_mutable_cache_is_not_function_table(self):
        data = image()
        struct.pack_into('<I', data, 0x188 + 40 + 36, 0xC0000040)
        with self.assertRaises(InvalidImage):
            find(data)

    def test_duplicate_guest_starts_are_rejected(self):
        data = image()
        struct.pack_into('<I', data, 0x420 + 24, 0x10000)
        with self.assertRaises(InvalidImage):
            find(data)

    def test_refuses_ambiguous_valid_tables(self):
        data = image()
        data[0x498:0x500] = data[0x418:0x480]
        with self.assertRaises(InvalidImage):
            find(data)

    def test_rejects_truncated_wrong_arch_and_overlapping_pe(self):
        cases = [bytes(5), bytes(image()[:0x450])]
        data = image()
        struct.pack_into('<H', data, 0x84, 0x14C)
        cases.append(data)
        data = image()
        struct.pack_into('<I', data, 0x188 + 40 + 20, 0x200)
        cases.append(data)
        for data in cases:
            with self.subTest(size=len(data)):
                with self.assertRaises(InvalidImage):
                    PEImage(data)

    def test_pdata_ranges_and_unwind_flags(self):
        pe = PEImage(runtime_image())
        functions = pe.functions()
        self.assertEqual([f[:2] for f in functions],
                         [(0x140001010, 0x140001020), (0x140001020, 0x140001030),
                          (0x140001040, 0x140001050)])
        self.assertEqual(pe.function_range(0x140001016, functions), (0x140001010, 0x140001020))
        self.assertIsNone(pe.function_range(0x140001030, functions))
        self.assertEqual(pe.unwind_flags(0x140001020, functions),
                         {'rva': '0x20C0', 'version': 1, 'flags': 0,
                          'exception_handler': False, 'unwind_handler': False})

    def test_rip_relative_scan_resolves_displacement_not_absolute_value(self):
        pe = PEImage(runtime_image())
        hits = rip_refs_in_range(pe, 0x140001010, 0x140001020, 0x140002500)
        self.assertEqual(hits, [0x140001016])
        self.assertEqual(rip_refs_in_range(pe, 0x140001010, 0x140001020, 0x2500), [])

    def test_call_graph_uses_owner_function_for_interior_targets(self):
        pe = PEImage(runtime_image())
        callees, callers = call_graph(pe)
        self.assertIn(0x140001010, callees[0x140001040])
        self.assertIn(0x140001040, callers[0x140001010])


if __name__ == '__main__':
    unittest.main()
