# -------------------------
# Paths / SDK
# -------------------------
BUILD_DIR := build
N64_INST  := $(N64_INST)

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

# -------------------------
# Project settings
# -------------------------
N64_ROM_TITLE := "N64BrewJam"
N64_ROM_SAVETYPE := eeprom4k

# -------------------------
# Sources
# -------------------------
SRCS := \
	main.c \
	src/scene.c \
	src/controls.c \
	src/save.c \
	src/collision_loader.c \
	src/player.c \
	src/particles.c \
	src/effects.c \
	src/hud.c \
	src/camera.c \
	src/countdown.c \
	src/celebrate.c \
	src/perf_graph.c \
	src/qrcodegen.c \
	src/qr_display.c \
	src/scenes/splash.c \
	src/scenes/logo_scene.c \
	src/scenes/title.c \
	src/scenes/level_select.c \
	src/scenes/game.c \
	src/scenes/pause.c \
	src/scenes/level_complete.c \
	src/scenes/cutscene_demo.c \
	src/scenes/map_test.c \
	src/scenes/debug_map.c \
	src/scenes/menu_scene.c \
	src/scenes/demo_scene.c \
	src/scenes/multiplayer.c \
	src/scriptsystem.c \

OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(SRCS:%.c=$(BUILD_DIR)/%.d)

