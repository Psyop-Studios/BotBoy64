#!/usr/bin/env python3
"""
Chunks large GLB models into smaller pieces for better frustum culling.
Run this BEFORE T3D model conversion in the build process.
Chunks are split spatially to match collision chunks.

Also automatically updates src/levels/level*.h files with the correct segments,
and creates new level headers from template if they don't exist.

No external dependencies required - pure Python GLB handling.
"""

import os
import sys
import json
import struct
import math
import glob
import re

MAPS_DIR = "maps"     # Source folder for map GLB files
ASSETS_DIR = "assets" # Output folder for chunks (and small maps)
LEVELS_DIR = "src/levels"  # Where level header files are
CHUNK_THRESHOLD = 500  # Split models with more triangles than this
MAX_TRIS_PER_CHUNK = 300  # Target triangles per chunk (balance between culling and memory)
CHUNK_BOUNDS_FILE = "build/chunk_bounds.json"  # Shared with collision chunker


def discover_level_maps():
    """Auto-discover all level*.glb files in the maps folder."""
    if not os.path.exists(MAPS_DIR):
        print(f"Maps folder '{MAPS_DIR}' not found!")
        return []

    # Only match level*.glb files (level1.glb, level2.glb, etc.)
    map_files = glob.glob(os.path.join(MAPS_DIR, "level*.glb"))
    # Return tuples of (model_name, level_number)
    results = []
    for f in map_files:
        name = os.path.splitext(os.path.basename(f))[0]
        # Extract level number from name (e.g., "level1" -> 1)
        match = re.match(r'level(\d+)', name)
        if match:
            results.append((name, int(match.group(1))))

    # Sort by level number
    results.sort(key=lambda x: x[1])
    return results

def read_glb(filepath):
    """Read a GLB file and return JSON and binary data."""
    with open(filepath, 'rb') as f:
        # GLB header
        magic = f.read(4)
        if magic != b'glTF':
            raise ValueError(f"Not a valid GLB file: {filepath}")

        version, length = struct.unpack('<II', f.read(8))

        # Read chunks
        json_data = None
        bin_data = None

        while f.tell() < length:
            chunk_length, chunk_type = struct.unpack('<II', f.read(8))
            chunk_data = f.read(chunk_length)

            if chunk_type == 0x4E4F534A:  # JSON
                json_data = json.loads(chunk_data.decode('utf-8'))
            elif chunk_type == 0x004E4942:  # BIN
                bin_data = chunk_data

        return json_data, bin_data

def write_glb(filepath, gltf_json, bin_data):
    """Write a GLB file from JSON and binary data."""
    json_str = json.dumps(gltf_json, separators=(',', ':')).encode('utf-8')

    # Pad JSON to 4-byte boundary
    while len(json_str) % 4 != 0:
        json_str += b' '

    # Pad binary to 4-byte boundary
    bin_padded = bin_data
    while len(bin_padded) % 4 != 0:
        bin_padded += b'\x00'

    # Calculate total length
    total_length = 12 + 8 + len(json_str) + 8 + len(bin_padded)

    with open(filepath, 'wb') as f:
        # Header
        f.write(b'glTF')
        f.write(struct.pack('<II', 2, total_length))

        # JSON chunk
        f.write(struct.pack('<II', len(json_str), 0x4E4F534A))
        f.write(json_str)

        # Binary chunk
        f.write(struct.pack('<II', len(bin_padded), 0x004E4942))
        f.write(bin_padded)

