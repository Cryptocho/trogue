class_name TroSchema
extends RefCounted

# trogue 资产序列化核心 v3（tro-scene / tro-tileset v2）。
# 编辑器菜单（scene_exporter.gd）与 headless 导出（headless_export.gd）共用本文件的全部逻辑。
#
# 产物（相对引擎仓库根 trogue/）：
#   assets/tilesets/<name>.json   tro-tileset v2（每贴图一个）
#   assets/scenes/<name>.json     tro-scene v2
#   assets/textures/<name>.png    贴图（自动拷贝）
#
# v3 能力：多 TileSet/多贴图（tilemap.tilesets + 层级引用）、实体 sprite
# （实体 Sprite2D 或场景 tile 自动转实体）、peering_bits 透传、实体 z/solid。
#
# 坐标约定：Godot 与 tro-scene 一致（原点左上、y 向下、cell(0,0)=像素(0,0)），零换算。

const SCENE_FORMAT := "tro-scene"
const SCENE_VERSION := 2
const TILESET_FORMAT := "tro-tileset"
const TILESET_VERSION := 2

const DEFAULT_BACKGROUND := "#101018"
const DEFAULT_COLOR := "#ffffff"
const MAX_TILESETS := 8

# 保留 metadata 名（不进 props）；z = 渲染排序键，solid = 实体/层碰撞标记
const RESERVED_META := ["type", "w", "h", "color", "solid", "background", "z"]

# Godot TileSet.TerrainMode
const TERRAIN_MODE_CORNERS := 0
const TERRAIN_MODE_SIDES := 1
const TERRAIN_MODE_CORNERS_AND_SIDES := 2


# ──────────────────────────────────────────────
#  对外入口
# ──────────────────────────────────────────────

# 导出 TileSet 资源 → tro-tileset v2 JSON（按贴图分组，可产出多个）+ 拷贝贴图。
static func export_tileset_from_file(res_path: String) -> Dictionary:
	var ts := ResourceLoader.load(res_path)
	if ts == null:
		return _fail("无法加载资源: %s" % res_path)
	if not (ts is TileSet):
		return _fail("不是 TileSet: %s" % res_path)

	var built := build_tileset_groups(ts as TileSet, res_path.get_file().get_basename())
	if not built.ok:
		return _fail(built.error)

	var log: Array[String] = []
	if built.groups.is_empty():
		log.append("警告: 该 TileSet 没有可导出的图集 tile")
	var written := _write_tileset_groups(built.groups, {}, log)
	if not written.ok:
		return _fail(written.error)
	log.append_array(built.warnings)
	return {"ok": true, "log": log}


