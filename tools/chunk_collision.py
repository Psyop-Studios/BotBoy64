#!/usr/bin/env python3
"""
Chunks collision meshes to match visual chunks from chunk_glb.py.

Uses the chunk bounds saved by chunk_glb.py to ensure collision chunks
have the same spatial boundaries as visual chunks. This is critical for
proper chunk-based visibility culling.

Run this AFTER chunk_glb.py and BEFORE generate_collision_registry.py.
"""

import os
import re
import glob
import json

MODELS_DIR = "src/models"
CHUNK_BOUNDS_FILE = "build/chunk_bounds.json"

# Don't chunk these - they use full rotation collision which doesn't use spatial grid
EXCLUDE_FROM_CHUNKING = ['cog', 'barrel', 'bolt', 'slime', 'spikes']

# Scale factor: Collision data is in game units (pre-scaled by 64),
# while visual chunk bounds are in Blender meters
COLLISION_SCALE_FACTOR = 64.0


def load_chunk_bounds():
    """Load chunk bounds from visual chunker."""
    if not os.path.exists(CHUNK_BOUNDS_FILE):
        print(f"WARNING: {CHUNK_BOUNDS_FILE} not found. Run chunk_glb.py first!")
        return {}

    with open(CHUNK_BOUNDS_FILE, 'r') as f:
        return json.load(f)


