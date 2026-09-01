class_name TroSchema
extends RefCounted

# trogue 资产序列化核心。编辑器菜单（scene_exporter.gd）与 headless 导出
# （headless_export.gd）共用本文件的全部逻辑。
#
# 产物（相对引擎仓库根 trogue/）：
#   assets/tilesets/<name>.json   tro-tileset v1
#   assets/scenes/<name>.json     tro-scene v1.1
#   assets/textures/<name>.png    贴图（自动拷贝）
#
# 坐标约定：Godot 与 tro-scene 一致（原点左上、y 向下、cell(0,0)=像素(0,0)），零换算。

const DEFAULT_BACKGROUND := "#101018"
const DEFAULT_COLOR := "#ffffff"


# ──────────────────────────────────────────────
#  对外入口
# ──────────────────────────────────────────────

# 导出 TileSet 资源 → tro-tileset v1 JSON + 拷贝贴图。
static func export_tileset_from_file(res_path: String) -> Dictionary:
	var ts := ResourceLoader.load(res_path)
	if ts == null:
		return _fail("无法加载资源: %s" % res_path)
	if not (ts is TileSet):
		return _fail("不是 TileSet: %s" % res_path)

	var built := build_tileset(ts as TileSet)
	if not built.ok:
		return _fail(built.error)

	var base := res_path.get_file().get_basename()
	var out_path := _assets_dir().path_join("tilesets/%s.json" % base)

	if not _write_json(out_path, built.data):
		return _fail("无法写出: %s" % out_path)

	var log: Array[String] = ["tro-tileset → %s (%d tiles)" % [out_path, built.data.tiles.size()]]
	if built.texture_copied != "":
		log.append("贴图拷贝 → %s" % built.texture_copied)
	for w in built.warnings:
		log.append("警告: " + w)
	return {"ok": true, "log": log}


# 导出场景 → tro-scene v1.1 JSON（连同其 TileSet 一起导出）。
# tree_root: 用于挂载实例化节点以计算 global_position（编辑器传 get_tree().root，headless 传 root）。
static func export_scene_from_file(scene_path: String, tree_root: Node) -> Dictionary:
	var ps := ResourceLoader.load(scene_path)
	if ps == null:
		return _fail("无法加载场景: %s" % scene_path)
	if not (ps is PackedScene):
		return _fail("不是 PackedScene: %s" % scene_path)

	var inst: Node = (ps as PackedScene).instantiate()
	tree_root.add_child(inst)  # 入树才能取 global_position
	var result := build_scene(inst)
	tree_root.remove_child(inst)
	inst.free()

	if not result.ok:
		return _fail(result.error)

	var data: Dictionary = result.data
	var log: Array[String] = []

	# ── 场景引用的 tileset：先导 tileset，再导场景 ──
	if data.has("_tileset_resource_path"):
		var ts_res_path: String = data["_tileset_resource_path"]
		data.erase("_tileset_resource_path")

		var ts := ResourceLoader.load(ts_res_path)
		if ts == null or not (ts is TileSet):
			return _fail("场景 tileset 无法加载: " + ts_res_path)
		var built := build_tileset(ts as TileSet)
		if not built.ok:
			return _fail(built.error)

		var ts_base := ts_res_path.get_file().get_basename()
		var ts_out := _assets_dir().path_join("tilesets/%s.json" % ts_base)
		if not _write_json(ts_out, built.data):
			return _fail("无法写出: %s" % ts_out)
		data.tilemap["tileset"] = "tilesets/%s.json" % ts_base
		log.append("tro-tileset → %s (%d tiles)" % [ts_out, built.data.tiles.size()])
		if built.texture_copied != "":
			log.append("贴图拷贝 → %s" % built.texture_copied)
		for w in built.warnings:
			log.append("警告: " + w)

	var base := scene_path.get_file().get_basename()
	var out_path := _assets_dir().path_join("scenes/%s.json" % base)
	if not _write_json(out_path, data):
		return _fail("无法写出: %s" % out_path)

	log.append("tro-scene → %s (%d layers, %d entities)" % [
		out_path, data.tilemap.layers.size(), data.entities.size()])
	return {"ok": true, "log": log}


# ──────────────────────────────────────────────
#  tro-tileset 构建
# ──────────────────────────────────────────────