# 导出场景 → tro-scene v2 JSON（连同其引用的全部 TileSet）。
# tree_root: 用于挂载实例化节点以计算 global_position（编辑器传 get_tree().root，headless 传 root）。
static func export_scene_from_file(scene_path: String, tree_root: Node) -> Dictionary:
	var ps := ResourceLoader.load(scene_path)
	if ps == null:
		return _fail("无法加载场景: %s" % scene_path)
	if not (ps is PackedScene):
		return _fail("不是 PackedScene: %s" % scene_path)

	var inst: Node = (ps as PackedScene).instantiate()
	tree_root.add_child(inst)  # 入树才能取 global_position

	# 第一遍：收集 TileSet 资源，构建分组（确定场景内 tileset 占位符与 tile 索引）
	var ts_res_paths := _collect_tileset_paths(inst)
	if ts_res_paths.is_empty():
		tree_root.remove_child(inst)
		inst.free()
		return _fail("场景没有任何设置 tile_set 的 TileMapLayer")

	var ts_groups := {}      # TileSet 资源路径 → build_tileset_groups 结果
	var tex_srcs := {}       # 独立贴图 rel 路径 → 编辑器源路径（build_scene 收集）
	var group_total := 0
	var warnings: Array[String] = []
	for p in ts_res_paths:
		var ts := ResourceLoader.load(p)
		if ts == null or not (ts is TileSet):
			tree_root.remove_child(inst)
			inst.free()
			return _fail("场景 tileset 无法加载: " + p)
		var built := build_tileset_groups(ts as TileSet, p.get_file().get_basename())
		ts_groups[p] = built
		warnings.append_array(built.warnings)
		group_total += built.groups.size()
	if group_total == 0:
		tree_root.remove_child(inst)
		inst.free()
		return _fail("场景没有任何可导出的图集 tile（tro-scene v2 需要 tilesets，palette 模式仅限手写场景）")
	if group_total > MAX_TILESETS:
		tree_root.remove_child(inst)
		inst.free()
		return _fail("场景共有 %d 个贴图组，超过 v2 限额 %d" % [group_total, MAX_TILESETS])

	var result := build_scene(inst, ts_groups, tex_srcs)
	tree_root.remove_child(inst)
	inst.free()
	if not result.ok:
		return _fail(result.error)

	var data: Dictionary = result.data
	warnings.append_array(result.warnings)
	var log: Array[String] = []

	# ── 拷贝实体/场景 tile 的独立贴图（tileset 贴图在 _write_tileset_groups 已拷）──
	var seen_tex := {}  # basename → 源路径（检测同名异文件）
	for rel in result.tex_srcs:
		var src: String = result.tex_srcs[rel]
		var base := src.get_file()
		if seen_tex.has(base) and seen_tex[base] != src:
			warnings.append("独立贴图重名：%s 与 %s 同名，后者未拷贝，请改名" % [seen_tex[base], src])
			continue
		seen_tex[base] = src
		var err := _copy_texture(src)
		if err != "":
			warnings.append(err)

	# ── 逐 TileSet 写 tro-tileset JSON（场景内 name 去重）──
	var used_names := {}
	var group_names := {}  # 占位符 "res路径#组序号" → 场景内最终 name
	for p in ts_res_paths:
		var written := _write_tileset_groups(ts_groups[p].groups, used_names, log)
		if not written.ok:
			return _fail(written.error)
		for gi in range(ts_groups[p].groups.size()):
			group_names["%s#%d" % [p, gi]] = written.names[gi]

	# ── 解析占位符：层与实体 sprite 引用的全部贴图组 → tilemap.tilesets（去重保序）──
	# 实体图集 sprite 可能引用没有层使用的组，必须一并登记，否则引擎会拒绝载入
	var all_refs: Array[String] = []
	for layer_data in data.tilemap.layers:
		if not all_refs.has(layer_data["_tileset_ref"]):
			all_refs.append(layer_data["_tileset_ref"])
	for e in data.entities:
		if e.has("sprite") and e.sprite.has("tileset") and not all_refs.has(e.sprite.tileset):
			all_refs.append(e.sprite.tileset)
	var resolved: Array = []
	for ref in all_refs:
		var name: String = group_names.get(ref, "")
		if name == "":
			return _fail("内部错误：tileset 占位符 %s 未解析" % ref)
		resolved.append({"name": name, "path": "tilesets/%s.json" % name})
	data.tilemap.tilesets = resolved
	for layer_data in data.tilemap.layers:
		layer_data["tileset"] = group_names[layer_data["_tileset_ref"]]
		layer_data.erase("_tileset_ref")
	for e in data.entities:
		if e.has("sprite") and e.sprite.has("tileset"):
			e.sprite["tileset"] = group_names[e.sprite.tileset]

	var base := scene_path.get_file().get_basename()
	var out_path := _assets_dir().path_join("scenes/%s.json" % base)
	if not _write_json(out_path, data):
		return _fail("无法写出: %s" % out_path)

	log.append("tro-scene → %s (%d layers, %d entities, %d tilesets)" % [
		out_path, data.tilemap.layers.size(), data.entities.size(), data.tilemap.tilesets.size()])
	log.append_array(warnings)
	return {"ok": true, "log": log}


# ──────────────────────────────────────────────
#  tro-tileset 构建（一个 TileSet 按贴图分成多个组）
# ──────────────────────────────────────────────

