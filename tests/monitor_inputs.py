#!/usr/bin/env python3
"""Live feed of encoder / shutter events from the board."""

import argparse
import sys

import serial

from board import add_port_arg, open_serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    add_port_arg(parser)
    args = parser.parse_args()

    try:
        ser = open_serial(args.port)
    except (OSError, serial.SerialException) as err:
        sys.exit(f"{err}")

    print(f"Listening on {ser.port}. Turn dial / press buttons. Ctrl+C exits.", flush=True)
    try:
        while True:
            line = ser.readline()
            if not line:
                continue
            text = line.decode("utf-8", errors="replace")
            if text.startswith(("EVT ", "READY", "CAM ", "LCD ", "SHOT ")):
                print(text, end="", flush=True)
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
