import os
import datetime
import random
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageDraw, ImageTk

CELL = 32
LIGHT = (36, 39, 58, 255)
DARK = (19, 22, 29, 255)
ZOOM_MIN = 0.025
ZOOM_MAX = 32.0
ZOOM_STEP = 1.1
SIDEBAR_W = 280
GRID_COLOR = (255, 255, 255, 80)
PAD_COLOR = (180, 180, 180, 100)
MARK_FILL = (80, 140, 255, 100)
MARK_OUTLINE = (255, 255, 255, 200)
HOVER_COLOR = (255, 255, 0, 120)
MASK_OFF = 0
MASK_ON = 1
MASK_IGNORE = 2
IGNORE_FILL = (120, 120, 120, 100)
IGNORE_OUTLINE = (200, 200, 200, 150)
BITMASK_YELLOW = (255, 255, 0, 153)
BITMASK_GRID_COLOR = (255, 255, 0, 100)
SELECT_COLOR = (0, 255, 128, 200)
BITMASK_DIALOG_SIZE = 350
PROP_PAINT_FILL = (255, 140, 0, 140)


def generate_group_color():
    import colorsys
    h = random.random()
    s = random.uniform(0.6, 0.9)
    v = random.uniform(0.7, 1.0)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return (int(r * 255), int(g * 255), int(b * 255), 120)

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(WORKSPACE, "src")

_loading_project = False

from tile_model import TilesetMeta, TileDef, GroupDef, TilesetProject
from tile_lua_parser import parse_lua_table


def relpath_from_src(path):
    abs_path = os.path.abspath(path)
    rel = os.path.relpath(abs_path, SRC_DIR)
    return "src/" + rel.replace(os.sep, "/")

root = tk.Tk()
root.withdraw()

filepath = filedialog.askopenfilename(
    initialdir="../src/assets",
    filetypes=[("支持的格式", "*.png *.jpg *.lua"), ("图片文件", "*.png *.jpg"), ("Lua 文件", "*.lua")],
)

_initial_import_data = None

