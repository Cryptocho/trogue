"""Original, hand-placed 16-bit cast for 苔径留灯's pure-2D route.

Run with: uv run --project tools/gloam/terrain python tools/gloam/paper/cast_art.py
The mid-distance cast is defined by native pixel polygons. The traveler and
two close canopy variants use the separately saved PixelLab drawings and
re-inking in canopy/. No photograph, game screenshot or HD-2D sprite input.
Picture +Y normals point up, +Z toward the viewer.
"""
from pathlib import Path
from math import sqrt
import json
from PIL import Image, ImageDraw
from canopy.paint import planes as source_planes

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'assets/gloam/paper/cast'
P = {
    'ink': '#192236', 'deep': '#22434b', 'leafshade': '#326157',
    'leaf': '#49876a', 'leaflight': '#71a77b', 'tip': '#a4c58b',
    'stoneback': '#39485e', 'stone': '#566984', 'stoneface': '#8498a4',
    'stonetop': '#b9c5b4', 'barkdark': '#393541', 'bark': '#534356',
    'wood': '#756052', 'woodlight': '#998165', 'woodtop': '#b99b71',
    'warmdark': '#974f4a', 'warm': '#cd7450', 'amber': '#e8a25e',
    'cream': '#ffe7aa', 'blueback': '#2c405b', 'blue': '#426b85',
    'bluelight': '#6b9bac', 'skinshade': '#bb8879', 'skin': '#e4bd98',
}
FRONT = (0., .06, 1.)
TOP = (0., .85, .53)
LEFT = (-.78, .08, .62)
RIGHT = (.78, .08, .62)
BOTTOM = (0., -.62, .78)


def rgba(c):
    if c in P:
        c = P[c]
    return tuple(bytes.fromhex(c.lstrip('#'))) + (255,)


def encoded(n):
    m = sqrt(sum(v*v for v in n))
    return tuple(round(127.5 * (1. + v/m)) for v in n) + (255,)


class Art:
    def __init__(self, w, h):
        self.color = Image.new('RGBA', (w, h))
        self.normal = Image.new('RGBA', (w, h), (128, 128, 255, 0))
        self.emission = Image.new('L', (w, h))
        self.cd, self.nd, self.ed = map(ImageDraw.Draw, (self.color, self.normal, self.emission))

    def poly(self, pts, c, n=FRONT, e=0):
        self.cd.polygon(pts, rgba(c))
        self.nd.polygon(pts, encoded(n))
        self.ed.polygon(pts, e)

    def line(self, pts, c, n=FRONT, width=1, e=0):
        self.cd.line(pts, rgba(c), width)
        self.nd.line(pts, encoded(n), width)
        self.ed.line(pts, e, width)

    def rect(self, box, c, n=FRONT, e=0):
        self.cd.rectangle(box, rgba(c))
        self.nd.rectangle(box, encoded(n))
        self.ed.rectangle(box, e)

    def save(self, name, anchor, notes):
        OUT.mkdir(parents=True, exist_ok=True)
        for suffix, image in [('', self.color), ('_normal', self.normal)]:
            image.save(OUT / f'{name}{suffix}.png')
        w, h = self.color.size
        return {
            'name': name, 'image': f'gloam/paper/cast/{name}.png',
            'normal': f'gloam/paper/cast/{name}_normal.png',
            'emission': None,
            'size_px': [w, h], 'ground_anchor_px': list(anchor),
            'ground_anchor_uv': [anchor[0]/w, anchor[1]/h],
            'source': 'tools/gloam/paper/cast_art.py', 'notes_zh': notes,
        }


def studio_sprite(kind):
    color, data, anchor = source_planes(kind)
    art = Art(*color.size)
    art.color, art.normal = color, data
    return art, anchor