# 返回 {ok, groups, warnings}；group = {name, texture_src, data, index, warnings}。
# data 即 tro-tileset v2 JSON；index: "source:x:y" → tile id。
static func build_tileset_groups(ts: TileSet, base_name: String) -> Dictionary:
	var warnings: Array[String] = []
	var by_texture := {}  # 贴图资源路径 → group

	for si in range(ts.get_source_count()):
		var sid := ts.get_source_id(si)
		var src = ts.get_source(sid)
		if src is TileSetScenesCollectionSource:
			continue  # 场景 tile 由 build_scene 转实体，不进 tileset
		if not (src is TileSetAtlasSource):
			warnings.append("TileSet '%s': source %d 不是 AtlasSource，跳过" % [base_name, sid])
			continue
		var atlas := src as TileSetAtlasSource
		if atlas.texture == null:
			continue  # 无贴图 source（如预留空 source），静默跳过
		var tex_path: String = atlas.texture.resource_path
		if tex_path == "":
			warnings.append("TileSet '%s': source %d 的贴图未保存到磁盘，跳过" % [base_name, sid])
			continue

		if not by_texture.has(tex_path):
			var g := {
				"name": base_name if by_texture.is_empty() else "%s_%d" % [base_name, by_texture.size()],
				"texture_src": tex_path,
				"data": {
					"format": TILESET_FORMAT, "version": TILESET_VERSION,
					"tile_width": ts.get_tile_size().x,
					"tile_height": ts.get_tile_size().y,
					"tiles": [],
				},
				"index": {},
				"warnings": [],
			}
			_append_terrain_sets(g.data, ts)
			by_texture[tex_path] = g
		var group: Dictionary = by_texture[tex_path]

		for i in range(atlas.get_tiles_count()):
			var coords: Vector2i = atlas.get_tile_id(i)
			var td := atlas.get_tile_data(coords, 0)
			var id: int = group.data.tiles.size()
			var entry := {"id": id, "col": coords.x, "row": coords.y}
			if td != null:
				entry["terrain_set"] = td.get_terrain_set()
				entry["terrain"] = td.get_terrain()
				_append_peering_bits(entry, ts, td)
				var cd := {}
				for j in range(ts.get_custom_data_layers_count()):
					var lname := ts.get_custom_data_layer_name(j)
					cd[lname] = td.get_custom_data(lname)
				if not cd.is_empty():
					entry["custom_data"] = cd
			group.index["%d:%d:%d" % [sid, coords.x, coords.y]] = id
			group.data.tiles.append(entry)

	var groups: Array = by_texture.values()
	for g in groups:
		_fill_texture_metrics(g, ts)
	return {"ok": true, "groups": groups, "warnings": warnings}


# columns/rows 由该贴图第一个 atlas source 的布局推导（透视 padding/margin/separation）
static func _fill_texture_metrics(group: Dictionary, ts: TileSet) -> void:
	for si in range(ts.get_source_count()):
		var src = ts.get_source(ts.get_source_id(si))
		if not (src is TileSetAtlasSource):
			continue
		var atlas := src as TileSetAtlasSource
		if atlas.texture == null or atlas.texture.resource_path != group.texture_src:
			continue
		var region: Vector2i = atlas.texture_region_size
		if region.x <= 0 or region.y <= 0:
			return
		var margin: Vector2i = atlas.margins
		var sep: Vector2i = atlas.separation
		group.data["columns"] = floori((atlas.texture.get_width() - 2 * margin.x + sep.x) / float(region.x + sep.x))
		group.data["rows"] = floori((atlas.texture.get_height() - 2 * margin.y + sep.y) / float(region.y + sep.y))
		return


# terrain_sets 透传：[{mode, terrains: [{name, color}]}]
static func _append_terrain_sets(data: Dictionary, ts: TileSet) -> void:
	var sets: Array = []
	for i in range(ts.get_terrain_sets_count()):
		var terrains: Array = []
		for j in range(ts.get_terrains_count(i)):
			var c := ts.get_terrain_color(i, j)
			terrains.append({
				"name": ts.get_terrain_name(i, j),
				"color": "#%02x%02x%02x" % [roundi(c.r * 255), roundi(c.g * 255), roundi(c.b * 255)],
			})
		var mode_name := "sides"
		match ts.get_terrain_set_mode(i):
			TERRAIN_MODE_CORNERS:
				mode_name = "corners"
			TERRAIN_MODE_CORNERS_AND_SIDES:
				mode_name = "corners_and_sides"
		sets.append({"mode": mode_name, "terrains": terrains})
	if not sets.is_empty():
		data["terrain_sets"] = sets


