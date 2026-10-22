"""PCSX2 PINE client and the edition's debug pad: drive the game and read its state from Python.

Enable PINE in PCSX2 (Settings > Advanced, or EnablePINE = true in PCSX2.ini; TCP 28011 on
Windows). The mod segment exports ee_debug_pad (fake pad 0 report while [0] is set), ee_debug_trace
and ee_debug_last (the id of the hook running / last entered). See docs/DEBUGGING.md.

  python tools/pine_pad.py                 # status, title, hero state
"""
import socket
import struct
import time
from pathlib import Path

SYM = dict(line.split("	") for line in
           (Path(__file__).resolve().parent.parent / "build/modseg/modseg.sym").read_text().splitlines())
S = {k: int(v, 16) for k, v in SYM.items()}
DEBUG_PAD = S["ee_debug_pad"]
TRACE = S["ee_debug_trace"]
LAST = S["ee_debug_last"]
HERO = 0x13F450

# buttons, game layout (active high): same bits as the report word
BTN = {"select": 0x0100, "l3": 0x0200, "r3": 0x0400, "start": 0x0800,
       "up": 0x1000, "right": 0x2000, "down": 0x4000, "left": 0x8000,
       "l2": 0x0001, "r2": 0x0002, "l1": 0x0004, "r1": 0x0008,
       "triangle": 0x0010, "circle": 0x0020, "cross": 0x0040, "square": 0x0080}


class Pine:
    def __init__(self, port=28011):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=5)

    def _cmd(self, op, payload=b"", reply=0):
        msg = struct.pack("<IB", 5 + len(payload), op) + payload
        self.s.sendall(msg)
        hdr = self._recv(5)
        size, res = struct.unpack("<IB", hdr)
        data = self._recv(size - 5) if size > 5 else b""
        if res != 0:
            raise RuntimeError(f"PINE op {op} failed")
        return data

    def _recv(self, n):
        out = b""
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk:
                raise ConnectionError("PINE closed")
            out += chunk
        return out

    def r8(self, a): return self._cmd(0, struct.pack("<I", a))[0]
    def r16(self, a): return struct.unpack("<H", self._cmd(1, struct.pack("<I", a)))[0]
    def r32(self, a): return struct.unpack("<I", self._cmd(2, struct.pack("<I", a)))[0]
    def rf(self, a): return struct.unpack("<f", self._cmd(2, struct.pack("<I", a)))[0]
    def w8(self, a, v): self._cmd(4, struct.pack("<IB", a, v & 0xFF))
    def w32(self, a, v): self._cmd(6, struct.pack("<II", a, v & 0xFFFFFFFF))
    def status(self):
        return {0: "running", 1: "paused", 2: "shutdown"}.get(struct.unpack("<I", self._cmd(0xF))[0], "?")
    def title(self): return self._cmd(0xB)[4:].split(b"\0")[0].decode(errors="replace")
    def version(self): return self._cmd(8)[4:].split(b"\0")[0].decode(errors="replace")

    # --- the edition's debug pad ---
    def pad(self, buttons=0, lx=0x7F, ly=0x7F, rx=0x7F, ry=0x7F):
        self.w32(DEBUG_PAD + 4, buttons)
        self.w32(DEBUG_PAD + 8, (lx & 0xFF) | ((ly & 0xFF) << 8))
        self.w32(DEBUG_PAD + 12, (rx & 0xFF) | ((ry & 0xFF) << 8))
        self.w32(DEBUG_PAD, 1)

    def pad_off(self):
        self.w32(DEBUG_PAD, 0)

    def press(self, names, hold=0.15, gap=0.25):
        b = 0
        for n in names.split("+"):
            b |= BTN[n]
        self.pad(b)
        time.sleep(hold)
        self.pad(0)
        time.sleep(gap)

    def hero(self):
        return {"state": self.r32(HERO + 0x2084), "mode": self.r32(HERO + 0x208C), "stick": self.rf(HERO + 0x229C),
                "pos": tuple(round(self.rf(HERO + 0x10 + 4 * i), 2) for i in range(3)),
                "trace": self.r32(TRACE), "last": self.r32(LAST),
                "strafe_held": self.r32(S["ee_strafe_held"]), "level": self.r32(0x15EE84)}


if __name__ == "__main__":
    p = Pine()
    print(p.version(), p.status(), p.title())
    print(p.hero())
