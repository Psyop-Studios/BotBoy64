#!/usr/bin/env python3
"""
Generate collision header files for level maps from GLB files in maps/ folder.
This ensures collision data always matches the visual models.

Run this BEFORE export_collision_binary.py in the build process.
"""

import os
import sys
import glob

# Add tools directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from glb_to_collision import extract_triangles_from_glb

MAPS_DIR = "maps"
MODELS_DIR = "src/models"
SCALE = 64.0  # Match T3D/visual model scale


def generate_collision_header(glb_path, output_path, model_name):
    """Generate a collision header file from a GLB."""
    triangles = extract_triangles_from_glb(glb_path, scale=SCALE)

    var_name = model_name
    triangles_name = f"{var_name}_collision_triangles"
    mesh_var_name = f"{var_name}_collision"
    guard_name = f"{var_name.upper()}_COLLISION_H"

    with open(output_path, 'w') as f:
        f.write(f"// N64 Collision Data - Auto-generated from {os.path.basename(glb_path)}\n")
        f.write(f"// Model: {var_name}\n")
        f.write(f"// Triangles: {len(triangles)}\n")
        f.write("\n")
        f.write(f"#ifndef {guard_name}\n")
        f.write(f"#define {guard_name}\n")
        f.write("\n")
        f.write('#include "../collision.h"\n')
        f.write("\n")

        if len(triangles) > 0:
            f.write(f"static CollisionTriangle {triangles_name}[] = {{\n")
            for tri in triangles:
                f.write("    {{ {:.1f}f, {:.1f}f, {:.1f}f,   {:.1f}f, {:.1f}f, {:.1f}f,   {:.1f}f, {:.1f}f, {:.1f}f }},\n".format(*tri))
            f.write("};\n")
        else:
            f.write(f"static CollisionTriangle {triangles_name}[] = {{\n")
            f.write("    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },\n")
            f.write("};\n")

        f.write("\n")
        f.write(f"static CollisionMesh {mesh_var_name} = {{\n")
        f.write(f"    .triangles = {triangles_name},\n")
        f.write(f"    .count = {len(triangles)}\n")
        f.write("};\n")
        f.write("\n")
        f.write(f"#endif // {guard_name}\n")

    return len(triangles)


def main():
    print("Level Collision Generator")
    print("=" * 50)

    if not os.path.exists(MAPS_DIR):
        print(f"Maps folder '{MAPS_DIR}' not found!")
        return

    os.makedirs(MODELS_DIR, exist_ok=True)

    # Find all level*.glb files in maps/
    glb_files = sorted(glob.glob(os.path.join(MAPS_DIR, "level*.glb")))

    if not glb_files:
        print("No level GLB files found in maps/")
        return

    total_triangles = 0

    for glb_path in glb_files:
        model_name = os.path.splitext(os.path.basename(glb_path))[0]
        output_path = os.path.join(MODELS_DIR, f"{model_name}_collision.h")

        tri_count = generate_collision_header(glb_path, output_path, model_name)
        print(f"  {model_name}: {tri_count} triangles -> {output_path}")
        total_triangles += tri_count

    print(f"\nGenerated {len(glb_files)} collision header(s)")
    print(f"Total: {total_triangles} triangles")


if __name__ == "__main__":
    main()
