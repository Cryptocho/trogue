"""Convert the original generate_image study into a lit, native 61×29 prop.

uv run --project tools/gloam/terrain tools/gloam/paper/deadfall/prepare.py
Original received RGBA despite the requested key colour; use actual alpha.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import find_objects, label

HERE = Path(__file__).resolve().parent
OUT = HERE.parents[3] / 'assets/gloam/paper'
# Source paint swatch -> our unlit ink -> picture-space facet. The hollow and
# thin ink contours remain matte and dark; the terraced fungi have distinct lips.
PAINT = (
    ((39,14,66),   '353044', (-.05,-.10,1.)),
    ((162,92,80),  '866651', (-.17,.57,.80)),
    ((108,70,104), '655061', (-.17,.08,.98)),
    ((96,80,143),  '495366', (.10,-.49,.86)),
    ((183,111,75), '9f7f60', (-.49,.50,.72)),
    ((164,178,95), '799a6e', (-.30,.79,.52)),
    ((128,141,70), '567955', (.50,.35,.79)),
    ((248,192,101),'be9d73', (.60,.31,.74)),
    ((207,146,72), 'a37d59', (.66,-.16,.74)),
    ((179,72,23),  'aa6049', (-.20,.48,.85)),
    ((244,151,50), 'd99862', (.22,.75,.62)),
)


def normal(n):
    n = np.array(n)
    return tuple(np.rint((n/np.linalg.norm(n)*.5+.5)*255).astype(int)) + (255,)


def main():
    source = np.asarray(Image.open(HERE/'original.webp').convert('RGBA'))
    components, _ = label(source[:,:,3] >= 192)
    chosen = np.bincount(components.ravel())[1:].argmax()+1
    bounds = find_objects(components)[chosen-1]
    opaque = components[bounds] == chosen
    src = source[bounds][:,:,:3].astype(np.int32)
    distances = [((src-np.array(c))**2).sum(axis=2) for c,_,_ in PAINT]
    pigment = np.argmin(distances, axis=0)
    size = (61,29)
    def coverage(mask):
        return np.asarray(Image.fromarray(mask.astype(np.uint8)*255).resize(size, Image.Resampling.BOX))
    idx = np.argmax([coverage(opaque & (pigment == i)) for i in range(len(PAINT))],axis=0)
    solid = coverage(opaque) >= 125
    color = np.zeros((size[1],size[0],4), np.uint8)
    normals = np.zeros_like(color)
    for i,(_,c,n) in enumerate(PAINT):
        m = solid & (idx == i)
        color[m] = tuple(bytes.fromhex(c))+(255,)
        normals[m] = normal(n)
    art, data = map(Image.fromarray,(color,normals))
    # Draw into existing pigment only: cracks never cut holes in the silhouette.
    def cut(points, value, n, allowed):
        line = Image.new('L',size)
        ImageDraw.Draw(line).line(points,fill=255)
        cutmask = np.asarray(line).astype(bool) & solid & np.isin(idx,allowed)
        mask = Image.fromarray(cutmask.astype(np.uint8)*255)
        art.paste(tuple(bytes.fromhex(value))+(255,), (0,0,*size), mask)
        data.paste(normal(n), (0,0,*size), mask)
    cut([(5,13),(9,13),(12,12)], 'a1ab74', (-.26,.88,.39), (5,6))
    cut([(8,17),(13,18),(16,18)], 'a77e5f', (-.25,.72,.65), (1,4))
    cut([(21,16),(24,15),(29,15),(32,14)], 'a77e5f', (-.25,.72,.65), (1,4))
    cut([(29,17),(33,17),(34,16)], '705750', (.3,-.18,.93), (1,4))
    cut([(34,11),(38,11),(40,10)], 'a77e5f', (-.25,.72,.65), (1,4))
    # The near end has a recessed crescent and three ring fragments, not a
    # bright doughnut or a solid black disc. Leave half the hollow untouched.
    cut([(48,16),(49,14),(52,13),(54,13)], '493d47', (-.66,.2,.73), (0,))
    cut([(47,17),(47,20),(48,22),(50,23)], '544551', (-.56,-.22,.80), (0,))
    cut([(53,25),(56,24),(58,22)], 'ac8a62', (.68,-.15,.73), (7,8))
    cut([(46,13),(46,11),(49,10)], 'd2b388', (.4,.68,.61), (7,8))
    art.save(OUT/'deadfall.png')
    data.save(OUT/'deadfall_normal.png')
    print(f'{size[0]}×{size[1]}: {bounds}, {solid.sum()} opaque texels')


if __name__ == '__main__':
    main()
