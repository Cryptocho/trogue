@tool
extends EditorPlugin

# Usage:
#   1. Copy this folder to your Godot project's addons/tileset_exporter/
#   2. Enable in Project > Project Settings > Plugins
#   3. Export via Project > Tools > "Export TileSet to JSON..." / "Export TileSet to Lua..."

const DIR_TILES := [
	TileSet.CELL_NEIGHBOR_RIGHT_SIDE,
	TileSet.CELL_NEIGHBOR_BOTTOM_SIDE,
	TileSet.CELL_NEIGHBOR_LEFT_SIDE,
	TileSet.CELL_NEIGHBOR_TOP_SIDE,
]

const DIR_CORNERS := [
	TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER,
	TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER,
	TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER,
	TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER,
]

const DIR_SIDES := [
	TileSet.CELL_NEIGHBOR_RIGHT_SIDE,
	TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER,
	TileSet.CELL_NEIGHBOR_BOTTOM_SIDE,
	TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER,
	TileSet.CELL_NEIGHBOR_LEFT_SIDE,
	TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER,
	TileSet.CELL_NEIGHBOR_TOP_SIDE,
	TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER,
]

const DIR_NAMES := {
	TileSet.CELL_NEIGHBOR_RIGHT_SIDE: "right_side",
	TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER: "bottom_right_corner",
	TileSet.CELL_NEIGHBOR_BOTTOM_SIDE: "bottom_side",
	TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER: "bottom_left_corner",
	TileSet.CELL_NEIGHBOR_LEFT_SIDE: "left_side",
	TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER: "top_left_corner",
	TileSet.CELL_NEIGHBOR_TOP_SIDE: "top_side",
	TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER: "top_right_corner",
}

var _input_dialog: EditorFileDialog
var _output_dialog: EditorFileDialog
var _source_path: String = ""
var _export_format: String = "json"


func _enter_tree() -> void:
	add_tool_menu_item("Export TileSet to JSON...", _on_menu_click.bind("json"))
	add_tool_menu_item("Export TileSet to Lua...", _on_menu_click.bind("lua"))


func _exit_tree() -> void:
	remove_tool_menu_item("Export TileSet to JSON...")
	remove_tool_menu_item("Export TileSet to Lua...")
	_free_dialogs()


func _free_dialogs() -> void:
	if is_instance_valid(_input_dialog):
		_input_dialog.queue_free()
	if is_instance_valid(_output_dialog):
		_output_dialog.queue_free()
	_input_dialog = null
	_output_dialog = null


func _on_menu_click(format: String) -> void:
	_export_format = format
	_input_dialog = EditorFileDialog.new()
	_input_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	_input_dialog.add_filter("*.tres,*.res", "TileSet Resources")
	_input_dialog.title = "Select TileSet to Export"
	add_child(_input_dialog)
	_input_dialog.file_selected.connect(_on_source_picked, CONNECT_ONE_SHOT)
	_input_dialog.canceled.connect(_on_input_canceled, CONNECT_ONE_SHOT)
	_input_dialog.popup_centered(Vector2i(800, 600))


func _on_source_picked(path: String) -> void:
	_source_path = path
	if is_instance_valid(_input_dialog):
		_input_dialog.queue_free()
	_input_dialog = null

	_output_dialog = EditorFileDialog.new()
	_output_dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE

	if _export_format == "lua":
		_output_dialog.add_filter("*.lua", "Lua Files")
		_output_dialog.title = "Save Exported TileSet Lua"
		_output_dialog.current_file = "tileset.lua"
	else:
		_output_dialog.add_filter("*.json", "JSON Files")
		_output_dialog.title = "Save Exported TileSet JSON"
		_output_dialog.current_file = "tileset.json"

	add_child(_output_dialog)
	_output_dialog.file_selected.connect(_on_save_picked, CONNECT_ONE_SHOT)
	_output_dialog.canceled.connect(_on_output_canceled, CONNECT_ONE_SHOT)
	_output_dialog.popup_centered(Vector2i(800, 600))


func _on_input_canceled() -> void:
	if is_instance_valid(_input_dialog):
		_input_dialog.queue_free()
	_input_dialog = null


func _on_output_canceled() -> void:
	if is_instance_valid(_output_dialog):
		_output_dialog.queue_free()
	_output_dialog = null


