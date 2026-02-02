#!/usr/bin/env python3
"""
Generate collision binary (.col) directly from GLB files.
Uses pygltflib to parse GLB and extract mesh triangles.

Binary format (.col):
  - Header: 4 bytes magic "COL1"
  - 4 bytes: triangle count (uint32 big-endian for N64)
  - N * 36 bytes: triangles (9 floats each, big-endian for N64)
"""

import struct
import sys
import os

try:
    from pygltflib import GLTF2
    import numpy as np
except ImportError:
    print("ERROR: Please install required packages:")
    print("  pip3 install pygltflib numpy")
    sys.exit(1)


def get_node_transform(gltf, node_index):
    """Get the world transform matrix for a node."""
    node = gltf.nodes[node_index]

    # Start with identity matrix
    local_matrix = np.eye(4)

    # If node has a matrix, use it directly
    if node.matrix is not None:
        local_matrix = np.array(node.matrix).reshape(4, 4).T  # GLB uses column-major
    else:
        # Build from TRS
        if node.translation is not None:
            t = node.translation
            local_matrix[:3, 3] = t
        if node.rotation is not None:
            # Quaternion to matrix
            q = node.rotation  # [x, y, z, w]
            x, y, z, w = q
            rot = np.array([
                [1 - 2*(y*y + z*z), 2*(x*y - z*w), 2*(x*z + y*w), 0],
                [2*(x*y + z*w), 1 - 2*(x*x + z*z), 2*(y*z - x*w), 0],
                [2*(x*z - y*w), 2*(y*z + x*w), 1 - 2*(x*x + y*y), 0],
                [0, 0, 0, 1]
            ])
            local_matrix = local_matrix @ rot
        if node.scale is not None:
            s = node.scale
            scale_mat = np.diag([s[0], s[1], s[2], 1])
            local_matrix = local_matrix @ scale_mat

    return local_matrix