# peering_bits：按 terrain set 的 mode 决定导出哪些邻位，值 = terrain 序号，未连接（-1）省略
static func _append_peering_bits(entry: Dictionary, ts: TileSet, td: TileData) -> void:
	if td.get_terrain_set() < 0 or td.get_terrain_set() >= ts.get_terrain_sets_count():
		return
	var bits: Array = []
	match ts.get_terrain_set_mode(td.get_terrain_set()):
		TERRAIN_MODE_SIDES:
			bits = [TileSet.CELL_NEIGHBOR_RIGHT_SIDE, TileSet.CELL_NEIGHBOR_BOTTOM_SIDE,
					TileSet.CELL_NEIGHBOR_LEFT_SIDE, TileSet.CELL_NEIGHBOR_TOP_SIDE]
		TERRAIN_MODE_CORNERS:
			bits = [TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER, TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER,
					TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER, TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER]
		TERRAIN_MODE_CORNERS_AND_SIDES:
			bits = [TileSet.CELL_NEIGHBOR_RIGHT_SIDE, TileSet.CELL_NEIGHBOR_BOTTOM_SIDE,
					TileSet.CELL_NEIGHBOR_LEFT_SIDE, TileSet.CELL_NEIGHBOR_TOP_SIDE,
					TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER, TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER,
					TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER, TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER]
	var bits_out := {}
	for bit in bits:
		var v: int = td.get_terrain_peering_bit(bit)
		if v >= 0:
			bits_out[_peering_name(bit)] = v
	if not bits_out.is_empty():
		entry["peering_bits"] = bits_out


static func _peering_name(bit: int) -> String:
	match bit:
		TileSet.CELL_NEIGHBOR_RIGHT_SIDE: return "right_side"
		TileSet.CELL_NEIGHBOR_BOTTOM_SIDE: return "bottom_side"
		TileSet.CELL_NEIGHBOR_LEFT_SIDE: return "left_side"
		TileSet.CELL_NEIGHBOR_TOP_SIDE: return "top_side"
		TileSet.CELL_NEIGHBOR_TOP_LEFT_CORNER: return "top_left_corner"
		TileSet.CELL_NEIGHBOR_TOP_RIGHT_CORNER: return "top_right_corner"
		TileSet.CELL_NEIGHBOR_BOTTOM_LEFT_CORNER: return "bottom_left_corner"
		TileSet.CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER: return "bottom_right_corner"
	return "unknown_%d" % bit


# ──────────────────────────────────────────────
#  tro-scene 构建
# ──────────────────────────────────────────────

# 深度优先收集场景引用的全部 TileSet 资源路径（保序去重）
static func _collect_tileset_paths(root: Node) -> Array[String]:
	var out: Array[String] = []
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if node is TileMapLayer and (node as TileMapLayer).tile_set != null:
			var p: String = (node as TileMapLayer).tile_set.resource_path
			if p != "" and not out.has(p):
				out.append(p)
	return out


