#!/usr/bin/env python3
"""Check real paired high-resolution BSP output without invoking a bake or generator.

The optional density-one control and dense diagnostics must be produced by the
real baker with -restir_shadowmap_diagnostics. --raw-radiance additionally requires
-restir_denoiser none and a fixture with no macro texture or entity _minlight.
This is the new forward-density contract; no inverse receiver tests execute.
Only hlight v4 / rshd v5 is admitted. Immutable local visibility, canonical
selected-light mapping and serialized prop/VHV domains are checked separately
from RGB pages; old enhanced assets must be rebaked.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import struct
import sys
import zlib
from shadowmap_contract_test import BSP as LegacyBSP, require, unpack, crc, rgbexp, quantum, worldlight_record

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
    require(game and game['version'] == 5, 'requires native rshd dictionary version 5 (rebake old assets)')
    raw = game['data']
    size, mask, asset_version, reserved = unpack('<4I', raw)
    require(size == len(raw) and mask == 3 and asset_version == 4 and not reserved, 'paired manifest header (v4 required; rebake)')
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
        selected_styles = set()
        selected = []
        local_styles = set()
        for li in range(lights):
            at = offset+li*116
            w = unpack('<9f3i7f3i', raw, at)
            source, angle, radius, start, end, cap, zero = unpack('<i5fI', raw, at+88)
            require(all(math.isfinite(x) for x in (*w[:9], *w[12:19], angle, radius, start, end, cap)), 'nonfinite selected light')
            require(all(x >= 0 for x in w[3:6]) and 0 <= w[11] < 64 and not w[19] and source >= -1 and not zero, 'selected light intensity/style/flags/provenance')
            selected_styles.add(w[11])
            require(all(x >= 0 for x in w[14:19]) and all(-1 <= x <= 1 for x in w[12:14]), 'selected light attenuation/cone range')
            if li == sun:
                require(w[10] == 3 and 0 <= angle < 90 and radius == start == end == cap == 0, 'selected sun record')
            else:
                require(w[10] in (1, 2) and radius >= 0 and angle == 0 and start >= 0 and cap > 0, 'selected local record')
                local_styles.add(w[11])
            record = worldlight_record(raw, at)
            record.update(source=source, shadow_radius=radius, fade_start=start, fade_end=end, cap=cap)
            selected.append(record)
        modes.append(dict(face=f, lighting=lighting, lighting_bytes=count, face_crc=face_crc,
                          lighting_crc=lighting_crc, sun=sun, lights=lights, asset=asset,
                          selected_styles=selected_styles, local_styles=local_styles, selected=selected,
                          selected_bytes=raw[offset:cursor], selected_crc=crc(raw[offset:cursor]),
                          receiver_styles={0} | {w['style'] for w in bsp.worldlights(mi)}))
    require(cursor == len(raw), 'trailing manifest bytes')
    return path, modes

def visibility_sections(data, cursor, sets_at, set_count, modes, manifests):
    """Parse immutable R8, world fallbacks, and inline v4 prop-direct blocks."""
    cursor = section(data, cursor, sets_at, set_count*128)
    require({m['visibility_set'] for m in modes} == set(range(set_count)), 'unreferenced visibility set')
    for si in range(set_count):
        record = unpack('<6IiI6Q2I4Q2I', data, sets_at+si*128)
        lights, selected_crc, face_count, entry_count, prop_count, mesh_count, sun, zero = record[:8]
        faces_at, entries_at, props_at, meshes_at, payload_at, payload_bytes = record[8:14]
        payload_crc, zero2, support_at, support_bytes = record[14:18]
        unbaked_faces_at, unbaked_indices_at, unbaked_face_count, unbaked_index_count = record[18:]
        owners = [m for m in modes if m['visibility_set'] == si]
        require(owners and all(len(m['faces']) == face_count for m in owners), 'shared visibility face domain')
        require(not (zero or zero2) and sun in (-1, 0) and
                (sun < 0 or lights > 0) and lights <= 8192, 'visibility identity/reserved')
        for owner in owners:
            mm = next(m for m in manifests if m['asset'] == modes.index(owner))
            require((lights, selected_crc, sun) == (mm['lights'], mm['selected_crc'], mm['sun']),
                    'visibility canonical selected manifest count/CRC/sun')
        cursor = section(data, cursor, faces_at, face_count*16)
        faces = [unpack('<4I', data, faces_at+i*16) for i in range(face_count)]
        cursor = section(data, cursor, entries_at, entry_count*16)
        entries = [unpack('<4I', data, entries_at+i*16) for i in range(entry_count)]
        cursor = section(data, cursor, props_at, prop_count*32)
        props = [unpack('<4I2Q', data, props_at+i*32) for i in range(prop_count)]
        cursor = section(data, cursor, meshes_at, mesh_count*32)
        meshes = [unpack('<8I', data, meshes_at+i*32) for i in range(mesh_count)]
        require(support_bytes == (face_count*lights+7)//8, 'complete visibility support bitset')
        cursor = section(data, cursor, support_at, support_bytes)
        support = data[support_at:support_at+support_bytes]
        bits = face_count*lights
        require(not bits % 8 or not support[-1] & (0xff << (bits % 8)), 'visibility support high-bit padding')
        cursor = section(data, cursor, unbaked_faces_at, unbaked_face_count*12)
        unbaked_faces = [unpack('<3I', data, unbaked_faces_at+i*12) for i in range(unbaked_face_count)]
        cursor = section(data, cursor, unbaked_indices_at, unbaked_index_count*4)
        unbaked_indices = [unpack('<I', data, unbaked_indices_at+i*4)[0] for i in range(unbaked_index_count)]
        unbaked_by_face = {}
        previous_face, next_index = -1, 0
        for ordinal, first, count in unbaked_faces:
            require(previous_face < ordinal < face_count and first == next_index and count > 0 and
                    first+count <= unbaked_index_count, 'canonical sparse unbaked face/index partition')
            indices = tuple(unbaked_indices[first:first+count])
            require(all(0 <= li < lights and li != sun for li in indices) and
                    all(a < b for a, b in zip(indices, indices[1:])), 'sorted unique unbaked selected locals (sun forbidden)')
            _, entry_first, face_entry_count, _ = faces[ordinal]
            supported = {e[0] for e in entries[entry_first:entry_first+face_entry_count]}
            for owner in owners:
                grid = owner['faces'][ordinal]
                require(grid[9] == 4 and grid[8] & 16, 'unbaked fallback requires full baked-direct face palette')
                mm = next(m for m in manifests if m['asset'] == modes.index(owner))
                local_records = [unpack('<2i', mm['selected_bytes'], li*116+40) for li in indices]
                require(all(kind in (1, 2) and style not in grid[10:10+grid[9]] for kind, style in local_records),
                        'unbaked local style must be absent from owning baked face')
            require(all(li in supported and support[(ordinal*lights+li)//8] & (1 << ((ordinal*lights+li) % 8))
                        for li in indices), 'unbaked local requires immutable visibility entry/support')
            unbaked_by_face[ordinal] = indices
            previous_face, next_index = ordinal, first+count
        require(next_index == unbaked_index_count, 'orphan unbaked selected-local indices')
        cursor = section(data, cursor, payload_at, payload_bytes)
        payload = data[payload_at:payload_at+payload_bytes]
        require(crc(payload) == payload_crc, 'visibility payload CRC')
        next_entry = next_mesh = payload_cursor = 0

        def entry_range(first, count, samples):
            nonlocal payload_cursor
            require(first == next_entry and first+count <= len(entries), 'visibility entry partition')
            previous = -1
            for light, encoding, offset, sample_count in entries[first:first+count]:
                require(previous < light < lights and light != sun and encoding <= 256,
                        'canonical local visibility ordering/encoding (sun never local)')
                previous = light
                if encoding < 256:
                    require(offset == sample_count == 0, 'uniform visibility has no payload')
                else:
                    aligned = (payload_cursor+3) & ~3
                    require(samples > 0 and sample_count == samples and offset == aligned and
                            offset+samples <= len(payload), 'dense visibility endpoint/vertex count/range')
                    require(not any(payload[payload_cursor:aligned]), 'dense visibility alignment padding')
                    payload_cursor = aligned+samples

        for fi, (ordinal, first, count, reserved) in enumerate(faces):
            require(ordinal == fi and not reserved, 'complete ordered visibility face directory')
            grid = owners[0]['faces'][fi]
            require(all(m['faces'][fi][6:8] == grid[6:8] for m in owners), 'shared visibility endpoint dimensions')
            entry_range(first, count, grid[6]*grid[7])
            required = [li for li in range(lights) if support[(fi*lights+li)//8] & (1 << ((fi*lights+li) % 8))]
            require(required == [e[0] for e in entries[first:first+count]] and sun not in required and
                    (not required or all(m['faces'][fi][9] for m in owners)),
                    'missing required/undeclared face visibility')
            for owner in owners:
                mm = next(m for m in manifests if m['asset'] == modes.index(owner))
                if owner['faces'][fi][9]:
                    require(all(li in required for li in range(lights) if li != sun and
                                unpack('<f', mm['selected_bytes'], li*116+60)[0] == 0),
                            'unbounded selected local visibility omitted')
            next_entry += count
        prop_direct = {}
        for pi, (ordinal, checksum, first, count, pose, reserved) in enumerate(props):
            require(ordinal == pi and first == next_mesh and first+count <= mesh_count and not reserved,
                    'visibility prop ordinal/mesh partition/identity')
            for mesh in range(first, first+count):
                ordinal, lod, vertices, begin, count_entries, vertex_crc, direct_at, direct_bytes = meshes[mesh]
                require(ordinal == mesh-first and vertices > 0, 'prop mesh ordinal/vertices')
                require(count_entries == lights-(sun >= 0), 'every local requires every prop mesh/LOD visibility')
                entry_range(begin, count_entries, vertices)
                require([e[0] for e in entries[begin:begin+count_entries]] == [li for li in range(lights) if li != sun],
                        'prop canonical local light mapping')
                next_entry += count_entries
                if not direct_at and not direct_bytes:
                    prop_direct[mesh] = None  # Full runtime direct; immutable R8 remains usable.
                    continue
                aligned = (payload_cursor+3) & ~3
                require(direct_at == aligned and direct_bytes >= 48 and direct_at+direct_bytes <= len(payload),
                        'inline prop direct offset/bytes/range')
                require(not any(payload[payload_cursor:aligned]), 'prop direct alignment padding')
                flags, slots, *fields = unpack('<12I', payload, direct_at)
                styles, vertex_count, planes, radiance_bytes, fallback_count, r0, r1 = (
                    tuple(fields[:4]), *fields[4:])
                require(flags == 1 and 1 <= slots <= 4 and vertex_count == vertices and planes == 2 and
                        not (r0 or r1), 'prop direct flags/style count/vertices/planes/reserved')
                palette = styles[:slots]
                require(palette[0] == 0 and len(set(palette)) == slots and all(s < 64 for s in palette) and
                        all(s == 255 for s in styles[slots:]), 'prop direct canonical style palette')
                require(radiance_bytes == slots*2*vertices*8 and direct_bytes == 48+radiance_bytes+fallback_count*4,
                        'prop direct exact block/radiance bytes')
                pixels = [unpack('<4e', payload, direct_at+48+i*8) for i in range(slots*2*vertices)]
                words = [unpack('<4H', payload, direct_at+48+i*8) for i in range(slots*2*vertices)]
                require(all(all((c & 0x8000) == 0 and (c & 0x7c00) != 0x7c00 for c in rgb[:3]) and
                            rgb[3] == 0 for rgb in words), 'prop direct finite nonnegative RGB and canonical zero alpha')
                fallback = tuple(unpack('<I', payload, direct_at+48+radiance_bytes+i*4)[0]
                                 for i in range(fallback_count))
                require(all(0 <= li < lights and li != sun for li in fallback) and
                        all(a < b for a, b in zip(fallback, fallback[1:])),
                        'prop direct sorted unique fallback locals')
                for owner in owners:
                    mm = next(m for m in manifests if m['asset'] == modes.index(owner))
                    require(not fallback or slots == 4, 'prop direct fallback with free palette')
                    require(all(unpack('<2i', mm['selected_bytes'], li*116+40)[0] in (1, 2) and
                                unpack('<2i', mm['selected_bytes'], li*116+40)[1] not in palette for li in fallback),
                            'prop direct fallback style already baked/nonlocal')
                prop_direct[mesh] = dict(flags=flags, styles=palette, vertices=vertices, planes=planes,
                                         radiance_bytes=radiance_bytes, pixels=pixels, unbaked=fallback)
                payload_cursor = direct_at+direct_bytes
            next_mesh += count
        require((next_entry, next_mesh, payload_cursor) == (entry_count, mesh_count, payload_bytes),
                'orphan visibility entries/mesh/payload')
        visibility = dict(index=si, lights=lights, sun=sun, selected_crc=selected_crc, faces=faces,
                          entries=entries, props=props, meshes=meshes, support=support,
                          unbaked_faces=unbaked_faces, unbaked_light_indices=unbaked_indices,
                          unbaked_by_face=unbaked_by_face, payload=payload, payload_crc=payload_crc,
                          prop_direct=prop_direct)
        for owner in owners:
            owner['visibility'] = visibility
    return cursor


def visibility_stats(modes):
    """Counts decoded R8 receiver samples, once per shared visibility set."""
    reports, seen = [], set()
    for mode in modes:
        v = mode['visibility']
        if v['index'] in seen:
            continue
        seen.add(v['index'])
        counts = {'world': Counter(), 'props': Counter()}
        uniform = dense = omitted = 0

        def count_entries(first, count, samples, domain):
            nonlocal uniform, dense
            for light, encoding, offset, size in v['entries'][first:first+count]:
                if encoding < 256:
                    uniform += 1
                    counts[domain][encoding] += samples
                else:
                    dense += 1
                    counts[domain].update(v['payload'][offset:offset+size])

        locals_count = v['lights']-(v['sun'] >= 0)
        for fi, (ordinal, first, count, reserved) in enumerate(v['faces']):
            face = mode['faces'][fi]
            count_entries(first, count, face[6]*face[7], 'world')
            if face[9]:
                omitted += locals_count-count
        for mesh in v['meshes']:
            count_entries(mesh[3], mesh[4], mesh[2], 'props')
        report = dict(set=v['index'], selected_lights=v['lights'], sun=v['sun'],
                      uniform_entries=uniform, dense_entries=dense, omitted_lit_face_locals=omitted,
                      props=len(v['props']), meshes=len(v['meshes']), lods=sorted({m[1] for m in v['meshes']}),
                      payload_bytes=len(v['payload']))
        for domain, values in counts.items():
            report[domain] = dict(blocked=values[0], open=values[255],
                                  fractional=sum(n for value, n in values.items() if 0 < value < 255))
        reports.append(report)
    return reports



def asset(bsp, path, manifests, density):
    require(path in bsp.pak, 'manifest asset missing from BSP pak')
    data = bsp.pak[path]
    h = unpack('<4IQ2IQ2IQ2I', data)
    magic, version, header, endian, size, actual_density, count, modes_offset, checksum, sets, sets_at, *reserved = h
    require((magic, version, header, endian) == (0x54494c48, 4, 64, 0x01020304) and size == len(data), 'asset header identity (v4 required; rebake)')
    require(actual_density == density and count == 2 and 0 < sets <= count and not any(reserved), 'asset density/mode/visibility count/reserved')
    require(checksum == crc(data[:40]+bytes(4)+data[44:]), 'whole-file CRC')
    cursor = section(data, 64, modes_offset, count*128)
    modes = []
    previous_pair = (-1, -1)
    for mi in range(count):
        r = unpack('<4I2Q4I5Q4IQ4I', data, modes_offset+mi*128)
        face, lighting, fv, lv, face_bytes, lighting_bytes, face_crc, lighting_crc, faces, models = r[:10]
        face_at, model_at, tile_at, page_at, identity_at, tiles, pages, identities, zero, zero64, visibility_set, *reserved = r[10:]
        require(visibility_set < sets and not any(reserved), 'mode visibility index/reserved')
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
            require(not flags & ~31 and not any(f[30:]) and bool(flags & 4) == bool(styles) and
                    bool(flags & 16) == bool(styles) and styles <= 4, 'v4 baked-direct face flags/styles/reserved')
            require(bool(flags & 8) == bool(styles and model_records[owner][3] & 1 and manifests[0 if lighting == 8 else 1]['sun'] >= 0), 'sun face eligibility')
            require(all(0 <= s < 64 for s in f[10:10+styles]) and len(set(f[10:10+styles])) == styles and
                    (not styles or f[10] == 0) and all(s == 255 for s in f[10+styles:14]), 'authored highres styles before pruning')
            authored = f[10:10+styles]
            mm = manifests[0 if lighting == 8 else 1]
            require(tuple(s for s in authored if s in ns[i]['styles']) == ns[i]['styles'], 'native styles are ordered highres subset')
            require(all(s in mm['receiver_styles'] or s in mm['local_styles'] for s in authored),
                    'highres style is neither native receiver nor selected local')
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
        mm = manifests[0 if lighting == 8 else 1]
        modes.append(dict(face=face, lighting=lighting, face_crc=face_crc, lighting_crc=lighting_crc,
                          lighting_bytes=lighting_bytes, faces=fs, native=ns, tiles=ts, pages=ps, models=model_records,
                          visibility_set=visibility_set, selected=mm['selected'], local_styles=mm['local_styles'],
                          selected_styles=mm['selected_styles'], receiver_styles=mm['receiver_styles']))
    cursor = visibility_sections(data, cursor, sets_at, sets, modes, manifests)
    require(cursor == len(data), 'trailing asset bytes')
    require(sorted(m['asset'] for m in manifests) == list(range(len(modes))), 'manifest/asset bijection')
    for mm in manifests:
        mode = modes[mm['asset']]
        require(all(mode[k] == mm[k] for k in ('face', 'lighting', 'lighting_bytes', 'face_crc', 'lighting_crc')), 'paired manifest asset identity')
        visibility = mode['visibility']
        if mm['lights'] > (mm['sun'] >= 0):
            require(len(visibility['props']) == bsp.static_dictionary()[1], 'missing static prop visibility domain')
        for prop in visibility['props']:
            ordinal, checksum, first, count, pose, reserved = prop
            name = f"sp_{'hdr_' if mode['lighting'] == 53 else ''}{ordinal}.vhv"
            if name not in bsp.pak:
                continue  # NO_PER_VERTEX_LIGHTING still requires its visibility block.
            raw = bsp.pak[name]
            version, vhv_checksum, flags, stride, vertices, meshes = unpack('<6I', raw)
            require(version == 2 and stride == 4 and vhv_checksum == checksum and
                    meshes == count and meshes <= (len(raw)-40)//28, 'prop visibility/VHV model checksum/mesh domain')
            total = 0
            for mesh, visibility_mesh in enumerate(visibility['meshes'][first:first+count]):
                lod, vertex_count, offset = unpack('<3I', raw, 40+mesh*28)
                require((lod, vertex_count) == visibility_mesh[1:3] and offset+vertex_count*stride <= len(raw),
                        'prop visibility all-LOD serialized hardware vertex mapping')
                total += vertex_count
            require(total == vertices, 'prop visibility/VHV total vertex coverage')
    return data, modes


def pixel(data, mode, face, style, plane, x, y):
    tile = mode['tiles'][face[14+style*4+plane]]
    page = mode['pages'][tile[3]]
    at = page[8]+((tile[5]+y)*page[0]+tile[4]+x)*8
    return unpack('<4e', data, at)

def mode_diagnostics(base, mode):
    suffix = '.ldr.json' if mode['lighting'] == 8 else '.hdr.json'
    d = json.loads(Path(str(base)+suffix).read_text())
    count = d['highGridCount']
    for key in ('localDirectPositions', 'luxelNormals', 'luxelBumpNormals'):
        require(len(d[key]) == count, 'missing/misaligned direct geometry ' + key)
    require(all(len(v) == 3 and all(math.isfinite(x) for x in v)
                for key in ('localDirectPositions', 'luxelNormals') for v in d[key]),
            'nonfinite direct receiver geometry')
    require(all(len(bases) == 3 and all(len(v) == 3 and all(math.isfinite(x) for x in v) for v in bases)
                for bases in d['luxelBumpNormals']), 'nonfinite runtime bump geometry')
    return d


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def local_radiance(light, position):
    """Independent reference for ShadowMap_StandardLightRadiance (unstyled)."""
    offset = tuple(a-b for a, b in zip(light['origin'], position))
    squared = dot(offset, offset)
    distance = math.sqrt(squared)
    hard_fade = light['fade_end'] > light['fade_start']
    if squared <= 0 or (light['radius'] > 0 and distance > light['radius']) or (
            hard_fade and distance > light['fade_end']):
        return (0, 0, 0), (0, 0, 0)
    direction = tuple(x/distance for x in offset)
    clamped = max(distance, 1)
    evaluation = min(clamped, light['cap']) if light['cap'] > 0 else clamped
    denominator = light['constant_attn'] + evaluation*light['linear_attn'] + evaluation**2*light['quadratic_attn']
    falloff = 1/denominator if denominator > 0 else 0
    if light['type'] == 2:
        cosine = -dot(direction, light['normal'])
        cone = 1
        if cosine <= light['stopdot']:
            span = light['stopdot']-light['stopdot2']
            cone = max(0, min(1, (cosine-light['stopdot2'])/span)) if span else 0
            if light['exponent'] not in (0, 1):
                cone **= light['exponent']
        falloff = 0 if cosine <= light['stopdot2'] else falloff*cosine*cone
    if hard_fade:
        t = 1-max(0, min(1, (clamped-light['fade_start'])/(light['fade_end']-light['fade_start'])))
        falloff *= t**3*(t*(t*6-15)+10)
    return tuple(x*falloff for x in light['intensity']), direction


def face_visibility(mode, ordinal, local):
    visibility = mode['visibility']
    _, first, count, _ = visibility['faces'][ordinal]
    return {light: (encoding if encoding < 256 else visibility['payload'][offset+local])/255
            for light, encoding, offset, samples in visibility['entries'][first:first+count]}


def local_direct_rgb(d, mode, face, light_index, plane, local):
    """One local's full diffuse term, independent of its baked/unbaked assignment."""
    light = mode['selected'][light_index]
    if light['type'] not in (1, 2):
        return (0, 0, 0)
    index = face['firstLuxel']+local
    normal = d['luxelNormals'][index] if plane == 0 else d['luxelBumpNormals'][index][plane-1]
    radiance, direction = local_radiance(light, d['localDirectPositions'][index])
    angular = max(0, min(1, dot(normal, direction)))  # Angular mode0 / one-hot mode2.
    if not angular or not any(radiance):
        return (0, 0, 0)
    visibility = face_visibility(mode, face['dface'], local)
    require(light_index in visibility, 'contributing local omitted from visibility')
    return tuple(value*angular*visibility[light_index] for value in radiance)


