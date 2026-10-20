"""Inspect a runner PE as bytes. Never loads an image, executes code or opens a process.

Locating a function table does NOT validate the register ABI or authorize native calls.
For the known build it also checks the static contract around the input hook (callback mutex not reached by the
direct XInput HLE path, unwind records, RIP references). That is evidence, not an executor or live validation.
Usage: python tools/inspect_native_bridge.py <runner.exe>
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


class InvalidImage(ValueError):
    pass


@dataclass(frozen=True)
class Section:
    rva: int
    virtual_size: int
    raw: int
    size: int
    flags: int


class PEImage:
    def __init__(self, data: bytes):
        self.data = data
        if self.take(0, 2) != b'MZ':
            raise InvalidImage('missing MZ header')
        pe = self.unpack('<I', 0x3C)[0]
        if self.take(pe, 4) != b'PE\0\0':
            raise InvalidImage('missing PE header')
        machine, count, self.timestamp = self.unpack('<HHI', pe + 4)
        optional_size = self.unpack('<H', pe + 20)[0]
        opt = pe + 24
        if machine != 0x8664 or not 1 <= count <= 96 or optional_size < 112:
            raise InvalidImage('expected bounded x64 PE32+ image')
        self.take(opt, optional_size)
        if self.unpack('<H', opt)[0] != 0x20B:
            raise InvalidImage('expected PE32+ optional header')
        self.base = self.unpack('<Q', opt + 24)[0]
        self.image_size = self.unpack('<I', opt + 56)[0]
        # DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = index 3, at optional header + 112.
        self.exception_rva, self.exception_size = self.unpack('<II', opt + 112 + 3 * 8)
        self.sections: list[Section] = []
        for i in range(count):
            off = opt + optional_size + i * 40
            self.take(off, 40)
            virtual_size, rva, raw_size, raw = self.unpack('<IIII', off + 8)
            flags = self.unpack('<I', off + 36)[0]
            self.take(raw, raw_size)
            if rva + max(virtual_size, raw_size) > self.image_size:
                raise InvalidImage('section extends beyond image')
            section = Section(rva, virtual_size, raw, raw_size, flags)
            for old in self.sections:
                if raw_size and old.size and raw < old.raw + old.size and old.raw < raw + raw_size:
                    raise InvalidImage('overlapping raw sections')
                if rva < old.rva + max(old.virtual_size, old.size) and old.rva < rva + max(virtual_size, raw_size):
                    raise InvalidImage('overlapping virtual sections')
            self.sections.append(section)

    def take(self, off: int, size: int) -> bytes:
        if off < 0 or size < 0 or off + size > len(self.data):
            raise InvalidImage('truncated image')
        return self.data[off:off + size]

    def unpack(self, fmt: str, off: int):
        return struct.unpack(fmt, self.take(off, struct.calcsize(fmt)))

    def raw_section(self, off: int, size: int) -> Section | None:
        return next((s for s in self.sections if s.raw <= off and off + size <= s.raw + s.size), None)

    def rva(self, off: int, size: int = 1) -> int:
        section = self.raw_section(off, size)
        if section is None:
            raise InvalidImage('file offset is not section-backed')
        return section.rva + off - section.raw

    def code_offset(self, va: int) -> int | None:
        rva = va - self.base
        section = next((s for s in self.sections if s.rva <= rva < s.rva + s.size), None)
        if section is None or not section.flags & 0x20000000:
            return None
        return section.raw + rva - section.rva

    def raw_offset_rva(self, rva: int, size: int = 1) -> int | None:
        """File offset of a file-backed RVA, or None for virtual-only tails."""
        section = next((s for s in self.sections if s.rva <= rva < s.rva + s.size), None)
        if section is None or rva + size > section.rva + section.size:
            return None
        return section.raw + rva - section.rva

    def functions(self) -> list[tuple[int, int, int]]:
        """(start, end, unwind_rva) from .pdata; empty when the directory is absent."""
        if not self.exception_rva or self.exception_size % 12:
            return []
        off = self.raw_offset_rva(self.exception_rva, self.exception_size)
        if off is None:
            return []
        out = []
        for i in range(self.exception_size // 12):
            begin, end, unwind = self.unpack('<III', off + i * 12)
            if begin and end > begin:
                out.append((self.base + begin, self.base + end, unwind))
        return out

    def function_range(self, va: int, functions=None) -> tuple[int, int] | None:
        table = self.functions() if functions is None else functions
        starts = [f[0] for f in table]
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and table[i][0] <= va < table[i][1]:
            return table[i][0], table[i][1]
        return None

    def unwind_flags(self, va: int, functions=None) -> dict | None:
        table = self.functions() if functions is None else functions
        row = next((f for f in table if f[0] == va), None)
        if row is None:
            return None
        off = self.raw_offset_rva(row[2], 1)
        if off is None:
            return None
        byte = self.take(off, 1)[0]
        flags = byte >> 3
        return {'rva': f'0x{row[2]:X}', 'version': byte & 7, 'flags': flags,
                'exception_handler': bool(flags & 1), 'unwind_handler': bool(flags & 2)}


@dataclass(frozen=True)
class Entry:
    guest: int
    end: int
    mid_entry: int
    native: int


@dataclass(frozen=True)
class FunctionTable:
    raw: int
    entries: tuple[Entry, ...]


def read_entry(pe: PEImage, off: int) -> Entry | None:
    section = pe.raw_section(off, 24)
    # The immutable function table differs from the mutable per-call-site caches.
    if section is None or not section.flags & 0x40000000 or section.flags & 0xA0000000:
        return None
    start, end, mid, reserved, native = pe.unpack('<IIB7sQ', off)
    if not (0x10000 <= start < end <= 0x1000000 and mid in (0, 1) and reserved == bytes(7)):
        return None
    if pe.code_offset(native) is None:
        return None
    return Entry(start, end, mid, native)


def locate_table(pe: PEImage, seeds=(0x2BD910, 0x2CB270, 0x2CBC40), min_entries=1024) -> FunctionTable:
    if not seeds or min_entries < len(set(seeds)):
        raise InvalidImage('invalid discovery parameters')
    candidates: dict[int, FunctionTable] = {}
    needle = struct.pack('<I', seeds[0])
    for section in pe.sections:
        if not section.flags & 0x40000000 or section.flags & 0xA0000000:
            continue
        off = section.raw
        while (off := pe.data.find(needle, off, section.raw + section.size)) != -1:
            hit = off
            off += 1
            current = read_entry(pe, hit)
            if current is None:
                continue
            lo, hi = hit, hit
            first, last = current, current
            while (previous := read_entry(pe, lo - 24)) is not None and previous.guest < first.guest:
                lo -= 24
                first = previous
            while (following := read_entry(pe, hi + 24)) is not None and following.guest > last.guest:
                hi += 24
                last = following
            count = (hi - lo) // 24 + 1
            if not min_entries <= count <= 65536 or lo - 8 < section.raw:
                continue
            if pe.unpack('<Q', lo - 8)[0] != count:
                continue
            entries = tuple(read_entry(pe, i) for i in range(lo, hi + 1, 24))
            if not set(seeds).issubset(e.guest for e in entries):
                continue
            candidates[lo] = FunctionTable(lo, entries)
    if len(candidates) != 1:
        raise InvalidImage(f'expected exactly one validated function table, found {len(candidates)}')
    return next(iter(candidates.values()))


TARGETS = {
    'allocate': 0x1B3B90, 'free': 0x1B3C00, 'get_player': 0x2BD910,
    'factory_ctor': 0x2CB270, 'factory_dtor': 0x2CB2C0, 'build_human': 0x2CBC40,
    'vehicle_dtor': 0x100D70, 'vehicle_update': 0xFFD90,
}


KNOWN_CONTRACTS = {
    '51354a042caf24ffc73a989a2beb93a867401b6f2f4b61c0a5d0d2565696be8d': {
        'timestamp': 0x6AD54432, 'table_rva': 0x2416820,
        'input_return_rva': 0x306984, 'input_iat_rva': 0x24146D8,
        'code': {
            'preserving_call': (0x3AD270, '41 57 41 56 56 57 53 48 81 ec 80 01 00 00 31 c0'),
            'raw_call': (0x39EFA0, '41 56 56 57 55 53 48 83 ec 30 48 89 d6 45 85 c0'),
            'context_callback': (0x31B690, '56 57 53 48 83 ec 70 48 bf ff ff ff ff ff ff ff'),
        },
        'globals': {
            'guest_eax': 0x28E7B20, 'guest_ecx': 0x28E7B28, 'guest_esp': 0x28E7B38,
            'guest_base': 0x2C667B8, 'current_guest_record': 0x2C6E160,
            'optional_call_callback': 0x2C127E8,
        },
    },
}

# Static reachability around the input hook. Addresses are RVAs of the same build as KNOWN_CONTRACTS.
# This does not authorize execution: it answers one concrete question (is the callback mutex already held by
# the XInput HLE path?) and leaves reentrancy/lifecycle unproved.
RUNTIME_CONTRACTS = {
    '51354a042caf24ffc73a989a2beb93a867401b6f2f4b61c0a5d0d2565696be8d': {
        'callback_mutex_va': 0x1427E9EE8,
        'mutex_acquire_rva': 0x3105B0,
        'callback_rva': 0x31B690,
        'input_hle_rva': 0x300D00,
        'input_poll_rva': 0x306BB0,
        'input_call_rva': 0x3068D0,
        'input_return_rva': 0x306984,
        'expected_mutex_refs': (0x31067F, 0x3106C5, 0x3106DC),
        'code': {
            'input_hle': (0x300D00, '41 57 41 56 41 55 41 54 56 57 55 53 48 83 ec 78'),
            'input_poll': (0x306BB0, '56 57 53 48 81 ec 90 00 00 00 48 89 d6 83 f9 04'),
            'input_entry': (0x3068D0, '56 57 53 48 83 ec 30 89 c8 83 f9 03'),
        },
        'ancestor_depth': 6,
        'ancestor_limit': 64,
        'unwind_rvas': (0x3AD270, 0x39EFA0),
    },
}


def rip_refs_in_range(pe: PEImage, start: int, end: int, target: int) -> list[int]:
    """RIP-relative disp32 sites in [start,end) that resolve to target. Byte scan, no disassembly."""
    off = pe.raw_offset_rva(start - pe.base, max(0, end - start))
    if off is None or end <= start:
        return []
    raw = pe.take(off, end - start)
    hits = []
    for i in range(max(0, len(raw) - 3)):
        if start + i + 4 + struct.unpack_from('<i', raw, i)[0] == target:
            hits.append(start + i)
    return hits


def call_graph(pe: PEImage, functions=None):
    """Direct call edges by function start: (callees, callers). Interior targets resolve to their owner."""
    table = pe.functions() if functions is None else functions
    starts = [f[0] for f in table]

    def owner(va: int) -> int | None:
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and table[i][0] <= va < table[i][1]:
            return table[i][0]
        return None

    callees: dict[int, set[int]] = {}
    callers: dict[int, set[int]] = {}
    for section in pe.sections:
        if not section.flags & 0x20000000:
            continue
        raw = pe.take(section.raw, section.size)
        for hit in re.finditer(b'\xe8', raw):
            i = hit.start()
            if i + 5 > len(raw):
                continue
            site = pe.base + section.rva + i
            source = owner(site)
            target = owner(site + 5 + struct.unpack_from('<i', raw, i + 1)[0])
            if source is not None and target is not None:
                callees.setdefault(source, set()).add(target)
                callers.setdefault(target, set()).add(source)
    return callees, callers


def inspect_runtime(pe: PEImage, digest: str, profiles=None) -> dict | None:
    """Static context report. `execution_safe` is always false; no callable or live state is produced."""
    profile = (RUNTIME_CONTRACTS if profiles is None else profiles).get(digest)
    if profile is None:
        return None
    functions = pe.functions()
    if not functions:
        raise InvalidImage('known runtime profile needs .pdata function ranges')
    ranges = {}
    for name, (rva, prefix) in profile['code'].items():
        off = pe.code_offset(pe.base + rva)
        expected = bytes.fromhex(prefix)
        if off is None or pe.take(off, len(expected)) != expected:
            raise InvalidImage(f'known runtime code mismatch: {name}')
        found = pe.function_range(pe.base + rva, functions)
        if found is None or found[0] != pe.base + rva:
            raise InvalidImage(f'known runtime function boundary mismatch: {name}')
        ranges[name] = found
    acquire = pe.function_range(pe.base + profile['mutex_acquire_rva'], functions)
    callback = pe.function_range(pe.base + profile['callback_rva'], functions)
    if acquire is None or acquire[0] != pe.base + profile['mutex_acquire_rva']:
        raise InvalidImage('callback mutex acquire function boundary mismatch')
    if callback is None or callback[0] != pe.base + profile['callback_rva']:
        raise InvalidImage('context callback function boundary mismatch')
    mutex = profile['callback_mutex_va']
    mutex_refs = tuple(r - pe.base for r in rip_refs_in_range(pe, *acquire, mutex))
    if mutex_refs != tuple(profile['expected_mutex_refs']):
        raise InvalidImage('callback mutex reference sites changed')
    input_ranges = [ranges['input_hle'], ranges['input_poll'], pe.function_range(pe.base + profile['input_call_rva'], functions)]
    if any(r is None for r in input_ranges):
        raise InvalidImage('input call function boundary missing')
    call_fn = input_ranges[2]
    if not call_fn[0] <= pe.base + profile['input_return_rva'] < call_fn[1]:
        raise InvalidImage('input return address is outside the profiled call function')
    input_refs = {f'0x{r[0] - pe.base:X}': len(rip_refs_in_range(pe, *r, mutex)) for r in input_ranges}
    if any(input_refs.values()):
        raise InvalidImage('callback mutex is referenced by an input function')
    callees, callers = call_graph(pe, functions)
    if pe.base + profile['mutex_acquire_rva'] not in callees.get(callback[0], set()):
        raise InvalidImage('context callback does not call the mutex acquire function')
    # Climb only direct callers of the input HLE handler. This is the path that reaches the XInput IAT call.
    ancestors: set[int] = set()
    front = [(pe.base + profile['input_hle_rva'], 0)]
    while front and len(ancestors) < profile['ancestor_limit']:
        current, depth = front.pop()
        if depth >= profile['ancestor_depth']:
            continue
        for parent in callers.get(current, set()):
            if parent not in ancestors:
                ancestors.add(parent)
                front.append((parent, depth + 1))
    hits = []
    for start in ancestors:
        found = pe.function_range(start, functions)
        if found is None:
            continue
        if rip_refs_in_range(pe, *found, mutex) or pe.base + profile['mutex_acquire_rva'] in callees.get(start, set()):
            hits.append(f'0x{start - pe.base:X}')
    if hits:
        raise InvalidImage(f'callback mutex reachable from the input call path: {hits}')
    unwind = {}
    for rva in profile['unwind_rvas']:
        info = pe.unwind_flags(pe.base + rva, functions)
        if info is None:
            raise InvalidImage(f'missing unwind record for 0x{rva:X}')
        if info['exception_handler'] or info['unwind_handler']:
            raise InvalidImage(f'unexpected unwind handler at 0x{rva:X}')
        unwind[f'0x{rva:X}'] = info
    return {
        'build_timestamp': f'0x{pe.timestamp:08X}',
        'callback_mutex_va': f'0x{mutex:X}',
        'mutex_acquire_rva': f"0x{profile['mutex_acquire_rva']:X}",
        'mutex_reference_rvas': [f'0x{r:X}' for r in mutex_refs],
        'mutex_reference_sites_are_in_acquire_function': True,
        'mutex_check_scope': 'acquire function, input HLE/poll functions and their direct-call ancestors',
        'input_path': {
            'hle_handler_rva': f"0x{profile['input_hle_rva']:X}",
            'poll_function_rva': f"0x{profile['input_poll_rva']:X}",
            'input_call_function_rva': f"0x{profile['input_call_rva']:X}",
            'input_call_return_rva': f"0x{profile['input_return_rva']:X}",
            'mutex_references_in_input_functions': input_refs,
            'direct_ancestor_functions_checked': len(ancestors),
            'callback_mutex_reachable_from_ancestors': False,
            'callback_mutex_free_on_direct_input_path': True,
        },
        'unwind': unwind,
        'execution_safe': False,
        'resolved_by_this_check': ['callback mutex is not held by the direct XInput HLE call chain'],
        'still_unproved': ['callback reentrancy semantics while the HLE call is active',
                           'indirect calls in the ancestor path', 'non-normal unwind/exception recovery',
                           'guest stack scratch depth for factory/build', 'persistent CarCfg ownership',
                           'vehicle replacement lifecycle, buckets and skill profile',
                           'live behavior on an authorized isolated run'],
    }


def inspect_contract(pe: PEImage, table: FunctionTable, digest: str, contracts=None) -> dict | None:
    """Match static signatures only. Never returns an executable callable or validates live state."""
    profile = (KNOWN_CONTRACTS if contracts is None else contracts).get(digest)
    if profile is None:
        return None
    if pe.timestamp != profile['timestamp'] or pe.rva(table.raw) != profile['table_rva']:
        raise InvalidImage('known contract metadata mismatch')
    code = {}
    for name, (rva, prefix) in profile['code'].items():
        off = pe.code_offset(pe.base + rva)
        expected = bytes.fromhex(prefix)
        if off is None or pe.raw_section(off, len(expected)) is None or pe.take(off, len(expected)) != expected:
            raise InvalidImage(f'known contract code mismatch: {name}')
        code[name] = f'0x{rva:X}'
    # Recheck uniqueness of the actual import call site, not a guessed controller index.
    sites = []
    for section in pe.sections:
        if not section.flags & 0x20000000:
            continue
        off = section.raw
        while (off := pe.data.find(b'\xff\x15', off, section.raw + section.size)) != -1:
            hit = off
            off += 1
            if hit + 6 > section.raw + section.size:
                continue
            returned = pe.rva(hit, 6) + 6
            if returned + pe.unpack('<i', hit + 2)[0] == profile['input_iat_rva']:
                sites.append(returned)
    if sites != [profile['input_return_rva']]:
        raise InvalidImage('known contract input call is missing or ambiguous')
    runtime = inspect_runtime(pe, digest)
    return {
        'static_signatures_match': True, 'code_rvas': code,
        'input_return_rva': f"0x{profile['input_return_rva']:X}",
        'global_rvas': {name: f'0x{rva:X}' for name, rva in profile['globals'].items()},
        'contract': 'preserving_call(guestVA, host_u32_args, count) returns host bool; guest EAX restored; guest ECX supplies this',
        'runtime_context': runtime,
        'execution_safe': False,
        'unproved': ['yield and reentrancy while a native HLE call is active',
                     'non-normal unwind/exception recovery',
                     'guest stack scratch depth for the factory/build',
                     'persistent CarCfg ownership', 'vehicle replacement lifecycle/buckets',
                     'skill-profile compatibility', 'live behavior on an authorized isolated run'],
    }


def inspect(data: bytes) -> dict:
    pe = PEImage(data)
    table = locate_table(pe)
    digest = hashlib.sha256(data).hexdigest()
    by_guest = {e.guest: e for e in table.entries}
    targets = {}
    for name, guest in TARGETS.items():
        if guest not in by_guest:
            raise InvalidImage(f'missing exact function entry for {name}')
        entry = by_guest[guest]
        off = pe.code_offset(entry.native)
        targets[name] = {'guest': f'0x{guest:08X}', 'end': f'0x{entry.end:08X}',
                         'native_rva': f'0x{entry.native - pe.base:X}',
                         'prefix': pe.take(off, 16).hex(' ')}
    return {
        'sha256': digest, 'timestamp': f'0x{pe.timestamp:08X}',
        'image_base': f'0x{pe.base:X}', 'image_size': f'0x{pe.image_size:X}',
        'function_table_rva': f'0x{pe.rva(table.raw, len(table.entries) * 24):X}',
        'function_count': len(table.entries), 'targets': targets,
        'known_static_contract': inspect_contract(pe, table, digest),
        'abi_validated': False, 'native_calls_enabled': False,
        'limitation': 'Static table/context inspection only. Targets may be dispatch thunks; register/fiber/IO ABI, '
                      'reentrancy and swap lifecycle are unproved.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runner', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.runner.read_bytes()), indent=2))
    except (OSError, InvalidImage) as error:
        parser.exit(1, f'Inspection refused: {error}\n')


if __name__ == '__main__':
    main()
