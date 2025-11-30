#!/usr/bin/env python3
"""
Write a raw splash.bin into the SPIFFS partition using esptool. This will overwrite the partition content
and may corrupt an existing filesystem; use with caution.

Usage: python tools/write_splash_to_spiffs.py COM6 main/img_rocket_png.bin
"""
import sys
import subprocess
import os

def main():
    if len(sys.argv) < 3:
        print("Usage: write_splash_to_spiffs.py <PORT> <splash.bin>")
        return 1
    port = sys.argv[1]
    splash_path = sys.argv[2]
    if not os.path.isfile(splash_path):
        print(f"splash file not found: {splash_path}")
        return 2
    # Partition offset from partitions.csv
    offset = 0x310000
    cmd = [
        "esptool.py",
        "--port", port,
        "write_flash",
        hex(offset),
        splash_path
    ]
    print("Running:", ' '.join(cmd))
    ret = subprocess.run(cmd)
    return ret.returncode

if __name__ == '__main__':
    sys.exit(main())
