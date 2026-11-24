"""Build the river surface and its data plate; never sample a scenic screenshot.

uv run --project tools/gloam/terrain tools/gloam/riverside/bank/prepare.py
The material kit is a generate_image result conditioned on the selected study.
R of the data plate is the discrete water domain used by every reflection;
G is submerged light, B a signed ripple (128=still), A a broken meniscus.
Dry A instead identifies exposed bank face. All maps are sampled nearest.
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt

HERE = Path(__file__).resolve().parent
OUT = HERE.parents[3] / 'assets/gloam/riverside'
LAYOUT = json.loads((OUT / 'layout.json').read_text())
W, H = LAYOUT['map_size_px']
DX, DY = (W-512)//2, (H-288)//2
Y, X = np.mgrid[:H, :W].astype(np.float32)
X -= DX
Y -= DY


def swatches():
    with Image.open(HERE/'original.webp') as src:
        # The actual division is y=512. Exclude the seam in both samples.
        moss = src.crop((0,4,1536,508)).convert('RGB').resize((108,36),Image.Resampling.BOX)
        soil = src.crop((0,518,1536,1018)).convert('RGB').resize((132,42),Image.Resampling.BOX)
    # Remove JPEG-like micro-variation; retain the model's actual turf marks
    # and broad irregular soil/slate pieces, never copies of the scene itself.
    grass = np.array(moss,dtype=np.int16)[:,:,1]
    clay = np.array(soil,dtype=np.int16)
    return np.digitize(grass,[74,94]), np.digitize(clay[:,:,0],[68,77,87])


def distribute(patch, salt):
    # Mirror addressing has no hard seams. Swatch sizes are in art pixels.
    h,w=patch.shape
    u = (X + 6*np.sin(Y*.052+salt) + 4*np.cos(X*.036-Y*.03)).astype(int) % (2*w)
    v = (Y + 5*np.sin(X*.039+salt)).astype(int) % (2*h)
    return patch[np.minimum(v,2*h-1-v),np.minimum(u,2*w-1-u)]


def bankplate(offset):
    im=Image.new('L',(W,H)); d=ImageDraw.Draw(im)
    for points in LAYOUT['banks']:
        d.polygon([(x+DX,y+DY+offset) for x,y in points], fill=255)
    return np.asarray(im)>0


def main():
    top=bankplate(0)
    # A low cut lip (4 logical pixels), never a freestanding isometric wall.
    shore=top.copy()
    for dy in (1,2,3,4): shore |= bankplate(dy)
    face=shore & ~top; wet=~shore
    for actor in LAYOUT['actors']:
        x,y=actor['foot']; assert top[round(y)+DY,round(x)+DX], f"floating root: {actor}"
    dist=distance_transform_edt(wet)
    d_in=distance_transform_edt(top)
    pads, mud=swatches(); pads=distribute(pads,3); mud=distribute(mud,11)
    clear=np.array([[66,80,45],[76,91,52],[86,101,59]],np.int16)[pads]
    # Small regional hue shifts are shared by big clusters; the land is mostly
    # quiet paint. This is much less texture than the previous forest floor.
    patches=np.sin(X*.027+Y*.037)+.62*np.sin(Y*.069-X*.011)
    clear += (patches>.99)[:,:,None]*np.array([-3,1,-1])
    clear -= (patches<-.92)[:,:,None]*np.array([4,5,2])
    # A walkable break in the lawn, intentionally not a bright broad road.
    tread=154 + .09*(X-218)+6*np.sin((X-103)*.021)
    path=(np.abs(Y-tread)<(3.4+1.1*np.cos(X*.09))) & (X<235) & (X>-72)
    path &= ((np.sin(X*.09+Y*.06)+np.sin(Y*.19))>-.87)
    clear[path] = np.array([[65,66,46],[69,70,48],[73,72,49],[75,72,49]],np.int16)[mud[path]]
    rim=top & (d_in<1.7) & (mud>1)
    clear[rim]=np.array([77,72,52]) + (mud[rim,None]-2)*6
    clear[face]=np.array([[46,43,36],[67,59,46],[86,76,59],[101,88,71]],np.int16)[mud[face]]
    # The bed has a few broad, cross-current ink fields and short discrete
    # marks. Optical glow is BELOW the water only. Nothing here depicts a tree
    # or lamp, nor their reflections: those are live mirror draws in Bevy.
    depth=np.minimum(1.0,dist/26.)
    bands=np.sin(X*.037+Y*.059+.6*np.sin(Y*.076)) + .62*np.cos(Y*.107-X*.016)
    sky=np.array([[6,15,31],[7,16,32],[8,17,33],[9,19,35]],np.int16)[np.digitize(bands,[-.75,.35,1.12])]
    sky += np.rint((1-depth)[:,:,None]*np.array([3,4,4])).astype(np.int16)
    clear[wet]=sky[wet]
    strength=np.zeros((H,W),np.float32)
    for well in LAYOUT['waterlights']:
        cx,cy=well['center']; rx,ry=well['radii']
        # Elongated light caught by sub-surface shelves, not identical discs.
        sx=(X-cx+4.0*np.sin(Y*.065)+2.1*np.sin(Y*.19+X*.013))/rx
        sy=(Y-cy+.11*(X-cx)+2.8*np.sin(X*.07))/ry
        g=np.exp(-1.26*sx*sx-1.13*sy*sy)*well['level']
        strength=np.maximum(strength,g)
    strength *= np.clip(.94+.043*bands, .82,1.)
    strength[~wet]=0.
    field=np.zeros((H,W,4),np.uint8)
    field[:,:,0]=wet*255; field[:,:,1]=np.uint8(strength.clip(0,1)*255+.5); field[:,:,2]=128
    # This one data mark controls both loss of mirror contrast and a 1–2 px
    # lateral ripple at certain rows. Strokes are sparse even in the lit water.
    ripple=Image.new('L',(W,H),128); brush=ImageDraw.Draw(ripple); rng=np.random.default_rng(16513)
    for _ in range(2700):
        px=int(rng.integers(0,W-8)); py=int(rng.integers(0,H)); length=int(rng.integers(2,9))
        if not wet[py,px] or dist[py,px]<2: continue
        if strength[py,px]<.18 and rng.random()>.23: continue
        brush.line((px,py,px+length,py),fill=int(rng.choice((52,81,171,202,238))))
    # Distinct wavelets over the focal glass: short, still, never screen-locked.
    for x,y,n,c in ((237,223,9,207),(244,232,8,49),(240,240,7,180),(392,105,8,192)):
        brush.line((x+DX,y+DY,x+DX+n,y+DY),fill=c)
    field[:,:,2]=np.where(wet,np.asarray(ripple),128)
    nick=(np.sin(X*.19+Y*.08)+.62*np.sin(Y*.27-X*.14))>-.47
    glints=wet & (dist<=1.45) & nick
    field[:,:,3]=np.where(wet,glints*255,face*255)
    Image.fromarray(np.uint8(np.clip(clear,0,255))).save(OUT/'bank-color.png')
    Image.fromarray(field).save(OUT/'bank-field.png')
    moss_color=np.array([[66,80,45],[76,91,52],[86,101,59]],np.uint8)
    kit=Image.new('RGB',(132,80),(25,28,29));kit.paste(Image.fromarray(moss_color[swatches()[0]]),(0,0))
    kit.paste(Image.fromarray(np.uint8(np.array([[39,39,35],[48,45,37],[61,55,43],[69,61,48]])[swatches()[1]])),(0,38))
    kit.resize((528,320),Image.Resampling.NEAREST).save(HERE/'swatches-review.png')
    main_view=wet[DY:DY+288,DX:DX+512]
    print(f'{W}×{H}, normal frame water: {main_view.mean():.1%}; {len(LAYOUT["actors"])} feet on land')


if __name__=='__main__':
    main()
