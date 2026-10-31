"""Generate a tileable 3D cloud noise volume (DDS, D3D11 TEXTURE3D).

Output: data/cloudnoise.dds, 64x64x64 R8G8B8A8_UNORM, DX10 header.
  R = base shape noise (tileable value-noise FBM, perlin-worley-ish)
  G = billow / worley cellular noise for cauliflower edges
  B = high-frequency detail for erosion
  A = 255

Everything is periodic on the 64-unit lattice so the volume tiles seamlessly; the shader samples it
with a WRAP sampler and adds procedural wind offset. Re-run: python tools/gen_cloud_noise.py
"""

import math
import struct
import numpy as np
from pathlib import Path

SIZE = 64
RNG = np.random.default_rng(1337)
OUT = Path(__file__).resolve().parents[1] / "data" / "cloudnoise.dds"


def periodic_value_noise(period: int, size: int) -> np.ndarray:
    lattice = RNG.random((period, period, period)).astype(np.float32)
    coords = np.arange(size, dtype=np.float32) * (period / size)
    idx = np.floor(coords).astype(np.int32) % period
    frac = coords - np.floor(coords)
    frac = frac * frac * (3.0 - 2.0 * frac)
    i0 = idx
    i1 = (idx + 1) % period
    fx = frac
    fx1 = 1.0 - frac

    def lerp3(axis):
        pass

    def gather(i, j, k):
        return lattice[np.ix_(i, j, k)]

    c000 = gather(i0, i0, i0)
    c100 = gather(i1, i0, i0)
    c010 = gather(i0, i1, i0)
    c110 = gather(i1, i1, i0)
    c001 = gather(i0, i0, i1)
    c101 = gather(i1, i0, i1)
    c011 = gather(i0, i1, i1)
    c111 = gather(i1, i1, i1)

    fx_ = fx[:, None, None]
    fy_ = fx[None, :, None]
    fz_ = fx[None, None, :]
    gx_ = fx1[:, None, None]
    gy_ = fx1[None, :, None]
    gz_ = fx1[None, None, :]

    z0 = c000 * gz_ + c001 * fz_
    z1 = c010 * gz_ + c011 * fz_
    y0 = z0 * gy_ + z1 * fy_
    z0 = c100 * gz_ + c101 * fz_
    z1 = c110 * gz_ + c111 * fz_
    y1 = z0 * gy_ + z1 * fy_
    return y0 * gx_ + y1 * fx_


def fbm(size: int, octaves: int) -> np.ndarray:
    total = np.zeros((size, size, size), dtype=np.float32)
    amp = 0.5
    norm = 0.0
    for octave in range(octaves):
        period = 4 * (2 ** octave)
        total += periodic_value_noise(period, size) * amp
        norm += amp
        amp *= 0.5
    return total / norm


def periodic_worley(size: int, cells: int) -> np.ndarray:
    points = RNG.random((cells, cells, cells, 3)).astype(np.float32)
    acc = np.full((size, size, size), 10.0, dtype=np.float32)
    lattice = np.stack(np.meshgrid(
        np.arange(size), np.arange(size), np.arange(size), indexing="ij"), axis=-1).astype(np.float32)
    lattice /= size  # 0..1 space
    for cx in range(cells):
        for cy in range(cells):
            for cz in range(cells):
                p = (np.array([cx, cy, cz], dtype=np.float32) + points[cx, cy, cz]) / cells
                delta = np.abs(lattice - p)
                delta = np.minimum(delta, 1.0 - delta)  # wrap
                dist = np.sqrt((delta * delta).sum(axis=-1))
                acc = np.minimum(acc, dist)
    return acc


def main() -> None:
    shape = fbm(SIZE, 5)
    shape = (shape - shape.min()) / max(float(shape.max() - shape.min()), 1e-6)
    worley = periodic_worley(SIZE, 8)
    worley = 1.0 - np.clip(worley * 1.6, 0.0, 1.0)
    detail = fbm(SIZE, 6)
    detail = (detail - detail.min()) / max(float(detail.max() - detail.min()), 1e-6)

    r = np.clip(shape * 0.75 + worley * 0.25, 0.0, 1.0)
    g = np.clip(worley, 0.0, 1.0)
    b = np.clip(detail, 0.0, 1.0)

    rgba = np.empty((SIZE, SIZE, SIZE, 4), dtype=np.uint8)
    rgba[..., 0] = (r * 255.0).astype(np.uint8)
    rgba[..., 1] = (g * 255.0).astype(np.uint8)
    rgba[..., 2] = (b * 255.0).astype(np.uint8)
    rgba[..., 3] = 255

    header = bytearray()
    header += b"DDS "
    header += struct.pack("<I", 124)              # dwSize
    header += struct.pack("<I", 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x80000)  # caps|height|width|pitch|depth|linearsize
    header += struct.pack("<I", SIZE)              # height
    header += struct.pack("<I", SIZE)              # width
    header += struct.pack("<I", SIZE * SIZE * 4)   # pitchOrLinearSize
    header += struct.pack("<I", SIZE)              # depth
    header += struct.pack("<I", 0)                 # mipMapCount
    header += b"\x00" * 44                        # reserved
    header += struct.pack("<I", 32)               # pf.dwSize
    header += struct.pack("<I", 0x4)              # pf.dwFlags = FOURCC
    header += b"DX10"                             # pf.dwFourCC
    header += struct.pack("<I", 0) * 5            # pf bit count/masks
    header += struct.pack("<I", 0x1000 | 0x8)     # caps = texture | complex
    header += struct.pack("<I", 0) * 4            # caps2..reserved
    assert len(header) == 4 + 124, len(header)

    dx10 = struct.pack("<IIIII", 28, 4, 0, 1, 0)  # R8G8B8A8_UNORM, TEXTURE3D, misc, array=1, misc2

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(bytes(header) + dx10 + rgba.tobytes())
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {SIZE}^3 RGBA8)")


if __name__ == "__main__":
    main()
