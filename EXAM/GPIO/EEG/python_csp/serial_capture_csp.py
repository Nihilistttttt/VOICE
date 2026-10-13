#!/usr/bin/env python3
"""Optional serial logger for PyCharm. You can also use any serial assistant and save txt manually."""
from __future__ import annotations

import argparse
from pathlib import Path
import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="Windows example: COM6")
    parser.add_argument("--baud", type=int, default=2000000)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent / "data" / "csp_log.txt")
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)

    print(f"Open {args.port} at {args.baud}, writing to {args.out}")
    print("Press Ctrl+C to stop.")
    with serial.Serial(args.port, args.baud, timeout=1) as ser, args.out.open("a", encoding="utf-8", errors="ignore") as f:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore")
            if line:
                print(line, end="")
                f.write(line)
                f.flush()


if __name__ == "__main__":
    main()