func _on_save_picked(save_path: String) -> void:
	if is_instance_valid(_output_dialog):
		_output_dialog.queue_free()
	_output_dialog = null

	var res = ResourceLoader.load(_source_path)
	if not res is TileSet:
		push_error("[TileSet Exporter] Not a TileSet: " + _source_path)
		return

	var data := _build_export(res as TileSet)
	var text: String

	if _export_format == "lua":
		text = _to_lua_file(data)
	else:
		text = JSON.stringify(data, "\t")

	var f := FileAccess.open(save_path, FileAccess.WRITE)
	if f == null:
		push_error("[TileSet Exporter] Cannot write: " + save_path)
		return
	f.store_string(text)
	f.close()

	var tiles: Array = data.get("tiles", [])
	var tsets: Array = data.get("terrain_sets", [])
	print("[TileSet Exporter] %s: %d tiles (%dx%d grid), %d terrain set(s) → %s" % [
		_export_format.to_upper(), tiles.size(), data["columns"], data["rows"], tsets.size(), save_path
	])


# ──────────────────────────────────────────────
#  Build export dictionary
# ──────────────────────────────────────────────

func _build_export(ts: TileSet) -> Dictionary:
	var d: Dictionary = {}
	d["format_version"] = 1

	var sz := ts.get_tile_size()
	d["tile_width"] = sz.x
	d["tile_height"] = sz.y

	# ── Custom data layers ──
	var layers: Array = []
	for i in range(ts.get_custom_data_layers_count()):
		layers.append({
			"name": ts.get_custom_data_layer_name(i),
			"type": _type_str(ts.get_custom_data_layer_type(i)),
		})
	d["custom_data_layers"] = layers

	# ── Terrain sets ──
	var terrain_sets: Array = []
	var mode_map: Dictionary = {}
	for tsi in range(ts.get_terrain_sets_count()):
		var mode := ts.get_terrain_set_mode(tsi)
		mode_map[tsi] = mode
		var tset: Dictionary = {
			"mode": _mode_str(mode),
			"terrains": [],
		}
		for ti in range(ts.get_terrains_count(tsi)):
			tset["terrains"].append({
				"name": ts.get_terrain_name(tsi, ti),
				"color": _color_hex(ts.get_terrain_color(tsi, ti)),
			})
		terrain_sets.append(tset)
	d["terrain_sets"] = terrain_sets

	# ── Tiles ──
	var all_tiles: Array = []
	var bitmask_map: Dictionary = {}
	var tex_path := ""

	for si in range(ts.get_source_count()):
		var sid := ts.get_source_id(si)
		var src = ts.get_source(sid)
		if not src is TileSetAtlasSource:
			continue
		var atlas: TileSetAtlasSource = src as TileSetAtlasSource

		if atlas.texture:
			tex_path = atlas.texture.resource_path

			# columns / rows from texture dimensions
			var tex_size: Vector2i = atlas.texture.get_size()
			var region: Vector2i = atlas.texture_region_size
			var margin: Vector2i = atlas.margins
			var sep: Vector2i = atlas.separation
			if region.x > 0 and region.y > 0:
				d["columns"] = floori((tex_size.x - 2 * margin.x + sep.x) / float(region.x + sep.x))
				d["rows"] = floori((tex_size.y - 2 * margin.y + sep.y) / float(region.y + sep.y))

		# Iterate only defined tiles
		for i in range(atlas.get_tiles_count()):
			var coords: Vector2i = atlas.get_tile_id(i)
			var info := _tile_info(atlas, coords, 0, ts, mode_map)
			if info.is_empty():
				continue
			all_tiles.append(info)

			var tsi_key: int = info.get("terrain_set", -1)
			var ti_key: int = info.get("terrain", -1)
			if tsi_key >= 0 and ti_key >= 0:
				if not bitmask_map.has(tsi_key):
					bitmask_map[tsi_key] = {}
				if not bitmask_map[tsi_key].has(ti_key):
					bitmask_map[tsi_key][ti_key] = {}
				var bm: int = info.get("bitmask", -1)
				if bm >= 0:
					bitmask_map[tsi_key][ti_key][bm] = [coords.x, coords.y]

	d["texture_path"] = tex_path.get_file() if tex_path else ""
	d["texture_res_path"] = tex_path
	d["tiles"] = all_tiles
	d["bitmask_map"] = bitmask_map

	return d


# ──────────────────────────────────────────────
#  Extract single tile info
# ──────────────────────────────────────────────

