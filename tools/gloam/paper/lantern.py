"""Original 42×56 pixel road lantern. No photograph, model or screenshot input.

uv run --project tools/gloam/terrain python tools/gloam/paper/lantern.py
The glass centre is x=12,y=16; ground/light anchor is (12,55), pole foot (31,55).
At 2.10 m card height the glass is 1.4625 m above the receiving plane.
"""
import json
from PIL import Image
from cast_art import Art, ROOT, TOP, LEFT, RIGHT, BOTTOM, FRONT

OUT = ROOT / 'assets/gloam/paper'


def main():
    a = Art(42, 56)
    # A notched post with four broad wood colours, no sampled wood grain.
    a.poly([(28,3),(29,0),(31,2),(33,1),(35,4),(34,15),(35,30),
            (34,49),(36,53),(31,55),(27,53),(28,38),(27,23)], 'ink')
    a.poly([(29,4),(30,2),(31,5),(31,22),(30,38),(31,51),(28,52),
            (29,35),(28,21)], 'woodlight', LEFT)
    a.poly([(31,5),(33,3),(33,17),(34,30),(33,50),(31,52),(30,38)], 'wood', FRONT)
    a.poly([(33,6),(34,5),(33,22),(34,32),(33,49),(32,50),(32,26)], 'bark', RIGHT)
    a.poly([(29,3),(30,1),(31,3),(33,2),(33,4),(31,6)], 'woodtop', TOP)
    a.line([(30,15),(30,22),(29,25)], 'bark', LEFT)
    a.line([(32,32),(31,37),(32,42)], 'barkdark', RIGHT)
    # Short crooked iron arm, reinforced below; no smooth vector curves.
    a.line([(31,13),(26,12),(22,9),(18,5),(13,4),(10,6),(10,8)], 'ink', width=3)
    a.line([(30,11),(25,10),(22,7),(18,4),(13,3),(10,5)], 'stone', TOP)
    a.line([(31,17),(26,16),(21,11)], 'deep', LEFT, width=2)
    a.rect((29,9,33,10), 'woodtop', TOP)
    a.rect((29,12,33,12), 'woodlight', FRONT)
    a.rect((29,14,33,14), 'woodtop', TOP)
    a.line([(28,13),(27,17),(28,20)], 'woodlight', LEFT)
    # Copper rain hood and hexagonal glass. Dark frame pixels overwrite the
    # emission map, so the post, bindings, cap and stone base can never glow.
    a.poly([(4,11),(6,8),(10,7),(11,6),(14,7),(16,9),(19,11),
            (18,13),(15,14),(8,14),(4,13)], 'ink')
    a.poly([(5,11),(8,9),(11,8),(10,11),(7,12)], 'woodtop', LEFT)
    a.poly([(11,8),(14,9),(17,11),(13,12),(10,11)], 'wood', TOP)
    a.line([(5,12),(10,13),(14,13),(18,12)], 'warm', TOP)
    a.poly([(6,14),(10,15),(10,22),(7,20)], 'warmdark', LEFT, 90)
    a.poly([(10,15),(14,14),(15,21),(12,24),(10,22)], 'amber', FRONT, 210)
    a.poly([(14,14),(18,13),(17,20),(15,22)], 'warm', RIGHT, 140)
    a.rect((11,16,12,21), 'cream', FRONT, 255)
    a.rect((12,18,13,20), 'cream', FRONT, 255)
    a.line([(6,14),(7,20),(10,23),(12,24),(16,22),(18,14)], 'wood', BOTTOM)
    a.line([(9,14),(10,22)], 'woodlight', LEFT)
    a.line([(14,14),(15,21)], 'wood', RIGHT)
    a.poly([(7,21),(11,24),(14,24),(17,21),(17,24),(13,26),(10,26),(7,24)], 'ink', BOTTOM)
    a.line([(8,23),(11,25),(14,25),(16,23)], 'warm', TOP)
    a.rect((11,26,13,27), 'stone', BOTTOM)
    # Flat slate foot, with just one moss cluster.
    a.poly([(25,49),(30,48),(36,49),(40,53),(38,55),(25,55),(22,53)], 'ink')
    a.poly([(25,50),(30,49),(34,50),(31,52),(24,53)], 'stonetop', TOP)
    a.poly([(31,52),(35,50),(38,53),(36,54),(31,54)], 'stone', RIGHT)
    a.poly([(24,53),(28,54),(31,53),(30,55),(25,55)], 'stoneback', LEFT)
    a.poly([(24,51),(26,50),(29,50),(28,51),(27,52),(24,52)], 'leafshade', TOP)
    a.line([(25,50),(27,50)], 'leaflight', TOP)
    (OUT/'cast').mkdir(parents=True, exist_ok=True)
    for suffix, image in [('', a.color), ('_normal', a.normal), ('_emission', a.emission)]:
        image.save(OUT / f'lantern{suffix}.png')
    preview = Image.new('RGB', (42*3,56), '#293449')
    preview.paste(a.color, (0,0), a.color)
    preview.paste(a.normal, (42,0), a.normal)
    preview.paste(a.emission.convert('RGB'), (84,0))
    preview.resize((756,336),Image.Resampling.NEAREST).save(OUT/'cast/lantern_planes_6x.png')
    (OUT/'cast/lantern.json').write_text(json.dumps({
        'name': 'lantern', 'image': 'gloam/paper/lantern.png',
        'normal': 'gloam/paper/lantern_normal.png',
        'emission': 'gloam/paper/lantern_emission.png',
        'size_px': [42,56], 'ground_anchor_px': [12,55],
        'pole_foot_px': [31,55], 'glass_center_px': [12,16],
        'source': 'tools/gloam/paper/lantern.py',
        'source_policy_zh': '原创逐像素多边形、四阶木纹，无摄影素材；仅独立六角玻璃遮罩发光。',
    },ensure_ascii=False,indent=2)+'\n')
    print('Wrote original pixel lamp, matched face normals and glass-only emission.')


if __name__ == '__main__':
    main()
