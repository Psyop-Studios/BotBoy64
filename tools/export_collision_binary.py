#!/usr/bin/env python3
"""
Exports collision data to binary .col files for ROM loading.
This moves collision data out of the ELF and into the DFS filesystem.

Binary format (.col):
  - Header: 4 bytes magic "COL1"
  - 4 bytes: triangle count (uint32 big-endian for N64)
  - N * 36 bytes: triangles (9 floats each, big-endian for N64)

N64 uses big-endian, so all multi-byte values must be big-endian.
"""

import os
import re
import glob
import struct

MODELS_DIR = "src/models"
ASSETS_DIR = "assets"

def parse_collision_header(filepath):
    """Parse a collision .h file and extract triangles."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Extract model name from filename
    basename = os.path.basename(filepath)
    if basename.endswith('_collision.h'):
        model_name = basename[:-12]
    else:
        model_name = basename.replace('.h', '')

    # Parse triangles: { x0, y0, z0,  x1, y1, z1,  x2, y2, z2 }
    triangles = []
    tri_pattern = r'\{\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?\s*\}'

    for match in re.finditer(tri_pattern, content):
        tri = tuple(float(match.group(i)) for i in range(1, 10))
        triangles.append(tri)

    return model_name, triangles

def write_collision_binary(filepath, triangles):
    """Write triangles to binary .col file (big-endian for N64)."""
    with open(filepath, 'wb') as f:
        # Magic header (ASCII, same in any endianness)
        f.write(b'COL1')
        # Triangle count (big-endian for N64)
        f.write(struct.pack('>I', len(triangles)))
        # Triangles (9 floats each, big-endian)
        for tri in triangles:
            for val in tri:
                f.write(struct.pack('>f', val))

def main():
    print("Collision Binary Exporter (N64 Big-Endian)")
    print("=" * 50)

    # Find all collision header files in src/models/
    pattern = os.path.join(MODELS_DIR, "*_collision.h")
    collision_files = sorted(glob.glob(pattern))

    # Also scan assets/ directory for _collision.h files
    assets_pattern = os.path.join(ASSETS_DIR, "*_collision.h")
    collision_files.extend(sorted(glob.glob(assets_pattern)))

    # Also check for .original backup files (from chunking process)
    original_pattern = os.path.join(MODELS_DIR, "*_collision.h.original")
    original_files = {os.path.basename(f).replace('.original', ''): f
                      for f in glob.glob(original_pattern)}

    os.makedirs(ASSETS_DIR, exist_ok=True)

    exported = []
    total_triangles = 0

    for filepath in collision_files:
        basename = os.path.basename(filepath)

        model_name, triangles = parse_collision_header(filepath)

        if not triangles:
            # Check for .original backup (chunked files may have empty current version)
            if basename in original_files:
                model_name, triangles = parse_collision_header(original_files[basename])

        # For chunk files, still create empty .col even with 0 triangles
        # This prevents "file not found" errors at runtime
        is_chunk = '_chunk' in model_name if model_name else '_chunk' in basename
        if not triangles and not is_chunk:
            continue

        output_path = os.path.join(ASSETS_DIR, f"{model_name}.col")
        write_collision_binary(output_path, triangles)

        file_size = os.path.getsize(output_path)
        print(f"  {model_name}: {len(triangles)} tris ({file_size} bytes)")
        exported.append(model_name)
        total_triangles += len(triangles)

    # Calculate total size
    total_size = sum(os.path.getsize(os.path.join(ASSETS_DIR, f"{name}.col"))
                     for name in exported)

    print(f"\n{'=' * 50}")
    print(f"Exported {len(exported)} collision files")
    print(f"Total: {total_triangles} triangles, {total_size:,} bytes ({total_size / 1024:.1f} KB)")
    print(f"Output directory: {ASSETS_DIR}/")

if __name__ == "__main__":
    main()
