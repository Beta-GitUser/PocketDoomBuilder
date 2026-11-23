# PocketDoomBuilder — Map node.
#
# A loaded map: parsed elements plus raw lump data for round-tripping.
# Holds vertices, linedefs, sidedefs, sectors, and things as typed
# arrays. Raw lump bytes are preserved in lump_data so save_map() can
# write them back without re-serialising from scratch.
#
# format is "doom", "hexen", "udmf", or "unknown".

class_name MapNode
extends RefCounted

## Map format: "doom", "hexen", "udmf", or "unknown".
var format: String = "unknown"

## Map name (e.g. "MAP01", "E1M1").
var map_name: String = ""

## Load warnings (non-fatal issues reported during parsing).
var warnings: PackedStringArray = PackedStringArray()

## True if the map has been modified since load/save.
var dirty: bool = false

## Parsed vertices: [{x: int, y: int}, ...]
var vertices: Array = []

## Parsed linedefs: [{v1, v2, flags, special, tag, front, back, args}, ...]
var linedefs: Array = []

## Parsed sidedefs: [{offsetX, offsetY, texTop, texMid, texBottom, sector}, ...]
var sidedefs: Array = []

## Parsed sectors: [{floorHeight, ceilHeight, floorTex, ceilTex, brightness, special, tag}, ...]
var sectors: Array = []

## Parsed things: [{x, y, z, angle, type, flags, special, tag, args}, ...]
var things: Array = []

## Raw lump data for each map lump (name -> PackedByteArray).
var lump_data: Dictionary = {}
