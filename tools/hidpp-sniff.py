#!/usr/bin/env python3
# duckling C2 recorder: prints HID++ reports from the Bolt receiver.
# Type a label and press Enter at any time to insert a marker line.
import os, sys, threading, time

DEV = sys.argv[1] if len(sys.argv) > 1 else "/dev/hidraw2"
fd = os.open(DEV, os.O_RDONLY)

def markers():
    for line in sys.stdin:
        print(f"{time.monotonic():.3f} ===== {line.strip() or 'MARK'} =====", flush=True)

threading.Thread(target=markers, daemon=True).start()
print(f"reading {DEV}; type a label + Enter before each action, Ctrl+C to stop", flush=True)
try:
    while True:
        r = os.read(fd, 64)
        note = ""
        if len(r) >= 5 and r[2] == 0x41:
            note = "  <- %s %s" % ({1: "keyboard", 2: "mouse"}.get(r[4] & 0x0F, "dev"),
                                   "OFFLINE" if r[4] & 0x40 else "online")
        print(f"{time.monotonic():.3f} {r.hex(' ')}{note}", flush=True)
except KeyboardInterrupt:
    pass