def baked_direct_rgb(d, mode, face, style, plane, local):
    """Compute baked RGB from geometry/R8, excluding this face's runtime fallbacks."""
    unbaked = mode['visibility']['unbaked_by_face'].get(face['dface'], ())
    result = [0.0]*3
    for light_index, light in enumerate(mode['selected']):
        if light_index in unbaked or light['type'] not in (1, 2) or light['style'] != style:
            continue
        for channel, value in enumerate(local_direct_rgb(d, mode, face, light_index, plane, local)):
            result[channel] += value
    return tuple(result)


def check_direct_partition(d, mode, face):
    """Derive admission from receiver slots and nonzero full direct, never from the sparse list."""
    disk = mode['faces'][face['dface']]
    receiver = tuple(style for style in face['styles'] if style in mode['receiver_styles'])
    palette, expected_unbaked, contributing = list(receiver), [], set()
    samples = face['luxelW']*face['luxelH']
    for li, light in enumerate(mode['selected']):
        if light['type'] not in (1, 2):
            continue
        if not any(any(local_direct_rgb(d, mode, face, li, plane, local))
                   for plane in range(face['numChannels']) for local in range(samples)):
            continue
        contributing.add(li)
        if light['style'] not in palette:
            if len(palette) == 4:
                expected_unbaked.append(li)
                continue
            palette.append(light['style'])
    actual_unbaked = mode['visibility']['unbaked_by_face'].get(face['dface'], ())
    require(actual_unbaked == tuple(expected_unbaked),
            'missing/extra unbaked selected-local IDs or lost nonzero selected direct')
    require(tuple(disk[10:10+disk[9]]) == tuple(palette),
            'highres palette differs from receiver plus canonical contributing local admission')
    require(not (set(actual_unbaked)-contributing), 'unbaked fallback has no nonzero full direct')
    return contributing, tuple(expected_unbaked)