# 返回 {ok, data, index, texture_copied, warnings}；index: "src:x:y" → tile id
static func build_tileset(ts: TileSet) -> Dictionary:
	var d := {
		"format": "tro-tileset",
		"version": 1,
		"tile_width": ts.get_tile_size().x,
		"tile_height": ts.get_tile_size().y,
		"tiles": [],
	}
	var warnings: Array[String] = []
	var index := {}
	var next_id := 0
	var texture_src := ""
	var texture_copied := ""
	var first_atlas: TileSetAtlasSource = null

	# custom_data 层名集合（透传）
	var cd_names: Array[String] = []
	for i in range(ts.get_custom_data_layers_count()):
		cd_names.append(ts.get_custom_data_layer_name(i))

	for si in range(ts.get_source_count()):
		var sid := ts.get_source_id(si)
		var src = ts.get_source(sid)
		if not (src is TileSetAtlasSource):
			warnings.append("source %d 不是 AtlasSource，v1 跳过（scene tiles 后置支持）" % sid)
			continue
		var atlas := src as TileSetAtlasSource
		if first_atlas == null:
			first_atlas = atlas

		# 贴图：只处理第一个有贴图的 source，v1 限定单贴图
		if atlas.texture != null:
			if texture_src == "":
				texture_src = atlas.texture.resource_path
			elif atlas.texture.resource_path != texture_src:
				warnings.append("source %d 使用了不同贴图，v1 限定单贴图，已忽略" % sid)
				continue

		for i in range(atlas.get_tiles_count()):
			var coords: Vector2i = atlas.get_tile_id(i)
			var entry := {"id": next_id, "col": coords.x, "row": coords.y}

			var td := atlas.get_tile_data(coords, 0)
			if td != null and cd_names.size() > 0:
				var cd := {}
				for lname in cd_names:
					cd[lname] = td.get_custom_data(lname)
				entry["custom_data"] = cd
			# terrain / peering_bits 透传（autotile 阶段消费）
			if td != null:
				entry["terrain_set"] = td.get_terrain_set()
				entry["terrain"] = td.get_terrain()

			index["%d:%d:%d" % [sid, coords.x, coords.y]] = next_id
			d.tiles.append(entry)
			next_id += 1

	if texture_src != "":
		var tex_name := texture_src.get_file()
		d["texture"] = "textures/" + tex_name
		var err := _copy_texture(texture_src)
		if err != "":
			warnings.append(err)
		else:
			texture_copied = _assets_dir().path_join("textures/" + tex_name)

	if first_atlas != null and first_atlas.texture != null:
		var region: Vector2i = first_atlas.texture_region_size
		var margin: Vector2i = first_atlas.margins
		var sep: Vector2i = first_atlas.separation
		if region.x > 0 and region.y > 0:
			d["columns"] = floori((first_atlas.texture.get_width() - 2 * margin.x + sep.x) / float(region.x + sep.x))
			d["rows"] = floori((first_atlas.texture.get_height() - 2 * margin.y + sep.y) / float(region.y + sep.y))

	return {"ok": true, "data": d, "index": index,
			"texture_copied": texture_copied, "warnings": warnings}


# ──────────────────────────────────────────────
#  tro-scene 构建
# ──────────────────────────────────────────────

static func build_scene(root: Node) -> Dictionary:
	var data := {
		"format": "tro-scene",
		"version": 1,
		"meta": {"name": root.name, "background": DEFAULT_BACKGROUND},
		"tilemap": {"layers": []},
		"entities": [],
	}
	if root.has_meta("background"):
		data.meta.background = _variant_to_json(root.get_meta("background"))

	# 场景根的 metadata "tileset" 可显式指定 TileSet 资源；否则从 TileMapLayer 收集
	var ts_res_path := ""
	if root.has_meta("tileset"):
		ts_res_path = str(root.get_meta("tileset"))

	var layers: Array = []
	var entities: Array = []
	var used_ids := {}
	var ts: TileSet = null

	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)

		if node is TileMapLayer:
			var layer := node as TileMapLayer
			if layer.tile_set == null:
				return _fail("TileMapLayer '%s' 没有设置 tile_set" % layer.name)
			if ts == null:
				ts = layer.tile_set
				if ts_res_path == "":
					ts_res_path = ts.resource_path
			elif ts != layer.tile_set:
				return _fail("v1 限定整个场景共用一个 TileSet（'%s' 使用了不同 tileset）" % layer.name)

			var layer_data := _build_layer(layer, ts)
			if not layer_data.ok:
				return _fail(layer_data.error)
			for w in layer_data.warnings:
				push_warning("[scene_exporter] " + w)
			if layer_data.data.width == 0:
				push_warning("[scene_exporter] 跳过空层 '%s'" % layer.name)
			else:
				layers.append(layer_data.data)

		elif node != root and node.has_meta("type"):
			var ent := _build_entity(node, used_ids, ts)
			if not ent.ok:
				return _fail(ent.error)
			entities.append(ent.data)

	if ts == null:
		return _fail("场景没有任何 TileMapLayer；tro-scene v1 的 tilemap 为必填，纯实体场景暂不支持")
	data.tilemap["tile_width"] = ts.get_tile_size().x
	data.tilemap["tile_height"] = ts.get_tile_size().y
	data.tilemap["layers"] = layers
	data["entities"] = entities
	data["_tileset_resource_path"] = ts_res_path
	return {"ok": true, "data": data}


