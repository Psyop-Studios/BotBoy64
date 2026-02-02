# SPDX-License-Identifier: GPL-3.0-or-later

bl_info = {
    "name": "N64 Collision Exporter",
    "author": "N64 Brew Jam",
    "version": (1, 2),
    "blender": (4, 0, 0),
    "location": "File > Export > N64 Collision Data",
    "description": "Export mesh triangles as C collision data for N64/tiny3d",
    "category": "Import-Export",
}

import bpy
import bmesh
import mathutils
import os
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, FloatProperty, EnumProperty
from math import radians


class EXPORT_OT_n64_collision(bpy.types.Operator, ExportHelper):
    """Export mesh as N64 collision data"""
    bl_idname = "export_scene.n64_collision"
    bl_label = "Export N64 Collision"
    bl_options = {'PRESET'}

    filename_ext = ".h"

    filter_glob: StringProperty(
        default="*.h",
        options={'HIDDEN'},
        maxlen=255,
    )

    apply_modifiers: BoolProperty(
        name="Apply Modifiers",
        description="Apply modifiers before exporting",
        default=True,
    )

    apply_transform: BoolProperty(
        name="Apply Transform",
        description="Apply object transforms (location, rotation, scale)",
        default=True,
    )

    coord_system: EnumProperty(
        name="Coordinate System",
        description="Output coordinate system",
        items=[
            ('GLTF', "glTF Y-up (recommended)", "Convert to glTF coordinates (matches gltf_to_t3d)"),
            ('BLENDER', "Blender Z-up", "Keep Blender coordinates"),
        ],
        default='GLTF',
    )

    scale: FloatProperty(
        name="Scale",
        description="Scale factor (64.0 matches gltf_to_t3d default)",
        default=64.0,
        min=0.001,
        max=1000.0,
    )

    mesh_name: StringProperty(
        name="Variable Name",
        description="Base name for collision variables (e.g., 'testLVL' creates testLVL_collision)",
        default="",
    )

    export_selected: BoolProperty(
        name="Selected Only",
        description="Only export selected mesh objects",
        default=True,
    )

    def invoke(self, context, event):
        # Auto-suggest name from selected object
        if context.selected_objects:
            for obj in context.selected_objects:
                if obj.type == 'MESH':
                    name = obj.name.split('.')[0].replace(' ', '_')
                    self.mesh_name = name
                    self.filepath = f"{name}_collision.h"
                    break
        return super().invoke(context, event)

    def execute(self, context):
        # Get objects to export
        if self.export_selected:
            objects = [obj for obj in context.selected_objects if obj.type == 'MESH']
        else:
            objects = [obj for obj in context.scene.objects if obj.type == 'MESH']

        if not objects:
            self.report({'ERROR'}, "No mesh objects selected. Select a mesh first.")
            return {'CANCELLED'}

        # Auto-detect name from filename if not set
        if not self.mesh_name:
            basename = os.path.basename(self.filepath)
            if basename.endswith('_collision.h'):
                self.mesh_name = basename[:-12]
            else:
                self.mesh_name = basename.replace('.h', '').replace('_collision', '')

        all_triangles = []

        for obj in objects:
            # Get mesh data with modifiers applied
            if self.apply_modifiers:
                depsgraph = context.evaluated_depsgraph_get()
                obj_eval = obj.evaluated_get(depsgraph)
                mesh = obj_eval.to_mesh()
            else:
                mesh = obj.data.copy()

            # Convert to bmesh for triangulation
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bmesh.ops.triangulate(bm, faces=bm.faces[:])

            # Build transformation matrix
            if self.apply_transform:
                matrix = obj.matrix_world.copy()
            else:
                matrix = mathutils.Matrix.Identity(4)

            # Convert Blender Z-up to glTF Y-up
            if self.coord_system == 'GLTF':
                rot_matrix = mathutils.Matrix.Rotation(radians(-90), 4, 'X')
                matrix = rot_matrix @ matrix

            # Apply scale
            scale_matrix = mathutils.Matrix.Scale(self.scale, 4)
            matrix = scale_matrix @ matrix

            # Extract triangles
            for face in bm.faces:
                verts = [matrix @ v.co for v in face.verts]
                all_triangles.append((
                    (verts[0].x, verts[0].y, verts[0].z),
                    (verts[1].x, verts[1].y, verts[1].z),
                    (verts[2].x, verts[2].y, verts[2].z),
                ))

            bm.free()
            if self.apply_modifiers:
                obj_eval.to_mesh_clear()

        # Generate variable names
        var_name = self.mesh_name
        triangles_name = f"{var_name}_collision_triangles"
        mesh_var_name = f"{var_name}_collision"
        guard_name = f"{var_name.upper()}_COLLISION_H"

        # Write output file
        with open(self.filepath, 'w') as f:
            f.write(f"// N64 Collision Data - Generated by Blender Export\n")
            f.write(f"// Model: {var_name}\n")
            f.write(f"// Triangles: {len(all_triangles)}\n")
            f.write(f"\n")
            f.write(f"#ifndef {guard_name}\n")
            f.write(f"#define {guard_name}\n")
            f.write(f"\n")
            f.write(f'#include "../collision.h"\n')
            f.write(f"\n")

            if len(all_triangles) > 0:
                f.write(f"static CollisionTriangle {triangles_name}[] = {{\n")
                for tri in all_triangles:
                    v0, v1, v2 = tri
                    f.write(f"    {{ {v0[0]:.1f}f, {v0[1]:.1f}f, {v0[2]:.1f}f,   ")
                    f.write(f"{v1[0]:.1f}f, {v1[1]:.1f}f, {v1[2]:.1f}f,   ")
                    f.write(f"{v2[0]:.1f}f, {v2[1]:.1f}f, {v2[2]:.1f}f }},\n")
                f.write(f"}};\n")
            else:
                f.write(f"static CollisionTriangle {triangles_name}[] = {{\n")
                f.write(f"    {{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }},\n")
                f.write(f"}};\n")

            f.write(f"\n")
            f.write(f"static CollisionMesh {mesh_var_name} = {{\n")
            f.write(f"    .triangles = {triangles_name},\n")
            f.write(f"    .count = {len(all_triangles)}\n")
            f.write(f"}};\n")
            f.write(f"\n")
            f.write(f"#endif // {guard_name}\n")

        self.report({'INFO'}, f"Exported {len(all_triangles)} triangles to {self.filepath}")
        return {'FINISHED'}


def menu_func_export(self, context):
    self.layout.operator(EXPORT_OT_n64_collision.bl_idname, text="N64 Collision Data (.h)")


classes = (
    EXPORT_OT_n64_collision,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    # Check if already registered to avoid errors
    try:
        unregister()
    except:
        pass
    try:
        register()
    except RuntimeError as e:
        # If we can't register (readonly state), just run the export directly
        if "readonly state" in str(e):
            # Direct execution mode - invoke operator if possible
            if hasattr(bpy.ops.export_scene, 'n64_collision'):
                bpy.ops.export_scene.n64_collision('INVOKE_DEFAULT')
            else:
                print("Please install this as an addon via Edit > Preferences > Add-ons > Install")
                print("Or run Blender normally (not in background/readonly mode)")
        else:
            raise
