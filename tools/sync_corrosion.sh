#!/bin/sh
# ETCH CRANK -- by Synth Centric
# Copies CORROSION from ETCH (Etch---GB), unmodified, with ETCH's own host
# tests, and records checksums and the upstream commit.
#
#   tools/sync_corrosion.sh /path/to/Etch---GB
set -e
SRC=${1:?usage: tools/sync_corrosion.sh /path/to/Etch---GB}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST=$ROOT/corrosion
mkdir -p "$DST/tests"
cp "$SRC"/src/generator/corrosion.c "$SRC"/src/generator/pattern.c "$SRC"/src/generator/names.c "$DST/"
cp "$SRC"/include/corrosion.h "$SRC"/include/etch.h "$DST/"
cp "$SRC"/LICENSE "$DST/LICENSE.ETCH"
cp "$SRC"/tests/test_generator.c "$DST/tests/"
git -C "$SRC" rev-parse HEAD > "$DST/UPSTREAM_COMMIT"
(cd "$DST" && sha256sum corrosion.c pattern.c names.c corrosion.h etch.h tests/test_generator.c > SHA256SUMS)
echo "corrosion/ synced from $SRC @ $(cat "$DST/UPSTREAM_COMMIT")"
