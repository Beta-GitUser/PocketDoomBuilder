# PocketDoomBuilder — GDExtension spike autoload.
# Loads the PDCore GDExtension so its class is available globally.

extends Node

var _pdcore_extension: GDExtension

func _ready() -> void:
	_pdcore_extension = load("res://ext/pd_core.gdextension")
	if _pdcore_extension == null:
		push_error("PDCore GDExtension failed to load.")
		return
	print("PDCore GDExtension loaded successfully.")
