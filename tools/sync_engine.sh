#!/bin/sh
# ETCH CRANK -- by Synth Centric
# Copies ETCH DS's sound engine and sequencer into engine/, unmodified, and
# records the checksums and the upstream commit.  The engine is never edited
# in this repository: a fix goes to ETCH-DS first and comes back with this
# script.  `make check` refuses to build if engine/ no longer matches.
#
#   tools/sync_engine.sh /path/to/ETCH-DS
set -e
SRC=${1:?usage: tools/sync_engine.sh /path/to/ETCH-DS}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST=$ROOT/engine
mkdir -p "$DST/tests"
cp "$SRC"/common/ipc.h "$SRC"/common/seq_core.c "$SRC"/common/seq_core.h "$DST/"
for f in acid.arm.c foundry.arm.c fx.arm.c render.arm.c dsp_tables.c dsp_tables.h audio.h \
         demo.c evolve.c edit.c app.h storage.c storage.h; do
    cp "$SRC/arm9/source/$f" "$DST/"
done
# ETCH DS's own host tests, so they keep running against this copy
cp "$SRC"/tests/host/test_seq.c "$SRC"/tests/host/test_dsp.c "$DST/tests/"
commit=$(git -C "$SRC" rev-parse HEAD)
cat > "$DST/UPSTREAM" <<EOT
repository  swebdan-design/ETCH-DS
commit      $commit
map         engine/{ipc.h,seq_core.c,seq_core.h}   <- common/*
            engine/*.c, engine/*.h              <- arm9/source/*
            engine/tests/*                      <- tests/host/*
note        corrosion/ is checked separately, against ETCH (Etch---GB)
EOT
(cd "$DST" && find . -type f ! -name SHA256SUMS ! -name UPSTREAM | sed 's|^\./||' | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
echo "engine/ synced from $SRC @ $commit"
