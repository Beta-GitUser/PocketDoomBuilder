# PocketDoomBuilder — Godot touch-first tabbed UI.
#
# Tabs: Main | WAD Explorer | Mapping | Coding
# Switch tabs with the left (‹) and right (›) buttons.
# Main tab: open WAD, map select, save, save as, exit.
# Base directory for file dialogs: /storage/emulated/0/ (Android).

extends Control

const TAB_MAIN := 0
const TAB_WAD_EXPLORER := 1
const TAB_MAPPING := 2
const TAB_CODING := 3

const TAB_NAMES: PackedStringArray = ["Main", "WAD Explorer", "Mapping", "Coding"]

const VERSION_TEXT := "PDB Version 0.0.0"

## Base directory for file dialogs on Android; falls back to project dir.
const BASE_DIR := "/storage/emulated/0/"

var _current_tab: int = TAB_MAIN
var _tab_content: VBoxContainer

# Main-tab widgets (rebuilt on each tab entry).
var _wad_label: Label
var _map_option: OptionButton
var _map_label: Label
var _status_label: Label
var _tab_name_label: Label

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

func _ready() -> void:
	anchors_fill = true
	anchor_left = 0.0
	anchor_right = 1.0
	anchor_top = 0.0
	anchor_bottom = 1.0
	offset_left = 0.0
	offset_right = get_size().x
	offset_top = 0.0
	offset_bottom = get_size().y

	_build_root()
	_show_tab(TAB_MAIN)

func _build_root() -> void:
	var root_v = VBoxContainer
	root_v.anchors_fill = true
	root_v.anchor_left = 0.0
	root_v.anchor_right = 1.0
	root_v.anchor_top = 0.0
	root_v.anchor_bottom = 1.0
	root_v.offset_left = 0.0
	root_v.offset_right = get_size().x
	root_v.offset_top = 0.0
	root_v.offset_bottom = get_size().y
	root_v.add_theme_constant_override("separation", 0)
	add_child(root_v)

	# --- Top bar: tab name (left) + version + options (right) ---
	var top_bar = HBoxContainer
	top_bar.custom_minimum_size = Vector2(0, 56)
	top_bar.add_theme_constant_override("separation", 8)
	root_v.add_child(top_bar)

	_tab_name_label = Label()
	_tab_name_label.text = TAB_NAMES[_current_tab]
	_tab_name_label.add_theme_font_size_override("font_size", 22)
	_tab_name_label.add_theme_color_override("font_color", Color(0.95, 0.95, 0.97))
	top_bar.add_child(_tab_name_label)

	top_bar.add_spacer()

	var version_label = Label()
	version_label.text = VERSION_TEXT
	version_label.add_theme_font_size_override("font_size", 14)
	version_label.add_theme_color_override("font_color", Color(0.7, 0.7, 0.72))
	top_bar.add_child(version_label)

	var options_btn = Button()
	options_btn.text = "⋮"
	options_btn.custom_minimum_size = Vector2(48, 40)
	options_btn.flat = true
	top_bar.add_child(options_btn)

	# --- Tab navigation: left / right buttons ---
	var nav = HBoxContainer
	nav.custom_minimum_size = Vector2(0, 56)
	nav.add_theme_constant_override("separation", 12)
	root_v.add_child(nav)

	var prev_btn = Button()
	prev_btn.text = "‹  Prev"
	prev_btn.custom_minimum_size = Vector2(120, 48)
	prev_btn.pressed.connect(_tab_prev)
	nav.add_child(prev_btn)

	nav.add_spacer()

	var next_btn = Button()
	next_btn.text = "Next  ›"
	next_btn.custom_minimum_size = Vector2(120, 48)
	next_btn.pressed.connect(_tab_next)
	nav.add_child(next_btn)

	# --- Content area (switches per tab) ---
	_tab_content = VBoxContainer
	_tab_content.anchors_fill = true
	_tab_content.anchor_left = 0.0
	_tab_content.anchor_right = 1.0
	_tab_content.anchor_top = 0.0
	_tab_content.anchor_bottom = 1.0
	_tab_content.offset_left = 12.0
	_tab_content.offset_right = -12.0
	_tab_content.offset_top = 12.0
	_tab_content.offset_bottom = -12.0
	_tab_content.add_theme_constant_override("separation", 16)
	root_v.add_child(_tab_content)

# ---------------------------------------------------------------------------
# Tab switching
# ---------------------------------------------------------------------------

func _show_tab(index: int) -> void:
	_current_tab = clampf(index, 0, TAB_NAMES.size() - 1)
	if _tab_name_label:
		_tab_name_label.text = TAB_NAMES[_current_tab]

	# Clear previous content.
	for child in _tab_content.get_children():
		_tab_content.remove_child(child)
		child.queue_free()

	var content: Node
	if _current_tab == TAB_MAIN:
		content = _build_main_tab()
	elif _current_tab == TAB_WAD_EXPLORER:
		content = _build_wad_explorer_tab()
	elif _current_tab == TAB_MAPPING:
		content = _build_mapping_tab()
	elif _current_tab == TAB_CODING:
		content = _build_coding_tab()
	else:
		content = _build_main_tab()

	_tab_content.add_child(content)
	_refresh_wad_status()

