# PocketDoomBuilder — WAD container + map operations (pure GDScript).
#
# Replaces the C++ GDExtension shim. All WAD/map I/O is done in
# GDScript using Godot's FileAccess API. Lumps are WadLump nodes,
# maps are MapNode instances — Godot's RefCounted manages memory
# automatically.
#
# API mirrors the former PDCore extension so the UI code is unchanged.

class_name WadFile
extends RefCounted

## WAD type: "IWAD" or "PWAD".
var type: String = ""

## Ordered lumps in the WAD.
var lumps: Array[WadLump] = []

## Original file path (for save_map to write back to the same file).
var wad_path: String = ""

## The currently loaded map (if any).
var _current_map: MapNode = null

## Known map lump names (matching the C++ core's kMapLumpNames).
const MAP_LUMP_NAMES := [
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS",
    "NODES", "SECTORS", "REJECT", "BLOCKMAP", "BEHAVIOR", "TEXTMAP",
    "ZNODES", "DIALOGUE", "ENDMAP", "SCRIPTS", "LEAFS", "GL_NODES",
    "GL_SEGS", "GL_SSECT", "GL_PVS",
]

# =====================================================================
# Public API (mirrors the former PDCore GDExtension)
# =====================================================================

## Open a WAD file from disk. Returns the map count, or -1 on failure.
func open_wad(path: String) -> int:
	wad_path = path
	_current_map = null
	lumps.clear()
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return -1
	var bytes: PackedByteArray = f.get_buffer(f.get_length())
	f.close()
	if not _parse_wad(bytes):
		return -1
	return get_map_count()

## List map names as a comma-separated string.
func list_maps() -> String:
	var maps: PackedStringArray = PackedStringArray()
	for i in range(lumps.size()):
		if i + 1 >= lumps.size():
			continue
		var next := lumps[i + 1].name
		if next == "THINGS" or next == "TEXTMAP":
			maps.append(lumps[i].name)
	return ", ".join(maps)

## Load a map by name. Stores it internally and returns a summary string.
## Returns "format=..." or "error: ...".
func load_map(name: String) -> String:
	var want := name.strip_edges().to_upper()
	var header_idx := -1
	for i in range(lumps.size()):
		if lumps[i].name == want:
			header_idx = i
			break
	if header_idx == -1:
		return "error: map not found: %s" % want

	var indices := _map_lump_indices(header_idx)
	var map := MapNode.new()
	map.map_name = want

	# Detect format: UDMF if TEXTMAP present, else check LINEDEFS size
	# for Doom (14 bytes) vs Hexen (20 bytes).
	var has_textmap := false
	var has_things := false
	var linedefs_data: PackedByteArray = PackedByteArray()
	for idx in indices:
		if idx == header_idx:
			continue
		var n := lumps[idx].name
		if n == "THINGS":
			has_things = true
		if n == "TEXTMAP":
			has_textmap = true
		if n == "LINEDEFS":
			linedefs_data = lumps[idx].data

	if has_textmap:
		map.format = "udmf"
	elif has_things:
		if linedefs_data.size() > 0 and linedefs_data.size() % 20 == 0:
			map.format = "hexen"
		else:
			map.format = "doom"

	# Store raw lump data for round-tripping.
	for idx in indices:
		if idx != header_idx:
			map.lump_data[lumps[idx].name] = lumps[idx].data

	# Parse the map elements.
	if map.format == "udmf":
		map.warnings.append("UDMF parsing not yet implemented; raw data preserved.")
	elif map.format == "hexen":
		_parse_map_hexen(map, indices)
	else:
		_parse_map_doom(map, indices)

	_current_map = map

	if map.warnings.size() > 0:
		return "format=%s; warnings=%s" % [map.format, ", ".join(map.warnings)]
	return "format=%s" % map.format

## Close/clear the current WAD.
func close() -> void:
	lumps.clear()
	type = ""
	wad_path = ""
	_current_map = null

## Save the currently loaded map back into the WAD and write to disk.
## Returns "ok" or "error: ...".
func save_map() -> String:
	if _current_map == null:
		return "error: no map loaded"
	if wad_path.is_empty():
		return "error: no wad open"
	_serialize_map(_current_map)
	var result := save_wad_as(wad_path)
	if result != 0:
		return "error: failed to write WAD"
	_current_map.dirty = false
	return "ok"