def extract_triangles_from_glb(glb_path, scale=64.0):
    """Extract all triangles from a GLB file.

    Args:
        glb_path: Path to the .glb file
        scale: Scale factor (N64 uses 64x scale)

    Returns:
        List of triangles, each being 9 floats (x0,y0,z0, x1,y1,z1, x2,y2,z2)
    """
    gltf = GLTF2().load(glb_path)
    triangles = []

    # Build node to mesh mapping and collect node transforms
    node_transforms = {}
    for i, node in enumerate(gltf.nodes):
        if node.mesh is not None:
            transform = get_node_transform(gltf, i)
            if node.mesh not in node_transforms:
                node_transforms[node.mesh] = []
            node_transforms[node.mesh].append(transform)

    for mesh_idx, mesh in enumerate(gltf.meshes):
        # Skip material library meshes (Fast64 adds these)
        mesh_name = mesh.name if mesh.name else ""
        if "material_library" in mesh_name.lower() or "_library" in mesh_name.lower():
            continue

        # Get transforms for this mesh (or identity if not found)
        transforms = node_transforms.get(mesh_idx, [np.eye(4)])

        for primitive in mesh.primitives:
            if primitive.mode is not None and primitive.mode != 4:
                # Mode 4 = TRIANGLES, skip non-triangle primitives
                continue

            # Get position accessor
            pos_accessor_idx = primitive.attributes.POSITION
            if pos_accessor_idx is None:
                continue

            pos_accessor = gltf.accessors[pos_accessor_idx]
            pos_buffer_view = gltf.bufferViews[pos_accessor.bufferView]
            pos_buffer = gltf.buffers[pos_buffer_view.buffer]

            # Get position data
            pos_data = gltf.get_data_from_buffer_uri(pos_buffer.uri) if pos_buffer.uri else gltf.binary_blob()
            if pos_data is None:
                pos_data = gltf.binary_blob()

            pos_offset = pos_buffer_view.byteOffset or 0
            pos_offset += pos_accessor.byteOffset or 0

            # Parse positions and apply transforms
            positions = []
            for i in range(pos_accessor.count):
                offset = pos_offset + i * 12  # 3 floats * 4 bytes
                x, y, z = struct.unpack('<fff', pos_data[offset:offset+12])

                # Apply each node transform that uses this mesh
                for transform in transforms:
                    # Transform point
                    point = np.array([x, y, z, 1.0])
                    transformed = transform @ point

                    # Apply scale (GLB already has Y-up coords from Blender export)
                    positions.append((
                        transformed[0] * scale,
                        transformed[1] * scale,
                        transformed[2] * scale
                    ))
                    break  # Only use first transform for now

            # Get indices
            if primitive.indices is not None:
                idx_accessor = gltf.accessors[primitive.indices]
                idx_buffer_view = gltf.bufferViews[idx_accessor.bufferView]
                idx_buffer = gltf.buffers[idx_buffer_view.buffer]

                idx_data = gltf.get_data_from_buffer_uri(idx_buffer.uri) if idx_buffer.uri else gltf.binary_blob()
                if idx_data is None:
                    idx_data = gltf.binary_blob()

                idx_offset = idx_buffer_view.byteOffset or 0
                idx_offset += idx_accessor.byteOffset or 0

                # Determine index type
                if idx_accessor.componentType == 5123:  # UNSIGNED_SHORT
                    idx_fmt = '<H'
                    idx_size = 2
                elif idx_accessor.componentType == 5125:  # UNSIGNED_INT
                    idx_fmt = '<I'
                    idx_size = 4
                else:
                    idx_fmt = '<B'
                    idx_size = 1

                # Parse indices and create triangles
                indices = []
                for i in range(idx_accessor.count):
                    offset = idx_offset + i * idx_size
                    idx = struct.unpack(idx_fmt, idx_data[offset:offset+idx_size])[0]
                    indices.append(idx)

                # Create triangles from indexed vertices
                for i in range(0, len(indices), 3):
                    if i + 2 < len(indices):
                        p0 = positions[indices[i]]
                        p1 = positions[indices[i+1]]
                        p2 = positions[indices[i+2]]
                        triangles.append(p0 + p1 + p2)
            else:
                # Non-indexed geometry
                for i in range(0, len(positions), 3):
                    if i + 2 < len(positions):
                        p0 = positions[i]
                        p1 = positions[i+1]
                        p2 = positions[i+2]
                        triangles.append(p0 + p1 + p2)

    return triangles


def write_collision_binary(filepath, triangles):
    """Write triangles to binary .col file (big-endian for N64)."""
    with open(filepath, 'wb') as f:
        # Magic header
        f.write(b'COL1')
        # Triangle count (big-endian for N64)
        f.write(struct.pack('>I', len(triangles)))
        # Triangles (9 floats each, big-endian)
        for tri in triangles:
            for val in tri:
                f.write(struct.pack('>f', val))


def main():
    if len(sys.argv) < 2:
        print("Usage: glb_to_collision.py <input.glb> [output.col]")
        print("       glb_to_collision.py --batch file1.glb file2.glb ...")
        sys.exit(1)

    if sys.argv[1] == '--batch':
        # Batch mode: process multiple files
        for glb_path in sys.argv[2:]:
            if not os.path.exists(glb_path):
                print(f"ERROR: {glb_path} not found")
                continue

            # Output to assets directory
            base_name = os.path.splitext(os.path.basename(glb_path))[0]
            output_path = os.path.join('assets', f'{base_name}.col')

            triangles = extract_triangles_from_glb(glb_path)
            write_collision_binary(output_path, triangles)
            print(f"  {base_name}: {len(triangles)} triangles")
    else:
        # Single file mode
        glb_path = sys.argv[1]
        if not os.path.exists(glb_path):
            print(f"ERROR: {glb_path} not found")
            sys.exit(1)

        if len(sys.argv) >= 3:
            output_path = sys.argv[2]
        else:
            base_name = os.path.splitext(os.path.basename(glb_path))[0]
            output_path = os.path.join('assets', f'{base_name}.col')

        triangles = extract_triangles_from_glb(glb_path)
        write_collision_binary(output_path, triangles)
        print(f"Generated {output_path}: {len(triangles)} triangles")


if __name__ == '__main__':
    main()
