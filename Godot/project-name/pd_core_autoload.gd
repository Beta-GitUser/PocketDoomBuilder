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

	# Request external-storage permission on Android so we can use
	# /storage/emulated/0/ as the base directory (avoids SAF).
	if OS.has_feature("android"):
		_request_storage_permission()

func _request_storage_permission() -> void:
	# Android 11+ (API 30+) uses MANAGE_EXTERNAL_STORAGE for broad access
	# to the shared external storage root. Older versions use
	# READ/WRITE_EXTERNAL_STORAGE.
	var perms := ["MANAGE_EXTERNAL_STORAGE", "READ_EXTERNAL_STORAGE", "WRITE_EXTERNAL_STORAGE"]
	for perm in perms:
		if not OS.has_android_permission(perm):
			OS.request_android_permission(perm)
