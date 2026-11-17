#!/bin/sh
# ETCH CRANK -- by Synth Centric
# Proves corrosion/ is byte-identical to CORROSION in an ETCH (Etch---GB)
# checkout, file by file, and that it matches its own checksum manifest.
# Optionally also against an ETCH-DS checkout (its corrosion/ directory).
#
#   tools/verify_corrosion.sh /path/to/Etch---GB [/path/to/ETCH-DS]
set -e
GB=${1:?usage: tools/verify_corrosion.sh /path/to/Etch---GB [/path/to/ETCH-DS]}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
C=$ROOT/corrosion
(cd "$C" && sha256sum --quiet -c SHA256SUMS)
fail=0
chk() { if cmp -s "$1" "$2"; then echo "  same  $3"; else echo "  DIFF  $3"; fail=1; fi; }
echo "corrosion/ against ETCH @ $(git -C "$GB" rev-parse --short HEAD 2>/dev/null || echo '?')"
chk "$C/corrosion.c"  "$GB/src/generator/corrosion.c" corrosion.c
chk "$C/pattern.c"    "$GB/src/generator/pattern.c"   pattern.c
chk "$C/names.c"      "$GB/src/generator/names.c"     names.c
chk "$C/corrosion.h"  "$GB/include/corrosion.h"       corrosion.h
chk "$C/etch.h"       "$GB/include/etch.h"            etch.h
chk "$C/tests/test_generator.c" "$GB/tests/test_generator.c" tests/test_generator.c
if [ -n "$2" ]; then
    echo "corrosion/ against ETCH DS @ $(git -C "$2" rev-parse --short HEAD 2>/dev/null || echo '?')"
    for f in corrosion.c pattern.c names.c corrosion.h etch.h; do chk "$C/$f" "$2/corrosion/$f" "$f"; done
    chk "$C/tests/test_generator.c" "$2/tests/host/test_generator.c" tests/test_generator.c
fi
[ $fail -eq 0 ] && echo "OK: CORROSION is byte-identical" || { echo "FAIL: CORROSION differs"; exit 1; }
