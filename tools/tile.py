import os
import datetime
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageDraw, ImageTk

CELL = 32
LIGHT = (36, 39, 58, 255)
DARK = (19, 22, 29, 255)
ZOOM_MIN = 0.025
ZOOM_MAX = 32.0
ZOOM_STEP = 1.1
SIDEBAR_W = 210
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

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(WORKSPACE, "src")


def relpath_from_src(path):
    abs_path = os.path.abspath(path)
    rel = os.path.relpath(abs_path, SRC_DIR)
    return "src/" + rel.replace(os.sep, "/")

root = tk.Tk()
root.withdraw()

filepath = filedialog.askopenfilename(
    initialdir="../src/assets",
    filetypes=[("图片文件", "*.png *.jpg")],
)

if filepath:
    root.deiconify()
    img = Image.open(filepath).convert("RGBA")

    main_frame = tk.Frame(root)
    main_frame.pack(fill=tk.BOTH, expand=True)

    canvas = tk.Canvas(main_frame, highlightthickness=0)
    canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    sidebar = tk.Frame(main_frame, width=SIDEBAR_W, bg="#2d2d2d")
    sidebar.pack(side=tk.RIGHT, fill=tk.Y)
    sidebar.pack_propagate(False)

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

    clear_btn = dark_button(mark_btn_frame, "清除", None)
    clear_btn.pack(side=tk.LEFT)
    invert_btn = dark_button(mark_btn_frame, "反选", None)
    invert_btn.pack(side=tk.LEFT, padx=(4, 0))
    fmt_btn = dark_button(mark_btn_frame, "格式刷", None)
    fmt_btn.pack(side=tk.LEFT, padx=(4, 0))

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
        st = state["selected_tile"]
        if st and st in state["bitmasks"]:
            bitmask_label.config(text="位掩码: 已设置")
        else:
            bitmask_label.config(text="位掩码: 无")
        if st:
            bitmask_btn.config(state=tk.NORMAL)
        else:
            bitmask_btn.config(state=tk.DISABLED)
        if state["format_brush_mode"]:
            fmt_btn.config(state=tk.NORMAL)
        elif st and st in state["bitmasks"]:
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
        st = state["selected_tile"]
        if not state["format_brush_mode"]:
            if st is None or st not in state["bitmasks"]:
                return
            bm = state["bitmasks"][st]
            state["format_brush_bitmask"] = [row[:] for row in bm]
            state["format_brush_mode"] = True
            state["drag_mark"] = None
            fmt_btn.config(text="取消格式刷")
        else:
            exit_format_brush()
        update_bitmask_label()
        redraw()

    fmt_btn.config(command=on_format_brush)

    tk.Frame(sidebar, height=1, bg="#444444").pack(fill=tk.X, padx=10, pady=8)

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
        "marked_cells": set(),
        "hover_cell": None,
        "drag_mark": None,  # (start_x, start_y, mark_state)
        "bitmasks": {},          # {(col, row): [[0,0,0],[0,0,0],[0,0,0]]}
        "selected_tile": None,   # (col, row) or None
        "format_brush_mode": False,
        "format_brush_bitmask": None,  # [[0,0,0],[0,0,0],[0,0,0]] or None
    }

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

        for col, row in state["marked_cells"]:
            l, t, r, b = get_cell_rect(col, row, scale, ox, oy)
            draw.rectangle([l, t, r, b], fill=MARK_FILL, outline=MARK_OUTLINE, width=1)

        st = state["selected_tile"]
        if st:
            l, t, r, b = get_cell_rect(st[0], st[1], scale, ox, oy)
            draw.rectangle([l, t, r, b], outline=SELECT_COLOR, width=2)

        for (col, row), bm in state["bitmasks"].items():
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
        mark_count_label.config(text=f"已标记: {len(state['marked_cells'])}")

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

    def on_grid_changed(*args):
        state["marked_cells"].clear()
        state["bitmasks"].clear()
        state["selected_tile"] = None
        state["format_brush_mode"] = False
        state["format_brush_bitmask"] = None
        state["drag_mark"] = None
        fmt_btn.config(text="格式刷")
        update_tile_info()
        update_mark_count()
        update_bitmask_label()
        redraw()

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
        if state["format_brush_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_format_brush()
                return
            fbm = state["format_brush_bitmask"]
            if fbm is not None:
                has_any = any(fbm[r][c] for r in range(3) for c in range(3))
                if has_any:
                    state["bitmasks"][cell] = [row[:] for row in fbm]
                else:
                    state["bitmasks"].pop(cell, None)
                state["marked_cells"].add(cell)
                state["drag_mark"] = (event.x, event.y, True, cell)
                update_mark_count()
                update_bitmask_label()
                redraw()
            return

        cell = cell_at_canvas_pos(event.x, event.y)
        if cell is None:
            state["selected_tile"] = None
            state["hover_cell"] = None
            update_bitmask_label()
            redraw()
            return
        if cell in state["marked_cells"]:
            if state["selected_tile"] == cell:
                state["selected_tile"] = None
            else:
                state["selected_tile"] = cell
        else:
            state["marked_cells"].add(cell)
            mark_state = True
            state["drag_mark"] = (event.x, event.y, mark_state, cell)
        update_mark_count()
        update_bitmask_label()
        redraw()

    def on_left_release(event):
        state["drag_mark"] = None

    def on_right_press(event):
        if state["format_brush_mode"]:
            cell = cell_at_canvas_pos(event.x, event.y)
            if cell is None:
                exit_format_brush()
                return
            state["bitmasks"].pop(cell, None)
            state["marked_cells"].discard(cell)
            state["drag_mark"] = (event.x, event.y, False, cell)
            update_mark_count()
            update_bitmask_label()
            redraw()
            return

        cell = cell_at_canvas_pos(event.x, event.y)
        if cell is None:
            return
        if cell in state["marked_cells"]:
            state["marked_cells"].discard(cell)
            if cell in state["bitmasks"]:
                del state["bitmasks"][cell]
            if state["selected_tile"] == cell:
                state["selected_tile"] = None
            state["drag_mark"] = (event.x, event.y, False, cell)
            update_mark_count()
            update_bitmask_label()
            redraw()

    def on_right_release(event):
        state["drag_mark"] = None

    def on_mouse_move(event):
        if state["drag_start"]:
            return

        if state["format_brush_mode"]:
            fbm = state["format_brush_bitmask"]
            cell = cell_at_canvas_pos(event.x, event.y)
            dm = state["drag_mark"]
            if dm and cell and cell != dm[3]:
                if dm[2]:
                    if fbm is not None and any(fbm[r][c] for r in range(3) for c in range(3)):
                        state["bitmasks"][cell] = [row[:] for row in fbm]
                    else:
                        state["bitmasks"].pop(cell, None)
                    state["marked_cells"].add(cell)
                else:
                    state["bitmasks"].pop(cell, None)
                    state["marked_cells"].discard(cell)
                state["drag_mark"] = (dm[0], dm[1], dm[2], cell)
                update_mark_count()
                update_bitmask_label()
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
                    state["marked_cells"].add(cell)
                else:
                    state["marked_cells"].discard(cell)
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

    def on_clear():
        state["marked_cells"].clear()
        state["bitmasks"].clear()
        state["selected_tile"] = None
        state["format_brush_mode"] = False
        state["format_brush_bitmask"] = None
        state["drag_mark"] = None
        fmt_btn.config(text="格式刷")
        update_mark_count()
        update_bitmask_label()
        redraw()

    def on_invert():
        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        all_cells = {(c, r) for r in range(rows) for c in range(cols)}
        state["marked_cells"] = all_cells - state["marked_cells"]
        state["bitmasks"] = {k: v for k, v in state["bitmasks"].items() if k in state["marked_cells"]}
        if state["selected_tile"] not in state["marked_cells"]:
            state["selected_tile"] = None
            update_bitmask_label()
        update_mark_count()
        redraw()

    clear_btn.config(command=on_clear)
    invert_btn.config(command=on_invert)

    def open_bitmask_editor():
        st = state["selected_tile"]
        if st is None:
            return

        col, row = st
        dlg = tk.Toplevel()
        dlg.title(f"位掩码编辑 - ({col}, {row})")
        dlg.configure(bg="#2d2d2d")
        dlg.resizable(False, False)
        dlg.attributes('-topmost', True)
        dlg.geometry(f"{BITMASK_DIALOG_SIZE + 20}x{BITMASK_DIALOG_SIZE + 90}+{root.winfo_x() + 60}+{root.winfo_y() + 60}")
        dlg.deiconify()
        dlg.lift()
        dlg.focus_force()

        bitmask = state["bitmasks"].get((col, row))
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
                state["bitmasks"][(col, row)] = [row[:] for row in edit_bitmask]
            elif (col, row) in state["bitmasks"]:
                del state["bitmasks"][(col, row)]
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
        marked = state["marked_cells"]
        if not marked:
            messagebox.showwarning("导出", "没有已标记的图块。")
            return

        cols = _safe_int(cols_var)
        rows = _safe_int(rows_var)
        padding = _safe_int(padding_var)
        cw = img.width // cols
        ch = img.height // rows
        sorted_cells = sorted(marked, key=lambda c: (c[1], c[0]))

        now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        normalized = filepath.replace("\\", "/")
        src_idx = normalized.find("/src/")
        if src_idx >= 0:
            rel_source = normalized[src_idx + 1:]
        else:
            rel_source = os.path.basename(normalized)
        lines = [
            f"-- Tileset mapping generated by tile.py at {now}",
            f"-- Marked: {len(sorted_cells)} tiles",
            f"-- Bitmask values: 0 = Off, 1 = On, 2 = Ignore (Minimal 3x3 Autotile)",
            "",
            "return {",
            f"    source = {repr(rel_source)},",
            f"    cols = {cols},",
            f"    rows = {rows},",
            f"    tile_width = {cw},",
            f"    tile_height = {ch},",
            f"    padding = {padding},",
            f"    count = {len(sorted_cells)},",
            "    tiles = {",
        ]
        for i, (col, row) in enumerate(sorted_cells):
            bm = state["bitmasks"].get((col, row))
            if bm and any(any(cell for cell in row_bm) for row_bm in bm):
                lines.append(f"        [{i}] = {{ col = {col}, row = {row}, bitmask = {{")
                lines.append(f"            {{{bm[0][0]}, {bm[0][1]}, {bm[0][2]}}},")
                lines.append(f"            {{{bm[1][0]}, {bm[1][1]}, {bm[1][2]}}},")
                lines.append(f"            {{{bm[2][0]}, {bm[2][1]}, {bm[2][2]}}},")
                lines.append(f"        }} }},")
            else:
                lines.append(f"        [{i}] = {{ col = {col}, row = {row} }},")
        lines.append("    }")
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