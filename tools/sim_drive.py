#!/usr/bin/env python3
"""
ETCH CRANK -- by Synth Centric
Drives the Playdate Simulator headless (Xvfb + a PulseAudio null sink) and
records what it plays: launch, undock, turn the crank, B + crank, dock, open
the system menu.  Screenshots and the recording go to build/sim/; the audio
analysis (level and brightness over time) is printed.

    DISPLAY=:99 python3 tools/sim_drive.py

Needs: the Simulator from the SDK (PLAYDATE_SDK_PATH), xdotool, ImageMagick's
import, pulseaudio with a null sink called "rec", numpy.  The click positions
are the Simulator 3.1.2 window's, at its default size on a 1280 x 900 screen.
"""
import os, subprocess, sys, time
import numpy as np

SDK = os.environ.get("PLAYDATE_SDK_PATH", "/opt/pd/PlaydateSDK-3.1.2")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "sim")
os.makedirs(OUT, exist_ok=True)

def sh(*a): subprocess.run(a, check=False)
def click(x, y): sh("xdotool", "mousemove", str(x), str(y), "click", "1")
def shot(name): sh("import", "-window", "root", os.path.join(OUT, name + ".png"))
import math
DIAL_X, DIAL_Y, DIAL_R = 748, 712, 67      # the Simulator's crank dial
angle_now = [0.0]
def dial_xy(deg):
    r = math.radians(deg)
    return int(DIAL_X + DIAL_R * math.sin(r)), int(DIAL_Y - DIAL_R * math.cos(r))
def crank_by(deg):
    """turn the crank by dragging the Simulator's dial handle around"""
    x, y = dial_xy(angle_now[0])
    sh("xdotool", "mousemove", str(x), str(y), "mousedown", "1")
    n = max(1, int(abs(deg) / 6))
    for i in range(1, n + 1):
        x, y = dial_xy(angle_now[0] + deg * i / n)
        sh("xdotool", "mousemove", str(x), str(y)); time.sleep(0.01)
    sh("xdotool", "mouseup", "1")
    angle_now[0] = (angle_now[0] + deg) % 360
def focus_lcd():
    """keyboard focus back on the Simulator (not its crank text field)"""
    click(1100, 450)
events = []
def mark(what): events.append((time.time() - t0, what)); print("%6.2f s  %s" % events[-1])

raw = os.path.join(OUT, "sim.raw")
rec = subprocess.Popen(["parec", "-d", "rec.monitor", "--format=s16le", "--rate=44100", "--channels=2", raw])
t0 = time.time()
sim = subprocess.Popen([os.path.join(SDK, "bin", "PlaydateSimulator"), os.path.join(ROOT, "ETCH_CRANK.pdx")],
                       stdout=open(os.path.join(OUT, "sim.log"), "w"), stderr=subprocess.STDOUT)
mark("launch")
time.sleep(6)
# raise the Simulator's volume slider (it starts at the bottom: silent)
sh("xdotool", "mousemove", "853", "317", "mousedown", "1", "mousemove", "853", "280",
   "mousemove", "853", "240", "mouseup", "1")
mark("volume up")
time.sleep(2); shot("1_docked"); mark("screenshot: docked")
click(658, 609); mark("undock")
time.sleep(2); crank_by(270); mark("crank +270 deg (cutoff up half the travel)")
time.sleep(3); shot("2_crank_open"); mark("screenshot: crank open")
crank_by(-540); mark("crank -540 deg (fully closed, past the hard stop)")
time.sleep(3); shot("2b_crank_closed")
crank_by(180); mark("crank +180 deg")
time.sleep(2)
focus_lcd(); sh("xdotool", "keydown", "a"); time.sleep(0.3)          # B is the 'a' key
crank_by(-180); mark("B + crank -180 deg (resonance down)")
time.sleep(1.5); shot("3_b_crank"); sh("xdotool", "keyup", "a"); mark("release B")
time.sleep(2); click(658, 609); mark("dock: glide home over one beat")
time.sleep(2); shot("4_docked_home"); mark("screenshot: home")
time.sleep(1); focus_lcd(); sh("xdotool", "keydown", "a"); sh("xdotool", "key", "Down"); sh("xdotool", "keyup", "a"); mark("B + DOWN: stop")
time.sleep(1.5); shot("4b_stopped")
sh("xdotool", "keydown", "a"); sh("xdotool", "key", "Down"); sh("xdotool", "keyup", "a"); mark("B + DOWN: play")
time.sleep(2); click(853, 190); mark("system menu open")
time.sleep(4); shot("5_menu"); mark("screenshot: menu")
focus_lcd(); sh("xdotool", "key", "a"); mark("system menu closed (B)")
time.sleep(3); shot("6_after_menu"); mark("end")
sim.kill(); time.sleep(1); rec.terminate(); time.sleep(0.5)

a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(np.float64) / 32768.0
m = a.mean(axis=1)
print("\nrecorded %.1f s" % (len(m) / 44100))
# the recording starts when parec starts (~ t0); report 0.5 s windows
win = 22050
print(" time   rms dBFS  centroid Hz")
for i in range(0, len(m) - win, win):
    x = m[i:i + win]
    rms = np.sqrt(np.mean(x * x)) + 1e-12
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / 44100)
    cen = (X * f).sum() / (X.sum() + 1e-12)
    note = ""
    for t, w in events:
        if i / 44100 <= t < (i + win) / 44100: note += "  <- " + w
    print("%5.1f  %7.1f  %8.0f%s" % (i / 44100, 20 * np.log10(rms), cen, note))
