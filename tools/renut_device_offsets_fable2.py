#!/usr/bin/env python3
"""fable2-nx (2026-11-12): moves Renut's device offsets and guest data addresses from Beautiful Katamari's layout (XDK
2.0.5632, as the base has them) to Fable 2's (XDK 2.0.7645, N&B's base-game layout).

Every site tools/renut_device_offsets_katamari.py rewrote carries `katamari-nx: TU3->KAT`; the TU3 value (N&B title
update 3, XDK 2.0.7978) is mapped to Fable 2 by renut-nx's title-update-3.md zones read backwards:
  TU3 10944..11827 -> -4, 11828..13199 -> -8, 13200 and up -> the same (below 10944 all three layouts agree).
The data addresses are Fable 2's (fable2-nx docs/nx/renut-import.md, each found by paired functions).
Lines rewritten here carry `fable2-nx:`; run once.

  python -P tools/renut_device_offsets_fable2.py [--dry-run]
"""
import argparse, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'app', 'src')
ANN = re.compile(r'katamari-nx: ((?:(?:0x[0-9A-Fa-f]+|\d+)->(?:0x[0-9A-Fa-f]+|\d+)(?:, )?)+)')
PAIR = re.compile(r'(0x[0-9A-Fa-f]+|\d+)->(0x[0-9A-Fa-f]+|\d+)')

DATA = {
    0x82DA3478: 0x83321A70,  # the XDK build qword (Fable 2: 000200001DDD4001)
    0x820007F8: 0x820009AC,  # VdGlobalDevice's import record
    0x82161B30: 0x820FA890,  # D3DCAPS9
    0x82001744: 0x82001AA8,  # the full viewport constant
    0x82DA3778: 0x83336BFC,  # XPhysicalAlloc's no-large-pages flag
    0x82005200: 0x82006F28,  # the format table
    0x82E108A0: 0x83491F80,  # the HiZ owner
    0x82D8A948: 0x83315D30,  # the render state table
    0x82D8AE28: 0x833161F0,  # the sampler state table
}


def fable(tu3):
    if tu3 >= 0x82000000:
        return DATA[tu3]
    if 10944 <= tu3 <= 11827:
        return tu3 - 4
    if 11828 <= tu3 <= 13199:
        return tu3 - 8
    return tu3


def fmt(v, like):
    if like.lower().startswith('0x'):
        return ('0x%08X' % v) if v >= 0x82000000 else ('0x%X' % v)
    return str(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    report, problems = [], []
    for dp, _, fs in os.walk(SRC):
        for f in fs:
            if not f.endswith(('.cpp', '.h', '.inc')):
                continue
            path = os.path.join(dp, f)
            raw = open(path, encoding='utf-8', newline='').read()
            if 'katamari-nx: ' not in raw:
                continue
            nl = '\r\n' if '\r\n' in raw else '\n'
            lines = raw.split(nl)
            changed = False
            for i, line in enumerate(lines):
                m = ANN.search(line)
                if not m:
                    continue
                cut = line.find('//')
                head, tail = line[:cut], line[cut:]
                pos = 0
                new_pairs = []
                for x, y in PAIR.findall(m.group(1)):
                    xv = int(x, 0)
                    fv = fable(xv)
                    fs_ = fmt(fv, y)
                    tok = re.compile(r'(?<![0-9A-Za-z_.])' + re.escape(y) + r'(?![0-9A-Za-z_.])', re.I)
                    mm = tok.search(head, pos)
                    if not mm:
                        # a repeat pair (the same literal twice) already handled, or not in code
                        mm2 = tok.search(head)
                        if not mm2 and fs_.lower() not in head.lower():
                            problems.append(f'{path}:{i+1}: {y} not in code: {line.strip()[:140]}')
                        new_pairs.append(f'{x}->{fs_}')
                        continue
                    head = head[:mm.start()] + fs_ + head[mm.end():]
                    pos = mm.start() + len(fs_)
                    new_pairs.append(f'{x}->{fs_}')
                tail = tail.replace(m.group(0), 'fable2-nx (N&B TU3->Fable 2): ' + ', '.join(new_pairs))
                new = head + tail
                if new != line:
                    lines[i] = new
                    changed = True
                    report.append(f'{os.path.relpath(path, SRC)}:{i+1}: {", ".join(new_pairs)}')
            if changed and not a.dry_run:
                open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    print(f'{len(report)} lines rewritten')
    for p in problems:
        print('  PROBLEM ' + p)
    os.makedirs(os.path.join(ROOT, 'out'), exist_ok=True)
    open(os.path.join(ROOT, 'out', 'fable2_offsets_report.txt'), 'w').write('\n'.join(report) + '\n')


if __name__ == '__main__':
    main()