def get_accessor_data(gltf, bin_data, accessor_idx, normalize_colors=False):
    """Extract data from a GLTF accessor.

    If normalize_colors=True, normalizes integer color values to 0-1 float range.
    """
    accessor = gltf['accessors'][accessor_idx]
    buffer_view = gltf['bufferViews'][accessor['bufferView']]

    offset = buffer_view.get('byteOffset', 0) + accessor.get('byteOffset', 0)

    # Component type sizes and normalization divisors
    type_info = {
        5120: ('b', 1, 127.0),     # BYTE -> max 127
        5121: ('B', 1, 255.0),     # UNSIGNED_BYTE -> max 255
        5122: ('h', 2, 32767.0),   # SHORT -> max 32767
        5123: ('H', 2, 65535.0),   # UNSIGNED_SHORT -> max 65535
        5125: ('I', 4, 1.0),       # UNSIGNED_INT (rare for colors)
        5126: ('f', 4, 1.0),       # FLOAT (already normalized)
    }

    fmt, size, divisor = type_info.get(accessor['componentType'], ('f', 4, 1.0))

    # Component counts
    type_counts = {
        'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16,
    }
    components = type_counts.get(accessor['type'], 1)

    stride = buffer_view.get('byteStride', size * components)
    result = []

    for i in range(accessor['count']):
        item_offset = offset + i * stride
        values = []
        for c in range(components):
            val = struct.unpack_from(fmt, bin_data, item_offset + c * size)[0]
            # Normalize if requested and not already float
            if normalize_colors and divisor != 1.0:
                val = val / divisor
            values.append(val)
        result.append(tuple(values) if components > 1 else values[0])

    return result

def quat_to_matrix(q):
    """Convert quaternion [x, y, z, w] to 3x3 rotation matrix."""
    x, y, z, w = q
    return [
        [1 - 2*y*y - 2*z*z, 2*x*y - 2*z*w, 2*x*z + 2*y*w],
        [2*x*y + 2*z*w, 1 - 2*x*x - 2*z*z, 2*y*z - 2*x*w],
        [2*x*z - 2*y*w, 2*y*z + 2*x*w, 1 - 2*x*x - 2*y*y],
    ]

def transform_point(pos, translation, rotation, scale):
    """Transform a point by scale, rotation, then translation."""
    # Apply scale
    x, y, z = pos[0] * scale[0], pos[1] * scale[1], pos[2] * scale[2]

    # Apply rotation (quaternion)
    if rotation != [0, 0, 0, 1]:
        mat = quat_to_matrix(rotation)
        rx = mat[0][0]*x + mat[0][1]*y + mat[0][2]*z
        ry = mat[1][0]*x + mat[1][1]*y + mat[1][2]*z
        rz = mat[2][0]*x + mat[2][1]*y + mat[2][2]*z
        x, y, z = rx, ry, rz

    # Apply translation
    return (x + translation[0], y + translation[1], z + translation[2])

def transform_normal(norm, rotation):
    """Transform a normal by rotation only."""
    if rotation == [0, 0, 0, 1]:
        return norm
    mat = quat_to_matrix(rotation)
    x, y, z = norm
    return (
        mat[0][0]*x + mat[0][1]*y + mat[0][2]*z,
        mat[1][0]*x + mat[1][1]*y + mat[1][2]*z,
        mat[2][0]*x + mat[2][1]*y + mat[2][2]*z,
    )

