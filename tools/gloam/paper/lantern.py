"""Draw the 2D counterpart of the old road lamp; no model or screenshot input.

uv run --project tools/gloam/terrain tools/gloam/paper/lantern.py
Colour is sRGB; the facet normals and glass-only emission mask are linear.
"""
from math import sqrt
from pathlib import Path
from random import Random

from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'assets/gloam/paper'
W, H = 96, 128
colour = Image.new('RGBA', (W, H))
normal = Image.new('RGBA', (W, H), (128, 128, 255, 0))
emission = Image.new('L', (W, H))
cd, nd, ed = map(ImageDraw.Draw, (colour, normal, emission))


def encoded(n):
    length = sqrt(sum(x*x for x in n))
    return tuple(round(127.5 * (1 + x / length)) for x in n) + (255,)


def polygon(points, c, n=(0, 0, 1), e=0):
    cd.polygon(points, c)
    nd.polygon(points, encoded(n))
    ed.polygon(points, e)


def line(points, c, n=(0, 0, 1), width=1):
    cd.line(points, c, width)
    nd.line(points, encoded(n), width)
    ed.line(points, 0, width)


def slate(x, y, scale, tint):
    def facet(points, bias, direction):
        polygon([(round(x+i*scale), round(y+j*scale)) for i, j in points],
                tuple(v+bias for v in tint), direction)
    facet([(-9,-1),(-4,-7),(6,-6),(10,0),(6,5),(-6,4)], -22, (0,-0.5,1))
    facet([(-9,-1),(-4,-7),(5,-6),(1,-2),(-5,0)], 1, (-0.47,0.86,0.4))
    facet([(-5,0),(1,-2),(5,-6),(9,-1),(3,2)], 7, (0.35,0.73,0.8))
    facet([(-8,0),(-5,1),(3,3),(6,1),(5,4),(-5,3)], -10, (0,-0.47,1))


slate(70, 116, 1.08, (87, 91, 77))
slate(62, 115, 0.86, (95, 90, 82))
# Split chestnut stake: subtle colour grain from the same original material,
# independently drawn silhouette and longitudinal plane normals.
post = [(64,117),(64,90),(66,61),(66,35),(64,13),(64,7),(66,8),(67,3),
        (69,5),(70,2),(72,6),(75,5),(74,23),(76,53),(75,90),(73,116),(70,120)]
polygon(post, (106, 81, 57))
wood = ImageOps.grayscale(Image.open(ROOT/'tools/gloam/waylight/timber-original.webp')).resize((35, 124), Image.Resampling.LANCZOS)
mask = Image.new('1', (W, H)); ImageDraw.Draw(mask).polygon(post, 1)
for y in range(2, 120):
    span = [x for x in range(63, 78) if mask.getpixel((x, y))]
    for j, x in enumerate(span):
        u = j / max(len(span)-1, 1)
        grain = (wood.getpixel((round(u*34), y)) - 107) * 0.30
        split = -13 if j == 0 or j == len(span)-1 else 0
        colour.putpixel((x,y), tuple(round(v + grain + split) for v in (115, 88, 60)) + (255,))
        v = 2*u-1
        normal.putpixel((x,y), encoded((v, 0.10, max(0.22, (1-v*v)**0.5))))
line([(67,11),(69,21),(68,28)], (64,48,36), (-0.7,0,0.6))
line([(69,50),(71,63),(70,78),(69,83)], (70,55,41), (-0.72,0,0.65))
line([(67,98),(67,109),(69,114)], (75,57,38), (-0.45,0,0.85))
polygon([(65,8),(67,4),(69,6),(70,3),(73,7),(72,10),(68,11)], (143,114,78), (0.1,1,0.35))
# Backplate, upward-swept bracket and its triangular brace.
polygon([(62,14),(69,13),(70,32),(64,34),(62,30)], (65,59,49), (-0.34,0,1))
line([(66,31),(58,30),(49,22),(42,14)], (43,46,45), (-0.2,0.3,1), 2)
arm = [(68,20),(61,22),(54,21),(48,17),(42,12),(37,10),(32,10),(28,12),(27,15),(29,17),(31,15)]
line(arm, (53,57,54), (0,0.4,1), 3)
line([(61,21),(54,20),(46,15),(40,11),(35,10),(31,11)], (111,104,79), (0,0.82,0.57))
for yy in (14, 18, 29):
    line([(64,yy),(69,yy-1)], (151,129,85), (0,0.6,1))