## Write the WAD to a new path. Returns 0 on success, -1 on failure.
func save_wad_as(path: String) -> int:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return -1
	f.store_buffer(_serialise_wad())
	f.close()
	wad_path = path
	return 0

## Get the current WAD file path.
func get_wad_path() -> String:
	return wad_path

## Get the name of the currently loaded map.
func get_current_map() -> String:
	if _current_map == null:
		return ""
	return _current_map.map_name

## Get the number of maps in the WAD.
func get_map_count() -> int:
	var count := 0
	for i in range(lumps.size()):
		if i + 1 >= lumps.size():
			continue
		var next := lumps[i + 1].name
		if next == "THINGS" or next == "TEXTMAP":
			count += 1
	return count

# =====================================================================
# WAD parsing
# =====================================================================

## Parse WAD bytes into lumps. Returns false on fatal error.
func _parse_wad(bytes: PackedByteArray) -> bool:
	if bytes.size() < 12:
		return false
	# Magic: 'I' or 'P', 'W', 'A'
	if not ((bytes[0] == 0x49 or bytes[0] == 0x50) and bytes[1] == 0x57 and bytes[2] == 0x41):
		return false
	type = "IWAD" if bytes[0] == 0x49 else "PWAD"

	var num_lumps := _read_u32(bytes, 4)
	var dir_ofs := _read_u32(bytes, 8)
	if num_lumps == 0:
		return true

	for i in range(num_lumps):
		var off := dir_ofs + i * 16
		if off + 16 > bytes.size():
			break
		var file_pos := _read_u32(bytes, off)
		var size := _read_u32(bytes, off + 4)
		var name := _read_lump_name(bytes, off + 8)
		var lump := WadLump.new()
		lump.name = name
		lump.data = bytes.slice(file_pos, file_pos + size)
		lumps.append(lump)
	return true

## Collect indices of all lumps belonging to the map at header_idx.
## Scans forward from the header, collecting known map lump names
## until hitting a non-map-lump.
func _map_lump_indices(header_idx: int) -> Array:
	var result: Array = []
	if header_idx < 0 or header_idx >= lumps.size():
		return result
	result.append(header_idx)
	var seen: Dictionary = {}
	for i in range(header_idx + 1, lumps.size()):
		var n := lumps[i].name
		if not _is_map_lump_name(n):
			break
		if seen.has(i):
			break
		seen[i] = true
		result.append(i)
	return result

func _is_map_lump_name(n: String) -> bool:
	for name in MAP_LUMP_NAMES:
		if n == name:
			return true
	return false

## Find a lump index by name, starting at `start`. Returns -1 if not found.
func _find_lump(name: String, start: int = 0) -> int:
	for i in range(start, lumps.size()):
		if lumps[i].name == name:
			return i
	return -1

## Find the map header lump by name. Returns -1 if not found.
func _find_header(name: String) -> int:
	return _find_lump(name.to_upper(), 0)

## Get lump data by name from the map's lump indices.
func _get_lump_data(indices: Array, name: String) -> PackedByteArray:
	for idx in indices:
		if lumps[idx].name == name:
			return lumps[idx].data
	return PackedByteArray()

# =====================================================================
# Map parsing — Doom format
# =====================================================================

