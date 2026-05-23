#!/usr/bin/env python3
"""
Tileset Generator for Trogue
Generates ASCII character tileset from TTF font.

Usage:
    python generate_tileset.py [tile_size]
    
Output:
    src/assets/tileset.png - Tileset image with characters
    src/assets/tileset_info.lua - Lua file mapping character names to indices
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont

# Tile definitions: (char, foreground_rgb, background_rgb_or_None)
TILES = [
    # Index 0: Floor
    (".", (102, 102, 102), None),                    # floor
    # Index 1: Wall
    ("#", (136, 136, 136), (51, 51, 51)),           # wall
    # Index 2: Player
    ("@", (255, 255, 0), (61, 61, 0)),             # player
    # Index 3: Enemy (goblin)
    ("g", (255, 80, 80), (80, 20, 20)),             # enemy
    # Index 4: Enemy (rat)
    ("r", (180, 120, 80), (60, 40, 20)),           # enemy
    # Index 5: Enemy (orc)
    ("O", (0, 200, 0), (0, 60, 0)),                # enemy
    # Index 6: Item (potion)
    ("!", (255, 100, 255), (80, 0, 80)),           # item
    # Index 7: Item (gold)
    ("*", (255, 215, 0), (100, 80, 0)),            # item
    # Index 8: Tree (solid)
    ("^", (34, 139, 34), (20, 80, 20)),           # tree
]

TILES_PER_ROW = 8


def generate_tileset(tile_size=16):
    """Generate tileset PNG from TTF font."""
    # Paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    font_path = os.path.join(project_root, "fonts", "BigBlueTerm437NerdFontMono-Regular.ttf")
    assets_dir = os.path.join(project_root, "src", "assets")
    
    # Check font exists
    if not os.path.exists(font_path):
        print(f"Error: Font not found at {font_path}")
        sys.exit(1)
    
    # Create assets directory
    os.makedirs(assets_dir, exist_ok=True)
    
    # Calculate canvas size
    num_tiles = len(TILES)
    num_rows = (num_tiles + TILES_PER_ROW - 1) // TILES_PER_ROW
    width = TILES_PER_ROW * tile_size
    height = num_rows * tile_size
    
    # Create image with black background
    img = Image.new('RGBA', (width, height), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    
    # Load font (slightly smaller than tile for padding)
    font_size = int(tile_size * 0.875)
    font = ImageFont.truetype(font_path, font_size)
    
    # Generate tiles
    for i, (char, fg, bg) in enumerate(TILES):
        x = (i % TILES_PER_ROW) * tile_size
        y = (i // TILES_PER_ROW) * tile_size
        
        # Draw background
        if bg:
            draw.rectangle([x, y, x + tile_size - 1, y + tile_size - 1], fill=(*bg, 255))
        else:
            # Transparent background - draw subtle checkerboard pattern for visibility
            for cy in range(y, y + tile_size, 2):
                for cx in range(x, x + tile_size, 2):
                    draw.point((cx, cy), fill=(30, 30, 30, 255))
        
        # Draw character centered
        bbox = draw.textbbox((0, 0), char, font=font)
        char_w = bbox[2] - bbox[0]
        char_h = bbox[3] - bbox[1]
        
        # Center the character in the tile
        tx = x + (tile_size - char_w) // 2 - bbox[0]
        ty = y + (tile_size - char_h) // 2 - bbox[1]
        
        draw.text((tx, ty), char, fill=(*fg, 255), font=font)
    
    # Save tileset
    tileset_path = os.path.join(assets_dir, "tileset.png")
    img.save(tileset_path)
    print(f"Generated: {tileset_path}")
    
    # Generate Lua info file
    info_path = os.path.join(assets_dir, "tileset_info.lua")
    with open(info_path, 'w') as f:
        f.write("-- Auto-generated tileset info\n")
        f.write("-- TILE_SIZE = " + str(tile_size) + "\n")
        f.write("-- TILES_PER_ROW = " + str(TILES_PER_ROW) + "\n\n")
        f.write("return {\n")
        for i, (char, fg, bg) in enumerate(TILES):
            tile_name = char.replace(".", "dot").replace("#", "hash")
            if not char.isalnum():
                tile_name = "tile_" + str(i)
            f.write(f"    {tile_name} = {i},  -- '{char}'\n")
        f.write("}\n")
    print(f"Generated: {info_path}")
    
    return tileset_path


if __name__ == "__main__":
    tile_size = 16
    if len(sys.argv) > 1:
        try:
            tile_size = int(sys.argv[1])
        except ValueError:
            print(f"Invalid tile size: {sys.argv[1]}")
            sys.exit(1)
    
    generate_tileset(tile_size)
    print("Done!")
