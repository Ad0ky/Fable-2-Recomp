#!/usr/bin/env python3
"""fable2-nx (2026-11-17): the Switch CPU profile from logs/rex/cpu_samples.bin (switch_profile_stacks and
switch_profile_raw in the toml), resolved against the run's ELF (out/switch/fable2-nx.elf of the same build).

  python tools/fable2_cpu_samples.py cpu_samples.bin --elf out/switch/fable2-nx.elf            # every busy thread
  python tools/fable2_cpu_samples.py cpu_samples.bin --elf ... --thread 0x2C                   # one thread (name or priority)
  python tools/fable2_cpu_samples.py cpu_samples.bin --elf ... --reports 12-20                  # the 10 s reports 12 to 20
  python tools/fable2_cpu_samples.py cpu_samples.bin --elf ... --thread 0x2C --function FillTextures   # its lines, callers, callees

Per thread: self time (the sampled pc's function), inclusive time (every function on the sampled call path, once a
sample: the time spent in it and everything it calls) and, with --function, the source lines of its own samples, who
calls it and what it calls. Samples in a kernel wait (pc right after an svc) are counted apart. The call path is the
x29 frame chain: native code (Renut, the SDK, libraries) keeps it; recompiled guest code may not, so a guest function's
inclusive time can miss callers above it."""
import argparse
import bisect
import collections
import os
import re
import shutil
import struct
import subprocess
import sys

FRAMES = 16


def read_blocks(path):
    data = open(path, 'rb').read()
    off, blocks = 0, []
    while off + 36 <= len(data):
        magic, version, base, elapsed, freq, nthreads = struct.unpack_from('<IIQQQI', data, off)
        if magic != 0x53503246:
            sys.exit(f'{path}: bad block at {off}')
        off += 36
        threads = {}
        for _ in range(nthreads):
            slot, cpu, prio = struct.unpack_from('<Hfi', data, off)
            name = data[off + 10:off + 42].split(b'\0')[0].decode('utf-8', 'replace')
            threads[slot] = (f'{name or "?"} [0x{prio:X}]', cpu)
            off += 42
        if off + 4 > len(data):
            break
        (count,) = struct.unpack_from('<I', data, off)
        off += 4
        size = 8 * (3 + FRAMES) + 2
        samples = []
        if off + count * size > len(data):  # the last block cut short (the game closed while writing it)
            print(f'{path}: the last block is incomplete, left out')
            break
        for _ in range(count):
            vals = struct.unpack_from(f'<{3 + FRAMES}QH', data, off)
            off += size
            pc, lr, frames, tick, slot = vals[0], vals[1], vals[2:2 + FRAMES], vals[2 + FRAMES], vals[3 + FRAMES]
            samples.append((slot, pc, lr, frames, tick))
        blocks.append({'base': base, 'seconds': elapsed / freq, 'threads': threads, 'samples': samples})
    return blocks


