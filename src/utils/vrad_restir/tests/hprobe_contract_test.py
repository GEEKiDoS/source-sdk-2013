#!/usr/bin/env python3
"""Check the ambient probe asset of a real paired high-resolution BSP without invoking a bake.

Locates the .hlight asset through the rshd manifest, derives maps/<name>.hprobe, parses it with the rules of
public/hprobe_bsp.h, binds it to the .hlight bytes by CRC and per-mode identity and spot-checks the stored grids.
--hlight-reference additionally requires the .hlight pak member to be byte-identical to the one in a BSP baked
from the same input before the probe stage existed (hlight_contract_test.py's unchanged() excludes that member).
"""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import struct
import sys
from hlight_contract_test import BSP, manifest, section, MISSING
from shadowmap_contract_test import require, unpack, crc

MAGIC, VERSION, ENDIAN = 0x42525048, 1, 0x01020304
BYTES_PER_PROBE = 29
MAX_BRICKS = 1 << 18
MAX_GRID_BYTES = 256 << 20
FORMATS = (26, 31, 61)  # R11G11B10_FLOAT, RGBA8_SNORM, R8_UNORM
SPOT_CHECKS = 64


def probe_path(hlight_path):
    require(hlight_path.endswith('.hlight'), 'asset path has no .hlight suffix')
    return hlight_path[:-len('.hlight')] + '.hprobe'


def hlight_identity(data):
    """The paired file's CRC and (faceLump, lightingLump) -> (facesCRC32, lightingCRC32, lightingBytes)."""
    _, count, modes_at, file_crc = unpack('<2IQI', data, 24)
    require(file_crc == crc(data[:40] + bytes(4) + data[44:]), 'hlight whole-file CRC')
    modes = {}
    for i in range(count):
        face, lighting, _, _, _, lighting_bytes, face_crc, lighting_crc = unpack('<4I2Q2I', data, modes_at + i * 128)
        modes[face, lighting] = (face_crc, lighting_crc, lighting_bytes)
    return file_crc, modes


def finite_dc(packed):
    """R11G11B10_FLOAT has no NaN/Inf: no exponent field may be all ones."""
    return (packed >> 6) & 31 != 31 and (packed >> 17) & 31 != 31 and (packed >> 27) & 31 != 31