def extract_triangles(gltf, bin_data):
    """Extract all triangles from meshes in the active scene, applying node transforms."""
    triangles = []

    # Get active scene (default to scene 0)
    active_scene_idx = gltf.get('scene', 0)
    scenes = gltf.get('scenes', [])
    if active_scene_idx < len(scenes):
        active_scene_nodes = set(scenes[active_scene_idx].get('nodes', []))
    else:
        active_scene_nodes = set(range(len(gltf.get('nodes', []))))

    # Build mesh index -> node transform mapping (only for active scene nodes)
    mesh_transforms = {}
    meshes_in_scene = set()
    for node_idx, node in enumerate(gltf.get('nodes', [])):
        if node_idx not in active_scene_nodes:
            continue  # Skip nodes not in active scene
        if 'mesh' in node:
            mesh_idx = node['mesh']
            meshes_in_scene.add(mesh_idx)
            translation = node.get('translation', [0, 0, 0])
            rotation = node.get('rotation', [0, 0, 0, 1])
            scale = node.get('scale', [1, 1, 1])
            mesh_transforms[mesh_idx] = (translation, rotation, scale)

    for mesh_idx, mesh in enumerate(gltf.get('meshes', [])):
        # Skip meshes not in the active scene
        if mesh_idx not in meshes_in_scene:
            continue
        # Get transform for this mesh (default to identity)
        translation, rotation, scale = mesh_transforms.get(mesh_idx, ([0,0,0], [0,0,0,1], [1,1,1]))

        for prim in mesh.get('primitives', []):
            mode = prim.get('mode', 4)
            if mode != 4:  # Only triangles
                continue

            attrs = prim['attributes']

            # Get positions
            positions = get_accessor_data(gltf, bin_data, attrs['POSITION'])

            # Get indices
            if 'indices' in prim:
                indices = get_accessor_data(gltf, bin_data, prim['indices'])
            else:
                indices = list(range(len(positions)))

            # Get optional data
            normals = None
            uvs = None
            colors = None

            if 'NORMAL' in attrs:
                normals = get_accessor_data(gltf, bin_data, attrs['NORMAL'])
            if 'TEXCOORD_0' in attrs:
                uvs = get_accessor_data(gltf, bin_data, attrs['TEXCOORD_0'])
            if 'COLOR_0' in attrs:
                colors = get_accessor_data(gltf, bin_data, attrs['COLOR_0'], normalize_colors=True)

            material = prim.get('material')

            # Build triangles with transformed positions
            for i in range(0, len(indices), 3):
                if i + 2 >= len(indices):
                    break

                i0, i1, i2 = indices[i], indices[i+1], indices[i+2]

                # Transform positions to world space
                p0 = transform_point(positions[i0], translation, rotation, scale)
                p1 = transform_point(positions[i1], translation, rotation, scale)
                p2 = transform_point(positions[i2], translation, rotation, scale)

                # Transform normals (rotation only)
                n0 = transform_normal(normals[i0], rotation) if normals else None
                n1 = transform_normal(normals[i1], rotation) if normals else None
                n2 = transform_normal(normals[i2], rotation) if normals else None

                tri = {
                    'positions': [p0, p1, p2],
                    'normals': [n0, n1, n2] if normals else None,
                    'uvs': [uvs[i0], uvs[i1], uvs[i2]] if uvs else None,
                    'colors': [colors[i0], colors[i1], colors[i2]] if colors else None,
                    'material': material,
                }
                triangles.append(tri)

    return triangles


def offset_triangles(triangles, offset_x, offset_z=0):
    """Shift all triangle positions by the given X/Z offset."""
    for tri in triangles:
        new_positions = []
        for pos in tri['positions']:
            new_positions.append((pos[0] + offset_x, pos[1], pos[2] + offset_z))
        tri['positions'] = new_positions
    return triangles


def get_triangle_centroid(tri):
    """Get the 3D centroid of a triangle."""
    p0, p1, p2 = tri['positions']
    return (
        (p0[0] + p1[0] + p2[0]) / 3.0,
        (p0[1] + p1[1] + p2[1]) / 3.0,
        (p0[2] + p1[2] + p2[2]) / 3.0
    )

def get_triangle_centroid_x(tri):
    """Get the X centroid of a triangle (for simple 1D chunking)."""
    p0, p1, p2 = tri['positions']
    return (p0[0] + p1[0] + p2[0]) / 3.0

def compute_bounds(triangles):
    """Compute the bounding box of a set of triangles."""
    if not triangles:
        return (0, 0, 0), (0, 0, 0)

    min_x = min_y = min_z = float('inf')
    max_x = max_y = max_z = float('-inf')

    for tri in triangles:
        for pos in tri['positions']:
            min_x = min(min_x, pos[0])
            min_y = min(min_y, pos[1])
            min_z = min(min_z, pos[2])
            max_x = max(max_x, pos[0])
            max_y = max(max_y, pos[1])
            max_z = max(max_z, pos[2])

    return (min_x, min_y, min_z), (max_x, max_y, max_z)

