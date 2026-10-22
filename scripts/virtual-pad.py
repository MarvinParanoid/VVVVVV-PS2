#!/usr/bin/env python3
"""Virtual gamepad for automated PCSX2 runs (Linux uinput, stdlib only).

Creates an evdev device that identifies as an Xbox 360 pad (VID 045e PID 028e)
so SDL inside PCSX2 maps it from its built-in database, then plays a scenario.
PCSX2's pad 1 must be bound to SDL-0 (scripts/pcsx2-common.sh does this when
PCSX2_VIRTUAL_PAD=1). Needs write access to /dev/uinput.

Usage: virtual-pad.py <scenario> [seconds]
  scenarios: newgame-explore  - start a new game, skip the intro, then walk and
                                flip around (left/right/flip pattern) until time runs out
             overlay-toggle     - title, then L3+R3 together three times (diagnostic overlay on/off/on)
             save-quick         - new game, walk right, map (Triangle) -> SAVE tab -> Cross (quicksave)
             continue           - title -> play -> continue (loads the quicksave)
             idle               - just keep the device present
The device disappears when the script exits.
"""
import fcntl
import os
import random
import struct
import sys
import time

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0
BTN = {"A": 0x130, "B": 0x131, "X": 0x133, "Y": 0x134, "LB": 0x136, "RB": 0x137,
       "BACK": 0x13A, "START": 0x13B, "GUIDE": 0x13C, "LS": 0x13D, "RS": 0x13E}
ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y = 0, 1, 3, 4, 2, 5, 0x10, 0x11

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
ABS_CNT = 64


class Pad:
    def __init__(self):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_SYN)
        for code in BTN.values():
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
        axes = {ABS_X: (-32768, 32767), ABS_Y: (-32768, 32767), ABS_RX: (-32768, 32767),
                ABS_RY: (-32768, 32767), ABS_Z: (0, 255), ABS_RZ: (0, 255),
                ABS_HAT0X: (-1, 1), ABS_HAT0Y: (-1, 1)}
        absmax, absmin = [0] * ABS_CNT, [0] * ABS_CNT
        for a, (lo, hi) in axes.items():
            fcntl.ioctl(self.fd, UI_SET_ABSBIT, a)
            absmin[a], absmax[a] = lo, hi
        name = b"Microsoft X-Box 360 pad"
        # struct uinput_user_dev: name[80], input_id{u16 bustype,vendor,product,version},
        # u32 ff_effects_max, s32 absmax[64], absmin[64], absfuzz[64], absflat[64]
        dev = struct.pack("80sHHHHI", name, 0x03, 0x045E, 0x028E, 0x0114, 0)
        dev += struct.pack(f"{ABS_CNT}i", *absmax) + struct.pack(f"{ABS_CNT}i", *absmin)
        dev += struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT)) + struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT))
        os.write(self.fd, dev)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        time.sleep(1.0)

    def _ev(self, etype, code, value):
        t = time.time()
        os.write(self.fd, struct.pack("llHHi", int(t), int((t % 1) * 1e6), etype, code, value))

    def _sync(self):
        self._ev(EV_SYN, SYN_REPORT, 0)

    def button(self, name, down):
        self._ev(EV_KEY, BTN[name], 1 if down else 0)
        self._sync()

    def press(self, name, hold=0.12, after=0.25):
        self.button(name, True)
        time.sleep(hold)
        self.button(name, False)
        time.sleep(after)

    def hat_x(self, value):
        self._ev(EV_ABS, ABS_HAT0X, value)
        self._sync()

    def close(self):
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        finally:
            os.close(self.fd)


def log(msg):
    print(f"[pad {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def newgame_explore(pad, seconds):
    end = time.time() + seconds
    time.sleep(14)                       # boot + preloader + title
    log("title: A")
    pad.press("A", after=1.5)            # "Press to Start" -> main menu
    log("main menu: A (play)")
    pad.press("A", after=1.5)            # play (fresh save -> new game / mode menu)
    pad.press("A", after=1.5)            # new game, if a submenu is shown
    log("intro: advance text")
    for _ in range(30):                  # skip cutscene text boxes
        pad.press("A", after=0.6)
    rng = random.Random(1234)            # deterministic pattern
    log("explore")
    while time.time() < end:
        direction = rng.choice((-1, 1))
        pad.hat_x(direction)
        t_hold = rng.uniform(0.8, 3.0)
        t0 = time.time()
        while time.time() - t0 < t_hold and time.time() < end:
            if rng.random() < 0.35:
                pad.press("A", hold=0.08, after=0.1)   # flip gravity
            time.sleep(0.25)
        pad.hat_x(0)
        time.sleep(rng.uniform(0.1, 0.6))


def new_game(pad):
    time.sleep(14)
    log("title: A")
    pad.press("A", after=1.5)
    log("main menu: A (play)")
    pad.press("A", after=1.5)
    pad.press("A", after=1.5)
    log("intro: advance text")
    for _ in range(30):
        pad.press("A", after=0.6)


def hat_tap(pad, value, after=0.6):
    pad.hat_x(value)
    time.sleep(0.15)
    pad.hat_x(0)
    time.sleep(after)


def save_quick(pad, seconds):
    end = time.time() + seconds
    new_game(pad)
    log("walk right 3 s")
    pad.hat_x(1)
    time.sleep(3)
    pad.hat_x(0)
    time.sleep(1.5)
    log("map: Y")
    pad.press("Y", after=2.0)
    for i in range(3):
        log(f"tab right {i + 1}")
        hat_tap(pad, 1, after=1.0)
    log("save: A")
    pad.press("A", after=3.0)
    log("leave map: Y")
    pad.press("Y", after=2.0)
    time.sleep(max(0, end - time.time()))


def continue_game(pad, seconds):
    end = time.time() + seconds
    time.sleep(14)
    log("title: A")
    pad.press("A", after=1.5)
    log("main menu: A (play)")
    pad.press("A", after=1.5)
    log("play menu: A (continue)")
    pad.press("A", after=3.0)
    time.sleep(max(0, end - time.time()))


def overlay_toggle(pad, seconds):
    time.sleep(14)
    log("title: A")
    pad.press("A", after=4)
    for i in range(3):
        log(f"L3+R3 #{i + 1}")
        pad.button("LS", True)
        pad.button("RS", True)
        time.sleep(0.3)
        pad.button("LS", False)
        pad.button("RS", False)
        time.sleep(6)
    time.sleep(max(0, seconds - 14 - 4 - 18))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    scenario = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 600
    pad = Pad()
    log(f"virtual pad created; scenario {scenario} for {seconds:.0f}s")
    try:
        if scenario == "newgame-explore":
            newgame_explore(pad, seconds)
        elif scenario == "save-quick":
            save_quick(pad, seconds)
        elif scenario == "continue":
            continue_game(pad, seconds)
        elif scenario == "overlay-toggle":
            overlay_toggle(pad, seconds)
        elif scenario == "idle":
            time.sleep(seconds)
        else:
            sys.exit(f"unknown scenario {scenario}")
    finally:
        pad.close()
        log("virtual pad removed")


if __name__ == "__main__":
    main()
