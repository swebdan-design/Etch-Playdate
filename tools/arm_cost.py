#!/usr/bin/env python3
"""
ETCH CRANK -- by Synth Centric
CPU cost per stage of the Playdate build, estimated without hardware.

The Playdate path (tests/cost.c: pump, crank, ETCH DS's engine, resampler,
plus the update loop's CORROSION work) is compiled with the SDK's exact device
flags for the Cortex-M7 and run under qemu-arm with instruction tracing
(-d in_asm,exec,nochain).  Every executed translation block is attributed to
the function it belongs to, and functions to stages:

  ACID      acid_render (+ the crank's wrap)   DRUMS   foundry_render ...
  DRIVE/CRUSH/DELAY/COMB/LIMIT   the FX chain  EVENTS  the renderer's own work
  SEQ       seq_core (ETCH's clock and step rules)
  RESAMPLE  the pump (resampler, ring, commands)
  UPDATE    CORROSION, evolve.c, the crank's gestures (the update loop, not audio)

Two run lengths are traced and subtracted, so boot (FOUNDRY rendering its
drums) drops out and what remains is exactly one second of steady state.

Instructions are what qemu proves.  Cycles are an estimate: the Cortex-M7 is
dual-issue, but the Playdate runs code and data from external SDRAM through
small caches, so cycles per instruction (CPI) depends on cache behaviour the
emulator does not model.  The table gives a range of CPI; the on-device meter
(DWT cycle counter) gives the real figure.

    python3 tools/arm_cost.py [seconds]
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "arm")
CC = os.environ.get("ARM_CC", "arm-none-eabi-gcc")
FLAGS = ("-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -D__FPU_USED=1 -O2 "
         "-falign-functions=16 -fomit-frame-pointer -mword-relocations -fno-common "
         "-ffunction-sections -fdata-sections -DTARGET_PLAYDATE=1 -DTARGET_EXTENSION=1").split()
E = os.path.join(ROOT, "engine")
SRC = [os.path.join(E, f) for f in ("acid.arm.c", "foundry.arm.c", "fx.arm.c", "render.arm.c", "dsp_tables.c",
                                     "seq_core.c", "demo.c", "evolve.c", "edit.c", "storage.c")]
SRC += [os.path.join(ROOT, "corrosion", f) for f in ("corrosion.c", "pattern.c", "names.c")]
SRC += [os.path.join(ROOT, "platform", f) for f in ("pump.c", "crank.c", "crank_audio.c", "resample_tab.c",
                                                     "app_glue.c")]

FN_STAGE = {
    "acid_render": "ACID", "__wrap_acid_render": "ACID", "acid_note": "ACID", "acid_glide": "ACID",
    "acid_gate_off": "ACID", "acid_step": "ACID", "acid_kill": "ACID", "accent_hit": "ACID",
    "foundry_render": "DRUMS", "foundry_trigger": "DRUMS", "foundry_kill": "DRUMS",
    "fx_drive": "DRIVE", "fx_crush": "CRUSH", "fx_delay": "DELAY", "fx_comb": "COMB", "fx_limit": "LIMIT",
    "audio_render_block": "EVENTS", "live_drain": "EVENTS", "acid_event": "EVENTS", "drum_event": "EVENTS",
    "crank_command": "RESAMPLE",
}
FILE_STAGE = {"seq_core": "SEQ", "pump": "RESAMPLE", "app_glue": "UPDATE", "names": "UPDATE", "crank_audio": "ACID", "evolve": "UPDATE",
              "corrosion": "UPDATE", "pattern": "UPDATE", "crank": "UPDATE", "edit": "UPDATE",
              "demo": "UPDATE", "acid.arm": "ACID", "foundry.arm": "DRUMS", "fx.arm": "FX?",
              "render.arm": "EVENTS"}
ORDER = ["ACID", "DRUMS", "DRIVE", "CRUSH", "DELAY", "COMB", "LIMIT", "EVENTS", "SEQ", "RESAMPLE"]


def build():
    """each source to a named object (so the link map names the file), then link"""
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)
    inc = ["-I" + os.path.join(ROOT, d) for d in ("engine", "corrosion", "platform")]
    objs = []
    for src in SRC + [os.path.join(ROOT, "tests", "cost.c"), os.path.join(ROOT, "tests", "arm", "linux_shim.c")]:
        o = os.path.join(OUT, "obj", os.path.basename(src).replace(".c", ".o"))
        subprocess.run([CC] + FLAGS + inc + ["-w", "-c", src, "-o", o], check=True)
        objs.append(o)
    exe = os.path.join(OUT, "cost")
    subprocess.run([CC] + FLAGS + ["-nostartfiles", "-specs=nosys.specs", "-Wl,--wrap=acid_render",
                    "-Wl,-Map=" + os.path.join(OUT, "cost.map"), "-o", exe] + objs + ["-lm"],
                   check=True, stderr=subprocess.DEVNULL)
    return exe


def fn_files():
    """function name -> object file stem, from the link map"""
    m = {}
    cur = None
    for line in open(os.path.join(OUT, "cost.map")):
        s = re.match(r"^ \.text\.(\S+)\s*(0x\S+\s+0x\S+\s+(\S+))?", line)
        if s:
            cur = s.group(1)
            if s.group(3):
                m[cur] = os.path.basename(s.group(3)).replace(".o", "")
                cur = None
            continue
        if cur:
            t = re.match(r"^\s+0x\S+\s+0x\S+\s+(\S+)", line)
            if t:
                m[cur] = os.path.basename(t.group(1)).replace(".o", "")
            cur = None
    return m


def thumb_count(hexstr):
    """instructions in a Thumb-2 byte string (little-endian halfwords)"""
    b = bytes.fromhex(hexstr)
    i = n = 0
    while i + 1 < len(b):
        hw = b[i] | (b[i + 1] << 8)
        i += 4 if (hw >> 11) in (0b11101, 0b11110, 0b11111) else 2
        n += 1
    return n


def trace(exe, scenario, secs):
    p = subprocess.Popen(["qemu-arm", "-cpu", "max", "-d", "in_asm,exec,nochain", exe, scenario, str(secs)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, bufsize=1 << 20)
    tb = {}
    per_fn = defaultdict(int)
    in_pc = None
    hexbuf = ""
    for line in p.stderr:
        if line.startswith("Trace"):
            br = line.index("[")
            pc = int(line[br + 1:].split("/")[1], 16)
            name = line[line.index("]") + 1:].strip()
            per_fn[name] += tb.get(pc, 1)
        elif line.startswith("IN:"):
            in_pc, hexbuf = None, ""
        elif line.startswith("0x") and in_pc is None:
            in_pc = int(line.split(":")[0], 16)
        elif line.startswith("OBJD-T:"):
            hexbuf += line[7:].strip()
        elif line.strip() == "" and in_pc is not None and hexbuf:
            tb[in_pc] = thumb_count(hexbuf)
            in_pc, hexbuf = None, ""
    p.wait()
    return per_fn


def stage_of(fn, files):
    if fn in FN_STAGE:
        return FN_STAGE[fn]
    base = fn.split(".")[0]
    if base in FN_STAGE:
        return FN_STAGE[base]
    f = files.get(fn) or files.get(base)
    if f in FILE_STAGE:
        return FILE_STAGE[f]
    if fn.startswith("corr_") or fn.startswith("evo_") or fn.startswith("crank_ui") or fn.startswith("anchor"):
        return "UPDATE"
    if fn in ("memcpy", "memset", "__aeabi_memcpy", "__aeabi_memset", "__aeabi_memclr", "memcmp"):
        return "LIBC"
    return "OTHER(" + fn + ")"


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    exe = build()
    files = fn_files()
    results = {}
    for scen in ("song", "stress"):
        a = trace(exe, scen, 0.3)
        b = trace(exe, scen, 0.3 + secs)
        st = defaultdict(float)
        for fn in set(a) | set(b):
            d = b.get(fn, 0) - a.get(fn, 0)
            if d:
                st[stage_of(fn, files)] += d / secs
        results[scen] = st
    print("Instructions per second of audio, Cortex-M7 build (qemu trace, %.1f s steady state)" % secs)
    print("and %% of a 168 MHz core at CPI 1.0 / 1.5 / 2.5\n" % ())
    print("%-10s %14s %22s %14s %22s" % ("stage", "song Minsn/s", "song % (CPI 1/1.5/2.5)",
                                         "stress Minsn/s", "stress % (CPI 1/1.5/2.5)"))
    keys = ORDER + sorted(k for k in set(results["song"]) | set(results["stress"]) if k not in ORDER)
    tot = {"song": 0.0, "stress": 0.0}
    for k in keys:
        row = []
        for scen in ("song", "stress"):
            v = results[scen].get(k, 0.0)
            if k != "UPDATE" and not k.startswith("OTHER") and k != "LIBC":
                tot[scen] += v
            row.append(v)
        if max(row) < 1:
            continue
        print("%-10s %14.2f %22s %14.2f %22s" % (
            k, row[0] / 1e6, "%.1f / %.1f / %.1f" % tuple(row[0] * c / 168e6 * 100 for c in (1, 1.5, 2.5)),
            row[1] / 1e6, "%.1f / %.1f / %.1f" % tuple(row[1] * c / 168e6 * 100 for c in (1, 1.5, 2.5))))
    print("%-10s %14.2f %22s %14.2f %22s" % (
        "AUDIO", tot["song"] / 1e6, "%.1f / %.1f / %.1f" % tuple(tot["song"] * c / 168e6 * 100 for c in (1, 1.5, 2.5)),
        tot["stress"] / 1e6, "%.1f / %.1f / %.1f" % tuple(tot["stress"] * c / 168e6 * 100 for c in (1, 1.5, 2.5))))
    print("\n(AUDIO = everything in the audio callback; UPDATE, LIBC and OTHER are listed for completeness)")


if __name__ == "__main__":
    main()