func _parse_map_doom(map: MapNode, indices: Array) -> void:
	# VERTEXES: s16 x, s16 y (4 bytes each)
	var vdata := _get_lump_data(indices, "VERTEXES")
	if vdata.size() > 0:
		var n := vdata.size() / 4
		for i in range(n):
			map.vertices.append({
				"x": _read_s16(vdata, i * 4),
				"y": _read_s16(vdata, i * 4 + 2),
			})

	# LINEDEFS: v1(u16), v2(u16), flags(u16), special(u8), tag(s16),
	#          front(u16), back(u16) = 14 bytes
	var ldata := _get_lump_data(indices, "LINEDEFS")
	if ldata.size() > 0:
		var n := ldata.size() / 14
		for i in range(n):
			var off := i * 14
			map.linedefs.append({
				"v1": _read_u16(ldata, off),
				"v2": _read_u16(ldata, off + 2),
				"flags": _read_u16(ldata, off + 4),
				"special": ldata[off + 6],
				"tag": _read_s16(ldata, off + 7),
				"front": _read_u16(ldata, off + 9),
				"back": _read_u16(ldata, off + 11),
				"args": [],
			})

	# SIDEDEFS: xOff(s16), yOff(s16), top(8), mid(8), bot(8), sector(s16)
	# = 30 bytes
	var sdata := _get_lump_data(indices, "SIDEDEFS")
	if sdata.size() > 0:
		var n := sdata.size() / 30
		for i in range(n):
			var off := i * 30
			map.sidedefs.append({
				"offsetX": _read_s16(sdata, off),
				"offsetY": _read_s16(sdata, off + 2),
				"texTop": _read_lump_name(sdata, off + 4),
				"texMid": _read_lump_name(sdata, off + 12),
				"texBottom": _read_lump_name(sdata, off + 20),
				"sector": _read_s16(sdata, off + 28),
			})

	# SECTORS: floor(s16), ceil(s16), floorTex(8), ceilTex(8),
	#         light(s16), special(s16), tag(s16) = 26 bytes
	var secdata := _get_lump_data(indices, "SECTORS")
	if secdata.size() > 0:
		var n := secdata.size() / 26
		for i in range(n):
			var off := i * 26
			map.sectors.append({
				"floorHeight": _read_s16(secdata, off),
				"ceilHeight": _read_s16(secdata, off + 2),
				"floorTex": _read_lump_name(secdata, off + 4),
				"ceilTex": _read_lump_name(secdata, off + 12),
				"brightness": _read_s16(secdata, off + 20),
				"special": _read_s16(secdata, off + 22),
				"tag": _read_s16(secdata, off + 24),
			})

	# THINGS: x(s16), y(s16), angle(s16), type(s16), flags(s16) = 10 bytes
	var tdata := _get_lump_data(indices, "THINGS")
	if tdata.size() > 0:
		var n := tdata.size() / 10
		for i in range(n):
			var off := i * 10
			map.things.append({
				"x": _read_s16(tdata, off),
				"y": _read_s16(tdata, off + 2),
				"angle": _read_s16(tdata, off + 4),
				"type": _read_s16(tdata, off + 6),
				"flags": _read_s16(tdata, off + 8),
				"z": 0,
				"special": 0,
				"tag": 0,
				"args": [],
			})

# =====================================================================
# Map parsing — Hexen format
# =====================================================================

func _parse_map_hexen(map: MapNode, indices: Array) -> void:
	# VERTEXES: s32 x, s32 y (8 bytes each in Hexen)
	var vdata := _get_lump_data(indices, "VERTEXES")
	if vdata.size() > 0:
		var n := vdata.size() / 8
		for i in range(n):
			map.vertices.append({
				"x": _read_s32(vdata, i * 8),
				"y": _read_s32(vdata, i * 8 + 4),
			})

	# LINEDEFS: 20 bytes — v1(u16), v2(u16), flags(u16), special(u8),
	#           tag(s16), front(u16), back(u16), args[5](s16)
	var ldata := _get_lump_data(indices, "LINEDEFS")
	if ldata.size() > 0:
		var n := ldata.size() / 20
		for i in range(n):
			var off := i * 20
			var args: Array = []
			for a in range(5):
				args.append(_read_s16(ldata, off + 10 + a * 2))
			map.linedefs.append({
				"v1": _read_u16(ldata, off),
				"v2": _read_u16(ldata, off + 2),
				"flags": _read_u16(ldata, off + 4),
				"special": ldata[off + 6],
				"tag": _read_s16(ldata, off + 7),
				"front": _read_u16(ldata, off + 9),
				"back": _read_u16(ldata, off + 11),
				"args": args,
			})

	# SIDEDEFS: same as Doom (30 bytes)
	var sdata := _get_lump_data(indices, "SIDEDEFS")
	if sdata.size() > 0:
		var n := sdata.size() / 30
		for i in range(n):
			var off := i * 30
			map.sidedefs.append({
				"offsetX": _read_s16(sdata, off),
				"offsetY": _read_s16(sdata, off + 2),
				"texTop": _read_lump_name(sdata, off + 4),
				"texMid": _read_lump_name(sdata, off + 12),
				"texBottom": _read_lump_name(sdata, off + 20),
				"sector": _read_s16(sdata, off + 28),
			})

	# SECTORS: same as Doom (26 bytes)
	var secdata := _get_lump_data(indices, "SECTORS")
	if secdata.size() > 0:
		var n := secdata.size() / 26
		for i in range(n):
			var off := i * 26
			map.sectors.append({
				"floorHeight": _read_s16(secdata, off),
				"ceilHeight": _read_s16(secdata, off + 2),
				"floorTex": _read_lump_name(secdata, off + 4),
				"ceilTex": _read_lump_name(secdata, off + 12),
				"brightness": _read_s16(secdata, off + 20),
				"special": _read_s16(secdata, off + 22),
				"tag": _read_s16(secdata, off + 24),
			})

	# THINGS: 28 bytes — x(s32), y(s32), z(s16), angle(s16), type(s16),
	#        flags(u16), special(s16), tag(s16), args[5](s16)
	var tdata := _get_lump_data(indices, "THINGS")
	if tdata.size() > 0:
		var n := tdata.size() / 28
		for i in range(n):
			var off := i * 28
			var args: Array = []
			for a in range(5):
				args.append(_read_s16(tdata, off + 16 + a * 2))
			map.things.append({
				"x": _read_s32(tdata, off),
				"y": _read_s32(tdata, off + 4),
				"z": _read_s16(tdata, off + 8),
				"angle": _read_s16(tdata, off + 10),
				"type": _read_s16(tdata, off + 12),
				"flags": _read_u16(tdata, off + 14),
				"special": _read_s16(tdata, off + 16),
				"tag": _read_s16(tdata, off + 18),
				"args": args,
			})

