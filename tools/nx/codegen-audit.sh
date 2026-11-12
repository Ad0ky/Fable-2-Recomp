#!/usr/bin/env bash
# fable2-nx on renut_nx_ultimate_base: the base's per-game checks of the generated code, after every code generation
# (docs/building.md "A new game's calling convention", the porting prompt's step 2, renut_config.toml's register keys).
# They report; they change nothing. Their lines go to the build log and out/audit/*.txt:
#   - tools/cc_entry_census.py: must end with "none" (vector registers read on entry);
#   - tools/cc_exit_census.py: every read it lists must be value-independent (reviewed by hand);
#   - tools/read_before_write.py: non-volatile registers read before written (split pieces: share_registers);
#   - tools/non_argument_audit.py --with-option: values crossing a call in r0/r2/r11/r12/f0/v32-v63 without a hand-over.
# --noreturn: Fable 2's longjmp (0x82CA9260).
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
mkdir -p out/audit
gen=app/generated
run() {
    local name="$1"; shift
    echo "---- $name"
    python3 "$@" > "out/audit/$name.txt" 2>&1
    echo "(exit $?)" >> "out/audit/$name.txt"
    tail -n 25 "out/audit/$name.txt"
}
run cc_entry_census tools/cc_entry_census.py --gen "$gen" --image out/renut_image.bin
run cc_exit_census tools/cc_exit_census.py --gen "$gen" --image out/renut_image.bin
run read_before_write tools/read_before_write.py "$gen" --noreturn=82CA9260
run non_argument_audit tools/non_argument_audit.py "$gen" --with-option --noreturn=82CA9260
exit 0