static func build_scene(root: Node, ts_groups: Dictionary, tex_srcs: Dictionary) -> Dictionary:
	var warnings: Array[String] = []
	var data := {
		"format": SCENE_FORMAT, "version": SCENE_VERSION,
		"meta": {"name": str(root.name), "background": DEFAULT_BACKGROUND},
		"tilemap": {"tilesets": [], "layers": []},
		"entities": [],
	}
	if root.has_meta("background"):
		data.meta.background = _variant_to_json(root.get_meta("background"))

	var scene_tileset_refs: Array[String] = []  # 占位符，首现顺序
	var layers: Array = []
	var normal_entities: Array = []
	var scene_entities: Array = []  # 场景 tile 转出的实体（排在数组前部，复刻 LÖVE 同行树先画）
	var used_ids := {}

	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)

		if node is TileMapLayer:
			var layer := node as TileMapLayer
			if layer.tile_set == null:
				return _fail("TileMapLayer '%s' 没有设置 tile_set" % layer.name)
			var ts_res: String = layer.tile_set.resource_path
			if ts_res == "":
				return _fail("TileMapLayer '%s' 的 TileSet 未保存为 .tres 文件（内联资源无法导出）" % layer.name)
			if not ts_groups.has(ts_res):
				return _fail("内部错误：TileSet %s 未在第一遍中收集" % ts_res)

			var ts: TileSet = layer.tile_set
			if data.tilemap.has("tile_width"):
				if ts.get_tile_size() != Vector2i(data.tilemap.tile_width, data.tilemap.tile_height):
					return _fail("TileMapLayer '%s' 的 tile 尺寸 %s 与场景 %s 不一致（v2 限定全场景一致）" % [
						layer.name, ts.get_tile_size(), Vector2i(data.tilemap.tile_width, data.tilemap.tile_height)])
			else:
				data.tilemap["tile_width"] = ts.get_tile_size().x
				data.tilemap["tile_height"] = ts.get_tile_size().y

			var layer_data := _build_layer(layer, ts, ts_res, ts_groups, scene_tileset_refs, warnings)
			if not layer_data.ok:
				return _fail(layer_data.error)
			warnings.append_array(layer_data.warnings)
			if layer_data.data.width > 0:
				layers.append(layer_data.data)
			elif layer_data.scene_cells.is_empty():
				warnings.append("跳过空层 '%s'" % layer.name)

			# 场景 tile cell → 实体（按 (y,x) 排序，保证稳定输出）
			var sc: Array = layer_data.scene_cells
			sc.sort_custom(func(a, b): return a.cell.y < b.cell.y or (a.cell.y == b.cell.y and a.cell.x < b.cell.x))
			var tile_size := ts.get_tile_size()
			var tpl_cache := {}
			for sc_entry in sc:
				var tpl: Dictionary = _scene_template(ts, sc_entry.sid, sc_entry.scene_id,
						tile_size, warnings, tex_srcs, tpl_cache)
				if tpl.is_empty():
					continue
				var cell_tl: Vector2 = layer.global_position + Vector2(sc_entry.cell * tile_size)
				var ent := _scene_tile_entity(tpl, cell_tl, used_ids)
				if not ent.is_empty():
					scene_entities.append(ent)

		elif node != root and node.has_meta("type"):
			var ent := _build_entity(node, used_ids, ts_groups, tex_srcs, warnings)
			if not ent.ok:
				return _fail(ent.error)
			normal_entities.append(ent.data)

	if not data.tilemap.has("tile_width"):
		return _fail("场景没有任何 TileMapLayer；tro-scene v2 需要至少一个 tile 层")
	data.tilemap["tilesets"] = scene_tileset_refs.duplicate()
	data.tilemap["layers"] = layers
	data["entities"] = scene_entities + normal_entities
	return {"ok": true, "data": data, "warnings": warnings, "tex_srcs": tex_srcs}


# 单个 TileMapLayer → 层数据 + 场景 tile cell 列表。
# 图集 cell → 所在贴图组（一层限一个贴图组，混用即报错）；场景 tile cell → scene_cells。
static func _build_layer(layer: TileMapLayer, ts: TileSet, ts_path: String,
		ts_groups: Dictionary, scene_tileset_refs: Array[String],
		warnings: Array[String]) -> Dictionary:
	var built: Dictionary = ts_groups[ts_path]
	var groups: Array = built.groups

	var cells := layer.get_used_cells()
	if cells.is_empty():
		return {"ok": true, "data": {
				"name": str(layer.name), "width": 0, "height": 0,
				"solid": false, "origin": [0, 0], "tiles": []},
			"scene_cells": [], "warnings": []}

	var tile_size := ts.get_tile_size()
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

	var tiles := PackedInt32Array()
	tiles.resize(width * height)
	tiles.fill(-1)

	var layer_gi := -1      # 本层使用的贴图组（一层限一个）
	var scene_cells: Array = []
	var skipped := 0
	var alt_warned := false

	for c in cells:
		var sid := layer.get_cell_source_id(c)
		var coords := layer.get_cell_atlas_coords(c)
		var alt := layer.get_cell_alternative_tile(c)
		var key := "%d:%d:%d" % [sid, coords.x, coords.y]
		var hit := _find_cell_in_groups(groups, key)
		if hit.gi >= 0:
			if layer_gi == -1:
				layer_gi = hit.gi
			elif layer_gi != hit.gi:
				return _fail("层 '%s' 混用多个贴图组（'%s' + '%s'）。v2 限定一层一张贴图，请在 Godot 里按贴图拆分 TileMapLayer" % [
					layer.name, groups[layer_gi].name, groups[hit.gi].name])
			if alt != 0 and not alt_warned:
				warnings.append("层 '%s' 使用了 alternative tile，v2 忽略其变体" % layer.name)
				alt_warned = true
			tiles[(c.y - min_y) * width + (c.x - min_x)] = hit.tile_id
		elif ts.get_source(sid) is TileSetScenesCollectionSource:
			scene_cells.append({"cell": c, "sid": sid, "scene_id": alt})
		else:
			skipped += 1

	if skipped > 0:
		warnings.append("层 '%s': %d 个 cell 未收录进 tileset，已置空" % [layer.name, skipped])

	# 层没有图集 tile（全是场景 tile 或全部未收录）→ v2 视为空层，不导出（场景 tile 仍转实体）
	if layer_gi < 0:
		return {"ok": true, "data": {
				"name": str(layer.name), "width": 0, "height": 0,
				"solid": false, "origin": [0, 0], "tiles": []},
			"scene_cells": scene_cells, "warnings": []}

	var gp: Vector2 = layer.global_position
	var data := {
		"name": str(layer.name),
		"width": width, "height": height,
		"solid": bool(layer.get_meta("solid", false)),
		"origin": [roundi(gp.x) + min_x * tile_size.x, roundi(gp.y) + min_y * tile_size.y],
		"tiles": Array(tiles),
	}
	if layer_gi >= 0:
		var ref := "%s#%d" % [ts_path, layer_gi]
		data["_tileset_ref"] = ref
		if not scene_tileset_refs.has(ref):
			scene_tileset_refs.append(ref)
	return {"ok": true, "data": data, "scene_cells": scene_cells, "warnings": []}


