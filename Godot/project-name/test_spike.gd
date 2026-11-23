# PocketDoomBuilder — GDExtension spike headless test.
# Run with:  godot --headless --script res://test_spike.gd
#
# Validates that the PDCore GDExtension loads, the C++ core links,
# and GDScript can call into it end-to-end.

extends SceneTree

func _init() -> void:
	# Wait for the autoload (PDCore) to finish loading.
	# In headless mode with --script, autoloads run before the script's _init
	# only if the script is the main scene; when using --script the autoload
	# may not have run. Load the extension explicitly as a fallback.
	var ext = load("res://ext/pd_core.gdextension")
	if ext == null:
		push_error("FAIL: GDExtension could not be loaded.")
		quit(1)
		return

	print("=== PDCore GDExtension spike test ===")

	var core = PDCore.new()
	if core == null:
		push_error("FAIL: PDCore.new() returned null.")
		quit(1)
		return

	# Test 1: open a WAD file.
	var wad_path = "res://test_data/test.wad"
	var count: int = core.open_wad(wad_path)
	if count < 0:
		push_error("FAIL: open_wad returned %d (file not found or bad WAD)." % count)
		quit(1)
		return
	print("PASS: open_wad -> map count = %d" % count)

	# Test 2: list maps.
	var maps: String = core.list_maps()
	print("PASS: list_maps -> %s" % maps)
	if maps.is_empty():
		push_error("FAIL: list_maps returned empty string.")
		quit(1)
		return

	# Test 3: load the first map.
	var first_map = maps.split(",")[0]
	var summary: String = core.load_map(first_map)
	print("PASS: load_map(%s) -> %s" % [first_map, summary])
	if summary.begins_with("error:"):
		push_error("FAIL: load_map returned error: %s" % summary)
		quit(1)
		return

	# Test 4: close.
	core.close()
	print("PASS: close()")

	print("=== All spike tests passed. ===")
	quit(0)