# =====================================================================
# Map serialization
# =====================================================================

func _serialize_map(map: MapNode) -> void:
	if map.format == "doom":
		_serialize_map_doom(map)
	elif map.format == "hexen":
		_serialize_map_hexen(map)
	# UDMF: lump_data already holds raw TEXTMAP bytes — nothing to do.

func _serialize_map_doom(map: MapNode) -> void:
	var header_idx := _find_header(map.map_name)
	if header_idx == -1:
		return
	var indices := _map_lump_indices(header_idx)
	_put_lump(indices, "VERTEXES", _write_vertices_doom(map))
	_put_lump(indices, "LINEDEFS", _write_linedefs_doom(map))
	_put_lump(indices, "SIDEDEFS", _write_sidedefs_doom(map))
	_put_lump(indices, "SECTORS", _write_sectors_doom(map))
	_put_lump(indices, "THINGS", _write_things_doom(map))

func _serialize_map_hexen(map: MapNode) -> void:
	var header_idx := _find_header(map.map_name)
	if header_idx == -1:
		return
	var indices := _map_lump_indices(header_idx)
	_put_lump(indices, "VERTEXES", _write_vertices_hexen(map))
	_put_lump(indices, "LINEDEFS", _write_linedefs_hexen(map))
	_put_lump(indices, "SIDEDEFS", _write_sidedefs_doom(map))
	_put_lump(indices, "SECTORS", _write_sectors_doom(map))
	_put_lump(indices, "THINGS", _write_things_hexen(map))

## Replace an existing map lump's data, or insert it after the header.
func _put_lump(indices: Array, name: String, data: PackedByteArray) -> void:
	var header_idx := indices[0]
	# Update existing lump if found.
	for idx in indices:
		if idx != header_idx and lumps[idx].name == name:
			lumps[idx].data = data
			return
	# Insert new lump after the last lump this map owns.
	var insert_idx := header_idx + 1
	for idx in indices:
		if idx != header_idx:
			insert_idx = idx + 1
	var lump := WadLump.new()
	lump.name = name
	lump.data = data
	lumps.insert(insert_idx, lump)

# =====================================================================
# Binary writers (Doom format)
# =====================================================================

