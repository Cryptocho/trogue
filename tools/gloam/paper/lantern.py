"""Hand-authored 42×56 pixel road lantern after the saved HD-guided art direction.

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
    # A crooked split post: narrow growth faces, two forked checks and a knot.
    # Its original silhouette, pole foot and hanging-glass position stay fixed.
    a.poly([(28,3),(29,0),(31,2),(33,1),(35,4),(34,15),(35,30),
            (34,49),(36,53),(31,55),(27,53),(28,38),(27,23)], 'barkdark')
    a.poly([(29,4),(30,2),(31,5),(31,20),(30,30),(31,42),(30,51),
            (28,52),(29,36),(28,22)], 'woodlight', LEFT)
    a.poly([(31,5),(33,3),(33,17),(34,30),(33,50),(31,52),
            (31,42),(30,31),(31,22)], 'wood', FRONT)
    a.poly([(33,6),(34,5),(33,22),(34,32),(33,49),(32,50),
            (32,36),(33,31)], 'bark', RIGHT)
    a.poly([(29,3),(30,1),(31,3),(33,2),(33,4),(31,6)], 'woodtop', TOP)
    a.line([(31,4),(30,7),(31,11),(30,15)], 'barkdark', LEFT)
    a.line([(30,7),(29,6)], 'bark', LEFT)
    a.line([(29,18),(30,21),(29,26),(30,29)], 'bark', LEFT)
    a.line([(29,31),(29,35)], 'woodtop', LEFT)
    a.line([(32,36),(31,40),(32,44),(31,48)], 'barkdark', RIGHT)
    a.line([(32,43),(33,41)], 'bark', RIGHT)
    a.poly([(32,28),(33,29),(33,32),(32,33),(31,31)], 'bark', RIGHT)
    a.line([(32,29),(32,31)], 'barkdark', RIGHT)
    a.line([(30,45),(30,49),(29,51)], 'woodtop', LEFT)
    # Hand-bent bronze arm and a diagonal load-bearing brace. The fine upper
    # edge and little return curl are metal, not a thick black branch.
    a.line([(31,13),(26,12),(22,9),(18,5),(13,4),(10,6),(10,8)],
           'copperdark', width=3)
    a.line([(30,12),(26,11),(22,8),(18,4),(13,3),(10,5)],
           'copper', TOP)
    a.line([(27,10),(24,9),(20,5)], 'copperlight', TOP)
    a.line([(31,17),(26,16),(21,11)], 'copperdark', LEFT, width=2)
    a.line([(29,16),(26,15),(24,13)], 'coppershade', LEFT)
    a.line([(33,14),(36,15),(37,17),(35,18),(33,16)], 'coppershade', RIGHT)
    a.line([(35,15),(36,16)], 'copperlight', TOP)
    a.rect((28,9,34,10), 'copperdark', FRONT)
    a.line([(29,9),(33,9)], 'copperlight', TOP)
    a.rect((28,13,34,14), 'copperdark', FRONT)
    a.line([(29,13),(32,13)], 'copper', TOP)
    a.rect((33,10,33,10), 'copperglint', RIGHT)
    a.line([(10,6),(10,8),(12,9)], 'copperdark', FRONT)
    a.rect((11,7,12,8), 'copper', LEFT)
    # Faceted warm copper rain hood: the right folded edge stays dull.
    a.poly([(4,11),(6,8),(10,7),(11,6),(14,7),(16,9),(19,11),
            (18,13),(15,14),(8,14),(4,13)], 'copperdark')
    a.poly([(5,11),(8,9),(11,8),(10,11),(7,12)], 'copper', LEFT)
    a.poly([(11,8),(14,9),(17,11),(13,12),(10,11)], 'copperlight', TOP)
    a.poly([(15,10),(18,11),(17,12),(14,12)], 'coppershade', RIGHT)
    a.line([(7,10),(9,9)], 'copperglint', TOP)
    a.line([(5,12),(10,13),(14,13),(18,12)], 'copperlight', TOP)
    a.line([(8,13),(10,14),(14,14),(16,13)], 'coppershade', BOTTOM)
    # The original glass footprint and hot core remain in place. Separate
    # side panes get separate authored normals; no metal or wood may emit.
    a.poly([(6,14),(10,15),(10,22),(7,20)], 'warmdark', LEFT, 90)
    a.poly([(10,15),(14,14),(15,21),(12,24),(10,22)], 'amber', FRONT, 210)
    a.poly([(14,14),(18,13),(17,20),(15,22)], 'warm', RIGHT, 140)
    a.rect((11,16,12,21), 'cream', FRONT, 255)
    a.rect((12,18,13,20), 'cream', FRONT, 255)
    a.line([(7,15),(8,18)], 'amber', LEFT, e=120)
    a.line([(16,15),(16,17)], 'cream', RIGHT, e=180)
    # One-pixel diagonal guard wires cross the glass, overwriting emission.
    a.line([(7,15),(10,18),(14,22)], 'coppershade', LEFT)
    a.line([(16,15),(13,18),(9,21)], 'copper', RIGHT)
    a.line([(6,14),(7,20),(10,23),(12,24),(16,22),(18,14)], 'copperdark', BOTTOM)
    a.line([(9,14),(10,22)], 'copper', LEFT)
    a.line([(14,14),(15,21)], 'coppershade', RIGHT)
    a.rect((7,16,7,18), 'copperlight', LEFT)
    a.poly([(7,21),(11,24),(14,24),(17,21),(17,24),(13,26),
            (10,26),(7,24)], 'copperdark', BOTTOM)
    a.line([(8,23),(11,25),(14,25),(16,23)], 'copper', TOP)
    a.line([(10,24),(12,25)], 'copperlight', TOP)
    a.rect((11,26,13,27), 'coppershade', BOTTOM)
    # Three offset slate feet, with an exposed cleavage and one moss lip.
    a.poly([(25,49),(30,48),(36,49),(40,53),(38,55),
            (25,55),(22,53)], 'slateedge')
    a.poly([(25,50),(30,49),(33,50),(31,52),(24,53),(23,52)],
           'slatebevel', TOP)
    a.poly([(24,53),(28,53),(30,52),(29,55),(25,55),(23,54)],
           'slate', LEFT)
    a.line([(24,54),(26,54)], 'slateshade', LEFT)
    a.poly([(33,50),(36,50),(39,53),(36,54),(31,54),(31,52)],
           'slate', RIGHT)
    a.line([(34,50),(36,51),(37,52)], 'slatebevel', TOP)
    a.poly([(30,54),(34,53),(36,54),(34,55),(29,55)], 'slateshade', BOTTOM)
    a.line([(31,53),(33,52)], 'slateedge', RIGHT)
    a.poly([(24,51),(26,50),(29,50),(28,51),(27,52),(24,52)], 'mossdark', TOP)
    a.line([(25,50),(27,50)], 'mosslight', TOP)
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
        'source_policy_zh': '参考已保存的HD引导构图原创逐像素重画，不采样参考像素；单像素木裂、铜罩折面与护线、错位板岩脚。仅独立六角玻璃遮罩发光。',
    },ensure_ascii=False,indent=2)+'\n')
    print('Wrote original pixel lamp, matched face normals and glass-only emission.')


if __name__ == '__main__':
    main()
