#!/usr/bin/env python3
import subprocess
from pathlib import Path

def is_pow2(n):
    return n > 0 and (n & (n - 1)) == 0

def next_pow2(n):
    p = 1
    while p < n:
        p *= 2
    return p

assets_dir = Path("assets")
for png in assets_dir.glob("*.png"):
    result = subprocess.run(["identify", "-format", "%w %h", str(png)], capture_output=True, text=True)
    if result.returncode != 0:
        continue
    w, h = map(int, result.stdout.strip().split())

    # Only resize if NOT already power of 2
    w_is_pow2 = is_pow2(w)
    h_is_pow2 = is_pow2(h)

    if w_is_pow2 and h_is_pow2:
        print(f"OK (already pow2): {png.name} ({w}x{h})")
        continue

    # Round to nearest power of 2
    new_w = next_pow2(w) if not w_is_pow2 else w
    new_h = next_pow2(h) if not h_is_pow2 else h

    print(f"Resizing {png.name}: {w}x{h} -> {new_w}x{new_h}")
    subprocess.run(["magick", str(png), "-resize", f"{new_w}x{new_h}!", str(png)])
