"""Strafe scenarios through tools/pine_pad.py: run with the Load Game list open on the save to use.

Loads the highlighted save, waits until Ratchet stands in a level, then holds each scenario's
buttons and stick on the edition's debug pad, printing the hero's state whenever it changes.
Stops at the first scenario in which the emulator stops running.

  python tools/pine_scenarios.py
"""

import subprocess
import time

from pine_pad import BTN, Pine

SCREENSHOT = "../screenshot.ps1"   # PowerShell script that saves the emulator window, from the working directory

# Stick axes: 0x00 is full left or up, 0x7F the centre, 0xFF full right or down.
CENTRE, MIN, MAX = 0x7F, 0x00, 0xFF

# name, held buttons, stick x, stick y, seconds, button tapped 1.5 s in
SCENARIOS = [
    ("run_forward",         0,         CENTRE, MIN,    3, None),
    ("strafe_forward",      BTN["r2"], CENTRE, MIN,    4, None),
    ("strafe_left",         BTN["r2"], MIN,    CENTRE, 4, None),
    ("strafe_back",         BTN["r2"], CENTRE, MAX,    4, None),
    ("strafe_right_flip",   BTN["r2"], MAX,    CENTRE, 5, "cross"),
    ("strafe_back_flip",    BTN["r2"], CENTRE, MAX,    5, "cross"),
    ("strafe_forward_jump", BTN["r2"], CENTRE, MIN,    5, "cross"),
    ("crouch_flip_plain",   BTN["r1"], CENTRE, CENTRE, 4, "cross"),
]
TAP_AFTER, TAP_HOLD = 1.5, 0.3   # seconds into a scenario, and how long the tap is held
POLL = 0.2                       # seconds between reads of the hero while a scenario runs


def screenshot(name):
    result = subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", SCREENSHOT, name],
                            capture_output=True)
    if result.returncode:
        print(f"  (no screenshot {name}: {SCREENSHOT} failed)")


def wait_for_level(pine):
    """Load the highlighted save and return the hero once Ratchet stands in a level (or after two minutes)."""
    pine.press("cross", hold=0.4, gap=2.0)
    for _ in range(40):
        time.sleep(3)
        hero = pine.hero()
        print(time.strftime("%H:%M:%S"), "loading", hero)
        standing = hero["state"] in (0, 2) and hero["mode"] in (0, 1)
        if hero["level"] and standing and hero["pos"] != (0.0, 0.0, 0.0):
            break
    time.sleep(8)   # let the level settle before sending inputs
    return pine.hero()


def run_scenario(pine, name, buttons, lx, ly, seconds, tap=None):
    """Hold BUTTONS and the stick for SECONDS, tapping TAP once; False if the emulator stops running."""
    print(f"=== {name}")
    pine.pad(buttons, lx=lx, ly=ly)
    start = time.time()
    last = None
    tapped = False
    while time.time() - start < seconds:
        status = pine.status()
        hero = pine.hero()
        if hero != last:
            print(f"  {time.time() - start:5.2f}s {status} state {hero['state']} mode {hero['mode']}"
                  f" stick {hero['stick']:.2f} pos {hero['pos']} trace {hero['trace']} last {hero['last']}"
                  f" held {hero['strafe_held']}")
            last = hero
        if status != "running":
            print("  !!! STOPPED:", status, hero)
            screenshot(f"crash_{name}.png")
            return False
        if tap and not tapped and time.time() - start > TAP_AFTER:
            pine.pad(buttons | BTN[tap], lx=lx, ly=ly)
            time.sleep(TAP_HOLD)
            pine.pad(buttons, lx=lx, ly=ly)
            tapped = True
        time.sleep(POLL)
    pine.pad(0)
    time.sleep(1.5)
    print("  ok:", pine.status(), pine.hero())
    return True


def main():
    pine = Pine()
    hero = wait_for_level(pine)
    print("in game:", hero)
    screenshot("g0.png")
    if not hero["level"]:
        raise SystemExit("not in a level: refusing to send inputs")
    ok = all(run_scenario(pine, *scenario) for scenario in SCENARIOS)   # all() stops at the first failure
    screenshot("d4.png")
    print("ALL OK" if ok else "FAILED")


if __name__ == "__main__":
    main()