func _tile_info(atlas: TileSetAtlasSource, coords: Vector2i, alt: int, ts: TileSet, mode_map: Dictionary) -> Dictionary:
	var td: TileData = atlas.get_tile_data(coords, alt)
	if td == null:
		return {}

	var info: Dictionary = {
		"col": coords.x,
		"row": coords.y,
	}

	var tsi: int = td.get_terrain_set()
	var ti: int = td.get_terrain()
	info["terrain_set"] = tsi
	info["terrain"] = ti

	# Peering bits → bitmask
	var dirs: Array = _dirs_for_mode(mode_map.get(tsi, 0))
	var peering: Dictionary = {}
	var bitmask := 0
	for i in range(dirs.size()):
		var dir: int = dirs[i]
		var val: int = td.get_terrain_peering_bit(dir)
		if val >= 0:
			peering[DIR_NAMES.get(dir, str(dir))] = val
			bitmask |= (1 << i)
	info["peering_bits"] = peering
	info["bitmask"] = bitmask

	# Custom data
	var cd_count := ts.get_custom_data_layers_count()
	if cd_count > 0:
		var custom: Dictionary = {}
		for j in range(cd_count):
			var lname := ts.get_custom_data_layer_name(j)
			custom[lname] = td.get_custom_data(lname)
		info["custom_data"] = custom

	return info


# ──────────────────────────────────────────────
#  Lua serializer
# ──────────────────────────────────────────────

func _to_lua_file(data: Dictionary) -> String:
	return "-- Auto-generated by TileSet Exporter\nreturn " + _lua_val(data, 0) + "\n"


func _lua_val(v: Variant, depth: int) -> String:
	var t := typeof(v)
	match t:
		TYPE_NIL: return "nil"
		TYPE_BOOL: return "true" if v else "false"
		TYPE_INT, TYPE_FLOAT: return str(v)
		TYPE_STRING: return _lua_str(v)
		TYPE_ARRAY: return _lua_array(v, depth)
		TYPE_DICTIONARY: return _lua_dict(v, depth)
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return "{%d, %d}" % [v.x, v.y]
		_: return "nil"


func _lua_str(s: String) -> String:
	var escaped := s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
	return "\"%s\"" % escaped


func _lua_array(arr: Array, depth: int) -> String:
	if arr.is_empty():
		return "{}"

	var inner_pad := "    ".repeat(depth + 1)
	var outer_pad := "    ".repeat(depth)

	if arr.size() <= 4:
		var short := true
		for item in arr:
			var t := typeof(item)
			if t == TYPE_ARRAY or t == TYPE_DICTIONARY:
				short = false
				break
		if short:
			var parts: PackedStringArray = []
			for item in arr:
				parts.append(_lua_val(item, depth + 1))
			return "{ " + ", ".join(parts) + " }"

	var lines: PackedStringArray = []
	for item in arr:
		lines.append(inner_pad + _lua_val(item, depth + 1) + ",")
	return "{\n" + "\n".join(lines) + "\n" + outer_pad + "}"


func _lua_dict(dict: Dictionary, depth: int) -> String:
	if dict.is_empty():
		return "{}"

	var inner_pad := "    ".repeat(depth + 1)
	var outer_pad := "    ".repeat(depth)
	var keys := dict.keys()

	var lines: PackedStringArray = []
	for key in keys:
		var key_str: String
		if typeof(key) == TYPE_INT or typeof(key) == TYPE_FLOAT:
			key_str = "[%d]" % key
		elif typeof(key) == TYPE_STRING:
			key_str = "[%s]" % _lua_str(key)
		else:
			key_str = "[%s]" % _lua_val(key, depth)
		lines.append(inner_pad + key_str + " = " + _lua_val(dict[key], depth + 1) + ",")

	return "{\n" + "\n".join(lines) + "\n" + outer_pad + "}"


# ──────────────────────────────────────────────
#  Helpers
# ──────────────────────────────────────────────

func _dirs_for_mode(mode: int) -> Array:
	match mode:
		0: return DIR_TILES
		1: return DIR_CORNERS
		2: return DIR_SIDES
		_: return DIR_TILES


func _mode_str(mode: int) -> String:
	match mode:
		0: return "tiles"
		1: return "corners"
		2: return "sides"
		_: return "unknown"


func _type_str(t: int) -> String:
	match t:
		TYPE_BOOL: return "bool"
		TYPE_INT: return "int"
		TYPE_FLOAT: return "float"
		TYPE_STRING: return "string"
		TYPE_VECTOR2: return "vector2"
		TYPE_VECTOR2I: return "vector2i"
		TYPE_RECT2: return "rect2"
		TYPE_RECT2I: return "rect2i"
		TYPE_COLOR: return "color"
		_: return "variant"


func _color_hex(c: Color) -> String:
	return "#%02x%02x%02x" % [int(c.r * 255), int(c.g * 255), int(c.b * 255)]