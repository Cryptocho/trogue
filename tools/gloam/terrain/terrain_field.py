"""Deterministic terrain placement in metres; public inputs are Bevy X/Z.

地表与植物共用的高度函数；接触点严格保持 Y=0。
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PARAMS = json.loads((ROOT / "parameters.json").read_text())
POINTS = sorted(PARAMS["path_control_points_xz"], key=lambda p: p[1])


def smoothstep(a, b, value):
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def path_x(z):
    for i in range(len(POINTS) - 1):
        x0, z0 = POINTS[i]
        x1, z1 = POINTS[i + 1]
        if z0 <= z <= z1:
            t = (z - z0) / (z1 - z0)
            prev = POINTS[max(0, i - 1)]
            nxt = POINTS[min(len(POINTS) - 1, i + 2)]
            m0 = (x1 - prev[0]) / (z1 - prev[1])
            m1 = (nxt[0] - x0) / (nxt[1] - z0)
            return ((2*t**3 - 3*t*t + 1)*x0 + (t**3 - 2*t*t + t)*(z1-z0)*m0
                    + (-2*t**3 + 3*t*t)*x1 + (t**3 - t*t)*(z1-z0)*m1)
    return POINTS[0][0] if z < POINTS[0][1] else POINTS[-1][0]


def path_distance(x, z):
    # Width measured perpendicular to a smooth centre line, not merely along X.
    slope = (path_x(z + 0.03) - path_x(z - 0.03)) / 0.06
    return abs(x - path_x(z)) / math.sqrt(1.0 + slope * slope)


def channel_value(x, z, channel):
    distance = 1e9
    for a, b in zip(channel["points"], channel["points"][1:]):
        dx, dz = b[0]-a[0], b[1]-a[1]
        t = max(0.0, min(1.0, ((x-a[0])*dx+(z-a[1])*dz)/(dx*dx+dz*dz)))
        distance = min(distance, math.hypot(x-a[0]-t*dx, z-a[1]-t*dz))
    width = channel["half_width"] * (1 + 0.16*math.sin(8.2*x+2.1*z)
                                     + 0.11*math.sin(5.8*z-3.3*x))
    return max(0.0, 1-distance/width)


def wetness(x, z):
    return max(channel_value(x, z, channel) for channel in PARAMS["wet_channels"])


def ground_height(x, z):
    distance = path_distance(x, z)
    off_path = smoothstep(0.65, 1.9, distance)
    broad = (0.022 * math.sin(0.47*x + 0.16*z)
             + 0.018 * math.sin(0.34*z - 0.28*x)
             + 0.010 * math.sin(1.20*x + 0.62*z))
    height = -0.008 + off_path * (0.035 + broad)
    # Fine geometry is deliberately subdued; the normal texture supplies pores.
    height += 0.004 * math.sin(2.7*x - 0.42*z) * math.sin(2.1*z)
    for channel in PARAMS["wet_channels"]:
        depression = smoothstep(0.06, 0.43, channel_value(x, z, channel))
        height = height*(1-depression) + (channel["level"]-0.023)*depression
    for patch in PARAMS["contact_patches"]:
        d = math.hypot(x - patch["x"], z - patch["z"])
        height *= smoothstep(patch["flat_radius"], patch["flat_radius"] + 0.58, d)
    return height


def vegetation_clearance(x, z):
    # Preserve the traveller silhouette and pools of light, not just their feet.
    if wetness(x, z) > 0.18:
        return False
    for patch in PARAMS["contact_patches"]:
        radius = patch["flat_radius"] + (0.25 if patch["name"] == "traveller" else 0.10)
        if math.hypot(x - patch["x"], z - patch["z"]) < radius:
            return False
    return True
