"""uv run --isolated --no-project tools/gloam/make_probe.py

Bake linear RGBA16F DDS irradiance + GGX radiance cubes. These are lighting
assets, not backgrounds. A bright opening in the canopy reflects in damp mud
and brass; the actual moon and oil lamp are separate shadow-casting lights.
"""
from math import atan2, cos, exp, log2, pi, sin, sqrt, tau
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parents[2] / 'assets/gloam/light'
OUT.mkdir(parents=True, exist_ok=True)

def norm(a):
    length = sqrt(sum(x*x for x in a))
    return tuple(x/length for x in a)

def sky(d):
    x, y, z = d
    az = atan2(z, x)
    # Uneven outline of fir crowns; the footpath opens the sky on one side.
    ridge = .29 + .11*sin(az*5+.7) + .075*cos(az*13) + .055*sin(az*35)
    visible = max(0., min(1., (y-ridge)*19+.5))
    gap = exp(6.2*(x*(-.48) + y*.69 + z*(-.54) - 1.0))
    return tuple(a + visible * (b+c*gap) for a,b,c in
                 [(0.012,.18,1.6), (0.019,.28,1.95), (0.023,.53,2.5)])

def direction(face, u, v):
    return norm(((1,-v,-u),(-1,-v,u),(u,1,v),(u,-1,-v),(u,-v,1),(-u,-v,-1))[face])

def reverse_bits(i):
    x, t = 0., .5
    while i:
        x += (i & 1)*t
        i >>= 1
        t *= .5
    return x

def filter_kernel(count, rough, diffuse):
    samples=[]
    for i in range(count):
        q, phi = (i+.5)/count, reverse_bits(i)*tau
        if diffuse:
            c = sqrt(1-q) # cosine-weighted hemisphere
        else:
            nh = sqrt((1-q)/(1+(rough**4-1)*q))
            c = 2*nh*nh-1 # reflect the view about a GGX half-vector
            if c <= 0: continue
        s = sqrt(max(0.,1-c*c))
        samples.append((s*cos(phi),s*sin(phi),c,1. if diffuse else c))
    return samples

def filtered(n, samples):
    x, y, z = n
    t = norm((z,0,-x)) if abs(y)<.9999 else (1,0,0)
    b = (y*t[2]-z*t[1], z*t[0]-x*t[2], x*t[1]-y*t[0])
    r=g=bl=total=0.
    for a,c,e,w in samples:
        rgb = sky((a*t[0]+c*b[0]+e*x, a*t[1]+c*b[1]+e*y, a*t[2]+c*b[2]+e*z))
        r+=w*rgb[0]; g+=w*rgb[1]; bl+=w*rgb[2]; total+=w
    return (r/total,g/total,bl/total)

def bake(file, size, diffuse=False):
    levels = 1 if diffuse else int(log2(size))+1
    header = [124, 0x100f | (0x20000 if levels>1 else 0), size, size, size*8, 0, levels,
              *([0]*11), 32, 4, 113, 0,0,0,0,0, 0x1008 | (0x400000 if levels>1 else 0),
              0xfe00,0,0,0]
    kernels = [filter_kernel(96, m/max(1,levels-1), diffuse) for m in range(levels)]
    with (OUT/file).open('wb') as f:
        f.write(b'DDS '+struct.pack('<31I',*header))
        for side in range(6):
            for m in range(levels):
                s = size >> m
                for iy in range(s):
                    for ix in range(s):
                        d = direction(side,(2*ix+1)/s-1,(2*iy+1)/s-1)
                        rgb = sky(d) if not diffuse and m==0 else filtered(d,kernels[m])
                        f.write(struct.pack('<4e',*rgb,1.))

def mist():
    # Static, world-space hollow mist, not a height veil over the whole scene.
    # Keep the traveler's clearing transparent; broken low layers build behind
    # it, with only a trace of real lamp-lit air left in the clearing.
    def smooth(a, b, value):
        t = max(0., min(1., (value-a)/(b-a)))
        return t*t*(3.-2.*t)
    w, h, d = 96, 32, 96
    header = [124,0x80100f,h,w,w*2,d,1,*([0]*11),32,4,111,0,0,0,0,0,
              0x1008,0x200000,0,0,0]
    with (OUT/'ground-mist.dds').open('wb') as f:
        f.write(b'DDS '+struct.pack('<31I',*header))
        for k in range(d):
            z = ((k+.5)/d-.5)*39-8
            for j in range(h):
                y = ((j+.5)/h-.5)*4.6+1.65
                for i in range(w):
                    x = ((i+.5)/w-.5)*54
                    rear = smooth(.6, 6.8, -z + .55*sin(x*.28+z*.16)
                                  + .30*cos(x*.50-z*.22))
                    height = .38+.13*sin(x*.43+z*.31)+.10*cos(z*.76-x*.19)
                    pockets = max(0., min(1., .54+.27*sin(x*.59+z*.43+y*.9)
                                           + .18*cos(z*.83-x*.26)))
                    low = .58*exp(-((y-height)/.34)**2)*pockets
                    ridge = .96+.30*sin(x*.25+z*.39)+.17*cos(z*.71-x*.21)
                    broken = max(0., .38+.35*sin(x*.37-z*.51+y*.7)
                                 + .23*cos(x*.73+z*.29))
                    upper = (.22+.12*rear)*exp(-((y-ridge)/(.43+.12*rear))**2)*broken
                    loft = .028*rear*exp(-((y-1.85)/.90)**2)*pockets
                    lamp_air = .04*exp(-((x-1.5)**2+(z-.83)**2)/2.2
                                       -((y-.90)/.80)**2)
                    edges = (smooth(0., 3., 27.-abs(x))
                             * smooth(0., 3., z+27.5)
                             * (1.-smooth(1.8, 4., z))
                             * smooth(-.5, -.08, y)
                             * (1.-smooth(2.7, 3.7, y)))
                    density = (rear*(low+upper+loft)+lamp_air)*edges
                    f.write(struct.pack('<e', min(1., density)))

mist()
bake('canopy-radiance.dds', 32)
bake('canopy-irradiance.dds', 8, diffuse=True)
