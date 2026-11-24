"""Local native brush marks from two original PixelLab pixel swatches.

Use only the middle dirt strip: the full soil drawing also depicts side bushes.
Only the hummock and leaf marks from the green swatch transfer, not its hue,
baked highlights or periodic layout. The authored path, banks and pool stay put.
"""
from pathlib import Path
import numpy as np
from PIL import Image

SOURCE = Path(__file__).resolve().parent / 'woodland-inks'


def marks(name, crop):
    source = Image.open(SOURCE/f'{name}-original.png').convert('RGB').crop(crop)
    # Two source pixels become one native ground pixel: pebbles are smaller
    # than a boot. Quantize by cluster, not with random/error-diffusion noise.
    source = source.resize((source.width//2,source.height//2),Image.Resampling.BOX)
    luminosity = np.array(source) @ np.array([.25,.60,.15])
    return np.digitize(luminosity, np.percentile(luminosity,(13,33,68,87)))


def relief_floor(color, normal, colors):
    rng = np.random.default_rng(841779)
    base = np.array(color,dtype=np.int16)
    dst = base.copy()
    data = np.array(normal)
    height,width = base.shape[:2]
    def mask_for(names):
        mask = np.zeros((height,width), bool)
        for n in names:
            mask |= (base == colors(n)).all(axis=2)
        return mask
    grass = mask_for(('grass','turf_light','turf_shade','duff','duff_light'))
    dirt = mask_for(('soil','soil_worn'))
    for tile, terrain, change, stride in [
        (marks('turf',(0,0,96,80)), grass, [(-11,-21,-13),(-7,-13,-8),(0,0,0),(12,18,8),(26,31,15)], (28,17)),
        (marks('soil',(30,0,66,80)), dirt, [(-21,-17,-11),(-11,-9,-6),(0,0,0),(12,9,5),(22,19,12)], (20,16)),
    ]:
        th,tw=tile.shape
        delta=np.array(change)
        # Stamp small patches with variable seams and quiet ground between.
        for row in range(-7,height+12,stride[1]):
            for col in range(-10,width+18,stride[0]):
                cx=col+int(rng.integers(-8,9)); cy=row+int(rng.integers(-5,6))
                rx=int(rng.integers(12,27)); ry=int(rng.integers(8,18))
                x0=max(0,cx-rx); x1=min(width,cx+rx+1); y0=max(0,cy-ry); y1=min(height,cy+ry+1)
                if x1<=x0 or y1<=y0: continue
                area=np.s_[y0:y1,x0:x1]; yy,xx=np.ogrid[y0-cy:y1-cy,x0-cx:x1-cx]
                if not np.any(terrain[area]): continue
                mirror=rng.choice((-1,1)); sx=int(rng.integers(tw)); sy=int(rng.integers(th))
                brush=tile[(yy+sy)%th,(xx*mirror+sx)%tw]
                shape=(xx/rx)**2 + (yy/ry)**2 + .13*np.sin(xx*.41+yy*.16) < 1.
                # Let long patches of turf breathe. Worn earth at the traveler's
                # boots gets fewer secondary marks than the leaf-strewn verge.
                spacing = np.sin(cx*.037 + cy*.016) + .72*np.cos(cy*.048 - cx*.021)
                quiet = spacing > .48 or (terrain is dirt and ((cx-614)/36)**2+((cy-547)/27)**2 < 1)
                put = terrain[area] & shape & (brush!=2)
                if quiet:
                    put &= (brush == 0) | (brush == 4)
                dst[area][put] = base[area][put] + (delta[brush[put]] * (.63 if quiet else 1.)).astype(np.int16)
                # Low material relief: chipped areas follow an oblique facet,
                # not a bevel around every isolated pixel. Water is excluded.
                lit = put & (brush == 4); dark = put & (brush == 0)
                data[area][lit] = (102,250,107); data[area][dark] = (150,249,151)
    # Little bent blades overlap the turf in scattered families. One-pixel
    # taps are used for an occasional tip, not as a full-frame grain texture.
    blades = (((0,0),(1,0),(2,-1),(3,-2)), ((0,0),(1,0),(1,-1),(2,-2)),
              ((0,0),(1,-1),(2,-1),(3,-1)), ((0,0),(0,-1),(1,-2)))
    for _ in range(width*height//110):
        cx=int(rng.integers(8,width-8)); cy=int(rng.integers(5,height-4)); flip=rng.choice((-1,1))
        if not grass[cy,cx]: continue
        for _ in range(int(rng.integers(2,5))):
            x=cx+int(rng.integers(-4,5)); y=cy+int(rng.integers(-2,3))
            points=blades[int(rng.integers(len(blades)))]; lift=rng.choice((13,17,24))
            for j,(dx,dy) in enumerate(points):
                u=x+dx*flip; v=y+dy
                if not grass[v,u]: continue
                shade=(lift//2,lift,lift//2) if j else (-8,-10,-6)
                dst[v,u]=base[v,u]+shade
                data[v,u]=(112 if flip>0 else 146,245,88)
    return Image.fromarray(np.clip(dst,0,255).astype(np.uint8)), Image.fromarray(data)
