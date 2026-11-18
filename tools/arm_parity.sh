#!/bin/sh
# ETCH CRANK -- by Synth Centric
# The sound consistency test with the device's own compiler and flags.
#
# tests/consistency.c's Playdate path (the pump, the command ring, the crank
# wrap, ETCH DS's engine, CORROSION) is compiled with arm-none-eabi-gcc and
# exactly the SDK's device flags (Cortex-M7, Thumb-2, FPv5-SP hard-float,
# -O2 -falign-functions=16 ...), linked with a tiny Linux runtime
# (tests/arm/linux_shim.c) and run under qemu-arm.  Its engine-rate output
# must equal ETCH DS's (built on the host) bit for bit, and so must the
# 44.1 kHz output equal the host build of the same path.
#
#   tools/arm_parity.sh [/path/to/ETCH-DS]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/arm
mkdir -p "$OUT"
CC=${ARM_CC:-arm-none-eabi-gcc}
FLAGS="-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -D__FPU_USED=1 -O2 -falign-functions=16 \
-fomit-frame-pointer -mword-relocations -fno-common -ffunction-sections -fdata-sections \
-DTARGET_PLAYDATE=1 -DTARGET_EXTENSION=1"
INC="-I$ROOT/engine -I$ROOT/corrosion -I$ROOT/platform"
E=$ROOT/engine
SRC="$E/acid.arm.c $E/foundry.arm.c $E/fx.arm.c $E/render.arm.c $E/dsp_tables.c $E/seq_core.c \
$E/demo.c $E/evolve.c $E/edit.c $E/storage.c $ROOT/corrosion/corrosion.c $ROOT/corrosion/pattern.c \
$ROOT/corrosion/names.c $ROOT/platform/pump.c $ROOT/platform/crank.c $ROOT/platform/crank_audio.c \
$ROOT/platform/resample_tab.c $ROOT/platform/app_glue.c"
echo "== $($CC --version | head -1)"
echo "   flags: $FLAGS"
$CC $FLAGS $INC -w -DCONSIST_PORT -nostartfiles -specs=nosys.specs -Wl,--wrap=acid_render \
    -o "$OUT/consist_port_m7" "$ROOT/tests/consistency.c" $SRC "$ROOT/tests/arm/linux_shim.c" -lm 2>&1 \
    | grep -v "is not implemented and will always fail" || true
echo "== host: ETCH DS reference, and the Playdate path"
make -s -C "$ROOT/tests" ${1:+DS=$1} ../build/host/consist_ds ../build/host/consist_port ../build/host/compare >/dev/null
"$ROOT/build/host/consist_ds" "$OUT/ds_host.raw"
"$ROOT/build/host/consist_port" "$OUT/port_host.raw" "$OUT/port_host_44k.raw" aligned
echo "== qemu-arm: the Playdate path built for the Cortex-M7"
qemu-arm -cpu max "$OUT/consist_port_m7" "$OUT/port_m7.raw" "$OUT/port_m7_44k.raw" aligned
echo "== compare (engine rate): ETCH DS (host) vs ETCH CRANK (Cortex-M7 build)"
"$ROOT/build/host/compare" "$OUT/ds_host.raw" "$OUT/port_m7.raw"
echo "== compare (44.1 kHz output): host build vs Cortex-M7 build"
if cmp -s "$OUT/port_host_44k.raw" "$OUT/port_m7_44k.raw"; then
    echo "  bit-identical: $(($(stat -c %s "$OUT/port_m7_44k.raw") / 4)) frames at 44.1 kHz"
else
    cmp "$OUT/port_host_44k.raw" "$OUT/port_m7_44k.raw" | head -1; exit 1
fi
