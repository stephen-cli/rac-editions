"""Strafe scenarios through tools/pine_pad.py: run with the Load Game list open on the save to use."""

import time
import subprocess
from pine_pad import Pine, BTN
def shot(name):
    subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", "../screenshot.ps1", name],
                   capture_output=True)
p = Pine()
p.press("cross", hold=0.4, gap=2.0)  # load the highlighted save
for i in range(40):
    time.sleep(3)
    h = p.hero()
    print(time.strftime("%H:%M:%S"), "loading", h)
    if h["level"] and h["state"] in (0, 2) and h["mode"] in (0, 1) and h["pos"] != (0.0, 0.0, 0.0):
        break
time.sleep(8)
h = p.hero()
print("in game:", h)
shot("g0.png")
if not h["level"]:
    raise SystemExit("not in a level: refusing to send inputs")
def scenario(name, buttons, lx, ly, seconds, tap=None):
    print(f"=== {name}")
    p.pad(buttons, lx=lx, ly=ly)
    t0 = time.time()
    last = None
    tapped = False
    while time.time() - t0 < seconds:
        st = p.status()
        h = p.hero()
        if h != last:
            print(f"  {time.time()-t0:5.2f}s {st} state {h['state']} mode {h['mode']} stick {h['stick']:.2f}"
                  f" pos {h['pos']} trace {h['trace']} last {h['last']} held {h['strafe_held']}")
            last = h
        if st != "running":
            print("  !!! STOPPED:", st, h)
            shot(f"crash_{name}.png")
            return False
        if tap and not tapped and time.time() - t0 > 1.5:
            p.pad(buttons | BTN[tap], lx=lx, ly=ly)
            time.sleep(0.3)
            p.pad(buttons, lx=lx, ly=ly)
            tapped = True
        time.sleep(0.2)
    p.pad(0)
    time.sleep(1.5)
    print("  ok:", p.status(), p.hero())
    return True

ok = scenario("run_forward", 0, 0x7F, 0x00, 3)
ok = ok and scenario("strafe_forward", BTN["r2"], 0x7F, 0x00, 4)
ok = ok and scenario("strafe_left", BTN["r2"], 0x00, 0x7F, 4)
ok = ok and scenario("strafe_back", BTN["r2"], 0x7F, 0xFF, 4)
ok = ok and scenario("strafe_right_flip", BTN["r2"], 0xFF, 0x7F, 5, tap="cross")
ok = ok and scenario("strafe_back_flip", BTN["r2"], 0x7F, 0xFF, 5, tap="cross")
ok = ok and scenario("strafe_forward_jump", BTN["r2"], 0x7F, 0x00, 5, tap="cross")
ok = ok and scenario("crouch_flip_plain", BTN["r1"], 0x7F, 0x7F, 4, tap="cross")
shot("d4.png")
print("ALL OK" if ok else "FAILED")
