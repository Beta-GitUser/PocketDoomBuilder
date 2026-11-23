# PocketDoomBuilder — GDScript WAD core headless test.
# Run with:  godot --headless --script res://test_spike.gd
#
# Validates that the GDScript WAD core loads a WAD, lists maps,
# loads a map, and closes — end-to-end, no C++ extension needed.

extends SceneTree

func _init() -> void:
	# Use load() so the script works regardless of class_name registration
	# in headless --script mode.
	var WadFileScript = load("res://wad_file.gd")
	var wad = WadFileScript.new()

	print("=== PDCore GDScript spike test ===")

	# Test 1: open a WAD file.
	var wad_path = "res://test_data/test.wad"
	var count: int = wad.open_wad(wad_path)
	if count < 0:
		push_error("FAIL: open_wad returned %d (file not found or bad WAD)." % count)
		quit(1)
		return
	print("PASS: open_wad -> map count = %d" % count)

	# Test 2: list maps.
	var maps: String = wad.list_maps()
	print("PASS: list_maps -> %s" % maps)
	if maps.is_empty():
		push_error("FAIL: list_maps returned empty string.")
		quit(1)
		return

	# Test 3: load the first map.
	var first_map = maps.split(", ")[0]
	var summary: String = wad.load_map(first_map)
	print("PASS: load_map(%s) -> %s" % [first_map, summary])
	if summary.begins_with("error:"):
		push_error("FAIL: load_map returned error: %s" % summary)
		quit(1)
		return

	# Test 4: verify parsed elements.
	var map = wad._current_map
	if map.vertices.size() == 0:
		push_error("FAIL: map has no vertices.")
		quit(1)
		return
	if map.linedefs.size() == 0:
		push_error("FAIL: map has no linedefs.")
		quit(1)
		return
	print("PASS: parsed %d vertices, %d linedefs, %d sidedefs, %d sectors, %d things" % [
		map.vertices.size(), map.linedefs.size(), map.sidedefs.size(),
		map.sectors.size(), map.things.size(),
	])

	# Test 5: save_map (round-trip).
	var save_result: String = wad.save_map()
	if save_result.begins_with("error:"):
		push_error("FAIL: save_map returned error: %s" % save_result)
		quit(1)
		return
	print("PASS: save_map -> %s" % save_result)

	# Test 6: close.
	wad.close()
	print("PASS: close()")

	print("=== All spike tests passed. ===")
	quit(0)