def rock():
    a = Art(36, 25)
    a.poly([(2,12),(6,6),(17,1),(26,3),(31,9),(34,13),(33,20),(25,23),(10,23),(3,20),(1,16)], 'ink')
    a.poly([(3,12),(8,8),(15,9),(14,18),(9,21),(3,18)], 'stoneback', LEFT)
    a.poly([(8,7),(17,2),(25,4),(28,8),(22,11),(15,10)], 'stonetop', TOP)
    a.poly([(15,11),(22,12),(27,9),(31,12),(32,18),(26,21),(16,21),(14,17)], 'stoneface', RIGHT)
    a.poly([(5,19),(12,20),(17,19),(17,22),(10,22)], 'stone', BOTTOM)
    a.poly([(18,12),(21,13),(23,17),(21,19),(17,19)], 'stone', FRONT)
    a.line([(23,12),(26,13),(28,13)], 'stonetop', TOP)
    a.poly([(7,7),(13,4),(18,3),(19,5),(17,6),(13,6),(11,9),(7,9)], 'leafshade', TOP)
    a.poly([(9,6),(12,5),(15,5),(14,6),(11,7),(9,8)], 'leaflight', TOP)
    a.poly([(24,19),(28,18),(31,18),(30,20),(26,21),(23,21)], 'deep', BOTTOM)
    return a, (18, 23)


def waystone():
    a = Art(39, 47)
    # Irregular cap, slanting break, broad face and a narrow opposing facet.
    a.poly([(8,10),(9,5),(17,1),(27,3),(31,8),(29,18),(31,30),(34,41),(31,45),(9,45),(5,41),(7,30),(6,20)], 'ink')
    a.poly([(10,7),(17,3),(25,4),(28,8),(21,12),(12,11),(8,14)], 'stonetop', TOP)
    a.poly([(8,15),(13,12),(18,14),(17,27),(14,32),(15,42),(10,43),(7,40),(9,30)], 'stoneback', LEFT)
    a.poly([(18,13),(23,11),(28,10),(26,20),(28,31),(31,40),(27,43),(17,42),(16,33),(19,27)], 'stoneface', RIGHT)
    a.poly([(16,14),(18,14),(16,23),(13,26),(13,18)], 'stone', LEFT)
    a.poly([(21,12),(26,10),(24,14),(23,19),(21,20)], 'stonetop', TOP)
    a.poly([(15,34),(21,30),(27,32),(29,39),(24,41),(18,40)], 'stone', FRONT)
    # An offset diagonal cut is a real normal face, not a grey line overlay.
    a.poly([(9,29),(15,27),(20,23),(27,22),(27,24),(20,26),(14,30),(9,32)], 'stoneback', BOTTOM)
    a.line([(10,29),(15,27),(20,23),(26,22)], 'stonetop', TOP)
    # One old waymark, deliberately legible at 44 logical pixels high.
    a.line([(22,16),(22,20),(19,22)], 'stoneback', LEFT)
    a.line([(19,17),(25,17)], 'stoneback', LEFT)
    a.line([(24,18),(24,20)], 'stone', RIGHT)
    a.line([(21,16),(21,19)], 'stonetop', TOP)
    # Moss is an intentional cap and two drooping tabs, not scattered speckle.
    a.poly([(8,9),(10,5),(16,2),(21,3),(24,4),(27,4),(29,7),(26,9),(21,9),(19,11),(14,10),(12,14),(9,13)], 'leafshade', TOP)
    a.poly([(10,7),(14,4),(17,4),(17,6),(21,6),(22,5),(25,6),(23,8),(18,8),(16,9),(12,9),(11,11),(10,11)], 'leaflight', TOP)
    a.line([(14,4),(16,3),(19,4)], 'tip', TOP)
    a.poly([(12,10),(15,10),(14,15),(12,16)], 'deep', LEFT)
    a.poly([(25,8),(28,8),(27,12),(25,13)], 'leaf', RIGHT)
    # Broad chips ground the obelisk; their planes keep the material readable.
    a.poly([(6,40),(12,41),(15,44),(12,46),(5,45),(3,43)], 'stoneback', LEFT)
    a.poly([(27,42),(32,40),(36,43),(35,45),(29,46),(25,44)], 'stone', RIGHT)
    a.line([(28,42),(32,41),(34,43)], 'stonetop', TOP)
    a.poly([(8,42),(11,42),(12,43),(9,44),(6,43)], 'leafshade', TOP)
    return a, (20, 45)


