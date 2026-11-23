#!/usr/bin/env python3
"""fable2-nx (2026-11-23): every shader of Fable 2's shader banks (data/Shaders/*.sbk), as the containers the game
creates its shaders from, named as the console's captures are (ps_/vs_<XXH3-64 of the container>.bin), for
XenosRecomp (tools/nx/build-shaders.sh): the build translates all of them, not only the ones a run has drawn.

A bank ("ShaderBankFile") is a header (the techniques' parameter names, tables of sizes and of offsets) and then its
groups, each a zlib stream (78 DA) that is flushed but not finished (no final block): the next group's header follows
it. Each group decompresses to a block holding its shaders' containers (0x102A1100 pixel, 0x102A1101 vertex: the
virtual part, then the physical microcode), which find_shader_headers (tools/renut_shader_dump.py) finds. A group is
decompressed until the stream stops being valid, the next group's header (a stored block of impossible lengths).

  python tools/fable2_shader_bank.py --game "<Fable 2 folder>" --out out/renut_shader_dump [--known DIR ...]
  python tools/fable2_shader_bank.py --bank data/Shaders/ShadersRelease.sbk --out DIR
  python tools/fable2_shader_bank.py --bank app/assets/shaders/*.sbk --out out/renut_shader_dump --prune   (the build)

--known: folders of .bin shaders (the console's captures, the old dump) to compare with: every one of them should be
in the banks (a missing one is printed). The first run, 2026-11-23: 588 shaders (451 pixel, 137 vertex) in each of
Shaders.sbk and ShadersRelease.sbk (the same ones), and every one of the 228 the console had captured among them, byte
for byte (100 of those named by fable2-nx's older hash). Game data: the output goes to out/ only, never committed or distributed.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from renut_shader_dump import find_shader_headers  # noqa: E402

MAGIC = b'ShaderBankFile'
GROUP = b'\x78\xda'


def groups(bank):
    """The decompressed groups of a bank, in order: (offset of the stream, bytes)."""
    out = []
    starts = []
    at = bank.find(GROUP)
    while at >= 0:
        starts.append(at)
        at = bank.find(GROUP, at + 1)
    i = 0
    while i < len(starts):
        start = starts[i]
        d = zlib.decompressobj(15)
        data = bytearray()
        pos = start
        end = len(bank)
        step = 4096
        failed_at = None
        while pos < end:
            piece = bank[pos:pos + step]
            try:
                data += d.decompress(piece)
                pos += len(piece)
                if d.eof:
                    break
            except zlib.error:
                if step == 1:
                    failed_at = pos
                    break
                # Again byte by byte from a copy of the state before this piece, to find where it stops being valid.
                d = zlib.decompressobj(15)
                data = bytearray(d.decompress(bank[start:pos]))
                step = 1
        if data and (failed_at is None or failed_at > start + 2):
            out.append((start, bytes(data)))
            stop = failed_at if failed_at is not None else pos
            # The next group starts at the 78 DA its first invalid bytes belong to.
            while i + 1 < len(starts) and starts[i + 1] < stop - 8:
                i += 1
        i += 1
    return out


def shaders(bank):
    """{name: container} of every shader in the bank's groups."""
    import xxhash
    found = {}
    for _, block in groups(bank):
        for h in find_shader_headers(block):
            virt, phys = struct.unpack_from('>II', block, h + 4)
            blob = block[h:h + virt + phys]
            if len(blob) != virt + phys:
                continue
            kind = 'vs' if struct.unpack_from('>I', block, h)[0] & 1 else 'ps'
            found.setdefault(f'{kind}_{xxhash.xxh3_64_hexdigest(blob)}', blob)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', help='the game folder (its data/Shaders/*.sbk)')
    ap.add_argument('--bank', nargs='*', default=[], help='bank files (instead of --game, or as well)')
    ap.add_argument('--out', help='where to write the containers (existing files are kept)')
    ap.add_argument('--known', nargs='*', default=[], help='folders of .bin shaders to compare with')
    ap.add_argument('--prune', action='store_true',
                    help='in --out, remove the .bin files whose name is not their content\'s hash (fable2-nx\'s old '
                         'captures, named by another hash: the same shaders again, under keys no draw looks up)')
    a = ap.parse_args()
    banks = list(a.bank)
    if a.game:
        d = os.path.join(a.game, 'data', 'Shaders')
        banks += [os.path.join(d, n) for n in sorted(os.listdir(d)) if n.lower().endswith('.sbk')]
    if not banks and not a.prune:
        ap.error('no bank: --game or --bank')
    everything = {}
    for path in banks:
        bank = open(path, 'rb').read()
        if not bank.startswith(MAGIC):
            print(f'{path}: not a shader bank')
            continue
        g = groups(bank)
        found = shaders(bank)
        new = sum(1 for n in found if n not in everything)
        everything.update({n: b for n, b in found.items() if n not in everything})
        ps = sum(1 for n in found if n.startswith('ps_'))
        print(f'{os.path.basename(path)}: {len(g)} groups, {len(found)} shaders ({ps} pixel, {len(found) - ps} vertex), '
              f'{new} not in the banks before it')
    ps = sum(1 for n in everything if n.startswith('ps_'))
    if banks:
        print(f'in all: {len(everything)} shaders ({ps} pixel, {len(everything) - ps} vertex)')
    for k in a.known:
        names = {n[:-4].lower() for n in os.listdir(k) if n.endswith('.bin')}
        missing = sorted(names - set(everything))
        print(f'{k}: {len(names)} shaders, {len(names) - len(missing)} in the banks, {len(missing)} not'
              + (': ' + ' '.join(missing[:20]) + (' ...' if len(missing) > 20 else '') if missing else ''))
    if a.out and a.prune and os.path.isdir(a.out):
        import xxhash
        pruned = 0
        for n in sorted(os.listdir(a.out)):
            if not n.endswith('.bin') or n[:3] not in ('ps_', 'vs_'):
                continue
            with open(os.path.join(a.out, n), 'rb') as f:
                h = xxhash.xxh3_64_hexdigest(f.read())
            if n[3:-4].lower() != h:
                os.remove(os.path.join(a.out, n))
                pruned += 1
        print(f'{a.out}: {pruned} files not named by their hash removed')
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        written = 0
        for n, b in everything.items():
            p = os.path.join(a.out, n + '.bin')
            if not os.path.exists(p):
                with open(p, 'wb') as f:
                    f.write(b)
                written += 1
        print(f'{a.out}: {written} written, {len(everything) - written} there already')


if __name__ == '__main__':
    main()
