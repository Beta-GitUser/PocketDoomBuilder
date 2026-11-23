# PocketDoomBuilder — PDB autoload.
#
# Holds a single shared WadFile instance for the UI to use.
# No GDExtension — the WAD core is pure GDScript (wad_file.gd).
# Also requests Android storage permissions for /storage/emulated/0/.

extends Node

## The shared WadFile instance for the UI to use.
var core: WadFile = WadFile.new()

func _ready() -> void:
	print("PDB autoload ready (GDScript WAD core).")

	# Android storage permissions are declared in project.godot under
	# [android] permissions=... and requested automatically by the engine
	# at runtime. No explicit API call needed here.