def chunk_triangles_spatial(triangles, max_per_chunk, split_history=None):
    """Split triangles into spatially coherent chunks using recursive subdivision.

    This creates chunks that are spatially grouped together, which is better for
    frustum culling than simple X-axis sorting.

    Returns list of (triangles, split_history) tuples where split_history records
    the sequence of splits used to create each chunk.
    """
    if split_history is None:
        split_history = []

    if len(triangles) <= max_per_chunk:
        return [(triangles, split_history)]

    # Compute bounds
    (min_x, min_y, min_z), (max_x, max_y, max_z) = compute_bounds(triangles)

    # Find the longest axis to split on
    dx = max_x - min_x
    dy = max_y - min_y
    dz = max_z - min_z

    # Choose split axis (longest dimension)
    if dx >= dy and dx >= dz:
        axis = 0  # X
        split_val = (min_x + max_x) / 2.0
    elif dy >= dx and dy >= dz:
        axis = 1  # Y
        split_val = (min_y + max_y) / 2.0
    else:
        axis = 2  # Z
        split_val = (min_z + max_z) / 2.0

    # Split triangles based on centroid position
    left = []
    right = []

    for tri in triangles:
        centroid = get_triangle_centroid(tri)
        if centroid[axis] < split_val:
            left.append(tri)
        else:
            right.append(tri)

    # Handle degenerate cases (all triangles on one side)
    if len(left) == 0:
        left = right[:len(right)//2]
        right = right[len(right)//2:]
    elif len(right) == 0:
        right = left[len(left)//2:]
        left = left[:len(left)//2]

    # Record this split for later use by collision chunker
    split_info = {'axis': axis, 'value': split_val}

    # Recursively chunk each half
    chunks = []
    chunks.extend(chunk_triangles_spatial(left, max_per_chunk, split_history + [('left', split_info)]))
    chunks.extend(chunk_triangles_spatial(right, max_per_chunk, split_history + [('right', split_info)]))

    return chunks

def chunk_triangles(triangles, max_per_chunk):
    """Split triangles into spatially coherent chunks.

    Uses 3D spatial subdivision for better frustum culling performance.
    Returns list of triangle lists (without split history for backwards compat).
    """
    if len(triangles) <= max_per_chunk:
        return [triangles]

    results = chunk_triangles_spatial(triangles, max_per_chunk)
    return [tris for tris, _ in results]

def chunk_triangles_with_bounds(triangles, max_per_chunk):
    """Split triangles and return chunks with their 3D bounds.

    Returns list of dicts: {'triangles': [...], 'bounds': {'min': [x,y,z], 'max': [x,y,z]}}
    """
    if len(triangles) <= max_per_chunk:
        bounds = compute_bounds(triangles)
        return [{
            'triangles': triangles,
            'bounds': {
                'min': list(bounds[0]),
                'max': list(bounds[1])
            }
        }]

    results = chunk_triangles_spatial(triangles, max_per_chunk)
    chunks_with_bounds = []
    for tris, _ in results:
        bounds = compute_bounds(tris)
        chunks_with_bounds.append({
            'triangles': tris,
            'bounds': {
                'min': list(bounds[0]),
                'max': list(bounds[1])
            }
        })
    return chunks_with_bounds

def create_chunk_glb(triangles, original_gltf, output_path, model_name, chunk_idx):
    """Create a new GLB file from a subset of triangles, preserving materials."""

    # Group triangles by material
    material_groups = {}
    for tri in triangles:
        mat_id = tri['material'] if tri['material'] is not None else -1
        if mat_id not in material_groups:
            material_groups[mat_id] = []
        material_groups[mat_id].append(tri)

    # Collect unique vertices (shared across all primitives)
    vertices = []
    vertex_map = {}

    # Check if ANY triangles have these attributes
    has_normals = any(tri['normals'] is not None for tri in triangles) if triangles else False
    has_uvs = any(tri['uvs'] is not None for tri in triangles) if triangles else False
    has_colors = any(tri['colors'] is not None for tri in triangles) if triangles else False

    # Build indices per material group
    material_indices = {}  # mat_id -> list of indices

    for mat_id, mat_tris in material_groups.items():
        material_indices[mat_id] = []
        for tri in mat_tris:
            for i in range(3):
                pos = tri['positions'][i]
                norm = tri['normals'][i] if tri['normals'] else (0, 0, 1)
                uv = tri['uvs'][i] if tri['uvs'] else (0, 0)
                col = tri['colors'][i] if tri['colors'] else None

                key = (pos, norm, uv, str(col))

                if key not in vertex_map:
                    vertex_map[key] = len(vertices)
                    vertices.append({
                        'position': pos,
                        'normal': norm,
                        'uv': uv,
                        'color': col,
                    })

                material_indices[mat_id].append(vertex_map[key])

    # Build binary buffer
    buffer_data = bytearray()
    buffer_views = []
    accessors = []

    # Positions
    pos_offset = len(buffer_data)
    pos_min = [float('inf')] * 3
    pos_max = [float('-inf')] * 3
    for v in vertices:
        for i, val in enumerate(v['position']):
            buffer_data.extend(struct.pack('f', val))
            pos_min[i] = min(pos_min[i], val)
            pos_max[i] = max(pos_max[i], val)

    buffer_views.append({
        'buffer': 0,
        'byteOffset': pos_offset,
        'byteLength': len(vertices) * 12,
        'target': 34962,  # ARRAY_BUFFER
    })
    accessors.append({
        'bufferView': 0,
        'componentType': 5126,  # FLOAT
        'count': len(vertices),
        'type': 'VEC3',
        'min': pos_min,
        'max': pos_max,
    })

    # Normals
    if has_normals:
        norm_offset = len(buffer_data)
        for v in vertices:
            for val in v['normal']:
                buffer_data.extend(struct.pack('f', val))

        buffer_views.append({
            'buffer': 0,
            'byteOffset': norm_offset,
            'byteLength': len(vertices) * 12,
            'target': 34962,
        })
        accessors.append({
            'bufferView': len(buffer_views) - 1,
            'componentType': 5126,
            'count': len(vertices),
            'type': 'VEC3',
        })

    # UVs
    if has_uvs:
        uv_offset = len(buffer_data)
        for v in vertices:
            for val in v['uv']:
                buffer_data.extend(struct.pack('f', val))

        buffer_views.append({
            'buffer': 0,
            'byteOffset': uv_offset,
            'byteLength': len(vertices) * 8,
            'target': 34962,
        })
        accessors.append({
            'bufferView': len(buffer_views) - 1,
            'componentType': 5126,
            'count': len(vertices),
            'type': 'VEC2',
        })

    # Colors
    if has_colors:
        color_offset = len(buffer_data)
        for v in vertices:
            col = v['color']
            if col:
                # Colors are already normalized to 0-1 during extraction
                for i in range(min(4, len(col))):
                    buffer_data.extend(struct.pack('f', float(col[i])))
                # Pad to 4 components if needed
                for _ in range(4 - min(4, len(col))):
                    buffer_data.extend(struct.pack('f', 1.0))
            else:
                # Default white color for vertices without color data
                buffer_data.extend(struct.pack('ffff', 1.0, 1.0, 1.0, 1.0))

        buffer_views.append({
            'buffer': 0,
            'byteOffset': color_offset,
            'byteLength': len(vertices) * 16,
            'target': 34962,
        })
        accessors.append({
            'bufferView': len(buffer_views) - 1,
            'componentType': 5126,
            'count': len(vertices),
            'type': 'VEC4',
        })

    # Build primitive attributes (shared across all primitives)
    attributes = {'POSITION': 0}
    accessor_idx = 1
    if has_normals:
        attributes['NORMAL'] = accessor_idx
        accessor_idx += 1
    if has_uvs:
        attributes['TEXCOORD_0'] = accessor_idx
        accessor_idx += 1
    if has_colors:
        attributes['COLOR_0'] = accessor_idx
        accessor_idx += 1

    # Write index buffers for each material group and build primitives
    primitives = []
    for mat_id in sorted(material_indices.keys()):
        indices = material_indices[mat_id]
        if not indices:
            continue

        # Pad to 4-byte alignment before writing indices
        while len(buffer_data) % 4 != 0:
            buffer_data.append(0)

        idx_offset = len(buffer_data)
        for idx in indices:
            buffer_data.extend(struct.pack('H', idx))

        # Pad indices to 4-byte alignment
        while len(buffer_data) % 4 != 0:
            buffer_data.append(0)

        buffer_views.append({
            'buffer': 0,
            'byteOffset': idx_offset,
            'byteLength': len(indices) * 2,
            'target': 34963,  # ELEMENT_ARRAY_BUFFER
        })
        accessors.append({
            'bufferView': len(buffer_views) - 1,
            'componentType': 5123,  # UNSIGNED_SHORT
            'count': len(indices),
            'type': 'SCALAR',
        })

        # Create primitive for this material
        prim = {
            'attributes': attributes,
            'indices': len(accessors) - 1,
            'mode': 4,  # TRIANGLES
        }
        if mat_id >= 0:  # Only assign material if it's a valid index
            prim['material'] = mat_id
        primitives.append(prim)

    # Build GLTF JSON
    # Use model_name directly if chunk_idx is -1 (non-chunked), otherwise add chunk suffix
    if chunk_idx >= 0:
        mesh_name = f'{model_name}_chunk{chunk_idx}'
    else:
        mesh_name = model_name

    gltf_json = {
        'asset': {'version': '2.0', 'generator': 'chunk_glb.py'},
        'buffers': [{'byteLength': len(buffer_data)}],
        'bufferViews': buffer_views,
        'accessors': accessors,
        'meshes': [{
            'name': mesh_name,
            'primitives': primitives
        }],
        'nodes': [{'mesh': 0, 'name': mesh_name}],
        'scenes': [{'nodes': [0]}],
        'scene': 0,
    }

    # Copy materials if present
    if 'materials' in original_gltf:
        gltf_json['materials'] = original_gltf['materials']

    # Copy textures/samplers but NOT embedded images (they reference old buffer views)
    # T3D loads textures from sprite files by name anyway
    for key in ['textures', 'samplers']:
        if key in original_gltf:
            gltf_json[key] = original_gltf[key]

    # Only copy images that use URI (external), not bufferView (embedded)
    if 'images' in original_gltf:
        external_images = []
        for img in original_gltf['images']:
            if 'uri' in img:
                external_images.append(img)
            elif 'name' in img:
                # Keep just the name for T3D to find the sprite
                external_images.append({'name': img['name']})
        if external_images:
            gltf_json['images'] = external_images

    # Write GLB
    write_glb(output_path, gltf_json, bytes(buffer_data))

    return len(triangles)

FILESYSTEM_DIR = "filesystem"  # Where .t3dm files go

def clean_old_chunks(model_name):
    """Delete old chunk files to force rebuild."""
    # Delete old .glb chunks in assets/
    for f in glob.glob(os.path.join(ASSETS_DIR, f"{model_name}_chunk*.glb")):
        os.remove(f)
    # Delete old .t3dm chunks in filesystem/ (forces make to rebuild)
    for f in glob.glob(os.path.join(FILESYSTEM_DIR, f"{model_name}_chunk*.t3dm")):
        os.remove(f)
    # Also delete non-chunked version if it exists
    for ext in ['.glb', '.t3dm']:
        path = os.path.join(ASSETS_DIR if ext == '.glb' else FILESYSTEM_DIR, f"{model_name}{ext}")
        if os.path.exists(path):
            os.remove(path)

def process_model(model_name, all_chunk_bounds):
    """Process a single model from maps/ folder, outputting chunks to assets/.

    all_chunk_bounds: dict to store chunk bounds for collision chunker
    """
    input_path = os.path.join(MAPS_DIR, f"{model_name}.glb")

    if not os.path.exists(input_path):
        print(f"  Skipping {model_name}: file not found in {MAPS_DIR}/")
        return None

    try:
        gltf, bin_data = read_glb(input_path)
    except Exception as e:
        print(f"  Error loading {model_name}: {e}")
        return None

    triangles = extract_triangles(gltf, bin_data)
    tri_count = len(triangles)
    print(f"  {model_name}: {tri_count} triangles")

    # Clean old chunks before regenerating
    clean_old_chunks(model_name)

    if tri_count <= CHUNK_THRESHOLD:
        # Small map - still need to flatten transforms into vertices for consistency
        # This ensures the model renders in the same position whether chunked or not
        output_path = os.path.join(ASSETS_DIR, f"{model_name}.glb")
        create_chunk_glb(triangles, gltf, output_path, model_name, -1)  # -1 = no chunk suffix
        print(f"    -> Flattened to assets/ (no chunking needed)")

        # Save bounds for the whole model (no chunks)
        bounds = compute_bounds(triangles)
        all_chunk_bounds[model_name] = [{
            'min': list(bounds[0]),
            'max': list(bounds[1])
        }]

        return {'name': model_name, 'chunks': 0, 'files': [f"{model_name}.t3dm"]}

    # Use bounds-aware chunking
    chunks_with_bounds = chunk_triangles_with_bounds(triangles, MAX_TRIS_PER_CHUNK)
    print(f"    -> Splitting into {len(chunks_with_bounds)} chunks")

    chunk_files = []
    chunk_bounds_list = []
    for i, chunk_data in enumerate(chunks_with_bounds):
        chunk_tris = chunk_data['triangles']
        bounds = chunk_data['bounds']

        output_path = os.path.join(ASSETS_DIR, f"{model_name}_chunk{i}.glb")
        tri_count = create_chunk_glb(chunk_tris, gltf, output_path, model_name, i)
        chunk_files.append(f"{model_name}_chunk{i}.t3dm")
        chunk_bounds_list.append(bounds)
        print(f"    -> {model_name}_chunk{i}.glb: {tri_count} triangles, bounds X[{bounds['min'][0]:.0f},{bounds['max'][0]:.0f}] Y[{bounds['min'][1]:.0f},{bounds['max'][1]:.0f}] Z[{bounds['min'][2]:.0f},{bounds['max'][2]:.0f}]")

    # Save bounds for collision chunker
    all_chunk_bounds[model_name] = chunk_bounds_list

    return {'name': model_name, 'chunks': len(chunks_with_bounds), 'files': chunk_files}


def update_level_header(level_num, segment_files):
    """Update or create the level header file with the correct segments."""
    header_path = os.path.join(LEVELS_DIR, f"level{level_num}.h")

    # Build the segments array string
    segments_str = ""
    for f in segment_files:
        segments_str += f'        "rom:/{f}",\n'
    segments_str = segments_str.rstrip(',\n')

    if os.path.exists(header_path):
        # Update existing header - replace segments section
        with open(header_path, 'r') as f:
            content = f.read()

        # Find and replace the segments block
        # Pattern: .segments = { ... }, followed by .segmentCount = N,
        pattern = r'(\.segments\s*=\s*\{)[^}]*(},?\s*\.segmentCount\s*=\s*)\d+'
        replacement = f'\\1\n{segments_str}\n    \\g<2>{len(segment_files)}'

        new_content, count = re.subn(pattern, replacement, content, flags=re.DOTALL)

        if count > 0:
            with open(header_path, 'w') as f:
                f.write(new_content)
            print(f"    -> Updated {header_path} with {len(segment_files)} segment(s)")
        else:
            print(f"    -> WARNING: Could not find segments block in {header_path}")
    else:
        # Create new header from template
        create_level_header(level_num, segment_files)


def create_level_header(level_num, segment_files):
    """Create a new level header file from template."""
    header_path = os.path.join(LEVELS_DIR, f"level{level_num}.h")

    # Build segments string
    segments_str = ""
    for f in segment_files:
        segments_str += f'        "rom:/{f}",\n'
    segments_str = segments_str.rstrip('\n')

    template = f'''#ifndef LEVEL{level_num}_H
#define LEVEL{level_num}_H

// ============================================================
// LEVEL {level_num} - New Level
// ============================================================

static const LevelData LEVEL_{level_num}_DATA = {{
    .name = "Level {level_num}",

    // Map segments (auto-generated by chunk_glb.py)
    .segments = {{
{segments_str}
    }},
    .segmentCount = {len(segment_files)},

    // Decorations - add your decorations here
    .decorations = {{
    {{ DECO_PLAYERSPAWN, 0.0f, 50.0f, 0.0f, 0.00f, 0.10f, 0.10f, 0.10f }},
}},
.decorationCount = 1,

    // Player start position
    .playerStartX = 0.0f,
    .playerStartY = 100.0f,
    .playerStartZ = 0.0f,

    // Background music
    .music = "rom:/scrap1.wav64",

    // Body part (1 = torso, 2 = arms)
    .bodyPart = 1,
}};

#endif // LEVEL{level_num}_H
'''

    with open(header_path, 'w') as f:
        f.write(template)
    print(f"    -> Created new {header_path} from template")


def update_menu_header(segment_files):
    """Update menu.h with the correct segments for MenuScene."""
    header_path = os.path.join(LEVELS_DIR, "menu.h")

    if not os.path.exists(header_path):
        print(f"    -> WARNING: {header_path} not found, skipping update")
        return

    # Build the segments array string
    segments_str = ""
    for f in segment_files:
        segments_str += f'        "rom:/{f}",\n'
    segments_str = segments_str.rstrip(',\n')

    with open(header_path, 'r') as f:
        content = f.read()

    # Find and replace the segments block
    # Pattern: .segments = { ... }, followed by .segmentCount = N,
    pattern = r'(\.segments\s*=\s*\{)[^}]*(},?\s*\.segmentCount\s*=\s*)\d+'
    replacement = f'\\1\n{segments_str}\n    \\g<2>{len(segment_files)}'

    new_content, count = re.subn(pattern, replacement, content, flags=re.DOTALL)

    if count > 0:
        with open(header_path, 'w') as f:
            f.write(new_content)
        print(f"    -> Updated {header_path} with {len(segment_files)} segment(s)")
    else:
        print(f"    -> WARNING: Could not find segments block in {header_path}")


def process_menuscene(all_chunk_bounds):
    """Process MenuScene.glb and update menu.h."""
    input_path = os.path.join(MAPS_DIR, "MenuScene.glb")
    if not os.path.exists(input_path):
        return None

    print("\nProcessing MenuScene (menu level)...")
    result = process_model("MenuScene", all_chunk_bounds)
    if result:
        print(f"  MenuScene: {result['chunks']} chunks")
        # Update menu.h with the correct segments
        update_menu_header(result['files'])
    return result


def save_chunk_bounds(all_chunk_bounds):
    """Save chunk bounds to JSON for collision chunker."""
    os.makedirs(os.path.dirname(CHUNK_BOUNDS_FILE), exist_ok=True)
    with open(CHUNK_BOUNDS_FILE, 'w') as f:
        json.dump(all_chunk_bounds, f, indent=2)
    print(f"\nSaved chunk bounds to {CHUNK_BOUNDS_FILE}")


def main():
    print("GLB Level Chunker")
    print("=" * 40)
    print(f"Source: {MAPS_DIR}/")
    print(f"Output: {ASSETS_DIR}/")
    print(f"Headers: {LEVELS_DIR}/")
    print("")

    # Auto-discover level maps from maps/ folder
    level_maps = discover_level_maps()

    if not level_maps:
        print("No level*.glb files found in maps/ folder!")
    else:
        print(f"Found {len(level_maps)} level(s): {', '.join(m[0] for m in level_maps)}")
        print("")

    # Collect chunk bounds for all models (shared with collision chunker)
    all_chunk_bounds = {}

    processed = []
    for model_name, level_num in level_maps:
        result = process_model(model_name, all_chunk_bounds)
        if result:
            result['level_num'] = level_num
            processed.append(result)

            # Update the corresponding level header
            update_level_header(level_num, result['files'])

    # Also process MenuScene if it exists (no header update for menu)
    menu_result = process_menuscene(all_chunk_bounds)

    # Save chunk bounds for collision chunker to use
    if all_chunk_bounds:
        save_chunk_bounds(all_chunk_bounds)

    if processed:
        print(f"\nProcessed {len(processed)} level(s)")
        chunked_count = sum(1 for p in processed if p['chunks'] > 0)
        if chunked_count > 0:
            print(f"  - {chunked_count} level(s) were chunked")
    elif not menu_result:
        print("\nNo levels were processed")

if __name__ == "__main__":
    main()
