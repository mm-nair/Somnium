#!/usr/bin/env python3
import sys
import time

import serial

ser = serial.Serial()
ser.port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
ser.baudrate = 921600
ser.timeout = 0.25
ser.dtr = False
ser.rts = False
ser.open()
ser.reset_input_buffer()
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 45
print(f"listening {ser.port} {secs:.0f}s — shoot a photo", flush=True)
deadline = time.monotonic() + secs
while time.monotonic() < deadline:
    data = ser.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
print("\n--- end ---", flush=True)
ser.close()