def atlas_bricks(n):
    x = min(n, 256)
    y = min(-(-n // x), 256)
    return x, y, -(-n // (x * y))


def grid(data, cursor, record, identity):
    face, lighting, face_crc, lighting_crc, lighting_bytes, grid_at, bricks, *reserved = record
    require(not any(reserved) and identity.get((face, lighting)) == (face_crc, lighting_crc, lighting_bytes),
            'probe mode identity does not match the hlight mode')
    require(grid_at == (cursor + 15) & ~15 and grid_at + 64 <= len(data), 'noncanonical grid section')
    ox, oy, oz, spacing, bx, by, bz, count, ax, ay, az, reach, dc_format, band_format, validity_format, zero = \
        unpack('<4f3II3II3II', data, grid_at)
    require(all(math.isfinite(v) for v in (ox, oy, oz)) and spacing == math.floor(spacing) and 8 <= spacing <= 512,
            'grid origin/spacing')
    require(all(0 < d <= 1024 for d in (bx, by, bz)) and bx * by * bz <= 1 << 30, 'indirection extent')
    require(0 < bricks <= MAX_BRICKS and count == bricks <= bx * by * bz, 'brick count')
    require((ax, ay, az) == atlas_bricks(bricks), 'atlas brick formula')
    require(reach <= 65536 and (dc_format, band_format, validity_format) == FORMATS and not zero, 'grid reach/formats/reserved')
    cells, texels = bx * by * bz, ax * ay * az * 125
    size = 64 + 4 * cells + texels * BYTES_PER_PROBE
    require(size <= MAX_GRID_BYTES, 'grid exceeds the 256 MiB budget')
    end = section(data, cursor, grid_at, size)
    indirection_at = grid_at + 64
    dc_at = indirection_at + 4 * cells
    bands_at = dc_at + 4 * texels
    validity_at = bands_at + 24 * texels
    slots = [v for v in memoryview(data)[indirection_at:dc_at].cast('I') if v != MISSING]
    require(len(slots) == bricks and len(set(slots)) == bricks and max(slots) < bricks, 'indirection slots')
    validity = data[validity_at:validity_at + texels]
    # Unallocated atlas slots are all zero, so valid texels cannot outnumber the allocated probes.
    require(validity.count(0) + validity.count(255) == texels and 0 < validity.count(255) <= bricks * 125, 'validity texels')
    # SPOT_CHECKS evenly spaced valid and invalid/unallocated texels (the first of each kind at or after the position),
    # and the last atlas texel, which lies in the last, possibly unallocated, slot.
    for position in (*range(0, texels, max(1, texels // SPOT_CHECKS)), texels - 1):
        for value in (255, 0):
            texel = validity.find(value, position)
            if texel < 0:
                continue
            dc = unpack('<I', data, dc_at + 4 * texel)[0]
            bands = [struct.unpack_from('<4b', data, bands_at + (band * texels + texel) * 4) for band in range(6)]
            if value:
                require(finite_dc(dc) and all(v != -128 for row in bands for v in row), f'invalid valid texel {texel}')
            else:
                require(dc == 0 and not any(any(row) for row in bands), f'invalid/unallocated texel {texel} is not all zero')
    return end, dict(bricks=bricks, spacing=spacing, dims=(bx, by, bz), atlas=(ax, ay, az), reach=reach,
                     valid=validity.count(255), texels=texels)


def parse(data, hlight):
    require(len(data) >= 64, 'truncated probe header')
    magic, version, header, endian, size, hlight_crc, count, modes_at, checksum, *reserved = unpack('<4IQ2IQ6I', data)
    require((magic, version, header, endian) == (MAGIC, VERSION, 64, ENDIAN) and size == len(data), 'probe header identity')
    require(0 < count <= 4 and not any(reserved), 'probe mode count/reserved')
    require(checksum == crc(data[:40] + bytes(4) + data[44:]), 'probe whole-file CRC')
    file_crc, identity = hlight_identity(hlight)
    require(hlight_crc == file_crc and count == len(identity), 'probe asset does not match the lightmap asset')
    cursor = section(data, 64, modes_at, count * 64)
    grids, previous = [], (-1, -1)
    for i in range(count):
        record = unpack('<4I2QI7I', data, modes_at + i * 64)
        require((record[0], record[1]) > previous, 'explicit mode pair order')
        previous = record[0], record[1]
        cursor, info = grid(data, cursor, record, identity)
        grids.append(info)
    require(cursor == len(data), 'trailing probe bytes')
    return grids


def asset(bsp, hlight_path):
    path = probe_path(hlight_path)
    require(path in bsp.pak, 'paired probe asset missing from BSP pak (rebake with -restir_shadowmaps)')
    return path, bsp.pak[path], parse(bsp.pak[path], bsp.pak[hlight_path])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bsp', type=Path)
    parser.add_argument('--hlight-reference', type=Path,
                        help='BSP baked from the same input before the probe stage: its .hlight member must be byte-identical')
    args = parser.parse_args()
    try:
        bsp = BSP(args.bsp)
        hlight_path, _ = manifest(bsp)
        path, data, grids = asset(bsp, hlight_path)
        if args.hlight_reference:
            require(BSP(args.hlight_reference).pak.get(hlight_path) == bsp.pak[hlight_path], '.hlight member changed')
        print(f'PASS hprobe modes={len(grids)} path={path} asset-bytes={len(data)} ' +
              ' '.join(f"[bricks={g['bricks']} spacing={g['spacing']:g} dims={g['dims']} atlas={g['atlas']} "
                       f"reach={g['reach']} valid={g['valid']}/{g['texels']}]" for g in grids))
        return 0
    except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error) as error:
        print(f'FAIL hprobe: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
