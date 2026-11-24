"""Build the original pixel forest's display palette lookup. No art or screenshot input.

uv run --project tools/gloam/terrain tools/gloam/paper/palette.py

Map a sqrt-linear RGB cube to authored sRGB ink colors in Oklab. No global noise,
Floyd–Steinberg or screenshot quantization: two neighbouring pixels of the same
ink stay joined. Hue ramps deliberately share shadow colors.
"""
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.spatial import cKDTree

# Cold ink and slate, lake/linen, cedar, fern, lichen, bark, trail, scarf, wick.
RAMPS = (
    '0b1221 101525 182037 222b42 303b54 3a465e 43506a 5a6780 758298 93a0b2 b0bec5 d7dacf',
    '162739 1d334d 294461 35597a 4a7290 6590a9 85b1c2 acd0d4 d1e8de',
    '142c33 183c43 225052 31665f 468273 659b86 87b59a accab0',
    '203135 2a423e 365747 486b52 5e815c 779c6e 96b585 b8cc96',
    '303639 40463c 53583f 6a6d49 878551 a0a068 bcb784 d6d499',
    '211d2d 241f31 302a3a 392d41 49344a 604455 78566a 936d7e b58fa0 d0adad',
    '302c34 382f37 4b3e43 584749 614f4b 796253 967961 af926f c9ab83 e2c7a1',
    '4e2c3b 693a42 88464a a3564d c66e52 de8960 efa37a f4c09b',
    '513a35 704c36 8e613a ae7b43 c48949 cd994e e8b666 f5cf87 ffe4a7 fff0bf fff8dc',
    '272635 353448 454461 514f70 626180 83839d a3a1b4 c7bcc9',
)
ROOT = Path(__file__).resolve().parents[3]
SIZE = 64


def oklab(rgb):
    m1 = np.array([[.4122214708,.5363325363,.0514459929],
                   [.2119034982,.6806995451,.1073969566],
                   [.0883024619,.2817188376,.6299787005]])
    m2 = np.array([[.2104542553,.7936177850,-.0040720468],
                   [1.9779984951,-2.4285922050,.4505937099],
                   [.0259040371,.7827717662,-.8086757660]])
    return np.cbrt(rgb @ m1.T) @ m2.T


def main():
    palette = np.asarray([tuple(bytes.fromhex(h)) for ramp in RAMPS for h in ramp.split()], np.uint8)
    srgb = palette.astype(float) / 255
    linear = np.where(srgb <= .04045, srgb/12.92, ((srgb+.055)/1.055)**2.4)
    green, blue, red = np.indices((SIZE,SIZE,SIZE), dtype=np.float32)
    grid = (np.stack((red,green,blue), axis=-1)/(SIZE-1))**2
    # Retain the hue of cold stone, green moss and warm earth at the same value.
    weight = np.array((1., 1.22, 1.12))
    tree = cKDTree(oklab(linear)*weight)
    _, closest = tree.query(oklab(grid)*weight)
    image = palette[closest].reshape(SIZE, SIZE*SIZE, 3)
    path = ROOT/'assets/gloam/paper/palette.png'
    Image.fromarray(image).save(path, optimize=True)
    print(f'{len(palette)} inks, {SIZE}³ → {path.name}')


if __name__ == '__main__':
    main()
