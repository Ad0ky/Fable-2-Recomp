#!/usr/bin/env python3
"""fable2-nx (2026-11-12): binds the base's Renut hooks (Beautiful Katamari's addresses) to Fable 2's functions.

Each hook line names its D3D function after the handler (`// D3DDevice_SetRenderState_ZEnable`). Sources, in order:
  1. fable2-nx's own hook table (renut/src/renut_engine/gpu/d3d/hooks/*.cpp of the Fable2NX branch, run on the console):
     its binding of that name, hooked or not, wins;
  2. fable2-nx docs/nx/d3d-hookmap-fable2.tsv (paired-function signatures): when the score is >= 0.75 and leads the
     second by >= 0.05;
  3. otherwise left unhooked (BANJO_RENUT_UNHOOKED_*), and reported.
The function symbol is the name Fable 2's code generation gives it (the manifest's names: ProcessAndProcess..._<va>).

  python3 tools/renut_hook_remap_fable2.py <fable2-nx root> [--dry-run]
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu', 'd3d', 'hooks')
LINE = re.compile(r'^(\s*)BANJO_RENUT_(UNHOOKED_)?(HOOK|HOOK_SHARED|HOOK_RAW|HOOK_RAW_SHARED)\((\w+)([,)])(.*)$')


def name_of(rest):
    m = re.search(r'//\s*([A-Za-z_][A-Za-z0-9_]*)', rest)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fable2nx')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    ours = {}
    fh = os.path.join(a.fable2nx, 'renut', 'src', 'renut_engine', 'gpu', 'd3d', 'hooks')
    for f in sorted(os.listdir(fh)):
        if not f.endswith('.cpp'):
            continue
        for line in open(os.path.join(fh, f), encoding='utf-8'):
            m = LINE.match(line)
            if not m:
                continue
            n = name_of(m.group(6))
            if m.group(5) == ')':  # a RAW body: its name is on the next lines, keyed by address below
                continue
            if n:
                ours.setdefault(n, (m.group(4), bool(m.group(2)), m.group(3)))
    tsv = {}
    for line in open(os.path.join(a.fable2nx, 'docs', 'nx', 'd3d-hookmap-fable2.tsv'), encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        if p[0] == 'name' or len(p) < 6:
            continue
        try:
            tsv[p[0]] = (p[3], float(p[4]), float(p[5]))
        except ValueError:
            pass
    report = []
    used = {}
    counts = {'fable2-nx': 0, 'tsv': 0, 'unhooked': 0, 'raw': 0}
    for f in sorted(os.listdir(HOOKS)):
        if not f.endswith('.cpp'):
            continue
        path = os.path.join(HOOKS, f)
        lines = open(path, encoding='utf-8').read().split('\n')
        for i, line in enumerate(lines):
            m = LINE.match(line)
            if not m:
                continue
            ind, unh, kind, sym, sep, rest = m.groups()
            if sym == 'subroutine' or line.rstrip().endswith('\\'):
                continue  # a macro's body
            if sep == ')' and unh:
                continue  # already left unhooked
            if sep == ')' and sym.startswith('sub_824'):
                # Beautiful Katamari's own RAW hooks (XDK 2.0.5632's full-form BeginTiling / EndTiling): not Fable 2's
                lines[i] = f'{ind}BANJO_RENUT_UNHOOKED_{kind}({sym}){rest}  // fable2-nx: Katamari\'s function, not hooked'
                counts['unhooked'] += 1
                report.append(f'RAW {f}:{i+1} {sym}: Katamari\'s, unhooked')
                continue
            if sep == ')':
                counts['raw'] += 1
                report.append(f'RAW {f}:{i+1} {sym}: by hand')
                continue
            n = name_of(rest)
            rest = re.sub(r'\s*//\s*(katamari-nx|fable2-nx):.*$', '', rest)
            target, why = None, ''
            if n in ours:
                target, off, kind = ours[n]  # fable2-nx's kind: SHARED where its game layer hooks the function too
                why = 'fable2-nx\'s table' + (' (unhooked there)' if off else '')
                if off:
                    target = None
                counts['fable2-nx'] += 1
            elif n in tsv and tsv[n][1] >= 0.75 and tsv[n][2] >= 0.05:
                target = tsv[n][0]
                why = f'signature {tsv[n][1]:.2f} (lead {tsv[n][2]:.2f})'
                counts['tsv'] += 1
            else:
                why = 'no Fable 2 counterpart' + (f' (best {tsv[n][0]} {tsv[n][1]:.2f}, lead {tsv[n][2]:.2f})' if n in tsv else '')
            if target and target in used and used[target] != n:
                why = f'{target} already bound to {used[target]}'
                target = None
            if target:
                used[target] = n
                new = f'{ind}BANJO_RENUT_{kind}({target}{sep}{rest}  // fable2-nx: {why}'
            else:
                counts['unhooked'] += 1
                old = sym if sym.startswith('sub_') else sym
                new = f'{ind}BANJO_RENUT_UNHOOKED_{kind}({old}{sep}{rest}  // fable2-nx: not hooked: {why}'
            lines[i] = new
            report.append(f'{f}:{i+1} {n}: {target or "-"}: {why}')
        if not a.dry_run:
            open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    os.makedirs(os.path.join(ROOT, 'out'), exist_ok=True)
    open(os.path.join(ROOT, 'out', 'fable2_hooks_report.txt'), 'w').write('\n'.join(report) + '\n')
    print(counts)


if __name__ == '__main__':
    main()