static func _find_cell_in_groups(groups: Array, key: String) -> Dictionary:
	for gi in range(groups.size()):
		if groups[gi].index.has(key):
			return {"gi": gi, "tile_id": int(groups[gi].index[key])}
	return {"gi": -1, "tile_id": -1}


# ── 场景 tile（TileSetScenesCollectionSource）→ 实体 ──

# 提取模板信息（每 (source_id, scene_id) 只实例化一次，经 tpl_cache 复用）。
# tex_srcs: 独立贴图 rel 路径 → 编辑器源路径（导出时统一拷贝）。返回空 Dictionary 表示跳过。
static func _scene_template(ts: TileSet, sid: int, scene_id: int, tile_size: Vector2i,
		warnings: Array[String], tex_srcs: Dictionary, tpl_cache: Dictionary) -> Dictionary:
	var cache_key := "%d:%d" % [sid, scene_id]
	if tpl_cache.has(cache_key):
		return tpl_cache[cache_key]

	var tpl := {}
	var src = ts.get_source(sid)
	var ps: PackedScene = src.get_scene_tile_scene(scene_id)
	if ps == null:
		warnings.append("场景 tile source=%d scene=%d 无法实例化，跳过" % [sid, scene_id])
		tpl_cache[cache_key] = {}
		return {}

	var inst: Node = ps.instantiate()
	var spr := _find_sprite2d(inst)
	if spr == null:
		warnings.append("场景 tile 模板 '%s' 没有 Sprite2D，跳过" % ps.resource_path)
		inst.free()
		tpl_cache[cache_key] = {}
		return {}
	if spr.texture == null:
		warnings.append("场景 tile 模板 '%s' 的 Sprite2D 没有贴图，跳过" % ps.resource_path)
		inst.free()
		tpl_cache[cache_key] = {}
		return {}

	# 贴图与 region（AtlasTexture → atlas 贴图 + region）
	var tex_path: String
	var region: Rect2
	if spr.texture is AtlasTexture:
		var at := spr.texture as AtlasTexture
		tex_path = at.atlas.resource_path
		region = at.region
	else:
		tex_path = spr.texture.resource_path
		region = Rect2(Vector2.ZERO, spr.texture.get_size())
	if tex_path == "":
		warnings.append("场景 tile 模板 '%s' 的贴图未保存到磁盘，跳过" % ps.resource_path)
		inst.free()
		tpl_cache[cache_key] = {}
		return {}

	if spr is Sprite2D and ((spr as Sprite2D).scale != Vector2.ONE):
		warnings.append("场景 tile 模板 '%s' 的 Sprite2D 带缩放，v2 忽略" % ps.resource_path)

	# 贴图尺寸（region 尺寸）；Sprite2D centered 时绘制中心 = 节点位置 + offset
	var size := region.size
	var centered := true
	var node_off := Vector2.ZERO
	if spr is Sprite2D:
		centered = (spr as Sprite2D).centered
	# 模板根 → Sprite2D 的位置链（模板未入树，手动累计 position）
	var n: Node = spr
	while n != null and n != inst:
		if n is Node2D:
			node_off += (n as Node2D).position
		n = n.get_parent()
	# Godot 把场景 tile 实例根放在 cell 中心（map_to_local），
	# 相对 cell 左上角 = tile/2；再叠加 offset 与 centered 修正得到贴图左上角。
	var center_adj := Vector2(tile_size.x, tile_size.y) / 2.0
	var half := size / 2.0 if centered else Vector2.ZERO
	var sprite_tl := center_adj + node_off + (spr as Sprite2D).offset - half

	# 足印 w/h：模板根 metadata w/h → 第一个 RectangleShape2D 的 size → tile 尺寸
	var fp := Vector2(tile_size)
	var root_node: Node = inst
	if root_node.has_meta("w") and root_node.has_meta("h"):
		fp = Vector2(float(root_node.get_meta("w")), float(root_node.get_meta("h")))
	else:
		var shape := _find_rect_shape(inst)
		if shape != null:
			fp = shape.size
	var etype := str(root_node.get_meta("type")) if root_node.has_meta("type") \
			else ps.resource_path.get_file().get_basename()

	var tex_rel := _texture_rel(tex_path)
	tex_srcs[tex_rel] = tex_path  # 同名覆盖无害（同文件）；异文件同名在导出时告警
	tpl_cache[cache_key] = {
		"type": etype,
		"w": fp.x, "h": fp.y,
		"solid": bool(root_node.get_meta("solid", true)),
		"z": int(root_node.get_meta("z", 0)),
		"sprite": {
			"texture": tex_rel,
			"region": [region.position.x, region.position.y, region.size.x, region.size.y],
			"offset": [sprite_tl.x, sprite_tl.y],
		},
	}
	var tpl_out: Dictionary = tpl_cache[cache_key]
	inst.free()
	return tpl_out