func _write_vertices_doom(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for v in map.vertices:
		_write_s16(buf, v.x)
		_write_s16(buf, v.y)
	return buf

func _write_linedefs_doom(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for l in map.linedefs:
		_write_u16(buf, l.v1)
		_write_u16(buf, l.v2)
		_write_u16(buf, l.flags)
		buf.append(l.special)
		_write_s16(buf, l.tag)
		_write_u16(buf, l.front)
		_write_u16(buf, l.back)
	return buf

func _write_sidedefs_doom(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for s in map.sidedefs:
		_write_s16(buf, s.offsetX)
		_write_s16(buf, s.offsetY)
		_write_lump_name(buf, s.texTop)
		_write_lump_name(buf, s.texMid)
		_write_lump_name(buf, s.texBottom)
		_write_s16(buf, s.sector)
	return buf

func _write_sectors_doom(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for s in map.sectors:
		_write_s16(buf, s.floorHeight)
		_write_s16(buf, s.ceilHeight)
		_write_lump_name(buf, s.floorTex)
		_write_lump_name(buf, s.ceilTex)
		_write_s16(buf, s.brightness)
		_write_s16(buf, s.special)
		_write_s16(buf, s.tag)
	return buf

func _write_things_doom(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for t in map.things:
		_write_s16(buf, t.x)
		_write_s16(buf, t.y)
		_write_s16(buf, t.angle)
		_write_s16(buf, t.type)
		_write_s16(buf, t.flags)
	return buf

# =====================================================================
# Binary writers (Hexen format)
# =====================================================================

func _write_vertices_hexen(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for v in map.vertices:
		_write_s32(buf, v.x)
		_write_s32(buf, v.y)
	return buf

func _write_linedefs_hexen(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for l in map.linedefs:
		_write_u16(buf, l.v1)
		_write_u16(buf, l.v2)
		_write_u16(buf, l.flags)
		buf.append(l.special)
		_write_s16(buf, l.tag)
		_write_u16(buf, l.front)
		_write_u16(buf, l.back)
		for a in l.args:
			_write_s16(buf, a)
	return buf

func _write_things_hexen(map: MapNode) -> PackedByteArray:
	var buf := PackedByteArray()
	for t in map.things:
		_write_s32(buf, t.x)
		_write_s32(buf, t.y)
		_write_s16(buf, t.z)
		_write_s16(buf, t.angle)
		_write_s16(buf, t.type)
		_write_u16(buf, t.flags)
		_write_s16(buf, t.special)
		_write_s16(buf, t.tag)
		for a in t.args:
			_write_s16(buf, a)
	return buf

# =====================================================================
# WAD serialization
# =====================================================================

## Build a complete WAD image (header + lump data + directory) as bytes.
func _serialise_wad() -> PackedByteArray:
	var file_data := PackedByteArray()
	var directory := PackedByteArray()
	var data_offset := 12  # header is 12 bytes

	for lump in lumps:
		var lump_offset := data_offset + file_data.size()
		file_data.append_array(lump.data)
		# Directory entry: [offset:4][size:4][name:8]
		_write_u32(directory, lump_offset)
		_write_u32(directory, lump.data.size())
		_write_lump_name(directory, lump.name)

	var header := PackedByteArray()
	header.append_array(type.utf8())  # "IWAD" or "PWAD" (4 bytes)
	_write_u32(header, lumps.size())
	_write_u32(header, 12 + file_data.size())

	return header + file_data + directory

# =====================================================================
# Binary read/write helpers
# =====================================================================

func _read_u8(b: PackedByteArray, off: int) -> int:
	return b[off]

func _read_s16(b: PackedByteArray, off: int) -> int:
	var v := b[off] | (b[off + 1] << 8)
	if v >= 0x8000:
		v -= 0x10000
	return v

func _read_u16(b: PackedByteArray, off: int) -> int:
	return b[off] | (b[off + 1] << 8)

func _read_s32(b: PackedByteArray, off: int) -> int:
	var v := b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)
	if v >= 0x80000000:
		v -= 0x100000000
	return v

func _read_u32(b: PackedByteArray, off: int) -> int:
	return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)

## Read an 8-byte lump name (null-padded), normalise to upper-case.
func _read_lump_name(b: PackedByteArray, off: int) -> String:
	var raw := b.slice(off, off + 8)
	var nul := raw.find(0, 0)
	if nul >= 0:
		raw = raw.slice(0, nul)
	return String.from_utf8(raw).to_upper()

func _write_u16(buf: PackedByteArray, v: int) -> void:
	buf.append(v & 0xFF)
	buf.append((v >> 8) & 0xFF)

func _write_s16(buf: PackedByteArray, v: int) -> void:
	_write_u16(buf, v & 0xFFFF)

func _write_s32(buf: PackedByteArray, v: int) -> void:
	buf.append(v & 0xFF)
	buf.append((v >> 8) & 0xFF)
	buf.append((v >> 16) & 0xFF)
	buf.append((v >> 24) & 0xFF)

func _write_u32(buf: PackedByteArray, v: int) -> void:
	buf.append(v & 0xFF)
	buf.append((v >> 8) & 0xFF)
	buf.append((v >> 16) & 0xFF)
	buf.append((v >> 24) & 0xFF)

## Write a lump name padded to 8 bytes, upper-case.
func _write_lump_name(buf: PackedByteArray, name: String) -> void:
	var bytes := name.to_upper().utf8()
	for i in range(8):
		var b := 0
		if i < bytes.size():
			b = bytes[i]
		buf.append(b)
