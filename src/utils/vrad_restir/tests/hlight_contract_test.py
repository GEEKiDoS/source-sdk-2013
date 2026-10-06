#!/usr/bin/env python3
"""Check real paired high-resolution BSP output without invoking a bake or generator.

The optional density-one control and dense diagnostics must be produced by the
real baker with -restir_shadowmap_diagnostics. --raw-radiance additionally requires
-restir_denoiser none and a fixture with no macro texture or entity _minlight.
This is the new forward-density contract; no inverse receiver tests execute.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import re
import struct
import sys
import zlib
from shadowmap_contract_test import BSP as LegacyBSP, require, unpack, crc, rgbexp, quantum

GEOMETRY = (1, 2, 3, 6, 12, 13, 14, 26, 33, 43, 44, 48)
MISSING = 0xffffffff


class BSP(LegacyBSP):
    def parse_shadow(self):
        # The common BSP/ZIP reader is reused, not its frozen v3 parser.
        return None


def section(data, cursor, offset, size):
    aligned = (cursor + 15) & ~15
    require(offset == aligned and size >= 0 and offset + size <= len(data), 'noncanonical section range')
    require(not any(data[cursor:aligned]), 'nonzero section padding')
    return offset + size


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def native_faces(bsp, lump):
    raw = bsp.lumps[lump]
    require(len(raw) % 56 == 0, 'native face stride')
    result = []
    for at in range(0, len(raw), 56):
        texinfo = unpack('<h', raw, at + 10)[0]
        offset, area, *domain = unpack('<if4i', raw, at + 20)
        styles = tuple(s for s in raw[at+16:at+20] if s != 255)
        flags = unpack('<I', bsp.lumps[6], texinfo*72+64)[0] if texinfo >= 0 else 0
        result.append(dict(styles=styles, offset=offset, mins=tuple(domain[:2]), extents=tuple(domain[2:]),
                           channels=4 if flags & 0x800 else 1, geometry=raw[at:at+16]+raw[at+24:at+56]))
    return result


def manifest(bsp):
    game = bsp.games.get('rshd')
    require(game and game['version'] == 4, 'requires native rshd dictionary version 4')
    raw = game['data']
    size, mask, asset_version, reserved = unpack('<4I', raw)
    require(size == len(raw) and mask == 3 and asset_version == 1 and not reserved, 'paired manifest header')
    require((bsp.flags & 0xc0000) == 0xc0000, 'paired selected-direct level flags')
    path_bytes = raw[144:400]
    path, null, tail = path_bytes.partition(b'\0')
    require(null and not any(tail), 'canonical asset path terminator/padding')
    path = path.decode('utf-8')
    require(path.startswith('maps/') and path.endswith('.hlight') and len(path) >= 13 and
            not any(ord(c) < 32 or ord(c) == 127 or c in '/\\\\:' for c in path[5:]), 'safe exact pak asset path')
    modes = []
    cursor = 400
    for mi in range(2):
        fields = unpack('<9Ii6I', raw, 16+mi*64)
        f, lighting, world, count, lighting_crc, face_crc, world_crc, offset, lights, sun, asset = fields[:11]
        require(not any(fields[11:]) and f in (7, 58) and lighting == (8, 53)[mi] and world == (15, 54)[mi], 'manifest mode identity/reserved')
        require(count == len(bsp.lumps[lighting]) and lighting_crc == crc(bsp.lumps[lighting]) and
                face_crc == crc(bsp.lumps[f]) and world_crc == crc(bsp.lumps[world]), 'manifest native lump CRC')
        require(offset == cursor and lights >= 0 and sun in (-1, 0) and (sun < 0 or lights > 0), 'canonical selected-light range/sun')
        cursor += lights*116
        require(cursor <= len(raw), 'selected-light span')
        for li in range(lights):
            at = offset+li*116
            w = unpack('<9f3i7f3i', raw, at)
            source, angle, radius, start, end, cap, zero = unpack('<i5fI', raw, at+88)
            require(all(math.isfinite(x) for x in (*w[:9], *w[12:19], angle, radius, start, end, cap)), 'nonfinite selected light')
            require(all(x >= 0 for x in w[3:6]) and 0 <= w[11] < 64 and not w[19] and source >= -1 and not zero, 'selected light intensity/style/flags/provenance')
            require(all(x >= 0 for x in w[14:19]) and all(-1 <= x <= 1 for x in w[12:14]), 'selected light attenuation/cone range')
            if li == sun:
                require(w[10] == 3 and 0 <= angle < 90 and radius == start == end == cap == 0, 'selected sun record')
            else:
                require(w[10] in (1, 2) and radius >= 0 and angle == 0 and start >= 0 and cap > 0, 'selected local record')
        modes.append(dict(face=f, lighting=lighting, lighting_bytes=count, face_crc=face_crc,
                          lighting_crc=lighting_crc, sun=sun, lights=lights, asset=asset))
    require(cursor == len(raw), 'trailing manifest bytes')
    return path, modes


def asset(bsp, path, manifests, density):
    require(path in bsp.pak, 'manifest asset missing from BSP pak')
    data = bsp.pak[path]
    h = unpack('<4IQ2IQI5I', data)
    magic, version, header, endian, size, actual_density, count, modes_offset, checksum = h[:9]
    require((magic, version, header, endian) == (0x54494c48, 1, 64, 0x01020304) and size == len(data), 'asset header identity')
    require(actual_density == density and count == 2 and not any(h[9:]), 'asset density/mode count/reserved')
    require(checksum == crc(data[:40]+bytes(4)+data[44:]), 'whole-file CRC')
    cursor = section(data, 64, modes_offset, count*112)
    modes = []
    previous_pair = (-1, -1)
    for mi in range(count):
        r = unpack('<4I2Q4I5Q4IQ', data, modes_offset+mi*112)
        face, lighting, fv, lv, face_bytes, lighting_bytes, face_crc, lighting_crc, faces, models = r[:10]
        face_at, model_at, tile_at, page_at, identity_at, tiles, pages, identities, zero, zero64 = r[10:]
        require((face, lighting) > previous_pair and face in (7, 58) and lighting in (8, 53), 'explicit mode pair order')
        previous_pair = (face, lighting)
        require(fv == bsp.directory[face][2] and lv == bsp.directory[lighting][2] and not zero and not zero64, 'mode version/reserved')
        require(face_bytes == len(bsp.lumps[face]) == faces*56 and lighting_bytes == len(bsp.lumps[lighting]), 'native mode byte counts')
        require(face_crc == crc(bsp.lumps[face]) and lighting_crc == crc(bsp.lumps[lighting]), 'asset native mode CRC')
        cursor = section(data, cursor, identity_at, identities*24)
        ids = [unpack('<IIQII', data, identity_at+i*24) for i in range(identities)]
        require([x[0] for x in ids] == sorted((*GEOMETRY, face, lighting)), 'complete sorted effective identity set')
        for lump, version, length, checksum, reserved in ids:
            require(not reserved and version == bsp.directory[lump][2] and length == len(bsp.lumps[lump]) and
                    checksum == crc(bsp.lumps[lump]), f'effective lump identity {lump}')
        cursor = section(data, cursor, model_at, models*64)
        model_records = [unpack('<4I12f', data, model_at+i*64) for i in range(models)]
        require(models == len(bsp.lumps[14])//48, 'native model count')
        next_face = 0
        for i, model in enumerate(model_records):
            index, first, num, flags = model[:4]
            native_first, native_num = unpack('<2i', bsp.lumps[14], i*48+40)
            require((index, first, num) == (i, native_first, native_num) and first == next_face and
                    flags in (0, 1) and all(math.isfinite(x) for x in model[4:]), 'native model partition/pose')
            next_face += num
        require(next_face == faces, 'model face partition coverage')
        cursor = section(data, cursor, face_at, faces*128)
        fs = [unpack('<2I2i28I', data, face_at+i*128) for i in range(faces)]
        ns = native_faces(bsp, face)
        for i, f in enumerate(fs):
            ordinal, owner, minx, miny, ex, ey, w, h, flags, styles = f[:10]
            require(ordinal == i and owner < models and model_records[owner][1] <= i < sum(model_records[owner][1:3]), 'face ordinal/model owner')
            require((minx, miny) == ns[i]['mins'] and (ex, ey) == tuple(max(0, x) for x in ns[i]['extents']), 'native unchanged face domain')
            require((w, h) == (ex*density+1, ey*density+1), 'true high-grid endpoint dimensions')
            require(not flags & ~15 and not any(f[30:]) and bool(flags & 4) == bool(styles) and styles <= 4, 'face flags/styles/reserved')
            require(bool(flags & 8) == bool(styles and model_records[owner][3] & 1 and manifests[0 if lighting == 8 else 1]['sun'] >= 0), 'sun face eligibility')
            require(all(0 <= s < 64 for s in f[10:10+styles]) and len(set(f[10:10+styles])) == styles and
                    (not styles or f[10] == 0) and all(s == 255 for s in f[10+styles:14]), 'authored styles before pruning')
            authored = f[10:10+styles]
            require(tuple(s for s in authored if s in ns[i]['styles']) == ns[i]['styles'], 'native styles are ordered authored subset')
            for s in range(4):
                for p in range(4):
                    used = s < styles and (p == 0 or flags & 1)
                    require((f[14+s*4+p] < tiles) if used else f[14+s*4+p] == MISSING, 'complete style/bump tile references')
        cursor = section(data, cursor, tile_at, tiles*32)
        ts = [unpack('<8I', data, tile_at+i*32) for i in range(tiles)]
        cursor = section(data, cursor, page_at, pages*48)
        ps = [unpack('<8I2Q', data, page_at+i*48) for i in range(pages)]
        next_tile = 0
        for pi, page in enumerate(ps):
            side, height, fmt, zero, first, num, r0, r1, pixel_at, pixel_bytes = page
            require(side in (2048, 4096, 8192, 16384) and side == height and fmt == 10 and not (zero or r0 or r1), 'RGBA16F page format')
            require(first == next_tile and num > 0 and first+num <= tiles and pixel_bytes == side*side*8, 'canonical page tile prefix/byte count')
            cursor = section(data, cursor, pixel_at, pixel_bytes)
            row_y = row_end = next_x = 0
            for ti in range(first, first+num):
                fi, style, plane, owner, x, y, width, height = ts[ti]
                require(fi < faces and style < fs[fi][9] and plane < 4 and owner == pi and
                        fs[fi][14+style*4+plane] == ti and (width, height) == fs[fi][6:8], 'tile backlink/dimensions')
                require(x >= 1 and y >= 1 and x+width < side and y+height < side, 'padded tile bounds')
                if y-1 != row_y:
                    require(y-1 == row_end and x == 1, 'canonical shelf row transition')
                    row_y, next_x = y-1, 0
                require(x-1 == next_x, 'tile shelf overlap/gap')
                next_x = x+width+1
                row_end = max(row_end, y+height+1)
                for yy in range(y-1, y+height+1):
                    for xx in range(x-1, x+width+1):
                        at = pixel_at+(yy*side+xx)*8
                        rgb = unpack('<4e', data, at)
                        require(all(math.isfinite(v) and v >= 0 for v in rgb[:3]), 'finite nonnegative retained half RGB')
                        expected_sun = style == plane == 0
                        require(0 <= rgb[3] <= 1 if expected_sun else rgb[3] == 0, 'designated base alpha/reserved alpha')
                        if expected_sun and not fs[fi][8] & 8:
                            require(rgb[3] == 1, 'neutral sun alpha without eligible sun')
                        cx = min(max(xx, x), x+width-1)
                        cy = min(max(yy, y), y+height-1)
                        require(data[at:at+8] == data[pixel_at+(cy*side+cx)*8:pixel_at+(cy*side+cx)*8+8], 'replicated LOD0 gutter')
            next_tile += num
        require(next_tile == tiles, 'all tiles belong to a canonical page')
        modes.append(dict(face=face, lighting=lighting, face_crc=face_crc, lighting_crc=lighting_crc,
                          lighting_bytes=lighting_bytes, faces=fs, native=ns, tiles=ts, pages=ps, models=model_records))
    require(cursor == len(data), 'trailing asset bytes')
    require(sorted(m['asset'] for m in manifests) == list(range(len(modes))), 'manifest/asset bijection')
    for mm in manifests:
        mode = modes[mm['asset']]
        require(all(mode[k] == mm[k] for k in ('face', 'lighting', 'lighting_bytes', 'face_crc', 'lighting_crc')), 'paired manifest asset identity')
    return data, modes


def pixel(data, mode, face, style, plane, x, y):
    tile = mode['tiles'][face[14+style*4+plane]]
    page = mode['pages'][tile[3]]
    at = page[8]+((tile[5]+y)*page[0]+tile[4]+x)*8
    return unpack('<4e', data, at)


def coarse_endpoints(bsp, data, modes, density):
    comparisons = 0
    for mode in modes:
        raw = bsp.lumps[mode['lighting']]
        for f, native in zip(mode['faces'], mode['native']):
            if not f[9]:
                continue
            nw, nh = (x+1 for x in native['extents'])
            for stock_slot, style in enumerate(native['styles']):
                asset_slot = f[10:14].index(style)
                for plane in range(native['channels']):
                    for y in range(nh):
                        for x in range(nw):
                            at = native['offset']+((stock_slot*native['channels']+plane)*nw*nh+y*nw+x)*4
                            sample = raw[at:at+4]
                            stock = rgbexp(sample)
                            high = pixel(data, mode, f, asset_slot, plane, x*density, y*density)
                            # The native diagnostic for a zero-sample face is red;
                            # the owned float asset never stores a red error sentinel.
                            if stock == (1.0, 0.0, 0.0) and high[:3] == (0.0, 0.0, 0.0):
                                continue
                            for a, b in zip(stock, high):
                                require(abs(a-b) <= quantum(sample)+abs(b)*0.0005+1e-7, 'coarse original endpoint does not match independent float lighting')
                                comparisons += 1
    require(comparisons > 0, 'no coarse endpoint RGB comparisons')
    return comparisons


def unchanged(before, after, asset_path):
    for lump in GEOMETRY:
        require(before.lumps[lump] == after.lumps[lump], f'native geometry/material-coordinate lump {lump} changed')
    for lump in (7, 58):
        old = native_faces(before, lump if before.lumps[lump] else 7)
        new = native_faces(after, lump)
        require(len(old) == len(new) and all(a['geometry'] == b['geometry'] for a, b in zip(old, new)), f'native face/grid topology {lump} changed')
    for path, raw in before.pak.items():
        if path == asset_path or re.fullmatch(r'(sp(_hdr)?_\d+\.vhv|texelslighting_\d+\.ppl)', path):
            continue
        require(after.pak.get(path) == raw, f'unrelated BSP ZIP member changed: {path}')


def diagnostics(base, control, modes, data, density, raw_radiance):
    cells = 0
    for mode in modes:
        suffix = '.ldr.json' if mode['lighting'] == 8 else '.hdr.json'
        d = json.loads(Path(str(base)+suffix).read_text())
        require(d['hlightDensity'] == density and d['sampleCellCount'] == len(d['sampleCells']) and
                d['highGridCount'] == len(d['luxelPositions']), 'real dense sample diagnostics')
        require(abs(d['assetRGBScale']-1/255) < 1e-11, 'linear pre-conversion RGB normalization')
        c = json.loads(Path(str(control)+suffix).read_text()) if control else None
        if c:
            require(c['hlightDensity'] == 1 and d['sampleCellCount'] > c['sampleCellCount'], 'no actual independent sample-cell increase')
            old = {f['dface']: f for f in c['faces']}
        for scene_index, f in enumerate(d['faces']):
            require(f['luxelW'] == (f['nativeW']-1)*density+1 and f['luxelH'] == (f['nativeH']-1)*density+1, 'diagnostic endpoint density')
            samples = d['sampleCells'][f['firstSample']:f['firstSample']+f['numSamples']]
            require(len(samples) == f['numSamples'] and all(s['face'] == scene_index for s in samples), 'dense face sample-cell range')
            require(all(math.isfinite(s['worldArea']) and s['worldArea'] >= 0 and all(math.isfinite(x) for x in s['position']) for s in samples), 'real finite dense cells/areas')
            if c and f['nativeW'] > 1 and f['nativeH'] > 1 and f['numSamples']:
                coarse = old[f['dface']]
                old_samples = c['sampleCells'][coarse['firstSample']:coarse['firstSample']+coarse['numSamples']]
                # A sub-cell-sized planar sliver need not cross any new cell
                # boundary. Its real clipping still runs at the requested density.
                splits_cell = f['flags'] & 2 or any(s['maxs'][a]-s['mins'][a] > 2/density for s in old_samples for a in (0, 1))
                if splits_cell:
                    require(f['numSamples'] > coarse['numSamples'], 'face output enlarged without independently subdividing sample cells')
                    require(len({tuple(s['position']) for s in samples}) > len({tuple(s['position']) for s in old_samples}), 'denser rays reused only the original receiver origins')
                if not f['flags'] & 2:
                    area = sum(s['worldArea'] for s in samples)
                    old_area = sum(s['worldArea'] for s in old_samples)
                    require(abs(area-old_area) <= max(area, old_area)*0.002+1e-5, 'subdivision did not conserve clipped planar world area')
            disk = mode['faces'][f['dface']]
            require(tuple(f['styles']) == disk[10:10+disk[9]] and f['numChannels'] == (4 if disk[8] & 1 else 1), 'authored style/bump data dropped before serialization')
            for style in range(f['numStyles']):
                for plane in range(f['numChannels']):
                    for y in range(f['luxelH']):
                        for x in range(f['luxelW']):
                            local = y*f['luxelW']+x
                            high = pixel(data, mode, disk, style, plane, x, y)
                            if style == plane == 0 and disk[8] & 8:
                                expected = f32(d['sunVisibility'][f['firstLuxel']+local])
                                require(high[3] == struct.unpack('<e', struct.pack('<e', expected))[0], 'independent designated sun alpha changed')
                            if raw_radiance:
                                at = f['firstOutput']+(style*f['numChannels']+plane)*f['luxelW']*f['luxelH']+local
                                value = d['receiverRadiance'][at] if d['luxelValid'][f['firstLuxel']+local] else (0, 0, 0)
                                expected = tuple(struct.unpack('<e', struct.pack('<e', f32(f32(v)*f32(1/255))))[0] for v in value)
                                require(high[:3] == expected, 'authored dense HDR RGB/style/bump output changed')
            cells += len(samples)
    return cells


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bsp', type=Path)
    parser.add_argument('--before', type=Path, required=True, help='untouched input BSP for topology/unrelated ZIP preservation')
    parser.add_argument('--density', type=int, default=4)
    parser.add_argument('--diagnostics', type=Path, required=True, help='base path passed to the real dense bake diagnostics option')
    parser.add_argument('--control-diagnostics', type=Path, required=True, help='base path from a separate real density-one control bake')
    parser.add_argument('--raw-radiance', action='store_true', help='compare every float style/bump output on a no-macro/minlight, no-denoiser fixture')
    parser.add_argument('--require-bump', action='store_true')
    parser.add_argument('--require-displacement', action='store_true')
    parser.add_argument('--require-styles', action='store_true', help='require retained nonzero authored style slots')
    parser.add_argument('--require-zero-lights', action='store_true', help='require actual RGB-only enhanced modes')
    parser.add_argument('--require-sun', action='store_true')
    args = parser.parse_args()
    try:
        require(args.density > 1, 'acceptance needs actual higher density than the control')
        bsp = BSP(args.bsp)
        path, mm = manifest(bsp)
        data, modes = asset(bsp, path, mm, args.density)
        if args.require_bump:
            require(all(any(f[9] and f[8] & 1 for f in m['faces']) for m in modes), 'bumped planes were not exercised in both modes')
        if args.require_displacement:
            require(all(any(f[9] and f[8] & 2 for f in m['faces']) for m in modes), 'forward displacement sampling was not exercised in both modes')
        if args.require_styles:
            require(all(any(f[9] > 1 for f in m['faces']) for m in modes), 'multiple authored styles were not exercised in both modes')
        if args.require_zero_lights:
            require(all(m['lights'] == 0 and m['sun'] == -1 for m in mm), 'RGB-only zero-selected-light admission was not exercised')
        if args.require_sun:
            require(all(m['sun'] == 0 for m in mm), 'designated selected sun was not exercised in both modes')
        unchanged(BSP(args.before), bsp, path)
        comparisons = coarse_endpoints(bsp, data, modes, args.density)
        cells = diagnostics(args.diagnostics, args.control_diagnostics, modes, data, args.density, args.raw_radiance)
        print(f'PASS hlight density={args.density} modes={len(modes)} real-cells={cells} coarse-RGB-comparisons={comparisons} asset-bytes={len(data)} path={path}')
        return 0
    except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error) as error:
        print(f'FAIL hlight: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