# -------------------------
# Assets
# -------------------------
assets_png := $(wildcard assets/*.png)
assets_glb := $(wildcard assets/*.glb)
assets_xm  := $(wildcard assets/*.xm)  $(wildcard assets/*.XM)
assets_ym  := $(wildcard assets/*.ym)  $(wildcard assets/*.YM)
assets_mp3 := $(wildcard assets/*.mp3)
assets_ttf := $(wildcard assets/*.ttf)
assets_col := $(wildcard assets/*.col)

# Separate music from SFX - music uses opus compression
# Music files go in assets/music/, SFX in assets/sfx/ or assets/ root
assets_music := $(wildcard assets/music/*.wav)
assets_sfx   := $(wildcard assets/sfx/*.wav) $(filter-out $(wildcard assets/music/*.wav),$(wildcard assets/*.wav))

# Filter music vs sfx based on filename patterns
# Files starting with "scrap" or in music folder use opus
music_files := $(filter assets/music/%,$(assets_music)) $(filter %scrap1.wav %scrap2.wav %scrap3.wav,$(assets_sfx))
sfx_files   := $(filter-out $(music_files),$(assets_sfx))

# -------------------------
# Jukebox track lists (parsed by tools/generate_sounds_registry.py)
# Add music and SFX filenames here (without path or extension)
# -------------------------
# JUKEBOX_MUSIC: Music tracks for the sound test
JUKEBOX_MUSIC := \
	scrap1 \
	scrap1-menu(full) \
	scrap1-level1(full) \
	android64break \
	androidjungle1 \
	CalmJunglescrap \
	androidbreak2(finished) \
	N64GameJamIdea1 \
	N64Chiller3 \
	N64JetForceTrack

# JUKEBOX_SFX: Sound effects for the sound test
JUKEBOX_SFX := \
	BoltCollected \
	N64_Press_A_5 \
	N64UIHover4 \
	321Go1 \
	321Go2 \
	321Go3 \
	321Go4 \
	FireworkExplosion1 \
	FireworkExplosion2 \
	FireworkExplosion3 \
	FireworkSetOff \
	FireworkSetOff2 \
	Jump1 \
	JumporLand \
	JumpSound \
	Landing1 \
	Landing2 \
	MiscDamage \
	MiscDamage2 \
	Misc_Zap_1 \
	RatDamage \
	RobotBeep \
	RobotBeep2 \
	RobotBeepsandBoops \
	RobotBoop \
	RobotBoop2 \
	RobotDamage2 \
	RobotDamage3 \
	RobotHittingGround1 \
	RobotHittingGround2 \
	RobotHittingGround3 \
	RobotHittingGround4 \
	RobotHittingGround5 \
	RobotTakeDamage \
	RobotTumble \
	Slide1 \
	Slide2 \
	Slide3 \
	SlimeDamage \
	SpinAttack1 \
	SpinAttack2 \
	SpinAttack3 \
	Turret_Fire \
	WalkingonArms

assets_conv := \
	$(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite))) \
	$(addprefix filesystem/,$(notdir $(assets_glb:%.glb=%.t3dm))) \
	$(addprefix filesystem/,$(notdir $(assets_xm:%.xm=%.xm64))) \
	$(addprefix filesystem/,$(notdir $(assets_xm:%.XM=%.xm64))) \
	$(addprefix filesystem/,$(notdir $(assets_ym:%.ym=%.ym64))) \
	$(addprefix filesystem/,$(notdir $(assets_ym:%.YM=%.ym64))) \
	$(addprefix filesystem/,$(notdir $(music_files:%.wav=%.wav64))) \
	$(addprefix filesystem/,$(notdir $(sfx_files:%.wav=%.wav64))) \
	$(addprefix filesystem/,$(notdir $(assets_mp3:%.mp3=%.wav64))) \
	$(addprefix filesystem/,$(notdir $(assets_ttf:%.ttf=%.font64))) \
	$(addprefix filesystem/,$(notdir $(assets_col)))

# -------------------------
# Targets
# -------------------------
# Use recursive make so assets are re-scanned after codegen
all: codegen
	@$(MAKE) --no-print-directory game.z64

# -------------------------
# Auto-generate code (collision registry, level registry)
# -------------------------
.PHONY: codegen
codegen:
	@python3 tools/chunk_glb.py
	@python3 tools/generate_level_collision.py
	@python3 tools/chunk_collision.py
	@python3 tools/export_collision_binary.py
	@python3 tools/generate_deco_collision.py
	@python3 tools/generate_collision_registry.py
	@python3 tools/generate_level_registry.py
	@python3 tools/generate_sounds_registry.py

# -------------------------
# Sprite conversion
# -------------------------
# UI sprites - use RGBA16 to avoid palette/clamp issues
filesystem/UI_%.sprite: assets/UI_%.png
	@mkdir -p $(dir $@)
	@echo "    [UI-SPRITE] $@"
	@$(N64_MKSPRITE) --format RGBA16 -o filesystem "$<"

# Psyops logo sprites - use RGBA16 for full color
filesystem/Psyops%.sprite: assets/Psyops%.png
	@mkdir -p $(dir $@)
	@echo "    [LOGO-SPRITE] $@"
	@$(N64_MKSPRITE) --format RGBA16 -o filesystem "$<"

# Game logo and press start - use RGBA16 for full color
filesystem/logo.sprite: assets/logo.png
	@mkdir -p $(dir $@)
	@echo "    [LOGO-SPRITE] $@"
	@$(N64_MKSPRITE) --format RGBA16 -o filesystem "$<"

filesystem/PressStart.sprite: assets/PressStart.png
	@mkdir -p $(dir $@)
	@echo "    [LOGO-SPRITE] $@"
	@$(N64_MKSPRITE) --format RGBA16 -o filesystem "$<"

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) --format CI4 -o filesystem "$<"

# -------------------------
# Font conversion
# -------------------------
filesystem/%.font64: assets/%.ttf
	@mkdir -p $(dir $@)
	@echo "    [FONT] $@"
	@$(N64_MKFONT) --size 16 -o filesystem "$<"

# -------------------------
# Model conversion - Level chunks with BVH for frustum culling
# -------------------------
filesystem/level1_chunk%.t3dm: assets/level1_chunk%.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL+BVH] $@"
	@$(T3D_GLTF_TO_3D) --bvh "$<" $@
	@$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

filesystem/level2.t3dm: assets/level2.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL+BVH] $@"
	@$(T3D_GLTF_TO_3D) --bvh "$<" $@
	@$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

filesystem/level3_chunk%.t3dm: assets/level3_chunk%.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL+BVH] $@"
	@$(T3D_GLTF_TO_3D) --bvh "$<" $@
	@$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

filesystem/MenuScene.t3dm: assets/MenuScene.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL+BVH] $@"
	@$(T3D_GLTF_TO_3D) --bvh "$<" $@
	@$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

# -------------------------
# Model conversion - Other models (no BVH needed for simple decorations)
# -------------------------
filesystem/%.t3dm: assets/%.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL] $@"
	@$(T3D_GLTF_TO_3D) "$<" $@
	@$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

# -------------------------
# Audio conversion
# -------------------------
filesystem/%.xm64: assets/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem "$<"

filesystem/%.xm64: assets/%.XM
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem "$<"

filesystem/%.ym64: assets/%.ym
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem "$<"

filesystem/%.ym64: assets/%.YM
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem "$<"

# SFX conversion (default compression)
filesystem/BoltCollected.wav64: assets/BoltCollected.wav
	@mkdir -p $(dir $@)
	@echo "    [SFX] $@"
	@$(N64_AUDIOCONV) --wav-mono -o filesystem "$<"

# Music conversion (VADPCM compression - supports looping, RSP-accelerated)
filesystem/scrap%.wav64: assets/scrap%.wav
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

# Additional music tracks (mp3 sources, VADPCM compression)
filesystem/android64break.wav64: assets/android64break.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/androidjungle1.wav64: assets/androidjungle1.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/CalmJunglescrap.wav64: assets/CalmJunglescrap.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/androidbreak2(finished).wav64: assets/androidbreak2(finished).mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/N64GameJamIdea1.wav64: assets/N64GameJamIdea1.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/N64Chiller3.wav64: assets/N64Chiller3.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/N64JetForceTrack.wav64: assets/N64JetForceTrack.mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/scrap1-menu(full).wav64: assets/scrap1-menu(full).mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

filesystem/scrap1-level1(full).wav64: assets/scrap1-level1(full).mp3
	@mkdir -p $(dir $@)
	@echo "    [MUSIC-VADPCM] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 --wav-mono --wav-resample 22050 --wav-loop true -o filesystem "$<"

# Generic wav fallback (default compression)
filesystem/%.wav64: assets/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) --wav-mono -o filesystem "$<"

# MP3 SFX conversion (convert to wav64 without opus)
filesystem/%.wav64: assets/%.mp3
	@mkdir -p $(dir $@)
	@echo "    [SFX-MP3] $@"
	@$(N64_AUDIOCONV) --wav-mono -o filesystem "$<"

# -------------------------
# Collision data (binary .col files loaded at runtime)
# -------------------------
filesystem/%.col: assets/%.col
	@mkdir -p $(dir $@)
	@echo "    [COLLISION] $@"
	@cp "$<" "$@"

# -------------------------
# DFS filesystem
# -------------------------
$(BUILD_DIR)/game.dfs: $(assets_conv)

$(BUILD_DIR)/game.elf: $(OBJS)

# Override ROM title
game.z64: N64_ROM_TITLE="N64BrewJam Game"
game.z64: $(BUILD_DIR)/game.dfs

# -------------------------
# Dependency includes
# -------------------------
-include $(DEPS)

# -------------------------
# Clean
# -------------------------
clean:
	rm -rf $(BUILD_DIR) game.z64
	rm -rf filesystem

.PHONY: all clean
