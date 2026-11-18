#!/usr/bin/env python3
"""
ETCH CRANK -- by Synth Centric
Does audio keep running while the Playdate's system menu is open?

Runs the Simulator headless (Xvfb + a PulseAudio null sink called "rec"),
lets the song play, opens the system menu with the MENU button, checks from a
screenshot that the menu really is open (the click is retried if not),
records with the menu open, closes it, and records again.  Prints the level
of each phase and leaves screenshots in build/sim/menu_*.png.

    DISPLAY=:99 python3 tools/sim_menu_test.py

Exit status 0 = the test ran (whatever the answer); 2 = the menu could not
be opened or closed.
"""
import os
import subprocess
import sys
import time

import numpy as np

SDK = os.environ.get("PLAYDATE_SDK_PATH", "/opt/pd/PlaydateSDK-3.1.2")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "sim")
os.makedirs(OUT, exist_ok=True)
MENU_X, MENU_Y = 853, 190            # the Simulator 3.1.2 window's MENU button
VOL_X, VOL_Y0, VOL_Y1 = 853, 317, 240


def sh(*a):
    return subprocess.run(a, check=False, capture_output=True, text=True).stdout


def shot(name):
    p = os.path.join(OUT, name + ".png")
    sh("import", "-window", "root", p)
    return p


def menu_is_open():
    """the system menu's selected row ("volume", below the game's own "cpu
    test" item) is drawn inverted: that strip at the right of the LCD reads
    about 0.39 with the menu up and 0.64 or more without it"""
    p = shot("menu_probe")
    v = sh("convert", p, "-crop", "180x18+625+188", "+repage", "-colorspace", "gray",
           "-format", "%[fx:mean]", "info:")
    return float(v or 1.0) < 0.5


def press_menu():
    sh("xdotool", "mousemove", str(MENU_X), str(MENU_Y), "mousedown", "1")
    time.sleep(0.15)
    sh("xdotool", "mouseup", "1")


def set_menu(want):
    for _ in range(5):
        if menu_is_open() == want:
            return True
        press_menu()
        time.sleep(1.2)
    return menu_is_open() == want


def record(secs):
    raw = os.path.join(OUT, "menu_rec.raw")
    subprocess.run(["timeout", str(secs), "parec", "-d", "rec.monitor", "--format=s16le",
                    "--rate=44100", "--channels=2", raw], check=False)
    a = np.fromfile(raw, dtype=np.int16).astype(np.float64) / 32768.0
    if not len(a):
        return -240.0, 0.0
    return 20 * np.log10(np.sqrt(np.mean(a * a)) + 1e-12), float(np.abs(a).max())


def main():
    sh("pkill", "-9", "-f", "PlaydateSimulator")
    time.sleep(1)
    sim = subprocess.Popen([os.path.join(SDK, "bin", "PlaydateSimulator"), os.path.join(ROOT, "ETCH_CRANK.pdx")],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(7)
    sh("xdotool", "mousemove", str(VOL_X), str(VOL_Y0), "mousedown", "1", "mousemove", str(VOL_X), "280",
       "mousemove", str(VOL_X), str(VOL_Y1), "mouseup", "1")
    time.sleep(1)
    ok = True
    results = []
    results.append(("playing, menu closed", record(3)))
    if set_menu(True):
        shot("menu_open")
        time.sleep(0.5)
        results.append(("system menu open", record(4)))
    else:
        ok = False
        print("could not open the system menu")
    if set_menu(False):
        shot("menu_closed")
        time.sleep(0.5)
        results.append(("menu closed again", record(3)))
    else:
        ok = False
        print("could not close the system menu")
    sim.kill()
    for what, (db, peak) in results:
        print("  %-22s  %7.1f dBFS RMS   peak %.3f%s" % (what, db, peak, "   (silent)" if peak == 0 else ""))
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