func _tab_prev() -> void:
	_show_tab(_current_tab - 1)

func _tab_next() -> void:
	_show_tab(_current_tab + 1)

# ---------------------------------------------------------------------------
# Tab content builders
# ---------------------------------------------------------------------------

## Main tab: open WAD, map select, save, save as, exit.
func _build_main_tab() -> Node:
	var h = HBoxContainer
	h.anchors_fill = true
	h.anchor_left = 0.0
	h.anchor_right = 1.0
	h.anchor_top = 0.0
	h.anchor_bottom = 1.0
	h.offset_left = 0.0
	h.offset_right = 0.0
	h.offset_top = 0.0
	h.offset_bottom = 0.0

	# Left column: buttons.
	var left_col = VBoxContainer
	left_col.add_theme_constant_override("separation", 14)
	left_col.custom_minimum_size = Vector2(300, 0)
	h.add_child(left_col)

	_status_label = Label()
	_status_label.text = "Ready."
	_status_label.add_theme_font_size_override("font_size", 14)
	_status_label.add_theme_color_override("font_color", Color(0.6, 0.6, 0.62))
	left_col.add_child(_status_label)

	var open_btn = Button()
	open_btn.text = "Open WAD"
	open_btn.custom_minimum_size = Vector2(300, 56)
	open_btn.pressed.connect(_on_open_wad)
	left_col.add_child(open_btn)

	_wad_label = Label()
	_wad_label.text = "No WAD open"
	_wad_label.add_theme_font_size_override("font_size", 14)
	_wad_label.add_theme_color_override("font_color", Color(0.5, 0.5, 0.52))
	left_col.add_child(_wad_label)

	_map_option = OptionButton()
	_map_option.custom_minimum_size = Vector2(300, 48)
	_map_option.disabled = true
	_map_option.item_selected.connect(_on_map_selected)
	left_col.add_child(_map_option)

	_map_label = Label()
	_map_label.text = "No map loaded"
	_map_label.add_theme_font_size_override("font_size", 14)
	_map_label.add_theme_color_override("font_color", Color(0.5, 0.5, 0.52))
	left_col.add_child(_map_label)

	var save_btn = Button()
	save_btn.text = "Save"
	save_btn.custom_minimum_size = Vector2(300, 56)
	save_btn.pressed.connect(_on_save)
	left_col.add_child(save_btn)

	var save_as_btn = Button()
	save_as_btn.text = "Save As"
	save_as_btn.custom_minimum_size = Vector2(300, 56)
	save_as_btn.pressed.connect(_on_save_as)
	left_col.add_child(save_as_btn)

	var exit_btn = Button()
	exit_btn.text = "Exit"
	exit_btn.custom_minimum_size = Vector2(300, 56)
	exit_btn.pressed.connect(_on_exit)
	left_col.add_child(exit_btn)

	# Right column: empty space — logo / flavor text.
	var right_col = VBoxContainer
	right_col.anchors_fill = true
	right_col.anchor_left = 0.0
	right_col.anchor_right = 1.0
	right_col.anchor_top = 0.0
	right_col.anchor_bottom = 1.0
	right_col.offset_left = 0.0
	right_col.offset_right = 0.0
	right_col.offset_top = 0.0
	right_col.offset_bottom = 0.0
	right_col.add_theme_constant_override("separation", 16)
	h.add_child(right_col)

	var logo = Label()
	logo.text = "PocketDoomBuilder"
	logo.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	logo.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	logo.add_theme_font_size_override("font_size", 32)
	logo.add_theme_color_override("font_color", Color(0.3, 0.32, 0.35))
	right_col.add_child(logo)

	var tagline = Label()
	tagline.text = "Mobile Doom map editing"
	tagline.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	tagline.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	tagline.add_theme_font_size_override("font_size", 16)
	tagline.add_theme_color_override("font_color", Color(0.45, 0.47, 0.5))
	right_col.add_child(tagline)

	var version_big = Label()
	version_big.text = VERSION_TEXT
	version_big.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	version_big.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	version_big.add_theme_font_size_override("font_size", 14)
	version_big.add_theme_color_override("font_color", Color(0.4, 0.42, 0.45))
	right_col.add_child(version_big)

	return h

## WAD Explorer tab — placeholder for the texture/lump browser.
func _build_wad_explorer_tab() -> Node:
	return _build_placeholder_tab(
		"WAD Explorer",
		"Texture & lump browser for the open WAD.\n\nThis tab is built first once storage is sorted out.")

## Mapping tab — placeholder for map editing tools.
func _build_mapping_tab() -> Node:
	return _build_placeholder_tab(
		"Mapping",
		"Map editing tools: geometry, things, UDMF.\n\nComing in a later phase.")