static func _build_layer(layer: TileMapLayer, ts: TileSet) -> Dictionary:
	var cells := layer.get_used_cells()
	if cells.is_empty():
		return {"ok": true, "data": {
				"name": layer.name, "width": 0, "height": 0,
				"solid": false, "origin": [0, 0], "tiles": []}, "warnings": []}

	var tw := ts.get_tile_size().x
	var th := ts.get_tile_size().y

	# 层矩形覆盖全部 used cell（可含负坐标），origin = 层节点位置 + 左上角 cell 的世界像素坐标。
	# 负坐标 cell 是正常关卡设计（如外围装饰环），由 origin 表达偏移而非报错；
	# TileMapLayer 节点自身被移动时其 transform 也计入 origin。
	var min_x := cells[0].x
	var min_y := cells[0].y
	var max_x := cells[0].x
	var max_y := cells[0].y
	for c in cells:
		min_x = min(min_x, c.x)
		min_y = min(min_y, c.y)
		max_x = max(max_x, c.x)
		max_y = max(max_y, c.y)
	var width := max_x - min_x + 1
	var height := max_y - min_y + 1

	var tiles := []
	tiles.resize(width * height)
	tiles.fill(-1)

	# 图集坐标 → tile id 索引
	var built := build_tileset(ts)
	if not built.ok:
		return _fail(built.error)
	var index: Dictionary = built.index

	var warnings: Array[String] = []
	var skipped := 0
	for c in cells:
		var sid := layer.get_cell_source_id(c)
		var coords := layer.get_cell_atlas_coords(c)
		var alt := layer.get_cell_alternative_tile(c)
		var key := "%d:%d:%d" % [sid, coords.x, coords.y]
		if not index.has(key):
			skipped += 1
			continue
		if alt != 0:
			warnings.append("cell %s 使用了 alternative tile，v1 忽略" % c)
		tiles[(c.y - min_y) * width + (c.x - min_x)] = index[key]

	if skipped > 0:
		warnings.append("layer '%s': %d 个 cell 未收录进 tileset，已置空" % [layer.name, skipped])

	var gp: Vector2 = layer.global_position
	return {"ok": true, "data": {
			"name": layer.name,
			"width": width, "height": height,
			"solid": bool(layer.get_meta("solid", false)),
			"origin": [roundi(gp.x) + min_x * tw, roundi(gp.y) + min_y * th],
			"tiles": tiles},
		"warnings": warnings}


static func _build_entity(node: Node, used_ids: Dictionary, ts: TileSet) -> Dictionary:
	var etype := str(node.get_meta("type"))
	var node_name := str(node.name)
	if node_name == "" or node_name == ".":
		return _fail("实体节点缺少有效名称（type=%s）" % etype)

	var id := node_name
	var n := 2
	while used_ids.has(id):
		id = "%s_%d" % [node_name, n]
		n += 1
	used_ids[id] = true

	var tw := 16
	var th := 16
	if ts != null:
		tw = ts.get_tile_size().x
		th = ts.get_tile_size().y

	var x := 0.0
	var y := 0.0
	if node is Node2D:
		var p: Vector2 = (node as Node2D).global_position
		x = p.x
		y = p.y
	else:
		return _fail("实体节点 '%s' 必须是 Node2D 派生" % id)

	var ent := {
		"id": id,
		"type": etype,
		"x": x, "y": y,
		"w": float(node.get_meta("w", tw)),
		"h": float(node.get_meta("h", th)),
		# color 可能是 "#rrggbb" 字符串或 Godot Color 类型，统一经 _variant_to_json 规范化
		"color": _variant_to_json(node.get_meta("color", DEFAULT_COLOR)),
	}

	# 其余 metadata → props（引擎 v0.1 忽略，为玩法移植预留）
	var props := {}
	for meta_name in node.get_meta_list():
		if meta_name in ["type", "w", "h", "color", "solid", "background"]:
			continue
		props[meta_name] = _variant_to_json(node.get_meta(meta_name))
	if not props.is_empty():
		ent["props"] = props

	return {"ok": true, "data": ent}


# ──────────────────────────────────────────────
#  工具
# ──────────────────────────────────────────────

static func _assets_dir() -> String:
	return ProjectSettings.globalize_path("res://").path_join("../assets")


static func _fail(msg: String) -> Dictionary:
	return {"ok": false, "error": msg}


static func _write_json(abs_path: String, data: Dictionary) -> bool:
	DirAccess.make_dir_recursive_absolute(abs_path.get_base_dir())
	var f := FileAccess.open(abs_path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(data, "\t"))
	f.close()
	return true


static func _copy_texture(res_path: String) -> String:
	var src := ProjectSettings.globalize_path(res_path)
	var dst := _assets_dir().path_join("textures/" + res_path.get_file())
	DirAccess.make_dir_recursive_absolute(dst.get_base_dir())
	var rf := FileAccess.open(src, FileAccess.READ)
	if rf == null:
		return "贴图读取失败: " + src
	var buf := rf.get_buffer(rf.get_length())
	rf.close()
	var wf := FileAccess.open(dst, FileAccess.WRITE)
	if wf == null:
		return "贴图写出失败: " + dst
	wf.store_buffer(buf)
	wf.close()
	return ""


# Godot Variant → JSON 安全值； exotic 类型降级为字符串
static func _variant_to_json(v: Variant) -> Variant:
	match typeof(v):
		TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return v
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return {"x": v.x, "y": v.y}
		TYPE_COLOR:
			return "#%02x%02x%02x" % [roundi(v.r * 255), roundi(v.g * 255), roundi(v.b * 255)]
		_:
			return str(v)
