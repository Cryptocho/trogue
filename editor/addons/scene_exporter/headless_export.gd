extends SceneTree

# headless 导出入口（Agent 自动化通道）。
#
# 用法（在仓库根 trogue/ 下执行）:
#   # 先导入资源（首次或资源变更后）
#   godot --headless --path editor --import
#   # 导出场景（连同其 TileSet）
#   godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- scene=res://assets/test.tscn
#   # 仅导出 TileSet
#   godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- tileset=res://assets/tile_set.tres

const TroSchema := preload("res://addons/scene_exporter/tro_schema.gd")


func _initialize() -> void:
	var scene_path := ""
	var tileset_path := ""
	for arg in OS.get_cmdline_user_args():
		var kv := arg.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"scene":
				scene_path = kv[1]
			"tileset":
				tileset_path = kv[1]

	if scene_path == "" and tileset_path == "":
		push_error("用法: -- scene=res://...tscn [tileset=res://...tres]")
		quit(1)
		return

	var result: Dictionary
	if scene_path != "":
		result = TroSchema.export_scene_from_file(scene_path, root)
	else:
		result = TroSchema.export_tileset_from_file(tileset_path)

	if result.get("ok", false):
		for line in result.get("log", []):
			print("[scene_exporter] ", line)
		quit(0)
	else:
		push_error("[scene_exporter] 导出失败: %s" % result.get("error", "?"))
		quit(1)