## Coding tab — placeholder for the ZScript / ACS IDE.
func _build_coding_tab() -> Node:
	return _build_placeholder_tab(
		"Coding",
		"ZScript / ACS editor.\n\nA mobile-friendly IDE for Doom scripting languages.\nComing in a later phase.")

func _build_placeholder_tab(title: String, body: String) -> Node:
	var v = VBoxContainer
	v.anchors_fill = true
	v.anchor_left = 0.0
	v.anchor_right = 1.0
	v.anchor_top = 0.0
	v.anchor_bottom = 1.0
	v.offset_left = 0.0
	v.offset_right = 0.0
	v.offset_top = 0.0
	v.offset_bottom = 0.0

	var title_label = Label()
	title_label.text = title
	title_label.add_theme_font_size_override("font_size", 24)
	title_label.add_theme_color_override("font_color", Color(0.25, 0.27, 0.3))
	v.add_child(title_label)

	var body_label = Label()
	body_label.text = body
	body_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	body_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	body_label.add_theme_font_size_override("font_size", 16)
	body_label.add_theme_color_override("font_color", Color(0.4, 0.42, 0.45))
	body_label.anchors_fill = true
	body_label.anchor_left = 0.0
	body_label.anchor_right = 1.0
	body_label.anchor_top = 0.0
	body_label.anchor_bottom = 1.0
	body_label.offset_left = 0.0
	body_label.offset_right = 0.0
	body_label.offset_top = 0.0
	body_label.offset_bottom = 0.0
	v.add_child(body_label)
	return v

# ---------------------------------------------------------------------------
# Main-tab actions
# ---------------------------------------------------------------------------

func _base_dir() -> String:
	if DirAccess.dir_exists_absolute(BASE_DIR):
		return BASE_DIR
	return "."

func _set_status(msg: String) -> void:
	if _status_label:
		_status_label.text = msg

func _refresh_wad_status() -> void:
	if not is_instance_valid(_wad_label) or not is_instance_valid(_map_option):
		return
	var core: WadFile = PDB.core
	if core == null:
		_wad_label.text = "No WAD open"
		_map_option.clear()
		_map_option.disabled = true
		return
	var path: String = core.get_wad_path()
	if path.is_empty():
		_wad_label.text = "No WAD open"
		_map_option.clear()
		_map_option.disabled = true
		_map_label.text = "No map loaded"
		return
	_wad_label.text = "WAD: " + path
	# Populate map dropdown.
	_map_option.clear()
	var maps: String = core.list_maps()
	if maps.is_empty():
		_map_option.disabled = true
		_map_label.text = "No maps in WAD"
		return
	_map_option.disabled = false
	for name in maps.split(","):
		_map_option.add_item(name.strip_edges())
	var current: String = core.get_current_map()
	if current.is_empty():
		_map_label.text = "No map loaded"
	else:
		_map_label.text = "Map: " + current

func _on_open_wad() -> void:
	var dialog = FileDialog.new()
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.mode = FileDialog.MODE_OPEN_FILE
	dialog.filter = "WAD files (*.wad;*.WAD)"
	dialog.current_path = _base_dir()
	dialog.popup_centered()
	dialog.file_selected.connect(func(path: String) -> void:
		_do_open_wad(path))

func _do_open_wad(path: String) -> void:
	var core: WadFile = PDB.core
	if core == null:
		_set_status("WAD core not ready.")
		return
	var count: int = core.open_wad(path)
	if count < 0:
		_set_status("Failed to open WAD.")
		push_error("WadFile.open_wad failed for: " + path)
	else:
		_set_status("Opened WAD: %d map(s)." % count)
	_refresh_wad_status()

func _on_map_selected(index: int) -> void:
	var core: WadFile = PDB.core
	if core == null or _map_option.disabled:
		return
	var name: String = _map_option.get_item_text(index)
	var summary: String = core.load_map(name)
	if summary.begins_with("error:"):
		_set_status(summary)
		_map_label.text = "Map load failed."
	else:
		_set_status("Loaded map: " + name)
		_map_label.text = "Map: " + name

func _on_save() -> void:
	var core: WadFile = PDB.core
	if core == null:
		_set_status("WAD core not ready.")
		return
	var result: String = core.save_map()
	_set_status(result)

func _on_save_as() -> void:
	var core: WadFile = PDB.core
	if core == null:
		_set_status("WAD core not ready.")
		return
	var dialog = FileDialog.new()
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.mode = FileDialog.MODE_OPEN_SAVE_FILE
	dialog.filter = "WAD files (*.wad;*.WAD)"
	dialog.current_path = _base_dir()
	dialog.popup_centered()
	dialog.file_selected.connect(func(path: String) -> void:
		var res: int = core.save_wad_as(path)
		if res == 0:
			_set_status("Saved WAD as: " + path)
			_refresh_wad_status()
		else:
			_set_status("Save As failed.")

func _on_exit() -> void:
	get_tree().quit()