def bough(a, points, width=6):
    a.line(points, 'ink', width=width+2)
    a.line(points, 'bark', width=width)
    a.line([(x-1,y-1) for x,y in points], 'wood', LEFT, max(1,width//3))


def foliage(a, x, y, w, h, tilt=0, pale=False, variant=0, rim='deep', shaded=False):
    """Three explicitly drawn leaf masses: lobed bean, shelf and drooping fork.

    Separate silhouettes and highlight islands prevent a stamp-like crown.
    All edges and internal veins are integer stepped runs, never leaf noise.
    """
    def p(points):
        return [(x+round(u*w/32)+round(v*tilt/24), y+round(v*h/24)) for u,v in points]
    if variant == 1:
        edge = [(0,13),(3,12),(2,9),(6,8),(6,5),(10,5),(12,2),(16,3),
                (18,1),(23,2),(23,5),(27,5),(27,7),(30,8),(29,11),
                (32,13),(31,16),(28,16),(28,19),(24,19),(22,22),
                (18,21),(15,23),(10,22),(9,20),(5,20),(5,18),(1,18)]
        shade = [(2,13),(6,10),(8,7),(13,5),(19,4),(24,6),(27,8),
                 (28,12),(30,14),(26,17),(23,18),(20,20),(15,19),
                 (11,21),(8,18),(4,17)]
        upper = [(6,10),(9,7),(13,6),(14,4),(18,5),(19,3),(22,4),
                 (21,7),(26,8),(25,11),(28,12),(24,14),(19,13),
                 (16,15),(12,13),(9,15),(5,14)]
        light = [(8,10),(11,8),(14,8),(15,6),(17,7),(16,9),
                 (20,9),(22,10),(21,12),(17,11),(14,12),(11,11),(8,12)]
        side = [(24,13),(29,13),(28,15),(26,16),(26,18),(23,18),
                (21,20),(17,20),(16,18),(20,17),(21,15)]
    elif variant == 2:
        edge = [(1,8),(6,7),(6,4),(11,4),(13,1),(19,2),(20,5),
                (25,4),(28,7),(27,10),(31,11),(32,15),(29,16),
                (30,19),(26,21),(22,21),(20,24),(15,22),(12,23),
                (9,20),(5,20),(5,17),(1,16),(3,13),(0,12)]
        shade = [(3,9),(8,7),(12,5),(15,3),(18,4),(20,7),(24,6),
                 (26,8),(25,12),(29,13),(29,16),(26,17),(26,19),
                 (21,20),(19,22),(15,20),(12,21),(10,18),(6,18),(5,15),(3,14)]
        upper = [(4,9),(9,8),(10,6),(14,6),(15,4),(17,5),(18,8),
                 (23,8),(24,10),(21,12),(25,13),(24,16),(20,15),
                 (18,17),(15,15),(12,16),(9,14),(5,14),(6,12)]
        light = [(6,10),(10,9),(12,7),(14,8),(13,10),(16,9),(18,10),
                 (17,12),(13,12),(11,13),(8,12),(6,13)]
        side = [(26,12),(29,14),(27,16),(27,18),(23,19),(21,21),
                (18,21),(16,19),(19,18),(20,16),(24,17)]
    else:
        edge = [(1,11),(4,11),(3,7),(7,7),(7,4),(12,4),(14,1),(20,1),
                (20,3),(25,3),(25,6),(29,6),(29,10),(32,12),(30,15),
                (30,18),(26,18),(25,21),(20,21),(18,23),(13,22),(12,20),
                (7,20),(7,18),(3,18),(4,15),(1,15)]
        shade = [(2,11),(6,9),(8,6),(14,4),(21,4),(25,7),(29,8),(29,13),
                 (25,17),(19,18),(14,20),(9,18),(5,18),(5,14),(2,14)]
        upper = [(7,8),(13,5),(17,3),(20,4),(20,6),(24,6),(24,8),(28,9),
                 (27,12),(23,13),(21,16),(16,15),(14,17),(10,16),(9,13),(6,12)]
        light = [(9,8),(13,6),(16,6),(16,5),(19,5),(19,7),(16,8),
                 (15,10),(12,9),(10,11),(8,10)]
        side = [(23,12),(28,11),(30,13),(28,16),(25,17),(24,19),(20,20),
                (18,21),(15,20),(16,18),(20,17),(20,15)]
    a.poly(p(edge), rim, BOTTOM)
    a.poly(p(shade), 'leafshade', LEFT)
    a.poly(p(upper), 'leafshade' if shaded else ('leaflight' if pale else 'leaf'), TOP)
    a.poly(p(side), 'leafshade' if pale else 'deep', RIGHT)
    a.poly(p(light), 'leaf' if shaded else ('tip' if pale else 'leaflight'), TOP)
    a.line(p([(18,10),(21,9),(23,10)]), 'leaf' if shaded else 'leaflight', TOP)
    a.line(p([(9,14),(11,13),(14,14)]), 'leaflight' if pale else 'leaf', FRONT)
    a.line(p([(20,18),(22,17),(24,17)]), 'leaf', RIGHT)


def oak():
    a = Art(150, 164)
    # Roots and fork first: openings between crowns reveal structural branches.
    a.poly([(68,96),(80,91),(85,108),(84,133),(90,149),(101,156),(99,159),
            (84,157),(79,160),(70,158),(55,161),(48,158),(60,151),(65,135)], 'ink')
    a.poly([(70,100),(76,97),(77,127),(74,143),(67,153),(57,157),(65,156),(73,154),(76,156),(80,154),(79,141),(80,120)], 'wood', LEFT)
    a.poly([(78,104),(82,108),(81,134),(86,151),(95,156),(86,154),(82,155),(77,144),(77,126)], 'barkdark', RIGHT)
    a.line([(71,113),(69,133),(71,141),(68,150)], 'woodlight', LEFT, 2)
    a.line([(75,116),(74,130),(75,138)], 'barkdark', RIGHT)
    bough(a, [(73,119),(57,101),(47,91),(38,78)], 7)
    bough(a, [(76,110),(89,96),(106,88),(117,75)], 7)
    bough(a, [(73,106),(68,85),(55,72),(48,58)], 6)
    bough(a, [(78,102),(89,75),(87,57)], 6)
    # One closed, asymmetrical crown, not a collection of outlined balls.
    # All local patches are painted into this continuous deep lower canopy.
    a.poly([(4,74),(9,67),(8,59),(16,54),(15,45),(24,40),(26,31),
            (37,28),(40,20),(52,18),(58,10),(68,9),(73,4),(87,6),
            (92,12),(104,13),(111,22),(122,25),(124,35),(133,39),
            (132,46),(141,52),(141,61),(147,67),(145,76),(149,82),
            (144,92),(146,100),(137,108),(127,108),(122,116),(110,116),
            (104,121),(91,119),(83,125),(72,120),(63,124),(54,119),
            (42,122),(35,117),(25,119),(21,112),(12,111),(13,103),
            (6,98),(9,90),(3,85)], 'ink', BOTTOM)
    a.poly([(7,74),(13,67),(12,59),(21,54),(20,46),(30,39),(35,31),
            (46,27),(52,19),(64,18),(73,9),(86,10),(94,18),(105,18),
            (111,28),(120,30),(123,41),(132,45),(137,55),(136,66),
            (143,73),(140,85),(144,91),(138,101),(127,103),(122,111),
            (110,110),(103,116),(91,113),(82,120),(73,115),(63,119),
            (55,114),(44,117),(36,112),(26,114),(23,106),(15,107),
            (16,99),(10,96),(12,85),(7,83)], 'deep', BOTTOM)
    # Large interlocking leaf planes have no black perimeter. Only the crown's
    # outside contour is ink; shorter painted fringes break the internal seams.
    for i, spec in enumerate([(23,34,64,36,-1),(68,19,57,39,0),
                             (4,56,67,42,-1),(87,47,59,45,0),
                             (38,49,67,47,1),(15,84,63,33,-1),
                             (69,78,72,37,0),(37,91,60,30,0),
                             (30,17,53,36,-1),(59,5,51,31,1)]):
        foliage(a, *spec, variant=[1,2,2,1,0,1,2,1,2,0][i], shaded=i in (5,6,7))
    # Overpaint seams with small, unequal leaf tongues and interleaved shelves.
    for pts in [
        [(23,69),(31,65),(34,66),(32,69),(37,69),(35,72),(29,71),(26,74),(21,73)],
        [(57,37),(62,34),(65,35),(64,38),(70,38),(69,41),(62,40),(59,43),(54,42)],
        [(76,67),(82,63),(87,65),(85,68),(90,68),(88,72),(82,70),(78,73),(73,72)],
        [(98,44),(104,41),(107,42),(106,45),(110,45),(108,48),(102,47),(100,49)],
        [(38,99),(45,96),(49,97),(48,99),(53,100),(51,103),(44,102),(39,105),(35,103)],
        [(106,93),(112,89),(118,90),(117,93),(123,93),(120,96),(114,95),(110,98)],
    ]:
        a.poly(pts, 'leaf', FRONT)
    for pts in [
        [(26,49),(31,46),(36,47),(34,49),(39,49),(37,51),(31,50),(28,53)],
        [(52,65),(56,61),(60,62),(59,65),(64,64),(63,67),(57,67),(54,69)],
        [(90,28),(95,26),(99,27),(98,29),(101,30),(97,32),(92,31)],
        [(88,93),(94,90),(98,91),(96,94),(99,95),(94,97),(89,96)],
    ]:
        a.poly(pts, 'leaflight', TOP)
    # Fork collar and roots sit in front of the lowest leaves.
    a.line([(68,121),(72,125),(75,123)], 'woodlight', TOP)
    a.poly([(55,156),(62,153),(68,154),(68,157),(61,158),(55,159)], 'leafshade', TOP)
    a.line([(58,156),(62,155),(65,156)], 'leaflight', TOP)
    a.poly([(81,153),(86,153),(90,155),(88,157),(83,156)], 'deep', TOP)
    return a, (75, 159)


def fir():
    a = Art(108, 168)
    a.poly([(48,116),(58,113),(60,146),(64,157),(72,163),(68,165),
            (56,162),(49,165),(39,164),(44,160),(48,147)], 'ink')
    a.poly([(50,123),(54,122),(54,147),(52,158),(46,162),(51,161),(55,160),(55,144)], 'wood', LEFT)
    a.poly([(56,125),(58,128),(57,150),(62,161),(57,160),(55,150)], 'bark', RIGHT)
    a.line([(51,137),(50,150),(48,156)], 'woodlight', LEFT)
    # One crooked leader connects every off-centre, hanging branch. Branch
    # ends alternate in height; no pair makes a symmetric triangular skirt.
    a.line([(52,7),(55,26),(51,45),(54,62),(50,85),(54,106),(52,140)], 'ink', width=6)
    a.line([(52,8),(54,26),(51,45),(53,62),(50,85),(53,106),(52,139)], 'bark', width=3)
    branches = [
        ([(53,104),(43,110),(29,114),(15,114),(13,118),(5,121),(2,127),(9,127),
          (7,132),(15,129),(18,134),(23,129),(27,135),(32,131),(36,137),(40,133),
          (44,138),(49,130),(54,127),(58,119)],
         [(51,108),(43,114),(30,118),(18,119),(11,124),(21,123),(19,128),
          (28,124),(27,130),(34,127),(36,131),(42,127),(45,131),(49,123)], LEFT),
        ([(53,98),(62,104),(78,105),(90,111),(103,112),(105,118),(99,117),(99,123),
          (94,121),(92,128),(86,124),(83,129),(77,124),(72,127),(68,121),
          (62,121),(57,115),(52,113)],
         [(56,104),(64,109),(77,110),(87,114),(97,115),(94,118),(89,117),
          (88,122),(82,119),(80,123),(74,119),(70,121),(65,115),(60,115)], RIGHT),
        ([(50,78),(43,86),(34,90),(20,91),(19,94),(10,99),(9,104),(16,103),
          (16,109),(22,105),(26,111),(31,106),(36,112),(39,105),(46,107),
          (48,100),(54,96),(55,87)],
         [(49,83),(42,90),(34,94),(23,95),(17,99),(23,99),(22,104),(29,100),
          (30,105),(36,100),(40,103),(42,97),(49,94)], LEFT),
        ([(54,72),(64,79),(74,81),(82,85),(94,85),(95,90),(89,91),(89,97),
          (83,94),(81,101),(76,96),(70,100),(67,94),(61,93),(57,87),(51,84)],
         [(56,77),(65,83),(74,85),(87,88),(83,91),(78,89),(79,94),(73,91),
          (71,95),(66,88),(61,88)], RIGHT),
        ([(51,55),(43,60),(35,63),(30,63),(28,67),(20,70),(19,76),(26,73),
          (26,80),(31,76),(35,83),(39,77),(45,80),(47,73),(54,71),(56,63)],
         [(50,59),(43,65),(35,66),(27,71),(32,71),(30,76),(36,72),
          (38,77),(41,71),(47,73),(49,67)], LEFT),
        ([(53,44),(60,49),(67,52),(76,52),(80,57),(85,58),(85,63),(79,61),
          (78,68),(72,64),(69,70),(64,64),(59,65),(57,59),(51,56)],
         [(55,49),(61,54),(68,55),(77,56),(79,59),(74,59),(75,63),(69,60),
          (67,65),(63,59),(58,59)], RIGHT),
        ([(52,31),(45,36),(37,37),(35,41),(28,45),(29,51),(35,48),
          (36,55),(41,50),(45,55),(47,49),(53,47),(57,41)],
         [(52,35),(45,40),(38,41),(33,46),(39,45),(39,49),(44,46),(47,48),(50,43)], LEFT),
        ([(54,24),(60,29),(67,30),(70,34),(76,36),(75,41),(69,39),
          (68,44),(63,40),(59,43),(56,37),(51,36)],
         [(55,28),(61,33),(67,33),(71,36),(65,35),(65,39),(61,36),(57,37)], RIGHT),
        ([(52,2),(55,8),(54,14),(59,18),(62,18),(64,23),(59,23),
          (59,28),(54,25),(51,29),(47,25),(42,26),(43,20),(47,16),(49,10)],
         [(52,6),(52,13),(49,18),(47,23),(51,21),(52,25),(54,21),(57,22),(54,17)], LEFT),
    ]
    for edge, face, normal in branches:
        a.poly(edge, 'ink', BOTTOM)
        a.poly(face, 'leafshade', normal)
    # Needle highlights follow the hanging sweep, not horizontal cone slices.
    for pts in [
        [(48,114),(43,118),(38,120),(34,126),(32,125),(35,120),(30,122),(27,127)],
        [(24,120),(20,123),(17,123),(14,126)],
        [(58,108),(63,112),(66,116),(69,115),(73,120),(75,120)],
        [(80,113),(84,116),(85,120)],
        [(47,90),(41,94),(37,100),(35,99),(37,95),(32,98),(29,102)],
        [(56,80),(61,84),(65,89),(67,87),(70,91)],
        [(48,64),(43,68),(39,73),(37,71),(39,68),(34,70)],
        [(56,51),(60,55),(63,59),(65,58),(68,62)],
        [(49,39),(44,43),(42,48)],
        [(55,30),(58,34),(61,36)],
        [(51,13),(49,19),(47,22)],
    ]:
        a.line(pts, 'leaf', TOP, width=2)
    for pts in [[(42,118),(38,122)],[(61,112),(64,116)],[(41,95),(38,99)],
                [(60,84),(63,87)],[(43,68),(40,71)],[(48,41),(45,44)],[(51,15),(49,20)]]:
        a.line(pts, 'leaflight', TOP)
    a.poly([(41,161),(46,159),(51,160),(50,163),(43,163)], 'leafshade', TOP)
    return a, (54, 165)


def snag():
    a = Art(81, 118)
    # Broken crown and elbows: broad bark planes survive the full-scene scale.
    a.poly([(34,36),(32,22),(29,15),(30,9),(35,12),(39,3),(43,7),(42,21),
            (46,34),(45,63),(49,86),(50,103),(58,111),(68,115),(63,117),
            (49,114),(42,116),(35,114),(22,117),(14,115),(27,109),(33,94)], 'ink')
    a.poly([(35,17),(38,10),(39,15),(38,29),(41,41),(39,67),(43,89),
            (41,105),(35,111),(25,114),(34,109),(37,94),(36,75)], 'wood', LEFT)
    a.poly([(40,29),(43,35),(42,64),(46,88),(47,106),(56,112),(48,111),
            (43,113),(41,105),(42,89),(38,68)], 'bark', RIGHT)
    a.line([(35,31),(37,44),(35,55),(37,68),(36,82)], 'woodlight', LEFT, 2)
    a.line([(42,75),(43,90),(42,97)], 'barkdark', RIGHT, 2)
    bough(a, [(37,65),(27,52),(22,39),(12,35),(5,24)], 5)
    bough(a, [(41,55),(52,47),(59,31),(69,22),(74,11)], 5)
    bough(a, [(55,44),(68,42),(75,35)], 3)
    bough(a, [(24,44),(13,46),(8,43)], 3)
    # Splinter tops and a hollow, with a discrete bevel around it.
    a.poly([(30,9),(35,13),(38,7),(40,7),(37,15),(34,18)], 'woodtop', TOP)
    a.poly([(35,77),(38,73),(40,78),(40,85),(37,87),(35,84)], 'ink', LEFT)
    a.line([(35,78),(34,82),(35,86),(38,88)], 'woodlight', LEFT)
    a.line([(58,32),(62,28),(66,26)], 'woodlight', TOP)
    a.poly([(24,111),(31,108),(36,109),(36,112),(31,113),(27,115),(22,115)], 'leafshade', TOP)
    a.line([(27,111),(31,110),(33,111)], 'leaflight', TOP)
    a.poly([(43,89),(46,91),(47,102),(44,105)], 'deep', RIGHT)
    return a, (40, 115)


def birch():
    a = Art(95, 150)
    # Two unequal pale trunks bend apart, then grow back toward the canopy.
    a.poly([(38,69),(44,54),(48,56),(47,85),(45,108),(48,136),(56,145),
            (51,148),(43,145),(35,148),(26,147),(34,140),(37,118)], 'ink')
    a.poly([(40,75),(44,62),(44,89),(41,113),(42,137),(38,143),(31,146),
            (37,140),(39,119)], 'stonetop', LEFT)
    a.poly([(44,85),(46,78),(44,110),(46,137),(51,145),(46,143),(42,137),(41,112)], 'stoneface', RIGHT)
    bough(a, [(41,104),(30,85),(26,64),(17,51)], 4)
    bough(a, [(44,89),(57,72),(60,49),(72,31)], 4)
    a.line([(40,102),(31,84),(28,65),(19,52)], 'stonetop', LEFT, 3)
    a.line([(45,87),(56,71),(59,49),(70,33),(62,21),(55,11)], 'stonetop', LEFT, 3)
    a.line([(43,77),(38,65),(37,48),(43,29)], 'stoneface', RIGHT, 3)
    a.line([(29,83),(15,75),(12,65)], 'ink', width=3)
    a.line([(29,82),(16,74),(13,65)], 'stonetop', LEFT)
    a.line([(58,64),(73,57),(82,43)], 'ink', width=3)
    a.line([(58,63),(72,56),(81,43)], 'stoneface', RIGHT)
    a.line([(38,57),(24,42),(26,32)], 'ink', width=3)
    a.line([(38,56),(25,42),(27,32)], 'stonetop', LEFT)
    a.line([(64,35),(53,24),(52,13)], 'ink', width=3)
    a.line([(64,34),(54,24),(53,13)], 'stonetop', LEFT)
    a.line([(54,77),(65,82),(77,73)], 'ink', width=3)
    a.line([(54,76),(65,81),(77,72)], 'stoneface', RIGHT)
    a.line([(40,96),(43,95)], 'barkdark', FRONT, 2)
    a.line([(40,111),(43,110)], 'barkdark', FRONT)
    a.line([(41,124),(44,125)], 'stoneback', RIGHT, 2)
    a.line([(38,136),(41,135)], 'barkdark', FRONT)
    a.line([(30,77),(33,77)], 'barkdark', FRONT)
    a.line([(58,59),(61,58)], 'barkdark', FRONT)
    for i, spec in enumerate([(9,43,31,22,-1),(60,31,33,23,0),
                             (29,16,30,25,0),(0,59,30,22,0),
                             (60,61,32,24,0),(26,61,28,22,1),
                             (41,0,28,24,-1),(12,22,29,21,0)]):
        foliage(a, *spec, pale=True, variant=[1,2,0,2,1,2,0,1][i])
    a.poly([(29,145),(34,142),(39,143),(38,146),(32,147)], 'leafshade', TOP)
    a.line([(32,144),(35,143),(37,144)], 'leaflight', TOP)
    return a, (42, 147)


def contact_sheet(arts):
    # Every visible pixel can be reviewed at native size and at exact integer 3x.
    sheet = Image.new('RGB', (640, 420), '#293449')
    d = ImageDraw.Draw(sheet)
    positions = [(12,26),(88,26),(250,26),(372,26),(470,26),(12,237),(90,237),(225,237),(420,237)]
    for (name, art, anchor), (x,y) in zip(arts, positions):
        sheet.paste(art.color, (x,y), art.color)
        d.text((x,y-16), name, fill='#c9d5c7')
        d.line([(x+anchor[0]-3,y+anchor[1]),(x+anchor[0]+3,y+anchor[1])], fill='#e8a25e')
    d.text((12,401), 'Native pixels | orange tick: ground anchor | source art only, not the rendered scene', fill='#a4c58b')
    sheet.save(OUT / 'contact_native.png')
    sheet.resize((1920,1260), Image.Resampling.NEAREST).save(OUT / 'contact_3x.png')
    normals = Image.new('RGB', (640,420), '#293449')
    for (_, art, _), (x,y) in zip(arts,positions):
        normals.paste(art.normal, (x,y), art.normal)
    normals.save(OUT / 'normals_native.png')


def stone_lighting(art):
    # Asset-side inspection only. Runtime lighting and white-key proof are owned
    # by the scene renderer. This image exposes our opposing discrete normals.
    import numpy as np
    c = np.asarray(art.color).astype(float)/255
    n = np.asarray(art.normal).astype(float)[...,:3]/127.5-1
    n /= np.maximum(1e-6,np.linalg.norm(n,axis=2,keepdims=True))
    panel = Image.new('RGB',(39*3,47),'#293449')
    for i, light in enumerate([(-.85,.35,.40),(0.,.1,1.),(.85,.35,.40)]):
        v = np.array(light); v /= np.linalg.norm(v)
        brightness = .3+.8*np.maximum(0,(n*v).sum(axis=2))
        rgb = np.clip(c[...,:3]*brightness[...,None],0,1)
        image = Image.fromarray(np.uint8(rgb*255)).convert('RGBA')
        image.putalpha(art.color.getchannel('A'))
        panel.paste(image,(39*i,0),image)
    panel.resize((702,282),Image.Resampling.NEAREST).save(OUT/'waystone_planes_6x.png')


def main():
    makers = [('traveler',lambda: studio_sprite('traveler')),('oak',oak),('fir',fir),('snag',snag),
              ('rock',rock),('waystone',waystone),('birch',birch)]
    notes = {
        'traveler': '原创 PixelLab 单帧，保留右向脸和围巾；去掉手中误生成的第二盏灯，用独立法线受光。',
        'oak': '单片不对称闭合冠影、无逐簇黑边；交错大小叶面、深色下沿、相连树叉。',
        'fir': '交错下垂的针枝、不对称三叉边；中干相连、离散宽叶面法线。',
        'snag': '断冠枯木；两侧粗枝、树洞、宽树皮面、少量根苔。',
        'rock': '低矮错位石片；苔帽、左右分离切面，允许正确法线镜像。',
        'waystone': '界石39×47；锚点(20,45)；苔帽、斜断层、旧路符和斜切面。',
        'birch': '不等长双枝、浅色断纹树干、小簇淡叶，不复用橡树轮廓。',
    }
    entries, arts = [], []
    for name, maker in makers:
        art, anchor = maker()
        entries.append(art.save(name, anchor, notes[name]))
        arts.append((name, art, anchor))
    near = []
    for kind in ('oak', 'fir'):
        art, anchor = studio_sprite(kind)
        name = f'near_{kind}'
        near.append(art.save(name, anchor, '前、中景原生 PixelLab 树冠，按本场景重新配墨色与离散法线。'))
        arts.append((name, art, anchor))
    manifest = {
        'version': 2, 'source_policy_zh': '原创整数像素多边形与本项目的 PixelLab 原生画稿；处理输入和任务单见 tools/gloam/paper/canopy。无照片或游戏截图输入。',
        'sampling': 'nearest, native integer pixels, binary alpha, no antialiasing',
        'normal_basis': '+X picture-right, +Y picture-up, +Z toward viewer; linear, flat planes',
        'palette_srgb': P, 'sprites': entries, 'near_trees': near,
    }
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    contact_sheet(arts)
    stone_lighting(next(art for name,art,_ in arts if name=='waystone'))
    print('Wrote seven actor types, two near canopies, matched normals and art previews.')


if __name__ == '__main__':
    main()
