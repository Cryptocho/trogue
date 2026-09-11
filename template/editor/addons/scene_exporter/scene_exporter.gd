@tool
extends EditorPlugin

# trogue 场景导出插件 v4。
# 序列化逻辑全部在 TroSchema（tro_schema.gd），本文件只做编辑器菜单与文件对话框。
# headless 自动化入口见 headless_export.gd。
# 菜单：Export tro-tileset / tro-animations（场景导出 UI 另行规划，headless scene= 保留）。

const TroSchema := preload("res://addons/scene_exporter/tro_schema.gd")

var _file_dialog: EditorFileDialog
var _mode := ""  # "tileset" | "animations"


func _enter_tree() -> void:
	add_tool_menu_item("Export tro-tileset...", _on_menu.bind("tileset"))
	add_tool_menu_item("Export tro-animations...", _on_menu.bind("animations"))


func _exit_tree() -> void:
	remove_tool_menu_item("Export tro-tileset...")
	remove_tool_menu_item("Export tro-animations...")
	if is_instance_valid(_file_dialog):
		_file_dialog.queue_free()
	_file_dialog = null


func _on_menu(mode: String) -> void:
	_mode = mode
	_file_dialog = EditorFileDialog.new()
	_file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	if mode == "tileset":
		_file_dialog.add_filter("*.tres,*.res", "TileSet Resources")
		_file_dialog.title = "选择要导出的 TileSet"
	else:
		_file_dialog.add_filter("*.tscn,*.scn", "Scenes")
		_file_dialog.title = "选择要导出动画的素材场景（AnimatedSprite2D）"
	add_child(_file_dialog)
	_file_dialog.file_selected.connect(_on_file, CONNECT_ONE_SHOT)
	_file_dialog.canceled.connect(_on_cancel, CONNECT_ONE_SHOT)
	_file_dialog.popup_centered(Vector2i(800, 600))


func _on_cancel() -> void:
	if is_instance_valid(_file_dialog):
		_file_dialog.queue_free()
	_file_dialog = null


func _on_file(path: String) -> void:
	if is_instance_valid(_file_dialog):
		_file_dialog.queue_free()
	_file_dialog = null

	var result: Dictionary
	if _mode == "tileset":
		result = TroSchema.export_tileset_from_file(path)
	else:
		result = TroSchema.export_animations_from_file(path)

	if result.get("ok", false):
		for line in result.get("log", []):
			print("[scene_exporter] ", line)
	else:
		push_error("[scene_exporter] 导出失败: %s" % result.get("error", "?"))