# 场景 tile cell → 实体数据
static func _scene_tile_entity(tpl: Dictionary, cell_tl: Vector2, used_ids: Dictionary) -> Dictionary:
	var id := "%s_1" % tpl.type
	var n := 2
	while used_ids.has(id):
		id = "%s_%d" % [tpl.type, n]
		n += 1
	used_ids[id] = true
	var ent := {
		"id": id, "type": tpl.type,
		"x": cell_tl.x, "y": cell_tl.y,
		"w": tpl.w, "h": tpl.h,
		"z": tpl.z,
		"sprite": tpl.sprite,
	}
	if tpl.solid:
		ent["solid"] = true
	return ent


static func _find_sprite2d(root: Node) -> Sprite2D:
	if root is Sprite2D:
		return root
	for child in root.get_children():
		var found := _find_sprite2d(child)
		if found != null:
			return found
	return null


static func _find_rect_shape(root: Node) -> RectangleShape2D:
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if node is CollisionShape2D:
			var s = (node as CollisionShape2D).shape
			if s is RectangleShape2D:
				return s
	return null


# ── 普通实体节点 ──
static func _build_entity(node: Node, used_ids: Dictionary, ts_groups: Dictionary,
		tex_srcs: Dictionary, warnings: Array[String]) -> Dictionary:
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

	if not (node is Node2D):
		return _fail("实体节点 '%s' 必须是 Node2D 派生" % id)
	var pos: Vector2 = (node as Node2D).global_position

	var tw := 16.0
	var thh := 16.0
	# 从场景任一 TileSet 取缺省实体尺寸（v2 限定全场景 tile 尺寸一致）
	for p in ts_groups:
		var built: Dictionary = ts_groups[p]
		if not built.groups.is_empty():
			tw = float(built.groups[0].data.tile_width)
			thh = float(built.groups[0].data.tile_height)
			break

	var ent := {
		"id": id, "type": etype,
		"x": pos.x, "y": pos.y,
		"w": float(node.get_meta("w", tw)),
		"h": float(node.get_meta("h", thh)),
		# color 可能是 "#rrggbb" 字符串或 Godot Color 类型，统一经 _variant_to_json 规范化
		"color": _variant_to_json(node.get_meta("color", DEFAULT_COLOR)),
	}
	if node.has_meta("z"):
		ent["z"] = int(node.get_meta("z"))
	if bool(node.get_meta("solid", false)):
		ent["solid"] = true

	# 实体 Sprite2D → sprite 字段（节点自身或子树中第一个）
	var spr := _find_sprite2d(node)
	if spr != null and spr.texture != null:
		var tex_path: String
		var region: Rect2
		if spr.texture is AtlasTexture:
			var at := spr.texture as AtlasTexture
			tex_path = at.atlas.resource_path
			region = at.region
		else:
			tex_path = spr.texture.resource_path
			region = Rect2(Vector2.ZERO, spr.texture.get_size())
		if tex_path == "":
			warnings.append("实体 '%s' 的 Sprite2D 贴图未保存到磁盘，忽略 sprite" % id)
		else:
			if spr is Sprite2D and ((spr as Sprite2D).flip_h or (spr as Sprite2D).flip_v):
				warnings.append("实体 '%s' 的 Sprite2D 带翻转，v2 忽略" % id)
			var size := region.size
			var centered := true
			if spr is Sprite2D:
				centered = (spr as Sprite2D).centered
			var half := size / 2.0 if centered else Vector2.ZERO
			# 贴图左上角世界坐标 = 贴图节点位置 + offset − (centered ? 尺寸/2 : 0)
			var tl := spr.global_position + (spr as Sprite2D).offset - half
			var m := _match_atlas_tile(ts_groups, tex_path, region)
			if m.found:
				ent["sprite"] = {"tileset": m.ref, "tile": m.tile}
			else:
				var tex_rel := _texture_rel(tex_path)
				tex_srcs[tex_rel] = tex_path
				var sprite := {
					"texture": tex_rel,
					"region": [region.position.x, region.position.y, region.size.x, region.size.y],
				}
				var off := tl - pos
				if off != Vector2.ZERO:
					sprite["offset"] = [off.x, off.y]
				ent["sprite"] = sprite

	# 其余 metadata → props（引擎不读取，玩法移植预留）
	var props := {}
	for meta_name in node.get_meta_list():
		if meta_name in RESERVED_META:
			continue
		props[meta_name] = _variant_to_json(node.get_meta(meta_name))
	if not props.is_empty():
		ent["props"] = props

	return {"ok": true, "data": ent}