def find_tool(names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    dkp = os.environ.get('DEVKITPRO')
    if dkp:
        for n in names:
            p = os.path.join(dkp, 'devkitA64', 'bin', n)
            if os.path.isfile(p):
                return p
    sys.exit(f'none of {names} found')


class Symbols:
    def __init__(self, elf):
        nm = find_tool(['aarch64-none-elf-nm', 'aarch64-none-elf-nm.exe', 'llvm-nm', 'nm'])
        out = subprocess.run([nm, '-n', '-C', '--defined-only', elf], capture_output=True, text=True, check=True).stdout
        self.starts, self.names = [], []
        for line in out.splitlines():
            parts = line.split(' ', 2)
            if len(parts) < 3 or parts[1] not in 'tTwW' or parts[2].startswith('$'):
                continue
            self.starts.append(int(parts[0], 16))
            self.names.append(parts[2])
        self.elf = elf

    def name(self, off):
        i = bisect.bisect_right(self.starts, off) - 1
        return self.names[i] if i >= 0 else '?'

    def lines(self, offs):
        tool = find_tool(['aarch64-none-elf-addr2line', 'aarch64-none-elf-addr2line.exe', 'llvm-addr2line', 'addr2line'])
        out = subprocess.run([tool, '-e', self.elf] + [hex(o) for o in offs], capture_output=True, text=True).stdout
        return [l.split('/')[-1].split('\\')[-1] for l in out.splitlines()]


def short(name):
    name = re.sub(r'\(anonymous namespace\)::', '', name)
    depth, cut = 0, len(name)
    for i, ch in enumerate(name):  # drop the argument list (the first top-level '(' after a name)
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif ch == '(' and depth == 0 and i > 0:
            cut = i
            break
    return name[:cut]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bin')
    ap.add_argument('--elf', required=True)
    ap.add_argument('--thread', default=None, help='a thread name part, or its priority as [0x2C] (the render thread)')
    ap.add_argument('--reports', default=None, help='a range of 10 s reports, e.g. 12-20 (1 = the first)')
    ap.add_argument('--min-cpu', type=float, default=10.0, help='threads under this %% of a core are left out')
    ap.add_argument('--top', type=int, default=30)
    ap.add_argument('--function', default=None, help='a function name part: its lines, callers and callees')
    args = ap.parse_args()

    blocks = read_blocks(args.bin)
    print(f'{len(blocks)} reports, {sum(len(b["samples"]) for b in blocks)} samples')
    for i, b in enumerate(blocks, 1):
        top = sorted(b['threads'].values(), key=lambda t: -t[1])[:5]
        print(f'  report {i:3d}: ' + ', '.join(f'{n[:16]} {c:.0f}%' for n, c in top))
    lo, hi = 1, len(blocks)
    if args.reports:
        a, _, b = args.reports.partition('-')
        lo, hi = int(a), int(b or a)
    chosen = blocks[lo - 1:hi]
    sym = Symbols(args.elf)

    per_thread = collections.defaultdict(list)
    cpu = collections.defaultdict(list)
    for b in chosen:
        base = b['base']
        for slot, (name, c) in b['threads'].items():
            cpu[(slot, name)].append(c)
        for slot, pc, lr, frames, tick in b['samples']:
            name = b['threads'].get(slot, ('?', 0))[0]
            per_thread[(slot, name)].append((pc - base, lr - base, [f - base for f in frames if f]))

    names_memo = {}

    def fname(off):
        if off not in names_memo:
            names_memo[off] = short(sym.name(off))
        return names_memo[off]

    for key in sorted(per_thread, key=lambda k: -sum(cpu[k]) / max(1, len(cpu[k]))):
        mean_cpu = sum(cpu[key]) / max(1, len(cpu[key]))
        if args.thread:
            if args.thread.lower() not in key[1].lower():
                continue
        elif mean_cpu < args.min_cpu:
            continue
        samples = per_thread[key]
        selfc, incl = collections.Counter(), collections.Counter()
        for pc, lr, frames in samples:
            f0 = fname(pc)
            selfc[f0] += 1
            seen = {f0}
            for fr in frames:
                seen.add(fname(fr - 4))
            for f in seen:
                incl[f] += 1
        n = len(samples)
        print(f'\n== thread "{key[1]}" (slot {key[0]}): {mean_cpu:.1f}% of a core, {n} samples')
        print('   self                                                      | inclusive')
        sl, il = selfc.most_common(args.top), incl.most_common(args.top)
        for i in range(max(len(sl), len(il))):
            a = f'{100 * sl[i][1] / n:5.1f}% {sl[i][0][:52]:52s}' if i < len(sl) else ' ' * 59
            b = f'{100 * il[i][1] / n:5.1f}% {il[i][0][:70]}' if i < len(il) else ''
            print(f'   {a} | {b}')
        if args.function:
            mine = [(pc, lr, fr) for pc, lr, fr in samples if args.function in fname(pc)]
            under = [(pc, lr, fr) for pc, lr, fr in samples
                     if args.function in fname(pc) or any(args.function in fname(f - 4) for f in fr)]
            print(f'\n   {args.function}: {len(mine)} self samples, {len(under)} inclusive')
            pcs = collections.Counter(pc for pc, _, _ in mine)
            addrs = list(pcs)
            lines = collections.Counter()
            for a, l in zip(addrs, sym.lines(addrs)):
                lines[l] += pcs[a]
            print('   its lines (self):')
            for l, c in lines.most_common(args.top):
                print(f'     {100 * c / max(1, len(mine)):5.1f}%  {l}')
            callers, callees = collections.Counter(), collections.Counter()
            for pc, lr, fr in under:
                chain = [fname(pc)] + [fname(f - 4) for f in fr]
                for i, f in enumerate(chain):
                    if args.function in f:
                        if i + 1 < len(chain):
                            callers[chain[i + 1]] += 1
                        if i > 0:
                            callees[chain[i - 1]] += 1
                        break
            print('   called from:')
            for f, c in callers.most_common(10):
                print(f'     {100 * c / len(under):5.1f}%  {f[:100]}')
            print('   time in what it calls (inclusive of theirs):')
            for f, c in callees.most_common(args.top):
                print(f'     {100 * c / len(under):5.1f}%  {f[:100]}')


if __name__ == '__main__':
    main()
