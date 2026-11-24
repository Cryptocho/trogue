"""Measure the actual native-window PNGs (requires the matching capture set).

uv run --project tools/gloam/terrain tools/gloam/riverside/check_view.py
Inputs: reference/gloam-comparison/riverside/{wide,detail,left,right}.png,
and captures/gloam/riverside/{mirror-on,mirror-off,pixel-zoom,pixel-back,flat}.png.
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
ART=ROOT/'assets/gloam/riverside'
REF=ROOT/'reference/gloam-comparison/riverside'
RUN=ROOT/'captures/gloam/riverside'
LUMA=np.array((.2126,.7152,.0722))


def window(path):
    with Image.open(path) as source:
        a=np.array(source.convert('RGB'))
    if a.shape != (1440,2560,3):
        raise ValueError(f'{path}: 需要目标主窗口 2560×1440')
    cells=a.reshape(288,5,512,5,3)
    native=cells[:,2,:,2]
    if np.any(cells != native[:,None,:,None,:]):
        raise ValueError(f'{path.name}: a 5×5 pixel square contains mixed values')
    return native.astype(np.int16)


def art_pixels(kind, item, art, pitch, mirrored, waves=None):
    w,h=art['size_px']; ax,ay=art['anchor_px']; scale=item['height']/h
    y,x=np.indices((288,512)); x=256+(x+.5-256)*25/pitch; y=144+(y+.5-144)*25/pitch
    fx,fy=item['foot']; u=(x-fx)/scale; v=(y-fy)/scale
    if mirrored:
        v=8/scale-v  # Reflect about four-pixel-lower water plane: twice the drop.
        u+=np.rint((waves.astype(float)-128)/58.)/scale
    u=ax+(-u if item.get('flip') else u); v=ay+v
    valid=(u>=0)&(u<w)&(v>=0)&(v<h)
    return valid, np.clip(np.floor(v).astype(int),0,h-1), np.clip(np.floor(u).astype(int),0,w-1)


def main():
    layout=json.loads((ART/'layout.json').read_text()); actors=layout['actors']
    spr={a['name']:a for group in ('props','trees') for a in json.loads((ART/group/'manifest.json').read_text())['sprites']}
    with Image.open(ART/'bank-field.png') as src: field=np.array(src.convert('RGBA'))
    oy=(field.shape[0]-288)//2; ox=(field.shape[1]-512)//2; field=field[oy:oy+288,ox:ox+512]
    wet=field[:,:,0]>127
    for f in ('wide','detail','no-mirrors','left','right'): window(REF/(f+'.png'))
    print('Final pixel frames: every 5×5 square uniform (512×288 → 2560×1440).')
    # These two captures are from a real R key in ONE session, with Bloom off
    # so that optical scattering cannot cross the mask during measurement.
    on,off=[window(RUN/(s+'.png')) for s in ('mirror-on','mirror-off')]
    d=on-off; changed=np.any(d!=0,axis=2)
    print(f'mirror: {changed.sum()} changed water pixels; {(changed & ~wet).sum()} changed dry pixels')
    assert not np.any(changed & ~wet) and np.count_nonzero(changed)>300
    for kind in ('lamp','cypress'):
        item=next(a for a in actors if a['kind']==kind and (kind=='lamp' or a['foot']==[397,31]))
        art=spr[kind]; valid,v,u=art_pixels(kind,item,art,25,True,field[:,:,2])
        if kind=='lamp':
            with Image.open(ROOT/'assets'/art['emission']) as im: mask=np.asarray(im.convert('L'))[v,u]>150
            select=wet & valid & mask
            lift=d[:,:,0][select].mean()
            print(f'inverted source-glass mask: {select.sum()} pixels, mean red-channel gain {lift:.2f}/255')
            assert select.sum()>30 and lift>12
        else:
            with Image.open(ROOT/'assets'/art['image']) as im: mask=np.asarray(im.convert('RGBA'))[v,u,3]>127
            select=wet & valid & mask & (field[:,:,1]>50); drop=(-d[select]@LUMA).mean()
            print(f'opposite-bank cypress, masked in jade water: {select.sum()} pixels, luma loss {drop:.2f}/255')
            assert select.sum()>60 and drop>8
    item=next(a for a in actors if a['kind']=='stone'); art=spr['stone']; w,h=art['size_px']
    valid,v,u=art_pixels('stone',item,art,42,False)
    with Image.open(ROOT/'assets'/art['normal']) as im: normals=np.asarray(im.convert('RGBA'))[v,u]
    keep=valid & (normals[:,:,3]==255) & (v>=.25*h) & (v<.80*h)
    nx=normals[:,:,0].astype(float)/127.5-1; left=window(REF/'left.png'); right=window(REF/'right.png')
    for label,s in [('left',keep & (nx<-.28)),('right',keep & (nx>.28))]:
        l,r=(np.mean(pic[s]@LUMA) for pic in (left,right)); print(f'{label} stone face: n={s.sum()}, left-light={l:.2f}, right-light={r:.2f}')
        assert s.sum()>12 and (l>r+7 if label=='left' else r>l+7)
    mad=np.abs(window(REF/'detail.png')[keep]-window(RUN/'flat.png')[keep]).mean(); print(f'mapped vs flat stone: mean |RGB| {mad:.2f}/255'); assert mad>1.5
    repeat=np.any(window(RUN/'pixel-zoom.png')!=window(RUN/'pixel-back.png'),axis=2).sum(); print('changed art pixels after Space round-trip at wheel zoom:',repeat); assert repeat<5


if __name__=='__main__': main()
