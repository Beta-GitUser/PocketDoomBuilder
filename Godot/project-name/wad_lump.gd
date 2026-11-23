# PocketDoomBuilder — WAD lump node.
#
# A single lump in a WAD file. Stores the normalised lump name
# (upper-case, no NULs, max 8 chars) and the raw lump data as a
# PackedByteArray. RefCounted base means Godot manages memory
# automatically — no manual new/delete.

class_name WadLump
extends RefCounted

## Normalised lump name (upper-case, no NULs, max 8 chars).
var name: String = ""

## Raw lump data bytes.
var data: PackedByteArray = PackedByteArray()
