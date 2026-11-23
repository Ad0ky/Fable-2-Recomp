#!/usr/bin/env python3
"""fable2-nx (2026-11-17, gameplay at the wrong speed): Fable 2's main loop (sub_82355460) advances the game in fixed
steps of 1/30 s (the rate is the double at 0x833194F0 in .rdata: 30.0). Each pass runs the steps that are due, but
when more than 2 are due it runs one and starts the clock again from now (`cmpwi cr6,r30,2` / `ble cr6,0x823556b4`):
the time of the others is thrown away. On the Xbox 360 the loop makes 30 passes a second and never comes near it;
here gameplay renders at 12-16 frames a second, the loop makes as many passes, more than 2 steps are often due, and
the game runs slow (animations, cutscenes, timers).

This step makes the limit the value of fable2_tick_catchup_max() (src/fable2/hotfunc/frame/TickCatchup.cpp, the
setting fable2_tick_catchup: 2 is the game's own). It finds the compare by its guest comment and the branch after it,
so every copy of the loop's code (hot copies, inlined copies) gets it. Idempotent; --restore puts the 2 back.
  python3 tools/fable2_tick_catchup.py --gen app/generated [--restore]
"""
import argparse
import glob
import os
import re
import sys

DECL = 'extern "C" int32_t fable2_tick_catchup_max();  // fable2-nx: tools/fable2_tick_catchup.py\n'
PATTERN = re.compile(r'(\t*// cmpwi cr6,r30,2\n\t*)cr6\.compare<int32_t>\(r30\.s32, 2, xer\);(\n\t*// ble cr6,0x823556b4\n)')
PATCHED = re.compile(r'cr6\.compare<int32_t>\(r30\.s32, fable2_tick_catchup_max\(\), xer\);(\n\t*// ble cr6,0x823556b4\n)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', required=True)
    ap.add_argument('--restore', action='store_true')
    a = ap.parse_args()
    files = glob.glob(os.path.join(a.gen, '**', '*.cpp'), recursive=True)
    if not files:
        print('fable2_tick_catchup: no generated files', file=sys.stderr)
        return 1
    total = 0
    for path in files:
        text = open(path, encoding='utf-8').read()
        if '0x823556b4' not in text:
            continue
        if a.restore:
            new, n = PATCHED.subn(r'cr6.compare<int32_t>(r30.s32, 2, xer);\1', text)
            new = new.replace(DECL, '')
        else:
            new, n = PATTERN.subn(r'\1cr6.compare<int32_t>(r30.s32, fable2_tick_catchup_max(), xer);\2', text)
            n += len(PATCHED.findall(text))  # already patched
            if n and DECL not in new:
                first = new.find('\n') + 1  # after the first #include
                new = new[:first] + DECL + new[first:]
        total += n
        if new != text:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(new)
    print(f'fable2_tick_catchup: {total} site(s) {"restored" if a.restore else "patched"}')
    if not a.restore and total == 0:
        print('fable2_tick_catchup: WARNING: the main loop\'s step limit was not found', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