def parse_collision_file(filepath):
    """Parse a collision .h file and extract triangles."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Extract model name from header comment
    name_match = re.search(r'// Model: (\w+)', content)
    model_name = name_match.group(1) if name_match else None

    # Extract triangle count
    count_match = re.search(r'// Triangles: (\d+)', content)
    tri_count = int(count_match.group(1)) if count_match else 0

    # Extract variable names
    array_match = re.search(r'static CollisionTriangle (\w+)\[\]', content)
    mesh_match = re.search(r'static CollisionMesh (\w+)\s*=', content)

    array_name = array_match.group(1) if array_match else None
    mesh_name = mesh_match.group(1) if mesh_match else None

    # Parse triangles: { x0, y0, z0,  x1, y1, z1,  x2, y2, z2 }
    triangles = []
    tri_pattern = r'\{\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?\s*\}'

    for match in re.finditer(tri_pattern, content):
        tri = tuple(float(match.group(i)) for i in range(1, 10))
        triangles.append(tri)

    return {
        'model_name': model_name,
        'tri_count': tri_count,
        'array_name': array_name,
        'mesh_name': mesh_name,
        'triangles': triangles,
    }


import math

def get_triangle_centroid(tri):
    """Get the 3D centroid of a triangle."""
    # tri = (x0, y0, z0, x1, y1, z1, x2, y2, z2)
    return (
        (tri[0] + tri[3] + tri[6]) / 3.0,
        (tri[1] + tri[4] + tri[7]) / 3.0,
        (tri[2] + tri[5] + tri[8]) / 3.0
    )


def compute_collision_bounds(triangles):
    """Compute bounding box of collision triangles."""
    min_x = min_y = min_z = float('inf')
    max_x = max_y = max_z = float('-inf')

    for tri in triangles:
        for i in range(3):
            x, y, z = tri[i*3], tri[i*3+1], tri[i*3+2]
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)
            min_z = min(min_z, z)
            max_z = max(max_z, z)

    return (min_x, min_y, min_z), (max_x, max_y, max_z)


def compute_visual_bounds(chunk_bounds_list):
    """Compute overall bounds from chunk bounds list."""
    min_x = min(b['min'][0] for b in chunk_bounds_list)
    max_x = max(b['max'][0] for b in chunk_bounds_list)
    min_y = min(b['min'][1] for b in chunk_bounds_list)
    max_y = max(b['max'][1] for b in chunk_bounds_list)
    min_z = min(b['min'][2] for b in chunk_bounds_list)
    max_z = max(b['max'][2] for b in chunk_bounds_list)
    return (min_x, min_y, min_z), (max_x, max_y, max_z)


def compute_transform_params(collision_bounds, visual_bounds):
    """Compute transform parameters to map collision coords to visual coords.

    Tries different rotations to find the best match, then computes scale/offset.
    """
    col_min, col_max = collision_bounds
    vis_min, vis_max = visual_bounds

    best_error = float('inf')
    best_params = None

    # Try different Y rotations (0, 90, 180, 270 degrees)
    for rot_deg in [0, 90, 180, 270]:
        rot_rad = math.radians(rot_deg)
        cos_r = math.cos(rot_rad)
        sin_r = math.sin(rot_rad)

        # Rotate collision bounds
        corners = [
            (col_min[0], col_min[2]),
            (col_min[0], col_max[2]),
            (col_max[0], col_min[2]),
            (col_max[0], col_max[2]),
        ]

        rotated_x = [c[0] * cos_r - c[1] * sin_r for c in corners]
        rotated_z = [c[0] * sin_r + c[1] * cos_r for c in corners]

        rot_min_x, rot_max_x = min(rotated_x), max(rotated_x)
        rot_min_z, rot_max_z = min(rotated_z), max(rotated_z)

        # Compute scale to match extents
        col_span_x = rot_max_x - rot_min_x
        col_span_z = rot_max_z - rot_min_z
        vis_span_x = vis_max[0] - vis_min[0]
        vis_span_z = vis_max[2] - vis_min[2]

        if col_span_x < 0.001 or col_span_z < 0.001:
            continue

        scale_x = vis_span_x / col_span_x if col_span_x > 0 else 1.0
        scale_z = vis_span_z / col_span_z if col_span_z > 0 else 1.0

        # Use average scale for uniform scaling
        scale = (scale_x + scale_z) / 2.0

        # Compute offset
        offset_x = vis_min[0] - rot_min_x * scale
        offset_z = vis_min[2] - rot_min_z * scale

        # Y is simpler - just scale and offset
        col_span_y = col_max[1] - col_min[1]
        vis_span_y = vis_max[1] - vis_min[1]
        scale_y = vis_span_y / col_span_y if col_span_y > 0.001 else scale
        offset_y = vis_min[1] - col_min[1] * scale_y

        # Compute error (how well the transformed bounds match visual bounds)
        error = abs(scale_x - scale_z)  # Prefer uniform scaling

        if error < best_error:
            best_error = error
            best_params = {
                'rotation': rot_deg,
                'scale': scale,
                'scale_y': scale_y,
                'offset_x': offset_x,
                'offset_y': offset_y,
                'offset_z': offset_z,
            }

    return best_params


def transform_point(x, y, z, params):
    """Transform a collision point to visual coordinates."""
    rot_rad = math.radians(params['rotation'])
    cos_r = math.cos(rot_rad)
    sin_r = math.sin(rot_rad)

    # Rotate
    rx = x * cos_r - z * sin_r
    rz = x * sin_r + z * cos_r

    # Scale and offset
    tx = rx * params['scale'] + params['offset_x']
    ty = y * params['scale_y'] + params['offset_y']
    tz = rz * params['scale'] + params['offset_z']

    return (tx, ty, tz)


def get_triangle_centroid_transformed(tri, params):
    """Get the transformed centroid of a collision triangle."""
    cx = (tri[0] + tri[3] + tri[6]) / 3.0
    cy = (tri[1] + tri[4] + tri[7]) / 3.0
    cz = (tri[2] + tri[5] + tri[8]) / 3.0
    return transform_point(cx, cy, cz, params)


def point_in_bounds(point, bounds, margin=10.0):
    """Check if a point is within bounds (with margin for edge cases)."""
    return (bounds['min'][0] - margin <= point[0] <= bounds['max'][0] + margin and
            bounds['min'][1] - margin <= point[1] <= bounds['max'][1] + margin and
            bounds['min'][2] - margin <= point[2] <= bounds['max'][2] + margin)


def scale_chunk_bounds(chunk_bounds_list, scale_factor):
    """Scale visual chunk bounds to match collision coordinate space."""
    scaled = []
    for bounds in chunk_bounds_list:
        scaled.append({
            'min': [bounds['min'][0] * scale_factor,
                    bounds['min'][1] * scale_factor,
                    bounds['min'][2] * scale_factor],
            'max': [bounds['max'][0] * scale_factor,
                    bounds['max'][1] * scale_factor,
                    bounds['max'][2] * scale_factor]
        })
    return scaled


def detect_axis_swap(collision_bounds, visual_bounds):
    """Detect if X and Z axes need to be swapped between collision and visual coords.

    Some levels have collision exported with X as the long axis while visuals have Z as long.
    Returns True if axes should be swapped.
    """
    col_min, col_max = collision_bounds
    vis_min, vis_max = visual_bounds

    col_span_x = col_max[0] - col_min[0]
    col_span_z = col_max[2] - col_min[2]
    vis_span_x = vis_max[0] - vis_min[0]
    vis_span_z = vis_max[2] - vis_min[2]

    # If collision is "long" in X but visual is "long" in Z, we need to swap
    col_x_is_long = col_span_x > col_span_z * 2
    vis_z_is_long = vis_span_z > vis_span_x * 2

    return col_x_is_long and vis_z_is_long


def assign_triangles_to_chunks_direct(triangles, chunk_bounds_list, swap_xz=False):
    """Assign triangles to chunks by directly comparing collision coords to scaled bounds.

    The visual chunk bounds are scaled by COLLISION_SCALE_FACTOR (64) to match
    the collision coordinate space. Then triangle centroids are compared directly.

    Args:
        triangles: List of collision triangles (in game units, already scaled by 64)
        chunk_bounds_list: List of bounds dicts from visual chunker (in Blender meters)
        swap_xz: If True, swap X and Z when comparing collision to visual bounds
    """
    # Scale visual bounds to match collision coordinate space
    scaled_bounds = scale_chunk_bounds(chunk_bounds_list, COLLISION_SCALE_FACTOR)

    # If swapping, transform the scaled bounds so collision X maps to visual Z
    if swap_xz:
        swapped_bounds = []
        for bounds in scaled_bounds:
            swapped_bounds.append({
                'min': [bounds['min'][2], bounds['min'][1], bounds['min'][0]],  # Swap X and Z
                'max': [bounds['max'][2], bounds['max'][1], bounds['max'][0]]
            })
        scaled_bounds = swapped_bounds

    chunks = [[] for _ in chunk_bounds_list]

    # Margin for matching (in collision units - game units)
    margin = 100.0  # About 1.5 Blender meters * 64

    for tri in triangles:
        # Get raw centroid in collision coordinates (no transform needed)
        centroid = get_triangle_centroid(tri)

        # Find which scaled chunk bounds contain this centroid
        assigned = False
        for i, bounds in enumerate(scaled_bounds):
            if point_in_bounds(centroid, bounds, margin):
                chunks[i].append(tri)
                assigned = True
                break

        if not assigned:
            # Find nearest chunk (by distance to center of scaled bounds)
            min_dist = float('inf')
            best_chunk = 0
            for i, bounds in enumerate(scaled_bounds):
                center = (
                    (bounds['min'][0] + bounds['max'][0]) / 2,
                    (bounds['min'][1] + bounds['max'][1]) / 2,
                    (bounds['min'][2] + bounds['max'][2]) / 2
                )
                dist = ((centroid[0] - center[0])**2 +
                        (centroid[1] - center[1])**2 +
                        (centroid[2] - center[2])**2)
                if dist < min_dist:
                    min_dist = dist
                    best_chunk = i
            chunks[best_chunk].append(tri)

    return chunks


def write_chunk_file(filepath, model_name, chunk_idx, triangles):
    """Write a collision chunk file."""
    chunk_name = f"{model_name}_chunk{chunk_idx}"
    array_name = f"{chunk_name}_collision_triangles"
    mesh_name = f"{chunk_name}_collision"

    # Format triangles
    tri_lines = []
    for tri in triangles:
        tri_lines.append(f"    {{ {tri[0]:.1f}f, {tri[1]:.1f}f, {tri[2]:.1f}f,   {tri[3]:.1f}f, {tri[4]:.1f}f, {tri[5]:.1f}f,   {tri[6]:.1f}f, {tri[7]:.1f}f, {tri[8]:.1f}f }},")

    content = f'''// N64 Collision Data - Auto-chunked by chunk_collision.py
// Original Model: {model_name}
// Chunk: {chunk_idx}
// Triangles: {len(triangles)}

#ifndef {chunk_name.upper()}_COLLISION_H
#define {chunk_name.upper()}_COLLISION_H

#include "../collision.h"

static CollisionTriangle {array_name}[] = {{
{chr(10).join(tri_lines)}
}};

static CollisionMesh {mesh_name} = {{
    .triangles = {array_name},
    .count = {len(triangles)},
}};

#endif // {chunk_name.upper()}_COLLISION_H
'''

    with open(filepath, 'w') as f:
        f.write(content)

    return chunk_name


def clean_old_collision_chunks(model_name):
    """Remove old collision chunk files for a model."""
    pattern = os.path.join(MODELS_DIR, f"{model_name}_chunk*_collision.h")
    for f in glob.glob(pattern):
        os.remove(f)

    # Also remove old .col files from assets
    pattern = os.path.join("assets", f"{model_name}_chunk*.col")
    for f in glob.glob(pattern):
        os.remove(f)


def main():
    print("Collision Chunker (using visual chunk bounds)")
    print("=" * 50)

    # Load visual chunk bounds
    all_chunk_bounds = load_chunk_bounds()
    if not all_chunk_bounds:
        print("No chunk bounds available. Skipping collision chunking.")
        return

    # Find all collision files
    pattern = os.path.join(MODELS_DIR, "*_collision.h")
    collision_files = sorted(glob.glob(pattern))

    chunked_models = []

    for filepath in collision_files:
        filename = os.path.basename(filepath)

        # Skip already-chunked files
        if '_chunk' in filename:
            continue

        data = parse_collision_file(filepath)

        if not data['triangles']:
            continue

        model_name = data['model_name'] or filename.replace('_collision.h', '')

        # Skip excluded models (decorations that use full rotation)
        if model_name in EXCLUDE_FROM_CHUNKING:
            continue

        # Check if we have visual chunk bounds for this model
        if model_name not in all_chunk_bounds:
            continue

        chunk_bounds_list = all_chunk_bounds[model_name]

        # Skip if not chunked (single bounds entry means no chunking)
        if len(chunk_bounds_list) <= 1:
            continue

        print(f"\nChunking {model_name}: {len(data['triangles'])} triangles -> {len(chunk_bounds_list)} chunks")

        # Clean old chunk files
        clean_old_collision_chunks(model_name)

        # Debug: print bounds comparison
        collision_bounds = compute_collision_bounds(data['triangles'])
        visual_bounds = compute_visual_bounds(chunk_bounds_list)
        print(f"  Collision bounds: X[{collision_bounds[0][0]:.1f}, {collision_bounds[1][0]:.1f}] "
              f"Y[{collision_bounds[0][1]:.1f}, {collision_bounds[1][1]:.1f}] "
              f"Z[{collision_bounds[0][2]:.1f}, {collision_bounds[1][2]:.1f}]")
        print(f"  Visual bounds (Blender): X[{visual_bounds[0][0]:.1f}, {visual_bounds[1][0]:.1f}] "
              f"Y[{visual_bounds[0][1]:.1f}, {visual_bounds[1][1]:.1f}] "
              f"Z[{visual_bounds[0][2]:.1f}, {visual_bounds[1][2]:.1f}]")
        print(f"  Visual bounds x64: X[{visual_bounds[0][0]*64:.1f}, {visual_bounds[1][0]*64:.1f}] "
              f"Y[{visual_bounds[0][1]*64:.1f}, {visual_bounds[1][1]*64:.1f}] "
              f"Z[{visual_bounds[0][2]*64:.1f}, {visual_bounds[1][2]*64:.1f}]")

        # Detect if axes need swapping (collision X long but visual Z long)
        swap_xz = detect_axis_swap(collision_bounds, visual_bounds)
        if swap_xz:
            print(f"  AXIS SWAP DETECTED: Collision X <-> Visual Z")

        # Use direct assignment with 64x scaled bounds
        print(f"  Using direct assignment with COLLISION_SCALE_FACTOR={COLLISION_SCALE_FACTOR}, swap_xz={swap_xz}")
        chunks = assign_triangles_to_chunks_direct(data['triangles'], chunk_bounds_list, swap_xz)

        # Write chunk files
        chunk_names = []
        total_assigned = 0
        for i, chunk_tris in enumerate(chunks):
            if not chunk_tris:
                print(f"  WARNING: Chunk {i} has no collision triangles!")
                # Still create empty chunk to maintain index alignment
                chunk_tris = []

            chunk_filepath = os.path.join(MODELS_DIR, f"{model_name}_chunk{i}_collision.h")
            chunk_name = write_chunk_file(chunk_filepath, model_name, i, chunk_tris)
            chunk_names.append(chunk_name)
            total_assigned += len(chunk_tris)

            bounds = chunk_bounds_list[i]
            # Print bounds in collision coordinates (scaled by 64)
            scaled_minX = bounds['min'][0] * COLLISION_SCALE_FACTOR
            scaled_maxX = bounds['max'][0] * COLLISION_SCALE_FACTOR
            scaled_minZ = bounds['min'][2] * COLLISION_SCALE_FACTOR
            scaled_maxZ = bounds['max'][2] * COLLISION_SCALE_FACTOR
            print(f"  -> {chunk_name}: {len(chunk_tris)} triangles, collision bounds X[{scaled_minX:.0f},{scaled_maxX:.0f}] Z[{scaled_minZ:.0f},{scaled_maxZ:.0f}]")

        if total_assigned != len(data['triangles']):
            print(f"  WARNING: Triangle count mismatch! Original: {len(data['triangles'])}, Assigned: {total_assigned}")

        chunked_models.append({
            'name': model_name,
            'chunks': chunk_names,
            'original_count': len(data['triangles']),
        })

        # Rename original file to .original (keep it for reference)
        backup_path = filepath + '.original'
        if not os.path.exists(backup_path):
            os.rename(filepath, backup_path)
            print(f"  -> Original backed up to {os.path.basename(backup_path)}")

    if chunked_models:
        print(f"\n{'=' * 50}")
        print(f"Chunked {len(chunked_models)} collision mesh(es)")
        for m in chunked_models:
            print(f"  {m['name']}: {m['original_count']} tris -> {len(m['chunks'])} chunks")
    else:
        print("\nNo collision meshes needed chunking")


if __name__ == "__main__":
    main()
