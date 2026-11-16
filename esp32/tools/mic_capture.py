#!/usr/bin/env python3
"""Record from the Key Cube mic (mic_test firmware) and save a WAV file.

Flash the test first:  pio run -e mic_test -t upload
Close any serial monitor, then:

    ~/.platformio/penv/bin/python tools/mic_capture.py            # 3 s, auto-detect port
    ~/.platformio/penv/bin/python tools/mic_capture.py --long     # 10 s
    ~/.platformio/penv/bin/python tools/mic_capture.py --port /dev/cu.usbmodem101 --out test.wav

The PlatformIO Python already has pyserial. Any other Python: pip install pyserial.
"""

import argparse
import array
import math
import os
import subprocess
import sys
import time
import wave

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing: pip install pyserial (or use ~/.platformio/penv/bin/python)")


def find_port():
    ports = [p.device for p in list_ports.comports()
             if "usbmodem" in p.device or "ttyACM" in p.device or p.vid == 0x303A]
    if not ports:
        sys.exit("No ESP32-S3 USB port found. Pass --port.")
    return ports[0]


def read_line(ser, timeout):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode(errors="replace").strip()
        buf += b
    return None


def wait_for(ser, prefix, timeout):
    end = time.time() + timeout
    while time.time() < end:
        line = read_line(ser, end - time.time())
        if line is None:
            break
        if line.startswith(prefix):
            return line
        if line.startswith("[stats]") or line.startswith("[rec]") or line.startswith("[wav]"):
            print(" ", line)
    sys.exit(f"Timed out waiting for '{prefix}'. Is the mic_test firmware running?")


def analyse(path):
    with wave.open(path) as w:
        rate, n = w.getframerate(), w.getnframes()
        s = array.array("h", w.readframes(n))
    if sys.byteorder == "big":
        s.byteswap()
    if not s:
        print("Empty recording.")
        return
    peak = max(abs(x) for x in s)
    rms = math.sqrt(sum(x * x for x in s) / len(s))
    db = lambda v: 20 * math.log10(v / 32767) if v else -100.0
    # Noise floor: quietest 100 ms window. Signal-to-noise vs the loudest window.
    win = rate // 10
    wins = [math.sqrt(sum(x * x for x in s[i:i + win]) / win) for i in range(0, len(s) - win, win)]
    print(f"\n{path}: {n / rate:.2f} s at {rate} Hz")
    print(f"  peak {db(peak):6.1f} dBFS   RMS {db(rms):6.1f} dBFS")
    if wins:
        lo, hi = min(wins), max(wins)
        print(f"  noise floor {db(lo):6.1f} dBFS   loudest 100 ms {db(hi):6.1f} dBFS"
              f"   range {db(hi) - db(lo):.0f} dB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    ap.add_argument("--out", default="mic_test.wav", help="output WAV (default: mic_test.wav)")
    ap.add_argument("--long", action="store_true", help="record 10 s instead of 3 s")
    ap.add_argument("--no-play", action="store_true", help="don't play the recording afterwards")
    args = ap.parse_args()

    port = args.port or find_port()
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.2
    ser.dtr = ser.rts = False  # don't reset the board when opening the port
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()

    secs = 10 if args.long else 3
    input(f"Port {port}. Press Enter, then talk for {secs} s...")
    ser.write(b"R" if args.long else b"r")
    wait_for(ser, "DONE", secs + 10)

    ser.reset_input_buffer()
    ser.write(b"d")
    size = int(wait_for(ser, "WAV ", 10).split()[1])
    data = bytearray()
    end = time.time() + 30
    while len(data) < size and time.time() < end:
        data += ser.read(size - len(data))
        print(f"\r  receiving {len(data) * 100 // size}%", end="")
    print()
    ser.close()
    if len(data) < size:
        sys.exit(f"Only got {len(data)} of {size} bytes.")

    with open(args.out, "wb") as f:
        f.write(data)
    analyse(args.out)

    if not args.no_play:
        player = ["afplay"] if sys.platform == "darwin" else ["aplay", "-q"]
        try:
            print("Playing...")
            subprocess.run(player + [args.out], check=False)
        except FileNotFoundError:
            print(f"Saved {os.path.abspath(args.out)} (no player found)")


if __name__ == "__main__":
    main()