if filepath:
    if filepath.lower().endswith(".lua"):
        with open(filepath, "r", encoding="utf-8") as f:
            text = f.read()
        data = parse_lua_table(text)
        source_rel = data.get("source", "")
        png_path = os.path.join(WORKSPACE, source_rel) if source_rel else ""
        if not png_path or not os.path.exists(png_path):
            messagebox.showerror("图片未找到", f"无法找到图片:\n{png_path}")
            root.destroy()
            exit(1)
        _initial_import_data = data
        filepath = png_path

    root.deiconify()
    img = Image.open(filepath).convert("RGBA")

    main_frame = tk.Frame(root)
    main_frame.pack(fill=tk.BOTH, expand=True)

    canvas = tk.Canvas(main_frame, highlightthickness=0, takefocus=True)
    canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    sidebar_outer = tk.Frame(main_frame, width=SIDEBAR_W, bg="#2d2d2d")
    sidebar_outer.pack(side=tk.RIGHT, fill=tk.Y)
    sidebar_outer.pack_propagate(False)

    sidebar_canvas = tk.Canvas(sidebar_outer, bg="#2d2d2d", highlightthickness=0, width=SIDEBAR_W - 12, takefocus=True)
    sidebar_scrollbar = tk.Scrollbar(sidebar_outer, orient=tk.VERTICAL, command=sidebar_canvas.yview)
    sidebar_canvas.configure(yscrollcommand=sidebar_scrollbar.set)
    sidebar_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
    sidebar_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    sidebar = tk.Frame(sidebar_canvas, bg="#2d2d2d")
    _sidebar_win_id = sidebar_canvas.create_window((0, 0), window=sidebar, anchor=tk.NW)

    def _configure_sidebar_width(event):
        sidebar_canvas.itemconfig(_sidebar_win_id, width=event.width)

    sidebar_canvas.bind("<Configure>", _configure_sidebar_width, add="+")

    def _update_scrollregion(event=None):
        sidebar_canvas.configure(scrollregion=sidebar_canvas.bbox("all"))

    sidebar.bind("<Configure>", _update_scrollregion)

    def _on_sidebar_wheel(event):
        if getattr(event, "num", 0) == 4 or getattr(event, "delta", 0) > 0:
            sidebar_canvas.yview_scroll(-1, "units")
        elif getattr(event, "num", 0) == 5 or getattr(event, "delta", 0) < 0:
            sidebar_canvas.yview_scroll(1, "units")

    def _root_wheel(event):
        rx = root.winfo_pointerx() - root.winfo_rootx()
        if rx > root.winfo_width() - SIDEBAR_W:
            _on_sidebar_wheel(event)

    root.bind_all("<MouseWheel>", _root_wheel, add="+")
    root.bind_all("<Button-4>", _root_wheel, add="+")
    root.bind_all("<Button-5>", _root_wheel, add="+")

    def _sidebar_steal_focus(event):
        w = event.widget
        if isinstance(w, (tk.Entry, tk.Button, tk.Spinbox)):
            return
        canvas.focus_set()

    root.bind_all("<Button-1>", _sidebar_steal_focus, add="+")

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="图片信息",
    ).pack(fill=tk.X, padx=10, pady=(10, 2))

    dim_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#cccccc", anchor="w",
        text=f"尺寸: {img.width} x {img.height} px",
    )
    dim_label.pack(fill=tk.X, padx=14, pady=1)

    zoom_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#cccccc", anchor="w",
        text="缩放: 100%",
    )
    zoom_label.pack(fill=tk.X, padx=14, pady=1)

    tk.Frame(sidebar, height=1, bg="#444444").pack(fill=tk.X, padx=10, pady=8)

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="网格",
    ).pack(fill=tk.X, padx=10, pady=(0, 2))

    cols_var = tk.IntVar(value=1)
    rows_var = tk.IntVar(value=1)

    cols_frame = tk.Frame(sidebar, bg="#2d2d2d")
    cols_frame.pack(fill=tk.X, padx=14, pady=1)
    tk.Label(cols_frame, bg="#2d2d2d", fg="#cccccc", text="列数:").pack(side=tk.LEFT)
    tk.Spinbox(
        cols_frame, from_=1, to=256, width=5, textvariable=cols_var,
        bg="#3a3a3a", fg="#cccccc", buttonbackground="#3a3a3a",
        relief=tk.FLAT, bd=2,
    ).pack(side=tk.RIGHT)

    rows_frame = tk.Frame(sidebar, bg="#2d2d2d")
    rows_frame.pack(fill=tk.X, padx=14, pady=1)
    tk.Label(rows_frame, bg="#2d2d2d", fg="#cccccc", text="行数:").pack(side=tk.LEFT)
    tk.Spinbox(
        rows_frame, from_=1, to=256, width=5, textvariable=rows_var,
        bg="#3a3a3a", fg="#cccccc", buttonbackground="#3a3a3a",
        relief=tk.FLAT, bd=2,
    ).pack(side=tk.RIGHT)

    padding_var = tk.IntVar(value=0)

    pad_frame = tk.Frame(sidebar, bg="#2d2d2d")
    pad_frame.pack(fill=tk.X, padx=14, pady=1)
    tk.Label(pad_frame, bg="#2d2d2d", fg="#cccccc", text="内边距:").pack(side=tk.LEFT)
    tk.Spinbox(
        pad_frame, from_=0, to=128, width=5, textvariable=padding_var,
        bg="#3a3a3a", fg="#cccccc", buttonbackground="#3a3a3a",
        relief=tk.FLAT, bd=2,
    ).pack(side=tk.RIGHT)

    tile_size_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#cccccc", anchor="w",
        text="图块尺寸: -",
    )
    tile_size_label.pack(fill=tk.X, padx=14, pady=1)

    tk.Frame(sidebar, height=1, bg="#444444").pack(fill=tk.X, padx=10, pady=8)

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="标记",
    ).pack(fill=tk.X, padx=10, pady=(0, 2))

    mark_count_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#cccccc", anchor="w",
        text="已标记: 0",
    )
    mark_count_label.pack(fill=tk.X, padx=14, pady=1)

    mark_btn_frame = tk.Frame(sidebar, bg="#2d2d2d")
    mark_btn_frame.pack(fill=tk.X, padx=14, pady=4)

    def dark_button(parent, text, command):
        btn = tk.Button(
            parent, text=text, command=command,
            bg="#3a3a3a", fg="#cccccc", relief=tk.FLAT, bd=0,
            activebackground="#555555", activeforeground="#ffffff",
            padx=8, pady=2,
        )
        return btn

    fmt_btn = dark_button(mark_btn_frame, "格式刷", None)
    fmt_btn.pack(side=tk.LEFT)

    tk.Frame(sidebar, height=1, bg="#444444").pack(fill=tk.X, padx=10, pady=8)

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="位掩码",
    ).pack(fill=tk.X, padx=10, pady=(0, 2))

    bitmask_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#cccccc", anchor="w",
        text="位掩码: 无",
    )
    bitmask_label.pack(fill=tk.X, padx=14, pady=1)

    bitmask_btn = dark_button(sidebar, "位掩码", None)
    bitmask_btn.pack(fill=tk.X, padx=14, pady=4)
    bitmask_btn.config(state=tk.DISABLED)

    def update_bitmask_label():
        if selected_tile_index is not None and selected_tile_index < len(project.tiles):
            tile = project.tiles[selected_tile_index]
            has_bm = tile.bitmask is not None and any(any(c != MASK_OFF for c in row) for row in tile.bitmask)
        else:
            has_bm = False
        if has_bm:
            bitmask_label.config(text="位掩码: 已设置")
        else:
            bitmask_label.config(text="位掩码: 无")
        if selected_tile_index is not None:
            bitmask_btn.config(state=tk.NORMAL)
        else:
            bitmask_btn.config(state=tk.DISABLED)
        if state["format_brush_mode"]:
            fmt_btn.config(state=tk.NORMAL)
        elif has_bm:
            fmt_btn.config(state=tk.NORMAL)
        else:
            fmt_btn.config(state=tk.DISABLED)

    def exit_format_brush():
        state["format_brush_mode"] = False
        state["format_brush_bitmask"] = None
        state["drag_mark"] = None
        fmt_btn.config(text="格式刷")
        update_bitmask_label()
        redraw()

    def on_format_brush():
        if state["group_edit_mode"] is not None:
            exit_group_edit()
        if not state["format_brush_mode"]:
            if selected_tile_index is None or selected_tile_index >= len(project.tiles):
                return
            tile = project.tiles[selected_tile_index]
            if tile.bitmask is None or not any(any(c != MASK_OFF for c in row) for row in tile.bitmask):
                return
            state["format_brush_bitmask"] = [row[:] for row in tile.bitmask]
            state["format_brush_mode"] = True
            state["drag_mark"] = None
            fmt_btn.config(text="取消格式刷")
        else:
            exit_format_brush()
        update_bitmask_label()
        redraw()

    fmt_btn.config(command=on_format_brush)

    tk.Frame(sidebar, height=1, bg="#444444").pack(fill=tk.X, padx=10, pady=8)

    # ── 分组 ──

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="分组",
    ).pack(fill=tk.X, padx=10, pady=(0, 2))

    groups_container = tk.Frame(sidebar, bg="#2d2d2d")
    groups_container.pack(fill=tk.X, padx=10, pady=1)

    new_group_btn = dark_button(sidebar, "+ 新建分组", None)
    new_group_btn.pack(fill=tk.X, padx=14, pady=4)

    group_hint_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#666666", anchor="w",
        text="",
    )
    group_hint_label.pack(fill=tk.X, padx=14, pady=1)
    group_hint_label.pack_forget()
    # NOTE: pack_forget() 后重新 pack() 必须带 before= 锚定位置,
    # 否则 Tkinter 默认追加到父容器末尾.

    groups_sep = tk.Frame(sidebar, height=1, bg="#444444")
    groups_sep.pack(fill=tk.X, padx=10, pady=8)

    group_rows = []

    # ── 分组管理函数 ──

    def rebuild_groups_list():
        for row in group_rows:
            for w in list(row.winfo_children()):
                w.destroy()
            row.destroy()
        group_rows.clear()

        for w in list(groups_container.winfo_children()):
            w.destroy()

        if project.groups:
            for gi, group in enumerate(project.groups):
                is_active = (state["group_edit_mode"] == gi)
                row = tk.Frame(groups_container, bg="#3a3a4a" if is_active else "#2d2d2d")
                row.pack(fill=tk.X, pady=1)

                color = state["group_color_map"].get(gi, (80, 80, 80, 255))
                color_hex = f"#{color[0]:02x}{color[1]:02x}{color[2]:02x}"
                color_indicator = tk.Canvas(row, width=8, height=8, bg=color_hex, highlightthickness=0)
                color_indicator.pack(side=tk.LEFT, padx=(2, 4))
                color_indicator.pack_propagate(False)

                name_label = tk.Label(
                    row, bg=row["bg"], fg="#cccccc", anchor="w",
                    text=group.name,
                    cursor="hand2",
                )
                name_label.pack(side=tk.LEFT, fill=tk.X, expand=True)
                name_label.bind("<Button-1>", lambda e, idx=gi: on_group_row_click(idx))

                count_label = tk.Label(
                    row, bg=row["bg"], fg="#888888", anchor="w",
                    text=str(len(group.tile_indices)),
                )
                count_label.pack(side=tk.LEFT, padx=(2, 2))

                del_btn = tk.Button(
                    row, text="X", command=lambda idx=gi: on_delete_group(idx),
                    bg="#3a3a3a", fg="#cc6666", relief=tk.FLAT, bd=0,
                    activebackground="#555555", activeforeground="#ff6666",
                    padx=6, pady=0, font=("", 10),
                )
                del_btn.pack(side=tk.RIGHT, padx=(1, 0))

                group_rows.append(row)
        else:
            tk.Label(
                groups_container, bg="#2d2d2d", fg="#666666", anchor="w",
                text="暂无分组",
            ).pack(fill=tk.X, padx=4, pady=1)

    def on_new_group():
        dlg = tk.Toplevel(root)
        dlg.title("新建分组")
        dlg.configure(bg="#2d2d2d")
        dlg.resizable(False, False)
        dlg.attributes('-topmost', True)
        dlg.geometry(f"260x100+{root.winfo_x() + 100}+{root.winfo_y() + 100}")
        dlg.deiconify()
        dlg.lift()
        dlg.focus_force()

        tk.Label(dlg, bg="#2d2d2d", fg="#cccccc", text="请输入分组名称:").pack(padx=10, pady=(10, 4), anchor=tk.W)
        entry = tk.Entry(dlg, bg="#3a3a3a", fg="#cccccc", insertbackground="#cccccc", relief=tk.FLAT)
        entry.pack(fill=tk.X, padx=10, pady=2)
        entry.focus_set()

        def on_ok(event=None):
            name = entry.get().strip()
            if name:
                if state["group_edit_mode"] is not None:
                    exit_group_edit()
                group = GroupDef(name=name, tile_indices=[])
                project.groups.append(group)
                rebuild_groups_list()
                redraw()
            dlg.destroy()

        def on_cancel():
            dlg.destroy()

        btn_row = tk.Frame(dlg, bg="#2d2d2d")
        btn_row.pack(fill=tk.X, padx=10, pady=(6, 10))
        dark_button(btn_row, "确定", on_ok).pack(side=tk.RIGHT)
        dark_button(btn_row, "取消", on_cancel).pack(side=tk.RIGHT, padx=(0, 4))
        entry.bind("<Return>", on_ok)
        dlg.protocol("WM_DELETE_WINDOW", on_cancel)

    def on_delete_group(gi):
        if gi < 0 or gi >= len(project.groups):
            return
        if state["group_edit_mode"] is not None:
            exit_group_edit()
        del project.groups[gi]
        old_map = state["group_color_map"]
        state["group_color_map"] = {}
        for idx in range(len(project.groups)):
            if idx in old_map:
                state["group_color_map"][idx] = old_map[idx]
            if idx >= gi and (idx + 1) in old_map:
                state["group_color_map"][idx] = old_map[idx + 1]
        rebuild_groups_list()
        redraw()

    def on_group_row_click(gi):
        if gi < 0 or gi >= len(project.groups):
            return
        if state["group_edit_mode"] == gi:
            exit_group_edit()
        else:
            enter_group_edit(gi)

    def enter_group_edit(gi):
        if gi < 0 or gi >= len(project.groups):
            return
        if state["format_brush_mode"]:
            exit_format_brush()
        if state["property_paint_mode"]:
            exit_property_paint()
        if gi not in state["group_color_map"]:
            state["group_color_map"][gi] = generate_group_color()
        state["group_edit_mode"] = gi
        state["drag_mark"] = None
        group_name = project.groups[gi].name
        group_hint_label.config(
            text=f"分组: {group_name} — 点击/拖拽 tile 加入/移出",
            fg="#ff8c00",
        )
        group_hint_label.pack(before=groups_sep, fill=tk.X, padx=14, pady=1)
        rebuild_groups_list()
        redraw()

    def exit_group_edit():
        if state["group_edit_mode"] is not None:
            state["group_edit_mode"] = None
            state["drag_mark"] = None
            group_hint_label.pack_forget()
            rebuild_groups_list()
            redraw()

    new_group_btn.config(command=on_new_group)

    tk.Label(
        sidebar, bg="#2d2d2d", fg="#888888", anchor="w", font=("", 9, "bold"),
        text="属性",
    ).pack(fill=tk.X, padx=10, pady=(0, 2))

    props_hint_label = tk.Label(
        sidebar, bg="#2d2d2d", fg="#666666", anchor="w",
        text="选择瓦片以编辑属性",
    )
    props_hint_label.pack(fill=tk.X, padx=14, pady=1)

    props_container = tk.Frame(sidebar, bg="#2d2d2d")
    props_container.pack(fill=tk.X, padx=10, pady=1)
    props_container.pack_forget()

    props_add_btn = dark_button(sidebar, "+ 添加属性", None)
    props_add_btn.pack(fill=tk.X, padx=10, pady=(0, 1))
    props_add_btn.pack_forget()

    props_bottom_sep = tk.Frame(sidebar, height=1, bg="#444444")
    props_bottom_sep.pack(fill=tk.X, padx=10, pady=8)

    export_btn = dark_button(sidebar, "导出 Lua", None)
    export_btn.pack(fill=tk.X, padx=14, pady=4)

    root.geometry(f"{img.width + SIDEBAR_W}x{img.height}")
    root.title(filepath)
    root.resizable(True, True)
    root.minsize(680, 520)

    state = {
        "zoom": 1.0,
        "view_cx": img.width / 2,
        "view_cy": img.height / 2,
        "drag_start": None,
        "hover_cell": None,
        "drag_mark": None,  # (start_x, start_y, mark_state)
        "format_brush_mode": False,
        "format_brush_bitmask": None,  # [[0,0,0],[0,0,0],[0,0,0]] or None
        "property_paint_mode": False,
        "property_paint_key": None,
        "property_paint_value": None,
        "group_edit_mode": None,  # None | int (index into project.groups)
        "group_color_map": {},    # dict[int, tuple] (group_index → RGBA color)
    }

    project = TilesetProject()
    selected_tile_index = None  # int | None，替代 state["selected_tile"]

    if _initial_import_data is not None:
        data = _initial_import_data
        project.meta.source = data.get("source", "")
        project.meta.cols = data.get("cols", 1)
        project.meta.rows = data.get("rows", 1)
        project.meta.tile_width = data.get("tile_width", 0)
        project.meta.tile_height = data.get("tile_height", 0)
        project.meta.padding = data.get("padding", 0)

        tiles_data = data.get("tiles", [])
        if isinstance(tiles_data, dict):
            tiles_list = [tiles_data[k] for k in sorted(tiles_data.keys())]
        else:
            tiles_list = tiles_data
        for i, tile_entry in enumerate(tiles_list):
            if tile_entry is None:
                continue
            tile = TileDef(
                index=i,
                col=tile_entry.get("col", 0),
                row=tile_entry.get("row", 0),
                properties=tile_entry.get("properties", {}),
                bitmask=tile_entry.get("bitmask"),
            )
            project.tiles.append(tile)

        groups_data = data.get("groups", {})
        if isinstance(groups_data, dict):
            for name, group_entry in groups_data.items():
                group = GroupDef(
                    name=name,
                    tile_indices=list(group_entry.get("tiles", [])),
                )
                project.groups.append(group)
        rebuild_groups_list()

        cols_var.set(project.meta.cols)
        rows_var.set(project.meta.rows)
        padding_var.set(project.meta.padding)
        selected_tile_index = None

    def find_tile_index(col, row):
        for i, t in enumerate(project.tiles):
            if t.col == col and t.row == row:
                return i
        return None

    def get_or_create_tile(col, row):
        idx = find_tile_index(col, row)
        if idx is not None:
            return idx, project.tiles[idx]
        new_idx = len(project.tiles)
        tile = TileDef(index=new_idx, col=col, row=row)
        project.tiles.append(tile)
        return new_idx, tile

    def remove_tile(col, row):
        global selected_tile_index
        idx = find_tile_index(col, row)
        if idx is not None:
            del project.tiles[idx]
            for i, t in enumerate(project.tiles):
                t.index = i
            if selected_tile_index == idx:
                selected_tile_index = None
            elif selected_tile_index is not None and selected_tile_index > idx:
                selected_tile_index -= 1
            for group in project.groups:
                new_indices = []
                for ti in group.tile_indices:
                    if ti == idx:
                        continue
                    elif ti > idx:
                        new_indices.append(ti - 1)
                    else:
                        new_indices.append(ti)
                group.tile_indices = new_indices

    checker_tile = Image.new("RGBA", (CELL * 2, CELL * 2))
    checker_tile.paste(LIGHT, (0, 0, CELL, CELL))
    checker_tile.paste(DARK, (CELL, 0, CELL * 2, CELL))
    checker_tile.paste(DARK, (0, CELL, CELL, CELL * 2))
    checker_tile.paste(LIGHT, (CELL, CELL, CELL * 2, CELL * 2))

    def tile_checkerboard(result, w, h):
        for y in range(0, h, CELL * 2):
            for x in range(0, w, CELL * 2):
                result.paste(checker_tile, (x, y))

    def cell_at_canvas_pos(mx, my):
        scale = state["zoom"]
        cw, ch = canvas.winfo_width(), canvas.winfo_height()
        ox = cw / 2 - state["view_cx"] * scale
        oy = ch / 2 - state["view_cy"] * scale
        ix = (mx - ox) / scale
        iy = (my - oy) / scale
        if ix < 0 or iy < 0 or ix >= img.width or iy >= img.height:
            return None
        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        cw_img = img.width // cols
        ch_img = img.height // rows
        col = int(ix // cw_img)
        row = int(iy // ch_img)
        if col >= cols:
            col = cols - 1
        if row >= rows:
            row = rows - 1
        return (col, row)

    def get_cell_rect(col, row, scale, ox, oy):
        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        cw_img = img.width // cols
        ch_img = img.height // rows
        left = ox + col * cw_img * scale
        top = oy + row * ch_img * scale
        right = ox + ((col + 1) * cw_img if col < cols - 1 else img.width) * scale
        bottom = oy + ((row + 1) * ch_img if row < rows - 1 else img.height) * scale
        return (left, top, right, bottom)

    def draw_grid_overlay(result, ox, oy, scale):
        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        cw_img = img.width // cols
        ch_img = img.height // rows
        sw = img.width * scale
        sh = img.height * scale

        overlay = Image.new("RGBA", result.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)

        for i in range(cols + 1):
            x = ox + i * cw_img * scale
            draw.line([(x, oy), (x, oy + sh)], fill=GRID_COLOR, width=1)

        for i in range(rows + 1):
            y = oy + i * ch_img * scale
            draw.line([(ox, y), (ox + sw, y)], fill=GRID_COLOR, width=1)

        padding = _safe_int(padding_var)
        if padding > 0:
            for row in range(rows):
                for col in range(cols):
                    l, t, r, b = get_cell_rect(col, row, scale, ox, oy)
                    p = padding * scale
                    if p * 2 < r - l and p * 2 < b - t:
                        draw.rectangle([l + p, t + p, r - p, b - p], outline=PAD_COLOR, width=1)

        for t in project.tiles:
            l, t, r, b = get_cell_rect(t.col, t.row, scale, ox, oy)
            draw.rectangle([l, t, r, b], fill=MARK_FILL, outline=MARK_OUTLINE, width=1)

        if selected_tile_index is not None and selected_tile_index < len(project.tiles):
            t = project.tiles[selected_tile_index]
            l, t_rect, r, b = get_cell_rect(t.col, t.row, scale, ox, oy)
            draw.rectangle([l, t_rect, r, b], outline=SELECT_COLOR, width=2)

        for t in project.tiles:
            if t.bitmask is None:
                continue
            col, row, bm = t.col, t.row, t.bitmask
            l, t, r, b = get_cell_rect(col, row, scale, ox, oy)
            bw = (r - l) / 3
            bh = (b - t) / 3
            for br in range(3):
                for bc in range(3):
                    val = bm[br][bc]
                    if val == MASK_ON:
                        bl = l + bc * bw
                        bt = t + br * bh
                        draw.rectangle([bl + 1, bt + 1, bl + bw - 1, bt + bh - 1], fill=BITMASK_YELLOW)
                    elif val == MASK_IGNORE:
                        bl = l + bc * bw
                        bt = t + br * bh
                        draw.rectangle([bl + 1, bt + 1, bl + bw - 1, bt + bh - 1], fill=IGNORE_FILL)

        fbm = state["format_brush_bitmask"]
        if fbm is not None and state["format_brush_mode"] and state["hover_cell"]:
            hc = state["hover_cell"]
            l, t, r, b = get_cell_rect(hc[0], hc[1], scale, ox, oy)
            bw = (r - l) / 3
            bh = (b - t) / 3
            for br in range(3):
                for bc in range(3):
                    val = fbm[br][bc]
                    if val == MASK_ON:
                        bl = l + bc * bw
                        bt = t + br * bh
                        draw.rectangle([bl + 1, bt + 1, bl + bw - 1, bt + bh - 1], fill=(255, 200, 0, 120))
                    elif val == MASK_IGNORE:
                        bl = l + bc * bw
                        bt = t + br * bh
                        draw.rectangle([bl + 1, bt + 1, bl + bw - 1, bt + bh - 1], fill=IGNORE_FILL)

        hover = state["hover_cell"]
        if hover:
            l, t, r, b = get_cell_rect(hover[0], hover[1], scale, ox, oy)
            draw.rectangle([l, t, r, b], outline=HOVER_COLOR, width=2)

        if state["property_paint_mode"] and state["hover_cell"]:
            hc = state["hover_cell"]
            p_l, p_t, p_r, p_b = get_cell_rect(hc[0], hc[1], scale, ox, oy)
            draw.rectangle([p_l, p_t, p_r, p_b], fill=PROP_PAINT_FILL)

        gi = state["group_edit_mode"]
        if gi is not None and gi < len(project.groups):
            gcolor = state["group_color_map"].get(gi)
            if gcolor:
                group = project.groups[gi]
                outline_color = (gcolor[0], gcolor[1], gcolor[2], 255)
                for idx_in_group, ti in enumerate(group.tile_indices):
                    if ti < len(project.tiles):
                        td = project.tiles[ti]
                        gl, gt, gr, gb = get_cell_rect(td.col, td.row, scale, ox, oy)
                        draw.rectangle([gl, gt, gr, gb], fill=gcolor)
                        if idx_in_group == 0:
                            draw.rectangle([gl, gt, gr, gb], outline=(255, 215, 0, 255), width=3)
                        else:
                            draw.rectangle([gl, gt, gr, gb], outline=outline_color, width=1)

        return Image.alpha_composite(result, overlay)

    def _safe_int(var):
        try:
            return var.get()
        except (tk.TclError, ValueError):
            return 1

    def update_tile_info():
        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        cw = img.width // cols if cols > 0 else 0
        ch = img.height // rows if rows > 0 else 0
        tile_size_label.config(text=f"图块尺寸: {cw} x {ch} px")

    def update_mark_count():
        mark_count_label.config(text=f"已标记: {len(project.tiles)}")

    def redraw():
        cw, ch = canvas.winfo_width(), canvas.winfo_height()
        if cw < 2 or ch < 2:
            return

        scale = state["zoom"]
        sw = max(1, int(img.width * scale))
        sh = max(1, int(img.height * scale))

        result = Image.new("RGBA", (cw, ch))
        tile_checkerboard(result, cw, ch)
        ox = int(cw / 2 - state["view_cx"] * scale)
        oy = int(ch / 2 - state["view_cy"] * scale)

        scaled = img.resize((sw, sh), Image.NEAREST)
        result.paste(scaled, (ox, oy), scaled)

        result = draw_grid_overlay(result, ox, oy, scale)

        draw = ImageDraw.Draw(result)
        draw.rectangle([ox, oy, ox + sw - 1, oy + sh - 1], outline="white", width=2)

        root._photo = ImageTk.PhotoImage(result)
        canvas.delete("all")
        canvas.create_image(cw // 2, ch // 2, image=root._photo)

        zoom_label.config(text=f"缩放: {int(scale * 100)}%")

    # ── 属性编辑器 ──

    def parse_value(s):
        s = s.strip()
        sl = s.lower()
        if sl == "true":
            return True
        if sl == "false":
            return False
        try:
            if "." in s:
                return float(s)
            return int(s)
        except ValueError:
            return s

    def on_property_changed(row_frame):
        if selected_tile_index is None or selected_tile_index >= len(project.tiles):
            return
        try:
            tile = project.tiles[selected_tile_index]
            old_key = row_frame._key_str
            new_key = row_frame._key_entry.get().strip()
            new_value_str = row_frame._value_entry.get().strip()
        except tk.TclError:
            return
        if old_key in tile.properties:
            del tile.properties[old_key]
        if new_key:
            tile.properties[new_key] = parse_value(new_value_str)
        row_frame._key_str = new_key

    def on_delete_property(row_frame):
        if selected_tile_index is None or selected_tile_index >= len(project.tiles):
            return
        try:
            tile = project.tiles[selected_tile_index]
            key = row_frame._key_entry.get().strip()
        except tk.TclError:
            return
        if key in tile.properties:
            del tile.properties[key]
        rebuild_property_rows(tile)

    def on_add_property():
        if selected_tile_index is None or selected_tile_index >= len(project.tiles):
            return
        tile = project.tiles[selected_tile_index]
        tile.properties[""] = ""
        rebuild_property_rows(tile)
        children = props_container.winfo_children()
        if children:
            new_row = children[-1]
            if hasattr(new_row, "_key_entry"):
                new_row._key_entry.focus_set()

    def on_property_row_click(row_frame):
        if selected_tile_index is None or selected_tile_index >= len(project.tiles):
            return
        try:
            key = row_frame._key_entry.get().strip()
            value_str = row_frame._value_entry.get().strip()
        except tk.TclError:
            return
        if not key:
            return
        if state["format_brush_mode"]:
            exit_format_brush()
        if state["group_edit_mode"] is not None:
            exit_group_edit()
        state["property_paint_mode"] = True
        state["property_paint_key"] = key
        state["property_paint_value"] = parse_value(value_str)
        state["drag_mark"] = None
        for child in props_container.winfo_children():
            if isinstance(child, tk.Frame) and hasattr(child, "_key_entry"):
                child.config(bg="#4a4a3a" if child is row_frame else "#2d2d2d")
        update_prop_paint_status()
        redraw()

    def exit_property_paint():
        state["property_paint_mode"] = False
        state["property_paint_key"] = None
        state["property_paint_value"] = None
        state["drag_mark"] = None
        for child in props_container.winfo_children():
            if isinstance(child, tk.Frame) and hasattr(child, "_key_entry"):
                child.config(bg="#2d2d2d")
        update_prop_paint_status()
        redraw()

    def update_prop_paint_status():
        if state["property_paint_mode"]:
            val = state["property_paint_value"]
            val_str = "true" if val is True else "false" if val is False else str(val)
            props_hint_label.config(
                text=f"属性刷: {state['property_paint_key']} = {val_str}",
                fg="#ff8c00",
            )
        elif selected_tile_index is not None and selected_tile_index < len(project.tiles):
            props_hint_label.config(text="选择瓦片以编辑属性", fg="#666666")
            props_hint_label.pack_forget()
        else:
            props_hint_label.config(text="选择瓦片以编辑属性", fg="#666666")

    def create_property_row(key_str, value):
        row_frame = tk.Frame(props_container, bg="#2d2d2d")
        row_frame.pack(fill=tk.X, pady=1)

        value_str = "true" if value is True else "false" if value is False else str(value)

        del_btn = tk.Button(
            row_frame, text="X", command=lambda: on_delete_property(row_frame),
            bg="#3a3a3a", fg="#cc6666", relief=tk.FLAT, bd=0,
            activebackground="#555555", activeforeground="#ff6666",
            padx=6, pady=0, font=("", 10),
        )
        del_btn.pack(side=tk.RIGHT, padx=(1, 0))

        paint_btn = tk.Button(
            row_frame, text="P", command=lambda: on_property_row_click(row_frame),
            bg="#4a4a2a", fg="#ffcc00", relief=tk.FLAT, bd=0,
            activebackground="#555555", activeforeground="#ffffff",
            padx=6, pady=0, font=("", 9),
        )
        paint_btn.pack(side=tk.RIGHT, padx=(2, 2))

        key_entry = tk.Entry(
            row_frame, width=10,
            bg="#3a3a3a", fg="#cccccc", insertbackground="#cccccc",
            relief=tk.FLAT,
        )
        key_entry.insert(0, str(key_str))
        key_entry.pack(side=tk.LEFT)
        key_entry.bind("<FocusOut>", lambda e: on_property_changed(row_frame))

        value_entry = tk.Entry(
            row_frame, width=14,
            bg="#3a3a3a", fg="#cccccc", insertbackground="#cccccc",
            relief=tk.FLAT,
        )
        value_entry.insert(0, value_str)
        value_entry.pack(side=tk.LEFT, padx=(2, 0))
        value_entry.bind("<FocusOut>", lambda e: on_property_changed(row_frame))

        row_frame._key_entry = key_entry
        row_frame._value_entry = value_entry
        row_frame._key_str = key_str

    def rebuild_property_rows(tile):
        for w in list(props_container.winfo_children()):
            w.destroy()
        for k, v in tile.properties.items():
            create_property_row(k, v)

    def update_properties_panel():
        if selected_tile_index is not None and selected_tile_index < len(project.tiles):
            props_hint_label.pack_forget()
            props_container.pack(before=props_bottom_sep, fill=tk.X, padx=10, pady=1)
            props_add_btn.pack(before=props_bottom_sep, fill=tk.X, padx=10, pady=(0, 1))
            props_add_btn.config(command=on_add_property)
            rebuild_property_rows(project.tiles[selected_tile_index])
        else:
            if state["property_paint_mode"]:
                exit_property_paint()
            props_hint_label.pack(before=props_bottom_sep, fill=tk.X, padx=14, pady=1)
            props_container.pack_forget()
            props_add_btn.pack_forget()

    def on_grid_changed(*args):
        if _loading_project:
            return
        global selected_tile_index
        project.tiles.clear()
        project.groups.clear()
        selected_tile_index = None
        state["format_brush_mode"] = False
        state["format_brush_bitmask"] = None
        state["drag_mark"] = None
        state["property_paint_mode"] = False
        state["property_paint_key"] = None
        state["property_paint_value"] = None
        state["group_edit_mode"] = None
        state["group_color_map"] = {}
        group_hint_label.pack_forget()
        fmt_btn.config(text="格式刷")
        rebuild_groups_list()
        update_prop_paint_status()
        update_tile_info()
        update_mark_count()
        update_bitmask_label()
        redraw()
        update_properties_panel()

    cols_var.trace_add("write", on_grid_changed)
    rows_var.trace_add("write", on_grid_changed)
    padding_var.trace_add("write", lambda *_: redraw())

    def on_wheel(event):
        old_zoom = state["zoom"]

        if getattr(event, "delta", 0) > 0 or getattr(event, "num", 0) == 4:
            state["zoom"] = min(state["zoom"] * ZOOM_STEP, ZOOM_MAX)
        elif getattr(event, "delta", 0) < 0 or getattr(event, "num", 0) == 5:
            state["zoom"] = max(state["zoom"] / ZOOM_STEP, ZOOM_MIN)
        else:
            return

        if old_zoom == state["zoom"]:
            return

        cw, ch = canvas.winfo_width(), canvas.winfo_height()
        state["view_cx"] += (event.x - cw / 2) * (1 / old_zoom - 1 / state["zoom"])
        state["view_cy"] += (event.y - ch / 2) * (1 / old_zoom - 1 / state["zoom"])
        redraw()

    def on_mid_drag_start(event):
        state["drag_start"] = (event.x, event.y)

    def on_mid_drag_move(event):
        ds = state["drag_start"]
        if ds:
            dx = event.x - ds[0]
            dy = event.y - ds[1]
            state["drag_start"] = (event.x, event.y)
            state["view_cx"] -= dx / state["zoom"]
            state["view_cy"] -= dy / state["zoom"]
            redraw()

    def on_mid_drag_end(event):
        state["drag_start"] = None

    def on_left_press(event):
        global selected_tile_index
        canvas.focus_set()

        if state["property_paint_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_property_paint()
                return
            idx, tile = get_or_create_tile(cell[0], cell[1])
            tile.properties[state["property_paint_key"]] = state["property_paint_value"]
            state["drag_mark"] = (event.x, event.y, True, cell)
            update_mark_count()
            update_properties_panel()
            redraw()
            return

        if state["group_edit_mode"] is not None:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_group_edit()
                return
            gi = state["group_edit_mode"]
            if gi < len(project.groups):
                idx = find_tile_index(cell[0], cell[1])
                if idx is not None:
                    group = project.groups[gi]
                    was_in = idx in group.tile_indices
                    if was_in:
                        group.tile_indices.remove(idx)
                    else:
                        group.tile_indices.append(idx)
                    state["drag_mark"] = (event.x, event.y, not was_in, cell)
                    rebuild_groups_list()
                    redraw()
            return

        if state["format_brush_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_format_brush()
                return
            fbm = state["format_brush_bitmask"]
            idx, tile = get_or_create_tile(cell[0], cell[1])
            if fbm is not None:
                has_any = any(fbm[r][c] for r in range(3) for c in range(3))
                if has_any:
                    tile.bitmask = [row[:] for row in fbm]
                else:
                    tile.bitmask = None
            else:
                tile.bitmask = None
            state["drag_mark"] = (event.x, event.y, True, cell)
            update_mark_count()
            update_bitmask_label()
            redraw()
            return

        cell = cell_at_canvas_pos(event.x, event.y)
        if cell is None:
            selected_tile_index = None
            state["hover_cell"] = None
            update_bitmask_label()
            redraw()
            return
        existing_idx = find_tile_index(cell[0], cell[1])
        if existing_idx is not None:
            remove_tile(cell[0], cell[1])
            state["drag_mark"] = (event.x, event.y, False, cell)
        else:
            get_or_create_tile(cell[0], cell[1])
            state["drag_mark"] = (event.x, event.y, True, cell)
        update_mark_count()
        update_bitmask_label()
        redraw()
        update_properties_panel()

    def on_left_release(event):
        state["drag_mark"] = None

    def on_right_press(event):
        global selected_tile_index
        canvas.focus_set()

        if state["property_paint_mode"]:
            exit_property_paint()
            return

        if state["format_brush_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_format_brush()
                return
            remove_tile(cell[0], cell[1])
            state["drag_mark"] = (event.x, event.y, False, cell)
            update_mark_count()
            update_bitmask_label()
            update_properties_panel()
            redraw()
            return

        if state["group_edit_mode"] is not None:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                return
            gi = state["group_edit_mode"]
            if gi < len(project.groups):
                idx = find_tile_index(cell[0], cell[1])
                if idx is not None:
                    group = project.groups[gi]
                    if idx in group.tile_indices:
                        group.tile_indices.remove(idx)
                        group.tile_indices.insert(0, idx)
                        state["drag_mark"] = (event.x, event.y, False, cell)
                        rebuild_groups_list()
                        redraw()
            return

        cell = cell_at_canvas_pos(event.x, event.y)
        if cell is None:
            selected_tile_index = None
            update_bitmask_label()
            update_properties_panel()
            redraw()
            return
        existing_idx = find_tile_index(cell[0], cell[1])
        if existing_idx is not None:
            if selected_tile_index == existing_idx:
                selected_tile_index = None
            else:
                selected_tile_index = existing_idx
            update_bitmask_label()
            update_properties_panel()
            redraw()

    def on_right_release(event):
        state["drag_mark"] = None

    def on_mouse_move(event):
        if state["drag_start"]:
            return

        if state["property_paint_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            dm = state["drag_mark"]
            if dm and cell and cell != dm[3]:
                idx, tile = get_or_create_tile(cell[0], cell[1])
                tile.properties[state["property_paint_key"]] = state["property_paint_value"]
                state["drag_mark"] = (dm[0], dm[1], dm[2], cell)
                update_mark_count()
                update_properties_panel()
                redraw()
            elif not dm and cell and cell != state["hover_cell"]:
                state["hover_cell"] = cell
                redraw()
            return

        if state["format_brush_mode"]:
            fbm = state["format_brush_bitmask"]
            cell = cell_at_canvas_pos(event.x, event.y)
            dm = state["drag_mark"]
            if dm and cell and cell != dm[3]:
                if dm[2]:
                    idx, tile = get_or_create_tile(cell[0], cell[1])
                    if fbm is not None and any(fbm[r][c] for r in range(3) for c in range(3)):
                        tile.bitmask = [row[:] for row in fbm]
                    else:
                        tile.bitmask = None
                else:
                    remove_tile(cell[0], cell[1])
                state["drag_mark"] = (dm[0], dm[1], dm[2], cell)
                update_mark_count()
                update_bitmask_label()
                redraw()
            elif not dm and cell and cell != state["hover_cell"]:
                state["hover_cell"] = cell
                redraw()
            return

        if state["group_edit_mode"] is not None:
            dm = state["drag_mark"]
            cell = cell_at_canvas_pos(event.x, event.y)
            if dm and cell and cell != dm[3]:
                gi = state["group_edit_mode"]
                if gi < len(project.groups):
                    idx = find_tile_index(cell[0], cell[1])
                    if idx is not None:
                        group = project.groups[gi]
                        if dm[2]:
                            if idx not in group.tile_indices:
                                group.tile_indices.append(idx)
                        else:
                            if idx in group.tile_indices:
                                group.tile_indices.remove(idx)
                        state["drag_mark"] = (dm[0], dm[1], dm[2], cell)
                        rebuild_groups_list()
                        redraw()
            elif not dm and cell and cell != state["hover_cell"]:
                state["hover_cell"] = cell
                redraw()
            return

        dm = state["drag_mark"]
        if dm:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell and cell != dm[3]:
                if dm[2]:
                    get_or_create_tile(cell[0], cell[1])
                else:
                    remove_tile(cell[0], cell[1])
                state["drag_mark"] = (dm[0], dm[1], dm[2], cell)
                update_mark_count()
                redraw()
        else:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell != state["hover_cell"]:
                state["hover_cell"] = cell
                redraw()

    def on_leave(event):
        if state["hover_cell"] is not None:
            state["hover_cell"] = None
            redraw()

    def open_bitmask_editor():
        if selected_tile_index is None or selected_tile_index >= len(project.tiles):
            return
        tile = project.tiles[selected_tile_index]
        col, row = tile.col, tile.row
        dlg = tk.Toplevel()
        dlg.title(f"位掩码编辑 - ({col}, {row})")
        dlg.configure(bg="#2d2d2d")
        dlg.resizable(False, False)
        dlg.attributes('-topmost', True)
        dlg.geometry(f"{BITMASK_DIALOG_SIZE + 20}x{BITMASK_DIALOG_SIZE + 90}+{root.winfo_x() + 60}+{root.winfo_y() + 60}")
        dlg.deiconify()
        dlg.lift()
        dlg.focus_force()

        bitmask = tile.bitmask
        if bitmask is None:
            bitmask = [[0, 0, 0], [0, 0, 0], [0, 0, 0]]
        edit_bitmask = [row[:] for row in bitmask]

        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        cw_img = img.width // cols
        ch_img = img.height // rows
        left = col * cw_img
        top = row * ch_img
        right = (col + 1) * cw_img if col < cols - 1 else img.width
        bottom = (row + 1) * ch_img if row < rows - 1 else img.height
        tile_img = img.crop((left, top, right, bottom))
        tile_img = tile_img.resize((BITMASK_DIALOG_SIZE, BITMASK_DIALOG_SIZE), Image.NEAREST)

        bm_canvas = tk.Canvas(dlg, width=BITMASK_DIALOG_SIZE, height=BITMASK_DIALOG_SIZE, highlightthickness=0)
        bm_canvas.pack(padx=10, pady=10)

        bm_drag = {"start": None, "mark_state": None, "last_cell": None}
        bm_rdrag = {"start": None, "last_cell": None}

        cell_w = BITMASK_DIALOG_SIZE / 3
        cell_h = BITMASK_DIALOG_SIZE / 3

        def bm_cell_at(mx, my):
            if mx < 0 or my < 0 or mx >= BITMASK_DIALOG_SIZE or my >= BITMASK_DIALOG_SIZE:
                return None
            r = int(my // cell_h)
            c = int(mx // cell_w)
            return (r, c)

        def bm_redraw():
            result = Image.new("RGBA", (BITMASK_DIALOG_SIZE, BITMASK_DIALOG_SIZE))
            tile_checkerboard(result, BITMASK_DIALOG_SIZE, BITMASK_DIALOG_SIZE)
            result.paste(tile_img, (0, 0), tile_img)

            overlay = Image.new("RGBA", (BITMASK_DIALOG_SIZE, BITMASK_DIALOG_SIZE), (0, 0, 0, 0))
            draw = ImageDraw.Draw(overlay)

            for r in range(3):
                for c in range(3):
                    val = edit_bitmask[r][c]
                    l = c * cell_w
                    t = r * cell_h
                    if val == MASK_ON:
                        draw.rectangle([l, t, l + cell_w, t + cell_h], fill=BITMASK_YELLOW)
                    elif val == MASK_IGNORE:
                        draw.rectangle([l + 2, t + 2, l + cell_w - 2, t + cell_h - 2], fill=IGNORE_FILL)
                        draw.line([(l + 2, t + 2), (l + cell_w - 2, t + cell_h - 2)], fill=IGNORE_OUTLINE, width=2)

            for i in range(1, 3):
                x = i * cell_w
                draw.line([(x, 0), (x, BITMASK_DIALOG_SIZE)], fill=BITMASK_GRID_COLOR, width=1)
                y = i * cell_h
                draw.line([(0, y), (BITMASK_DIALOG_SIZE, y)], fill=BITMASK_GRID_COLOR, width=1)

            result = Image.alpha_composite(result, overlay)

            dlg._photo = ImageTk.PhotoImage(result)
            bm_canvas.delete("all")
            bm_canvas.create_image(BITMASK_DIALOG_SIZE // 2, BITMASK_DIALOG_SIZE // 2, image=dlg._photo)

        def bm_on_press(event):
            cell = bm_cell_at(event.x, event.y)
            if cell is None:
                return
            r, c = cell
            shift = bool(event.state & 0x0001)
            if shift:
                if edit_bitmask[r][c] == MASK_OFF:
                    edit_bitmask[r][c] = MASK_IGNORE
                elif edit_bitmask[r][c] == MASK_IGNORE:
                    edit_bitmask[r][c] = MASK_OFF
                else:
                    edit_bitmask[r][c] = MASK_OFF
            else:
                if edit_bitmask[r][c] == MASK_OFF:
                    edit_bitmask[r][c] = MASK_ON
                elif edit_bitmask[r][c] == MASK_ON:
                    edit_bitmask[r][c] = MASK_OFF
                else:
                    edit_bitmask[r][c] = MASK_ON
            bm_drag["start"] = (event.x, event.y)
            bm_drag["mark_state"] = edit_bitmask[r][c]
            bm_drag["last_cell"] = cell
            bm_redraw()

        def bm_on_release(event):
            bm_drag["start"] = None
            bm_drag["mark_state"] = None
            bm_drag["last_cell"] = None

        def bm_on_right_press(event):
            cell = bm_cell_at(event.x, event.y)
            if cell is None:
                return
            r, c = cell
            edit_bitmask[r][c] = 0
            bm_rdrag["start"] = (event.x, event.y)
            bm_rdrag["last_cell"] = cell
            bm_redraw()

        def bm_on_right_release(event):
            bm_rdrag["start"] = None
            bm_rdrag["last_cell"] = None

        def bm_on_right_move(event):
            if bm_rdrag["start"] is None:
                return
            cell = bm_cell_at(event.x, event.y)
            if cell and cell != bm_rdrag["last_cell"]:
                r, c = cell
                edit_bitmask[r][c] = 0
                bm_rdrag["last_cell"] = cell
                bm_redraw()

        def bm_on_move(event):
            if bm_drag["start"] is None:
                return
            cell = bm_cell_at(event.x, event.y)
            if cell and cell != bm_drag["last_cell"]:
                r, c = cell
                edit_bitmask[r][c] = bm_drag["mark_state"]
                bm_drag["last_cell"] = cell
                bm_redraw()

        def bm_on_clear():
            for r in range(3):
                for c in range(3):
                    edit_bitmask[r][c] = 0
            bm_redraw()

        def _validate_minimal_mask(bm):
            corners = [(0, 0), (0, 2), (2, 0), (2, 2)]
            edge_pairs = [((0, 1), (1, 0)), ((0, 1), (1, 2)), ((2, 1), (1, 0)), ((2, 1), (1, 2))]
            violations = []
            for (cr, cc), (e1, e2) in zip(corners, edge_pairs):
                if bm[cr][cc] == MASK_ON:
                    e1r, e1c = e1
                    e2r, e2c = e2
                    if bm[e1r][e1c] == MASK_OFF or bm[e2r][e2c] == MASK_OFF:
                        violations.append((cr, cc, e1, e2))
            return violations

        def bm_on_confirm():
            edit_bitmask[1][1] = MASK_ON
            violations = _validate_minimal_mask(edit_bitmask)
            if violations:
                msg = "违反 Minimal 约束：以下角位为 On 但相邻边未同时 On：\n"
                for cr, cc, e1, e2 in violations:
                    msg += f"  角位 ({cr},{cc}) 要求边 ({e1[0]},{e1[1]}) 和 ({e2[0]},{e2[1]}) 也为 On\n"
                msg += "是否仍要强制保存？"
                if not messagebox.askyesno("Minimal 约束警告", msg, parent=dlg):
                    return
            has_any = any(edit_bitmask[r][c] != MASK_OFF for r in range(3) for c in range(3))
            if has_any:
                tile.bitmask = [row[:] for row in edit_bitmask]
            else:
                tile.bitmask = None
            update_bitmask_label()
            dlg.destroy()
            redraw()

        def bm_on_cancel():
            dlg.destroy()

        dlg.protocol("WM_DELETE_WINDOW", bm_on_cancel)

        bm_canvas.bind("<Button-1>", bm_on_press)
        bm_canvas.bind("<ButtonRelease-1>", bm_on_release)
        bm_canvas.bind("<Button-3>", bm_on_right_press)
        bm_canvas.bind("<ButtonRelease-3>", bm_on_right_release)
        bm_canvas.bind("<B3-Motion>", bm_on_right_move)
        bm_canvas.bind("<B1-Motion>", bm_on_move)

        bottom = tk.Frame(dlg, bg="#2d2d2d")
        bottom.pack(fill=tk.X, padx=10, pady=(0, 10))

        tk.Label(
            bottom, bg="#2d2d2d", fg="#888888",
            text="左键: On/Off 循环 | Shift+左键: Off/Ignore 循环 | 右键: Off",
        ).pack(side=tk.LEFT)

        btn_row = tk.Frame(dlg, bg="#2d2d2d")
        btn_row.pack(fill=tk.X, padx=10, pady=(0, 10))

        dark_button(btn_row, "清除", bm_on_clear).pack(side=tk.LEFT)
        dark_button(btn_row, "取消", bm_on_cancel).pack(side=tk.RIGHT, padx=(4, 0))
        dark_button(btn_row, "确定", bm_on_confirm).pack(side=tk.RIGHT)

        dlg.update()
        bm_redraw()

    bitmask_btn.config(command=open_bitmask_editor)

    def on_export():
        if not project.tiles:
            messagebox.showwarning("导出", "没有已标记的图块。")
            return

        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        padding = _safe_int(padding_var)
        cw = img.width // cols
        ch = img.height // rows
        sorted_tiles = sorted(project.tiles, key=lambda t: (t.row, t.col))

        now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        normalized = filepath.replace("\\", "/")
        src_idx = normalized.find("/src/")
        if src_idx >= 0:
            rel_source = normalized[src_idx + 1:]
        else:
            rel_source = os.path.basename(normalized)
        lines = [
            f"-- Tileset mapping generated by tile.py at {now}",
            f"-- Marked: {len(sorted_tiles)} tiles",
            f"-- Bitmask values: 0 = Off, 1 = On, 2 = Ignore (Minimal 3x3 Autotile)",
            "",
            "return {",
            f"    source = {repr(rel_source)},",
            f"    cols = {cols},",
            f"    rows = {rows},",
            f"    tile_width = {cw},",
            f"    tile_height = {ch},",
            f"    padding = {padding},",
            f"    count = {len(sorted_tiles)},",
            "    tiles = {",
        ]
        for i, t in enumerate(sorted_tiles):
            entry = f"        [{i}] = {{ col = {t.col}, row = {t.row}"
            if t.properties:
                props_parts = []
                for k, v in t.properties.items():
                    if isinstance(v, bool):
                        props_parts.append(f"{k} = {str(v).lower()}")
                    elif isinstance(v, (int, float)):
                        props_parts.append(f"{k} = {v}")
                    else:
                        props_parts.append(f"{k} = {repr(v)}")
                entry += f", properties = {{ {', '.join(props_parts)} }}"
            bm = t.bitmask
            if bm and any(any(c for c in row_bm) for row_bm in bm):
                lines.append(entry + f", bitmask = {{")
                lines.append(f"            {{{bm[0][0]}, {bm[0][1]}, {bm[0][2]}}},")
                lines.append(f"            {{{bm[1][0]}, {bm[1][1]}, {bm[1][2]}}},")
                lines.append(f"            {{{bm[2][0]}, {bm[2][1]}, {bm[2][2]}}},")
                lines.append(f"        }} }},")
            else:
                lines.append(entry + " },")
        lines.append("    },")
        if project.groups:
            lines.append("    groups = {")
            for group in project.groups:
                tiles_str = ", ".join(str(i) for i in group.tile_indices)
                lines.append(f'        ["{group.name}"] = {{ tiles = {{ {tiles_str} }} }},')
            lines.append("    },")
        lines.append("}")
        lines.append("")

        lua_code = "\n".join(lines)
        root.clipboard_clear()
        root.clipboard_append(lua_code)

        dlg = tk.Toplevel(root)
        dlg.title("导出 Lua 代码")
        dlg.resizable(True, True)
        dlg.geometry("500x400")
        dlg.minsize(640, 500)
        dlg.configure(bg="#2d2d2d")

        dlg_frame = tk.Frame(dlg, bg="#2d2d2d")
        dlg_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        tk.Label(
            dlg_frame, bg="#2d2d2d", fg="#cccccc",
            text=f"Lua 代码已生成。",
        ).pack(anchor=tk.W)

        btn_frame = tk.Frame(dlg_frame, bg="#2d2d2d")
        btn_frame.pack(fill=tk.X, pady=(6, 0))

        default_name = os.path.splitext(os.path.basename(filepath))[0] + "_tiles.lua"
        default_dir = os.path.dirname(os.path.abspath(filepath))

        def on_save():
            save_path = filedialog.asksaveasfilename(
                parent=dlg,
                initialdir=default_dir,
                initialfile=default_name,
                defaultextension=".lua",
                filetypes=[("Lua 文件", "*.lua"), ("所有文件", "*.*")],
            )
            if save_path:
                with open(save_path, "w", encoding="utf-8") as f:
                    f.write(lua_code)
                messagebox.showinfo("保存成功", f"已保存到:\n{save_path}", parent=dlg)

        tk.Button(
            btn_frame, text="保存到文件", command=on_save,
            bg="#3a3a3a", fg="#cccccc", relief=tk.FLAT, bd=0,
            activebackground="#555555", activeforeground="#ffffff",
            padx=12, pady=4,
        ).pack(side=tk.LEFT)

        tk.Button(
            btn_frame, text="关闭", command=dlg.destroy,
            bg="#3a3a3a", fg="#cccccc", relief=tk.FLAT, bd=0,
            activebackground="#555555", activeforeground="#ffffff",
            padx=12, pady=4,
        ).pack(side=tk.RIGHT)

        text = tk.Text(
            dlg_frame, bg="#1e1e1e", fg="#cccccc",
            font=("monospace", 10), relief=tk.FLAT, bd=4,
            insertbackground="#cccccc",
        )
        text.pack(fill=tk.BOTH, expand=True, pady=(6, 0))
        text.insert("1.0", lua_code)
        text.config(state=tk.DISABLED)

    export_btn.config(command=on_export)

    def on_import():
        global filepath, img, project, selected_tile_index, _loading_project
        lua_path = filedialog.askopenfilename(
            filetypes=[("Lua 文件", "*.lua"), ("所有文件", "*.*")],
        )
        if not lua_path:
            return

        with open(lua_path, "r", encoding="utf-8") as f:
            text = f.read()

        try:
            data = parse_lua_table(text)
        except Exception as e:
            messagebox.showerror("解析失败", f"无法解析文件:\n{e}")
            return

        source_rel = data.get("source", "")
        png_path = os.path.join(WORKSPACE, source_rel) if source_rel else ""
        if not png_path or not os.path.exists(png_path):
            messagebox.showerror("图片未找到", f"无法找到图片:\n{png_path}")
            return

        filepath = png_path
        img = Image.open(png_path).convert("RGBA")

        project.meta.source = source_rel
        project.meta.cols = data.get("cols", 1)
        project.meta.rows = data.get("rows", 1)
        project.meta.tile_width = data.get("tile_width", 0)
        project.meta.tile_height = data.get("tile_height", 0)
        project.meta.padding = data.get("padding", 0)

        project.tiles.clear()
        project.groups.clear()
        tiles_data = data.get("tiles", [])
        if isinstance(tiles_data, dict):
            tiles_list = [tiles_data[k] for k in sorted(tiles_data.keys())]
        else:
            tiles_list = tiles_data
        for i, tile_entry in enumerate(tiles_list):
            if tile_entry is None:
                continue
            tile = TileDef(
                index=i,
                col=tile_entry.get("col", 0),
                row=tile_entry.get("row", 0),
                properties=tile_entry.get("properties", {}),
                bitmask=tile_entry.get("bitmask"),
            )
            project.tiles.append(tile)

        groups_data = data.get("groups", {})
        if isinstance(groups_data, dict):
            for name, group_entry in groups_data.items():
                group = GroupDef(
                    name=name,
                    tile_indices=list(group_entry.get("tiles", [])),
                )
                project.groups.append(group)

        selected_tile_index = None

        _loading_project = True
        cols_var.set(project.meta.cols)
        rows_var.set(project.meta.rows)
        padding_var.set(project.meta.padding)
        _loading_project = False

        root.title(filepath)
        update_tile_info()
        update_mark_count()
        update_bitmask_label()
        state["format_brush_mode"] = False
        state["format_brush_bitmask"] = None
        state["drag_mark"] = None
        state["group_edit_mode"] = None
        state["group_color_map"] = {}
        group_hint_label.pack_forget()
        fmt_btn.config(text="格式刷")
        rebuild_groups_list()
        redraw()

    root.bind("<Control-o>", lambda e: on_import())

    def on_escape():
        if state["property_paint_mode"]:
            exit_property_paint()
        elif state["format_brush_mode"]:
            exit_format_brush()
        elif state["group_edit_mode"] is not None:
            exit_group_edit()

    root.bind("<Escape>", lambda e: on_escape())

    canvas.bind("<Configure>", lambda e: redraw())
    canvas.bind("<MouseWheel>", on_wheel)
    canvas.bind("<Button-4>", on_wheel)
    canvas.bind("<Button-5>", on_wheel)
    canvas.bind("<Button-2>", on_mid_drag_start)
    canvas.bind("<B2-Motion>", on_mid_drag_move)
    canvas.bind("<ButtonRelease-2>", on_mid_drag_end)
    canvas.bind("<Button-1>", on_left_press)
    canvas.bind("<ButtonRelease-1>", on_left_release)
    canvas.bind("<Button-3>", on_right_press)
    canvas.bind("<ButtonRelease-3>", on_right_release)
    canvas.bind("<B1-Motion>", on_mouse_move)
    canvas.bind("<Motion>", on_mouse_move)
    canvas.bind("<Leave>", on_leave)

    update_tile_info()
    update_mark_count()
    root.mainloop()