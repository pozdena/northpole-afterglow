#!/usr/bin/env python3
"""Fail if the linked module has writable data or a GOT: a code resource can't carry them."""
import subprocess
import sys

out = subprocess.run(["m68k-elf-objdump", "-h", sys.argv[1]], capture_output=True, text=True, check=True).stdout
bad = []
for line in out.splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[0].isdigit():
        name, size = parts[1], int(parts[2], 16)
        if name != ".text" and size and not name.startswith(".debug"):
            bad.append(f"{name} ({size} bytes)")
if bad:
    sys.exit("code resource can't hold " + ", ".join(bad) + ": make it const or move it into Globals")