def require_half_rgb(actual, expected, label):
    for channel, (a, b) in enumerate(zip(actual, expected)):
        # RGBA16F's half-ULP plus the f32 arithmetic/decimal diagnostic round trip.
        exponent = math.frexp(abs(b))[1] if b else -13
        half_quantum = max(2**-24, 2.0**(exponent-11))
        require(abs(a-b) <= half_quantum*.55 + abs(b)*2e-5 + 1e-8,
                f'{label}[{channel}]: {a} != {b}')



def coarse_endpoints(bsp, data, modes, density, diagnostic_base):
    comparisons = 0
    for mode in modes:
        raw = bsp.lumps[mode['lighting']]
        d = mode_diagnostics(diagnostic_base, mode)
        scene_faces = {f['dface']: f for f in d['faces']}
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
                            direct = baked_direct_rgb(d, mode, scene_faces[f[0]], style, plane, y*density*f[6]+x*density)
                            for a, b, addition in zip(stock, high, direct):
                                expected = a+addition
                                require(abs(expected-b) <= quantum(sample)+abs(b)*0.0005+abs(addition)*2e-5+1e-7,
                                        'coarse endpoint differs from native receiver plus exact baked local direct')
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


def diagnostics(base, control, modes, data, density, raw_radiance, require_source_overflow=False):
    cells = 0
    for mode in modes:
        suffix = '.ldr.json' if mode['lighting'] == 8 else '.hdr.json'
        d = mode_diagnostics(base, mode)
        require(d['hlightDensity'] == density and d['sampleCellCount'] == len(d['sampleCells']) and
                d['highGridCount'] == len(d['luxelPositions']), 'real dense sample diagnostics')
        require(abs(d['assetRGBScale']-1/255) < 1e-11, 'linear pre-conversion RGB normalization')
        c = json.loads(Path(str(control)+suffix).read_text()) if control else None
        if c:
            require(c['hlightDensity'] == 1 and d['sampleCellCount'] > c['sampleCellCount'], 'no actual independent sample-cell increase')
            old = {f['dface']: f for f in c['faces']}
        overflow_faces = 0
        retained_source_styles = set()
        fallback_styles = set()
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
            receiver_slots = [(slot, style) for slot, style in enumerate(f['styles']) if style in mode['receiver_styles']]
            receiver_styles = tuple(style for slot, style in receiver_slots)
            authored = disk[10:10+disk[9]]
            require(tuple(s for s in authored if s in receiver_styles) == receiver_styles and
                    f['numChannels'] == (4 if disk[8] & 1 else 1), 'native receiver style/bump data dropped before serialization')
            count = f['luxelW']*f['luxelH']
            _, unbaked = check_direct_partition(d, mode, f)
            fallback_styles.update(mode['selected'][li]['style'] for li in unbaked)
            direct = {(style, plane, local): baked_direct_rgb(d, mode, f, style, plane, local)
                      for style in mode['local_styles'] for plane in range(f['numChannels']) for local in range(count)}
            contributing = {style for (style, plane, local), rgb in direct.items() if any(rgb)}
            require(set(authored) == set(receiver_styles) | contributing,
                    'highres palette must retain exactly native receiver plus contributing local styles')
            for slot, style in enumerate(f['styles']):
                if style in mode['receiver_styles']:
                    continue
                require(style in mode['selected_styles'], 'omitted source style has no runtime direct light')
                first = f['firstOutput']+slot*f['numChannels']*f['luxelW']*f['luxelH']
                end = first+f['numChannels']*f['luxelW']*f['luxelH']
                require(all(not any(rgb) for rgb in d['receiverRadiance'][first:end]),
                        'runtime-only style removed nonzero receiver transport')
                if any(any(rgb) for rgb in d['sourceRadiance'][first:end]):
                    retained_source_styles.add(style)
            overflow_faces += f['numStyles'] > 4 and disk[9] <= 4
            for asset_slot, style in enumerate(authored):
                source_slot = f['styles'].index(style) if style in f['styles'] else None
                for plane in range(f['numChannels']):
                    for y in range(f['luxelH']):
                        for x in range(f['luxelW']):
                            local = y*f['luxelW']+x
                            high = pixel(data, mode, disk, asset_slot, plane, x, y)
                            if asset_slot == plane == 0 and disk[8] & 8:
                                expected = f32(d['sunVisibility'][f['firstLuxel']+local])
                                require(high[3] == struct.unpack('<e', struct.pack('<e', expected))[0], 'independent designated sun alpha changed')
                            if raw_radiance:
                                value = (0, 0, 0)
                                if source_slot is not None and d['luxelValid'][f['firstLuxel']+local]:
                                    at = f['firstOutput']+(source_slot*f['numChannels']+plane)*count+local
                                    value = d['receiverRadiance'][at]
                                addition = direct.get((style, plane, local), (0, 0, 0))
                                receiver = tuple(f32(f32(v)*f32(1/255)) for v in value)
                                if any(addition):
                                    require_half_rgb(high[:3], tuple(v+a for v, a in zip(receiver, addition)),
                                                     'dense receiver plus independent baked local RGB/style/bump')
                                else:
                                    expected = tuple(struct.unpack('<e', struct.pack('<e', v))[0] for v in receiver)
                                    require(high[:3] == expected, 'authored dense HDR RGB/style/bump output changed')
            cells += len(samples)
        if require_source_overflow:
            runtime_only = mode['selected_styles']-mode['receiver_styles']
            require(overflow_faces > 0 and retained_source_styles == runtime_only and fallback_styles == runtime_only,
                    'source overflow lost nonzero source transport or full runtime fallback style accounting')
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
    parser.add_argument('--require-source-overflow', action='store_true',
                        help='require >4 source styles, <=4 highres styles, full source transport and sparse runtime fallbacks')
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
        comparisons = coarse_endpoints(bsp, data, modes, args.density, args.diagnostics)
        cells = diagnostics(args.diagnostics, args.control_diagnostics, modes, data, args.density, args.raw_radiance,
                            args.require_source_overflow)
        print(f'PASS hlight density={args.density} modes={len(modes)} real-cells={cells} coarse-RGB-comparisons={comparisons} asset-bytes={len(data)} path={path}')
        print(json.dumps(dict(visibility=visibility_stats(modes)), sort_keys=True))
        return 0
    except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error) as error:
        print(f'FAIL hlight: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
