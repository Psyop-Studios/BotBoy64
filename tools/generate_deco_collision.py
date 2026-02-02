#!/usr/bin/env python3
"""
Generate collision binary (.col) files for decoration GLBs in assets/.
This ensures collision data always matches the visual models.

Run as part of codegen to keep collisions in sync with GLBs.
"""

import os
import sys
import glob

# Add tools directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from glb_to_collision import extract_triangles_from_glb, write_collision_binary

ASSETS_DIR = "assets"
SCALE = 64.0  # Match T3D/visual model scale

# GLBs that should have collision generated
# These are decorations with collision meshes
DECO_GLBS = [
    "Col_Cube",
    "barrel",
    "BlueCube",
    "bolt",
    "Bulldozer",
    "Cactus",
    # "Chargepad",  # Uses custom collision from src/models/Chargepad_collision.h
    "cog",
    "ConveyerLarge",
    "ConveyerLargeBelt",
    "ConveyerLargeFrame",
    "DECO_DAMAGECOLLISION",
    "Elevator",
    "FanBody",
    "FanBottom",
    "Ibeam",
    "JukeBox",
    "LaserWall",
    "LaserWallOff",
    "MonitorScreen",
    "PatrolPoint",
    "Platform",
    "rat",
    "Rock",
    # "RoundButtonBottom",  # Uses custom collision from src/models/RoundButtonBottom_collision.h
    # "RoundButtonTop",     # Uses custom collision from src/models/RoundButtonTop_collision.h
    "Screw",
    "Sign",
    "slime",
    "Slime_Lava",
    "spike",
    "spikes",
    "Table",
    "Toxic_Level2_Pipe",
    "Toxic_Level2_Running",
    "TransitionCollision",
    "Arms_playground",
    "Arms_playground_grab",
    "Arms_playground_hang",
    "level3_stream",
    "level3_water_level",
    # "Cliff_Hanging_Platform_L",  # Uses custom collision from assets/_collision.h
    # "Cliff_Hanging_Platform_S",  # Uses custom collision from assets/_collision.h
    "DECO_PAIN_TUBE",
    # "Chargepad",  # Duplicate - already commented out above
    "RTurret_Base",
    "RTurret_Cannon",
    "RTurret_Rail",
    "RTurret_P_Base",
    "RTurret_P_Cannon",
    "droid_sec",
    "DECO_LAVAFLOOR",
    "DECO_LAVAFALLS",
    # "DECO_GRINDER",  # Uses custom collision from src/models/DECO_GRINDER_collision.h
    "DECO_SINK_PLAT",
    "DECO_SPINNER",
    "DECO_MOVE_PLAT",
    "DECO_MOVE_COAL",
    "DECO_MOVING_ROCK",
    "DECO_FAN",
    "DECO_FLOATING_ROCK",
    "DECO_FLOATING_ROCK_2",
    "DECO_SPIN_ROCK",
    "DECO_SPIN_ROCK_2",
    "DECO_LAVA_RIVER",
    "projectile_pulse",
    # "DECO_MOVING_LASER",  # Uses custom collision from src/models/DECO_MOVING_LASER_collision.h (thin beam only, not FX glow)
    "DECO_HIVE_MOVING",
    "DECO_LVL3_STREAM",
    "DECO_SCREWG",
]


def main():
    print("Decoration Collision Generator")
    print("=" * 50)

    total_triangles = 0
    generated = 0

    for name in DECO_GLBS:
        glb_path = os.path.join(ASSETS_DIR, f"{name}.glb")
        col_path = os.path.join(ASSETS_DIR, f"{name}.col")

        if not os.path.exists(glb_path):
            continue

        triangles = extract_triangles_from_glb(glb_path, scale=SCALE)
        write_collision_binary(col_path, triangles)

        print(f"  {name}: {len(triangles)} triangles")
        total_triangles += len(triangles)
        generated += 1

    print(f"\nGenerated {generated} decoration collision files")
    print(f"Total: {total_triangles} triangles")


if __name__ == "__main__":
    main()