for yy in (17,20):
    line([(29,yy-1),(27,yy),(28,yy+2),(30,yy+1)], (88,82,65), (-0.5,0.5,0.6))
# Rain hood and six-sided frame. Only the recessed glass emits; spars, base,
# copper rivets, iron guards, stake and surrounding pixels never emit.
polygon([(15,29),(19,24),(25,21),(27,19),(31,19),(33,22),(40,24),(44,28),(43,31),(30,35),(17,32)], (54,65,60))
polygon([(15,28),(20,25),(26,22),(29,21),(25,28),(19,30)], (92,112,96), (-0.60,0.74,0.4))
polygon([(25,28),(29,21),(33,22),(38,26),(40,29),(31,31)], (84,102,91), (0.27,0.65,0.66))
line([(16,30),(25,32),(31,33),(42,30)], (141,116,74), (0,0.2,1), 2)
polygon([(18,31),(26,34),(27,51),(20,47)], (129,106,74), (-0.65,0,0.75), 80)
polygon([(26,34),(34,33),(36,47),(30,52),(26,50)], (157,137,92), (-0.05,0,1), 221)
polygon([(34,33),(41,31),(39,46),(35,49)], (144,119,75), (0.63,0,0.77), 135)
# An oil wick inside frosted panes: narrow hot centre, softer shoulders.
for yy in range(36, 48):
    r = 1 if yy < 40 or yy > 45 else 2
    ed.line((30-r,yy,30+r,yy), 252)
for pts in [[(18,31),(20,46),(26,51)],[(25,33),(26,50)],[(34,33),(35,48)],[(41,31),(39,45),(35,49)]]:
    line(pts, (87,76,53), (0.4,0,1))
line([(20,37),(23,43),(25,46)], (111,88,57), (-0.5,0.2,1))
line([(27,48),(31,45),(34,38)], (115,91,62), (0.5,0,1))
polygon([(19,46),(25,49),(30,50),(36,48),(40,45),(39,50),(32,54),(25,54),(20,51)], (76,67,53), (0,-0.63,0.8))
line([(20,47),(26,50),(30,51),(36,49),(40,46)], (151,115,73), (0,0.63,0.76))
polygon([(25,54),(33,54),(31,57),(27,57)], (73,86,77), (0,-0.42,0.90))
# Weather-worn binding and uneven pilgrim's ribbon, not another light source.
for yy in (26,28,30):
    line([(64,yy-1),(67,yy),(73,yy-1),(75,yy-2)], (148,118,77), (0,0.45,0.8))
polygon([(63,28),(68,28),(68,34),(66,40),(64,45),(64,39),(62,41),(64,33)], (114,115,94), (-0.4,0.2,0.9))
line([(65,30),(66,35),(64,41)], (159,147,110), (0.32,0.3,0.8))
polygon([(63,104),(75,103),(74,107),(64,109)], (66,64,51), (0,0.4,1))
slate(59, 119, 0.92, (106,104,86))
slate(76, 119, 0.84, (95,96,81))
slate(67, 123, 0.65, (104,99,86))
# Lichen flecks sit on the stone tops, not outside the silhouette.
rng = Random(841701)
for _ in range(30):
    x, y = rng.randrange(52,83), rng.randrange(115,124)
    if colour.getpixel((x,y))[3] and normal.getpixel((x,y))[1] > 180:
        colour.putpixel((x,y), rng.choice([(101,105,70,255),(89,98,72,255),(108,112,84,255)]))

OUT.mkdir(exist_ok=True, parents=True)
for suffix, image in [('',colour), ('_normal',normal), ('_emission',emission)]:
    image.save(OUT/f'lantern{suffix}.png')
