import time

import serial
import serial.tools.list_ports

BAUD = 921600
VID = 0x1A86
PID = 0x55D3


def find_port(explicit=None):
    if explicit:
        return explicit
    ports = [
        p.device
        for p in serial.tools.list_ports.comports()
        if p.vid == VID and p.pid == PID
    ]
    if len(ports) != 1:
        raise SystemExit("Use --port to select the Freenove UART bridge.")
    return ports[0]


def open_serial(port=None, baud=BAUD, timeout=0.2):
    path = find_port(port)
    ser = serial.Serial(port=None, baudrate=baud, timeout=timeout, exclusive=True)
    ser.dtr = True
    ser.rts = False
    ser.port = path
    ser.open()
    ser.dtr = False
    return ser


def add_port_arg(parser):
    parser.add_argument("--port", help="UART path, e.g. /dev/ttyACM0")


def handshake(ser, timeout_s=5.0):
    ser.reset_input_buffer()
    deadline = time.monotonic() + timeout_s
    next_hello = 0.0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_hello:
            ser.write(b"hello\n")
            next_hello = time.monotonic() + 0.4
        line = ser.readline()
        if line.startswith(b"OK somnium"):
            return line.decode("utf-8", errors="replace").strip()
    raise RuntimeError("No handshake. Flash firmware and close other serial monitors.")


def read_line(ser, deadline):
    while time.monotonic() < deadline:
        line = ser.readline()
        if line:
            return line
    return b""
