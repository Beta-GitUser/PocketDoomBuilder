# PocketDoomBuilder — GDExtension autoload (named "PDB").
# Loads the PDCore GDExtension so its class is available globally,
# and holds a single shared PDCore instance for the UI to use.

extends Node

## The shared PDCore GDExtension instance.
## Typed as Variant — the PDCore class is registered at runtime by the
## GDExtension, so the GDScript parser cannot resolve it as a type.
var core: Variant = null

## The loaded GDExtension resource.
var _pdcore_extension: GDExtension

func _ready() -> void:
	_pdcore_extension = load("res://ext/pd_core.gdextension")
	if _pdcore_extension == null:
		push_error("PDCore GDExtension failed to load.")
		return
	core = ClassDB.instantiate("PDCore")
	print("PDCore GDExtension loaded. PDB autoload ready.")

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