# 贴图 region 是否恰为某 tileset 组内的 tile 矩形 → 图集形态
static func _match_atlas_tile(ts_groups: Dictionary, tex_path: String, region: Rect2) -> Dictionary:
	if tex_path == "":
		return {"found": false}
	for p in ts_groups:
		var built: Dictionary = ts_groups[p]
		for gi in range(built.groups.size()):
			var g: Dictionary = built.groups[gi]
			if g.texture_src != tex_path:
				continue
			var tw := float(g.data.tile_width)
			var thh := float(g.data.tile_height)
			for t in g.data.tiles:
				var want := Vector2(float(t.col) * tw, float(t.row) * thh)
				if want == region.position and Vector2(tw, thh) == region.size:
					return {"found": true, "ref": "%s#%d" % [p, gi], "tile": int(t.id)}
	return {"found": false}


# ──────────────────────────────────────────────
#  写盘与工具
# ──────────────────────────────────────────────

# 写出全部组；used_names 跨组去重（场景导出时跨 TileSet 资源共享）。
# 返回 {ok, names: [每个组的最终 name], error?}
static func _write_tileset_groups(groups: Array, used_names: Dictionary, log: Array[String]) -> Dictionary:
	var names: Array = []
	for g in groups:
		var name: String = g.name
		var n := 2
		while used_names.has(name):
			name = "%s_%d" % [g.name, n]
			n += 1
		used_names[name] = true

		var data: Dictionary = g.data
		data["texture"] = "textures/" + g.texture_src.get_file()
		var err := _copy_texture(g.texture_src)
		if err != "":
			data.erase("texture")  # 贴图拷贝失败时宁可让引擎报缺贴图，也不写坏路径
			log.append("警告: " + err)

		var out_path := _assets_dir().path_join("tilesets/%s.json" % name)
		if not _write_json(out_path, data):
			return {"ok": false, "error": "无法写出: %s" % out_path}
		log.append("tro-tileset → %s (%d tiles)" % [out_path, data.tiles.size()])
		for w in g.warnings:
			log.append("警告: " + w)
		names.append(name)
	return {"ok": true, "names": names}


static func _assets_dir() -> String:
	return ProjectSettings.globalize_path("res://").path_join("../assets")


# 独立贴图统一拷贝到 assets/textures/<文件名>，故 rel 路径恒为 "textures/<文件名>"。
# 注意：不同目录下同名贴图会相互覆盖（导出时告警），素材命名需避免重名。
static func _texture_rel(editor_res_path: String) -> String:
	return "textures/" + editor_res_path.get_file()


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


# Godot Variant → JSON 安全值；exotic 类型降级为字符串
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
