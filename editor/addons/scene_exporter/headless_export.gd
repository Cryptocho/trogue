extends SceneTree

# headless 导出入口 v4（Agent 自动化通道）。
#
# 用法（在项目根目录执行）:
#   # 先导入资源（首次或资源变更后）
#   godot --headless --path editor --import
#   # 导出场景（可多个，逗号分隔；连同其 TileSet 与实体贴图；纯实体场景 → bare）
#   godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- scene=res://assets/test.tscn,scene=res://assets/forest.tscn
#   # 仅导出 TileSet
#   godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- tileset=res://assets/tile_set.tres
#   # 导出动画素材（AnimatedSprite2D → tro-animations v1；可多个，逗号分隔）
#   godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- animations=res://assets/soldier_animated_sprite_2d.tscn

const TroSchema := preload("res://addons/scene_exporter/tro_schema.gd")


func _initialize() -> void:
	var scene_paths: PackedStringArray = []
	var tileset_path := ""
	var anim_paths: PackedStringArray = []
	for arg in OS.get_cmdline_user_args():
		var kv := arg.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"scene":
				for p in kv[1].split(",", false):
					var path := p.strip_edges()
					if path.begins_with("scene="):
						path = path.substr(6)  # 兼容 scene=a,scene=b 写法
					if path != "":
						scene_paths.append(path)
			"tileset":
				tileset_path = kv[1]
			"animations":
				for p in kv[1].split(",", false):
					var path := p.strip_edges()
					if path.begins_with("animations="):
						path = path.substr(11)  # 兼容 animations=a,animations=b 写法
					if path != "":
						anim_paths.append(path)

	if scene_paths.is_empty() and tileset_path == "" and anim_paths.is_empty():
		push_error("用法: -- scene=res://...tscn[,scene=...] [tileset=res://...tres] [animations=res://...tscn[,animations=...]]")
		quit(1)
		return

	var failed := false
	for sp in scene_paths:
		var result: Dictionary = TroSchema.export_scene_from_file(sp, root)
		_print_result(result)
		if not result.get("ok", false):
			failed = true
	if tileset_path != "":
		var result: Dictionary = TroSchema.export_tileset_from_file(tileset_path)
		_print_result(result)
		if not result.get("ok", false):
			failed = true
	for ap in anim_paths:
		var result: Dictionary = TroSchema.export_animations_from_file(ap)
		_print_result(result)
		if not result.get("ok", false):
			failed = true

	quit(1 if failed else 0)


func _print_result(result: Dictionary) -> void:
	if result.get("ok", false):
		for line in result.get("log", []):
			print("[scene_exporter] ", line)
	else:
		push_error("[scene_exporter] 导出失败: %s" % result.get("error", "?"))
