"""Use the accepted native PixelLab studies, not HD-2D originals.

Crops come from real alpha. Preserve the pixel grid; convert lime foliage to the
night-forest's base pigments, keep the player's skin, scarf and cloak distinct.
Positions below are foot pivots on the uncropped source; never infer them from
an asymmetric branch at the last occupied row.
"""
from pathlib import Path
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
FOOT = {'oak':(80,143), 'fir':(54,138), 'traveler':(18,46)}
FOLIAGE = '192937 234048 2c544f 3b6b5b 508466 6b9d77 8ab682 adc88d'
BARK = '232738 383541 51404c 6c554f 89705c a58b6d c4ac82'
HERO = ('182236 263147 304358 3d626c 4c8091 68a2aa 283d5c 395c85 '
        '493239 69504d 926e4f b79265 994840 c56248 e79059 '
        '382b38 614651 866579 ab8176 ce9f85 eac8a2 f4dbad')


def colors(ramp):
    return np.array([tuple(bytes.fromhex(h)) for h in ramp.split()],np.uint8)


def planes(kind):
    original = 'oak-full' if kind=='oak' else kind
    with Image.open(HERE/f'{original}-original.png') as im:
        rgba=np.array(im.convert('RGBA'))
    with Image.open(HERE/f'{kind}-normal-original.png') as im:
        shape=np.array(im.convert('RGBA'))
    if shape.shape != rgba.shape: raise ValueError(f'{kind}: 法线与色图不同尺寸')
    mask=rgba[:,:,3]>=128
    if kind == 'traveler':
        # The generator added a second lantern under the outstretched hand.
        # Keep the cuff and fingers; only the fixed wayside lamp should emit.
        mask[27:,27:] = False
    ys,xs=np.nonzero(mask); x0,x1=xs.min(),xs.max()+1; y0,y1=ys.min(),ys.max()+1
    rgb=rgba[:,:,:3].astype(float)
    if kind=='traveler':
        inks=colors(HERO)
        distances=np.sum((rgb[:,:,None,:]-inks[None,None,:,:])**2*np.array((.80,1.30,.76)),axis=3)
        # Keep the iris/facial contour and the two boots dark, not beige.
        rgba[:,:,:3]=inks[distances.argmin(axis=2)]
    else:
        yy=np.arange(mask.shape[0])[:,None]
        low=(yy-y0)/(y1-y0)
        wood=(low>(.61 if kind=='oak' else .77)) & (rgb[:,:,0] > rgb[:,:,1]*.82) & (rgb[:,:,2] < rgb[:,:,1]*.92)
        lum=rgb @ np.array((.25,.60,.15))
        for segments,ramp,breaks in [(mask & ~wood,FOLIAGE,[12,26,44,64,81,92,98]),
                                     (mask & wood,BARK,[12,29,47,68,85,96])]:
            rgba[segments,:3]=colors(ramp)[np.digitize(lum[segments],np.percentile(lum[segments],breaks))]
    rgba[:,:,3]=mask*255; rgba[~mask]=0
    # Turn continuous generated normals into a restrained set of leaf/cloth
    # planes; dark opacity from the original is never used as a height field.
    n=shape[:,:,:3].astype(float)/127.5-1
    n=np.rint(n*6)/6
    n/=np.maximum(np.linalg.norm(n,axis=2,keepdims=True),.001)
    shape[:,:,:3]=np.rint((n*.5+.5)*255).astype(np.uint8)
    shape[:,:,3]=rgba[:,:,3]; shape[~mask]=(128,128,255,0)
    anchor=FOOT[kind]
    return (Image.fromarray(rgba[y0:y1,x0:x1]), Image.fromarray(shape[y0:y1,x0:x1]),
            (int(anchor[0]-x0),int(anchor[1]-y0)))
