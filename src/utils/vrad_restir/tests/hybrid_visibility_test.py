#!/usr/bin/env python3
"""Direct production hlight v4 validator regressions plus explicitly labelled oracles.

Build hybrid_visibility_probe.cpp exactly like hlight_pak_metadata_probe.cpp:
from an x64 MSVC developer shell, with public/ include path and x64 tier1.lib,
tier0.lib, vstdlib.lib; /std:c++17 /EHsc /MT /DCOMPILER_MSVC /DCOMPILER_MSVC64
/DWIN32 /D_WIN32 /DWIN64 /D_WIN64 /DPLATFORM_64BITS. Then run:
  python utils/vrad_restir/tests/hybrid_visibility_test.py --probe <probe.exe>
The probe executes public/hlight_bsp.h, not a Python reimplementation. Generated
assets are isolated temporary byte fixtures, never shipping BSPs. Sparse v4
world/prop style-overflow ranges are exercised against real RGB pages and manifest styles.
No bake, engine launch, GPU execution or shader compilation is performed here.
Reference disk/alpha/caster fixtures are mathematical oracles, NOT GPU results.
Source-contract checks are reported separately and prove wiring only.
"""
from __future__ import annotations
import argparse
import copy
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from hlight_contract_test import GEOMETRY, MISSING, visibility_sections
from shadowmap_contract_test import require

ROOT = Path(__file__).resolve().parents[3]


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def seal(data):
    struct.pack_into('<Q', data, 16, len(data))
    struct.pack_into('<I', data, 40, 0)
    struct.pack_into('<I', data, 40, crc(data))


def light_record(index, sun, hdr=False, style=0):
    is_sun = index == sun
    world = struct.pack('<9f3i7f3i', 0, 0, 10, 1+(hdr and index == 2), 1, 1, 0, 0, -1,
                        0, 3 if is_sun else 1, style, 1, -1, 1, 0 if is_sun else 32, 1, 0, 0, 0, -1, -1)
    return world+struct.pack('<i5fI', 70+index, .27 if is_sun else 0,
                             4 if not is_sun and index == 2 else 0, 0, 0 if is_sun else -1,
                             0 if is_sun else 1, 0)


def fixture(*, shared=True, sun=0, locals_count=3, mode_count=2, world=False,
            lit_second=False, baked_styles=(0,), light_styles=None, unbaked_faces=(), unbaked_light_indices=(),
            prop_direct=False, prop_palette=(0, 32, 33, 34)):
    """Canonical v4 byte fixture, including inline prop direct and all prop LODs."""
    light_styles = light_styles or {}
    light_count = locals_count+(sun >= 0)
    local_indices = [i for i in range(light_count) if i != sun]
    set_count = 1 if shared else mode_count
    data = bytearray(64+mode_count*128)
    locations = {'modes': [], 'sets': []}
    parsed_modes, manifests = [], []
    selected = [b''.join(light_record(i, sun, not shared and mi == 1, light_styles.get(i, 0))
                         for i in range(light_count)) for mi in range(mode_count)]

    def section(raw):
        data.extend(bytes((-len(data)) % 16))
        at = len(data)
        data.extend(raw)
        return at

    for mi in range(mode_count):
        face_lump, lighting_lump = 7, (8, 53)[mi]
        lighting_bytes, lighting_crc = (4, 0x55667788) if world else (0, 0)
        identities = bytearray()
        for lump in sorted((*GEOMETRY, face_lump, lighting_lump)):
            length = 112 if lump == face_lump else lighting_bytes if lump == lighting_lump else 0
            checksum = 0x11223344 if lump == face_lump else lighting_crc if lump == lighting_lump else 0
            identities.extend(struct.pack('<IIQII', lump, 1 if lump in (face_lump, lighting_lump) else 0, length, checksum, 0))
        identity_at = section(identities)
        model_at = section(struct.pack('<4I12f', 0, 0, 2, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))
        faces, tiles = [], []
        next_x = 1
        for fi in range(2):
            lit = world and (fi == 0 or lit_second)
            styles = baked_styles if lit else ()
            face_tiles = [MISSING]*16
            width = 3 if fi == 0 else 1
            for slot in range(len(styles)):
                face_tiles[slot*4] = len(tiles)
                tiles.append((fi, slot, 0, 0, next_x, 1, width, 1))
                next_x += width+2
            faces.append((fi, 0, 0, 0, 1 if fi == 0 else 0, 0, width, 1,
                          (4 | 16 | (8 if sun >= 0 else 0)) if lit else 0, len(styles),
                          *styles, *([255]*(4-len(styles))), *face_tiles, 0, 0))
        faces_at = section(b''.join(struct.pack('<2I2i28I', *f) for f in faces))
        tiles_at = section(b''.join(struct.pack('<8I', *t) for t in tiles))
        pages_at = section(bytes(48) if world else b'')
        if world:
            pixels = bytearray(2048*2048*8)
            for _, slot, _, _, tx, ty, width, height in tiles:
                for y in range(ty-1, ty+height+1):
                    for x in range(tx-1, tx+width+1):
                        struct.pack_into('<4e', pixels, (y*2048+x)*8, .25, .5, .75, 0 if slot else 1)
            pixel_at = section(pixels)
            struct.pack_into('<8I2Q', data, pages_at, 2048, 2048, 10, 0, 0, len(tiles), 0, 0, pixel_at, len(pixels))
        mode_at = 64+mi*128
        struct.pack_into('<4I2Q4I5Q4IQ4I', data, mode_at,
                         face_lump, lighting_lump, 1, 1, 112, lighting_bytes,
                         0x11223344, lighting_crc, 2, 1, faces_at, model_at, tiles_at, pages_at, identity_at,
                         len(tiles), int(world), 14, 0, 0, 0 if shared else mi, 0, 0, 0)
        locations['modes'].append(dict(record=mode_at, faces=faces_at, pages=pages_at,
                                      pixels=pixel_at if world else None))
        parsed_modes.append(dict(visibility_set=0 if shared else mi, faces=faces))
        manifests.append(dict(asset=mi, lights=light_count, sun=sun, selected_crc=crc(selected[mi]), selected_bytes=selected[mi]))

    sets_at = section(bytes(128*set_count))
    for si in range(set_count):
        entries, payload = [], bytearray()

        def add(light, value):
            if isinstance(value, int):
                entries.append((light, value, 0, 0))
            else:
                payload.extend(bytes((-len(payload)) % 4))
                entries.append((light, 256, len(payload), len(value)))
                payload.extend(value)

        support = bytearray((2*light_count+7)//8)
        visibility_faces = []
        for fi, face in enumerate(parsed_modes[0 if shared else si]['faces']):
            first = len(entries)
            if face[9]:
                for li in local_indices[:2]:
                    bit = fi*light_count+li
                    support[bit//8] |= 1 << (bit % 8)
                    add(li, 0 if li == local_indices[0] else bytes([0, 128, 255][:face[6]*face[7]]))
            visibility_faces.append((fi, first, len(entries)-first, 0))
        face_entries = len(entries)
        meshes, direct_locations = [], []
        for lod, vertices in enumerate((3, 2, 1)):
            first = len(entries)
            for j, li in enumerate(local_indices):
                add(li, bytes((17+lod*60+v*40) % 256 for v in range(vertices)) if j == 1 else (0, 128, 255)[(j+lod) % 3])
            direct_at = direct_bytes = 0
            if prop_direct:
                payload.extend(bytes((-len(payload)) % 4))
                direct_at = len(payload)
                palette = prop_palette
                fallback = tuple(li for li in local_indices if light_styles.get(li, 0) not in palette)
                radiance_bytes = len(palette)*2*vertices*8
                payload.extend(struct.pack('<12I', 1, len(palette), *palette, *([255]*(4-len(palette))),
                                           vertices, 2, radiance_bytes, len(fallback), 0, 0))
                for slot in range(len(palette)):
                    for plane in range(2):
                        for vertex in range(vertices):
                            payload.extend(struct.pack('<4e', .25*(slot+1), .5*(plane+1), .125*(vertex+1), 0))
                payload.extend(b''.join(struct.pack('<I', li) for li in fallback))
                direct_bytes = len(payload)-direct_at
                direct_locations.append(dict(offset=direct_at, bytes=direct_bytes, radiance=radiance_bytes,
                                             fallback=direct_at+48+radiance_bytes))
            meshes.append((lod, lod, vertices, first, len(local_indices),
                           crc(struct.pack('<'+'I'*vertices, *range(vertices))), direct_at, direct_bytes))
        faces_at = section(b''.join(struct.pack('<4I', *f) for f in visibility_faces))
        entries_at = section(b''.join(struct.pack('<4I', *e) for e in entries))
        props_at = section(struct.pack('<4I2Q', 0, 0x12345678, 0, len(meshes), 0x0102030405060708, 0))
        meshes_at = section(b''.join(struct.pack('<8I', *m) for m in meshes))
        support_at = section(support)
        unbaked_faces_at = section(b''.join(struct.pack('<3I', *f) for f in unbaked_faces))
        unbaked_indices_at = section(b''.join(struct.pack('<I', li) for li in unbaked_light_indices))
        payload_at = section(payload)
        record_at = sets_at+si*128
        struct.pack_into('<6IiI6Q2I4Q2I', data, record_at, light_count, crc(selected[0 if shared else si]),
                         2, len(entries), 1, len(meshes), sun, 0,
                         faces_at, entries_at, props_at, meshes_at, payload_at, len(payload), crc(payload), 0,
                         support_at, len(support), unbaked_faces_at, unbaked_indices_at,
                         len(unbaked_faces), len(unbaked_light_indices))
        locations['sets'].append(dict(record=record_at, faces=faces_at, entries=entries_at,
                                      props=props_at, meshes=meshes_at, support=support_at,
                                      unbaked_faces=unbaked_faces_at, unbaked_light_indices=unbaked_indices_at,
                                      payload=payload_at, payload_bytes=len(payload), face_entries=face_entries,
                                      direct=direct_locations))
    struct.pack_into('<4IQ2IQ2IQ2I', data, 0, 0x54494c48, 4, 64, 0x01020304, len(data),
                     2, mode_count, 64, 0, set_count, sets_at, 0, 0)
    seal(data)
    manifest = bytearray(400)
    mask = (1 << mode_count)-1
    struct.pack_into('<4I', manifest, 0, 400+sum(map(len, selected)), mask, 4, 0)
    path = b'maps/hybrid_fixture.hlight\0'
    manifest[144:144+len(path)] = path
    cursor = 400
    for mi in range(2):
        if mi < mode_count:
            struct.pack_into('<9Ii6I', manifest, 16+mi*64, 7, (8, 53)[mi], (15, 54)[mi],
                             4 if world else 0, 0x55667788 if world else 0, 0x11223344, 0,
                             cursor, light_count, sun, mi, *([0]*5))
            cursor += len(selected[mi])
        else:
            struct.pack_into('<i', manifest, 16+mi*64+36, -1)
    for raw in selected:
        manifest.extend(raw)
    return dict(data=data, manifest=manifest, flags=mask << 18, locations=locations,
                modes=parsed_modes, manifests=manifests, sets_at=sets_at, set_count=set_count)


def reference_disk(radius):
    # Perpendicular basis for receiver=(0,0,0), source=(0,0,10): tangent +X, bitangent +Y.
    if radius == 0:
        return [(0.0, 0.0, 10.0)]
    result = []
    for i in range(32):
        reverse5 = int(f'{i:05b}'[::-1], 2)
        r = radius*math.sqrt((i+.5)/32)
        angle = 2*math.pi*(reverse5+.5)/32
        result.append((r*math.cos(angle), r*math.sin(angle), 10.0))
    return result


def reference_visibility(radius, walls, *, skip=None):
    """Independent finite-segment rectangle/R8 oracle, not a production ray tracer."""
    targets = reference_disk(radius)
    visible = 0
    for x, y, z in targets:
        blocked = False
        length = math.sqrt(x*x+y*y+z*z)
        for wall in walls:
            if not wall.get('immutable', True) or wall.get('no_shadow', False) or wall.get('id') == skip and skip is not None:
                continue
            t = wall['z']/z
            if not .03125/length < t < 1-.03125/length:
                continue
            px, py = t*x, t*y
            if not wall.get('xmin', -100) <= px <= wall.get('xmax', 100) or not -100 <= py <= 100:
                continue
            coverage = wall.get('coverage', (255,))
            uv = (px/4+.5) % 1
            alpha = coverage[min(len(coverage)-1, math.floor(uv*len(coverage)))]
            if alpha >= 128:
                blocked = True
                break
        visible += not blocked
    return visible/len(targets)


def reference_checks():
    require(len(reference_disk(0)) == 1 and len(reference_disk(4)) == 32, 'R=0/positive reference ray counts')
    require(reference_disk(4) == reference_disk(4), 'disk reference determinism')
    require(all(math.hypot(x, y) < 4 and z == 10 for x, y, z in reference_disk(4)), 'disk plane/radius')
    require(reference_visibility(0, []) == reference_visibility(4, []) == 1, 'unoccluded reference')
    require(reference_visibility(0, [{'z': 5}]) == reference_visibility(4, [{'z': 5}]) == 0, 'opaque reference')
    require(reference_visibility(4, [{'z': 5, 'xmax': 0}]) == .5, 'stratified half disk reference')
    require(reference_visibility(4, [{'z': 5, 'coverage': (0, 255)}]) == .5, 'binary alpha reference')
    require(reference_visibility(0, [{'z': 5, 'coverage': (127,)}]) == 1 and
            reference_visibility(0, [{'z': 5, 'coverage': (128,)}]) == 0, 'R8 threshold reference')
    require(reference_visibility(0, [{'z': 20}]) == reference_visibility(4, [{'z': 20}]) == 1, 'finite segment reference')
    for name in ('movable_brush', 'dynamic_prop'):
        require(reference_visibility(4, [{'z': 5, 'immutable': False, 'class': name}]) == 1, 'immutable caster reference '+name)
    for name in ('world', 'displacement', 'static_prop'):
        require(reference_visibility(4, [{'z': 5, 'class': name}]) == 0, 'immutable caster reference '+name)
    require(reference_visibility(4, [{'z': 5, 'no_shadow': True}]) == 1, 'NO_SHADOW reference')
    require(reference_visibility(4, [{'z': 5, 'id': 7}], skip=7) == 1 and
            reference_visibility(4, [{'z': 5, 'id': 8}], skip=7) == 0, 'NO_SELF_SHADOW reference')
    require([math.floor(255*v+.5) for v in (0, .5, 1)] == [0, 128, 255], 'R8 quantization reference')


def source_contract_checks():
    def source(relative):
        raw = (ROOT/relative).read_text()
        return ' '.join(raw.split())
    kernel = source('utils/vrad_restir/shaders/restir_local_visibility.comp')
    for token in ('bitfieldReverse( sampleIndex ) >> 27u', 'radius * sqrt( u )',
                  'source.w == 0.0 ? 1u : 32u', 'segmentLength - RESTIR_DIST_EPSILON',
                  'RESTIR_RAY_MASK_STATIC_SUN, skipHitId', 'source.w < 0.0',
                  'query.selectedLightIndex = pc.pass', 'sunVisibilityOrigins[luxel]',
                  'query.flags & RESTIR_VISIBILITY_NO_SELF_SHADOW'):
        require(token in kernel, 'source-contract-only kernel wiring: '+token)
    require('radiance' not in kernel[kernel.index('float ComputeVisibility'):kernel.index('void main')],
            'source-contract-only kernel must not evaluate radiance')
    samples = source('utils/vrad_restir/restir_scene_samples.cpp')
    require('context.options->shadowMaps && scene.shadowLights.Count() != 0' in samples,
            'source-contract-only no-sun local receiver creation')
    trace = source('utils/vrad_restir/shaders/restir_trace.glsl')
    for token in ('( triangle.flags & mask ) == 0u', 'triangle.hitId == skipHitId',
                  'fract( uv )', 'texel, 0 ).r < 0.5', 'TriangleAccepts( triangleIndex, barycentrics, direction, mask, skipHitId, frontFaceOnly )'):
        require(token in trace, 'source-contract-only alpha/caster filter: '+token)
    props = source('utils/vrad_restir/restir_staticprops.cpp')
    for token in ('STATIC_PROP_NO_SHADOW', 'STATIC_PROP_NO_SELF_SHADOWING',
                  'RESTIR_TRI_STATIC_SUN', 'device.ComputeLocalVisibility', 'vq.selectedLightIndex'):
        # Assignment lives either in the per-vertex query or the later per-light batch.
        if token == 'vq.selectedLightIndex':
            require('visibilityQueries[vi].selectedLightIndex = localLightIndices[li]' in props or token in props,
                    'source-contract-only prop canonical light mapping')
        else:
            require(token in props, 'source-contract-only static prop semantics: '+token)
    world = source('utils/vrad_restir/restir_scene.cpp')
    require('model == 0 ? RESTIR_TRI_STATIC_SUN : 0' in world and
            'context.faceModels[faceIndex] == 0 ? RESTIR_TRI_STATIC_SUN : 0' in world,
            'source-contract-only movable brush/displacement exclusion')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--dll-dir', type=Path, action='append', default=[])
    parser.add_argument('--report', type=Path)
    options = parser.parse_args()
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p) for p in [ROOT/'../game/bin/x64', *options.dll_dir])+os.pathsep+env['PATH']
    results = []
    with tempfile.TemporaryDirectory(prefix='source-hybrid-visibility-') as temporary:
        folder = Path(temporary)

        def run(name, f, expected=(True, True, True), reason='', inspect=None, flags=None):
            try:
                (folder/'fixture.hlight').write_bytes(f['data'])
                (folder/'manifest.bin').write_bytes(f['manifest'])
                result = subprocess.run([str(options.probe.resolve()), str(folder/'fixture.hlight'),
                                         str(folder/'manifest.bin'), hex(f['flags'] if flags is None else flags)],
                                        env=env, capture_output=True, text=True)
                require(result.returncode == 0, 'probe exit '+str(result.returncode)+': '+result.stderr)
                observed = json.loads(result.stdout)
                require(tuple(bool(observed[k]) for k in ('file_valid', 'manifest_valid', 'pair_valid')) == expected,
                        'wrong admission: '+result.stdout)
                if not expected[0]:
                    require(observed['file_cleared'], 'failed file validation retained borrowed pointers')
                if not expected[1]:
                    require(observed['manifest_cleared'], 'failed manifest validation retained borrowed pointers')
                require(not reason or reason in ' '.join(observed[k] for k in ('file_error', 'manifest_error', 'pair_error')),
                        'wrong failure path: '+result.stdout)
                if inspect:
                    inspect(observed)
                row = dict(case=name, scope='production shared validator', passed=True)
            except (AssertionError, OSError, ValueError, KeyError) as error:
                row = dict(case=name, scope='production shared validator', passed=False, error=str(error))
            results.append(row)
            print(json.dumps(row))

        def inspect_fixture(f):
            def inspect(observed):
                modes = copy.deepcopy(f['modes'])
                end = visibility_sections(f['data'], f['sets_at'], f['sets_at'], f['set_count'], modes, f['manifests'])
                require(end == len(f['data']), 'Python visibility parser lost canonical endpoint')
                require(len(observed['sets']) == f['set_count'], 'shared/nonshared set count')
                for actual, parsed in zip(observed['sets'], [next(m['visibility'] for m in modes if m['visibility_set'] == si)
                                                          for si in range(f['set_count'])]):
                    require(actual['sun'] == parsed['sun'], 'canonical sun index')
                    require([(r['face'], r['first'], r['count']) for r in actual['unbaked_faces']] ==
                            parsed['unbaked_faces'], 'production sparse fallback face ranges')
                    require(actual['unbaked_light_indices'] == parsed['unbaked_light_indices'],
                            'production sparse fallback selected-local indices')
                    require([m['lod'] for m in actual['props'][0]['meshes']] == [0, 1, 2], 'serialized prop all LODs')
                    for mesh in actual['props'][0]['meshes']:
                        require(mesh['vertex_crc'] == crc(struct.pack('<'+'I'*mesh['vertices'], *range(mesh['vertices']))),
                                'uint32 serialized vertex-order CRC identity')
                    for mesh_index, mesh in enumerate(actual['props'][0]['meshes']):
                        direct = parsed['prop_direct'][mesh_index]
                        require((mesh['direct'] is None) == (direct is None), 'GetPropDirect absent/full-runtime admission')
                        if direct is not None:
                            actual_direct = mesh['direct']
                            require(actual_direct['styles'] == list(direct['styles']) and
                                    actual_direct['vertices'] == direct['vertices'] and
                                    actual_direct['planes'] == 2 and actual_direct['flags'] == 1 and
                                    actual_direct['unbaked'] == list(direct['unbaked']) and
                                    actual_direct['pixels'] == [list(p) for p in direct['pixels']],
                                    'production GetPropDirect metadata/pixels/fallback decode')
                    require(actual['props'][0]['checksum'] == 0x12345678 and
                            actual['props'][0]['pose'] == 0x0102030405060708, 'prop model/pose identity')
                    groups = [*actual['faces'], *(m['entries'] for m in actual['props'][0]['meshes'])]
                    decoded = [e for group in groups for e in group]
                    require([e['light'] for e in decoded] == [e[0] for e in parsed['entries']], 'canonical light IDs')
                    for actual_entry, entry in zip(decoded, parsed['entries']):
                        expected_bytes = list(parsed['payload'][entry[2]:entry[2]+entry[3]]) if entry[1] == 256 else [entry[1]]
                        require(len(actual_entry['values']) == len(expected_bytes) and
                                all(abs(a-b/255) < 1e-7 for a, b in zip(actual_entry['values'], expected_bytes)),
                                'production uniform/dense R8 decode')
            return inspect

        base = fixture()
        run('paired_shared_sun_props_all_lods', base, inspect=inspect_fixture(base))
        for name, kwargs in [('paired_nonshared', dict(shared=False)), ('no_sun_locals', dict(sun=-1)),
                             ('zero_selected_lights', dict(sun=-1, locals_count=0)), ('sun_only', dict(locals_count=0))]:
            f = fixture(**kwargs)
            run(name, f, inspect=inspect_fixture(f))
        for name, kwargs in [('prop_direct_nonshared', dict(shared=False, locals_count=6,
                              light_styles={1: 0, 2: 32, 3: 33, 4: 34, 5: 35, 6: 36})),
                             ('prop_direct_no_sun', dict(sun=-1, locals_count=5,
                              light_styles={0: 0, 1: 32, 2: 33, 3: 34, 4: 35}))]:
            positive = fixture(prop_direct=True, **kwargs)
            run(name, positive, inspect=inspect_fixture(positive))
        f = fixture(prop_direct=True, locals_count=6,
                    light_styles={1: 0, 2: 32, 3: 33, 4: 34, 5: 35, 6: 36})
        run('prop_direct_positive_all_lods', f, inspect=inspect_fixture(f))
        ds = f['locations']['sets'][0]
        block = ds['direct'][0]
        direct_at = ds['payload']+block['offset']
        fallback_at = ds['payload']+block['fallback']
        direct_mutations = [
            ('offset', ds['meshes']+24, '<I', block['offset']+4, False),
            ('bytes', ds['meshes']+28, '<I', block['bytes']-4, False),
            ('absent_offset_only', ds['meshes']+24, '<I', 0, False),
            ('absent_bytes_only', ds['meshes']+28, '<I', 0, False),
            ('flags', direct_at, '<I', 2, False),
            ('flags_missing', direct_at, '<I', 0, False),
            ('vertices', direct_at+24, '<I', 4, False),
            ('planes', direct_at+28, '<I', 1, False),
            ('radiance_bytes', direct_at+32, '<I', block['radiance']-8, False),
            ('fallback_count_bytes_mismatch', direct_at+36, '<I', 0xffffffff, False),
            ('style_count_zero', direct_at+4, '<I', 0, False),
            ('style_count_five', direct_at+4, '<I', 5, False),
            ('style_zero_required', direct_at+8, '<I', 1, False),
            ('style_duplicate', direct_at+12, '<I', 0, False),
            ('style_range', direct_at+12, '<I', 64, False),
            ('alpha', direct_at+48+6, '<H', 0x3c00, False),
            ('alpha_negative_zero', direct_at+48+6, '<H', 0x8000, False),
            ('negative_rgb', direct_at+48, '<H', 0xbc00, False),
            ('negative_zero_rgb', direct_at+48, '<H', 0x8000, False),
            ('nonfinite_rgb', direct_at+48, '<H', 0x7c00, False),
            ('nan_rgb', direct_at+48, '<H', 0x7e00, False),
            ('reserved_0', direct_at+40, '<I', 1, False),
            ('reserved_1', direct_at+44, '<I', 1, False),
            ('fallback_sun', fallback_at, '<I', 0, False),
            ('fallback_order', fallback_at+4, '<I', 5, False),
            ('fallback_range', fallback_at+4, '<I', 7, False),
            ('fallback_style_present', fallback_at, '<I', 4, True),
        ]
        for label, at, fmt, value, pair_only in direct_mutations:
            bad = dict(f, data=bytearray(f['data']))
            struct.pack_into(fmt, bad['data'], at, value)
            struct.pack_into('<I', bad['data'], ds['record']+80,
                             crc(bad['data'][ds['payload']:ds['payload']+ds['payload_bytes']]))
            seal(bad['data'])
            run('prop_direct_'+label, bad, (True, True, False) if pair_only else (False, True, False))
        bad = fixture(prop_direct=True, prop_palette=(0, 32, 33), locals_count=6,
                      light_styles={1: 0, 2: 32, 3: 33, 4: 34, 5: 35, 6: 36})
        run('prop_direct_free_palette', bad, (False, True, False), 'prop direct fallback with free style slot')
        bad = fixture(prop_direct=True, prop_palette=(0,), locals_count=1)
        bs = bad['locations']['sets'][0]
        struct.pack_into('<I', bad['data'], bs['payload']+bs['direct'][0]['offset']+12, 32)
        struct.pack_into('<I', bad['data'], bs['record']+80,
                         crc(bad['data'][bs['payload']:bs['payload']+bs['payload_bytes']]))
        seal(bad['data'])
        run('prop_direct_unused_style', bad, (False, True, False), 'prop direct unused style')
        f = fixture(locals_count=2)
        f['data'][f['locations']['sets'][0]['support']] = 0x80
        seal(f['data'])
        run('support_padding', f, (False, True, False), 'support padding')
        s = base['locations']['sets'][0]
        mutations = [
            ('old_hlight_v1', 4, '<I', 1, 'header/version'),
            ('old_hlight_v2', 4, '<I', 2, 'header/version'),
            ('old_hlight_v3', 4, '<I', 3, 'header/version'),
            ('missing_visibility_sets', 44, '<I', 0, 'header/version'),
            ('invalid_mode_set', 64+112, '<I', 1, 'mode identity'),
            ('overflow_set_offset', 48, '<Q', 0xfffffffffffffff0, 'visibility set section'),
            ('fallback_face_section_offset', s['record']+104, '<Q', 1, 'visibility section'),
            ('fallback_face_count_out_of_domain', s['record']+120, '<I', 3, 'visibility identity'),
            ('bad_sun_index', s['record']+24, '<i', 1, 'visibility identity'),
            ('incomplete_support_bytes', s['record']+96, '<Q', 0, 'visibility identity'),
            ('undeclared_required_unlit', s['support'], '<B', 2, 'incomplete required'),
            ('required_sun', s['support'], '<B', 1, 'incomplete required'),
            ('face_ordinal_gap', s['faces']+16, '<I', 3, 'face partition'),
            ('face_entry_prefix', s['faces']+16+4, '<I', 1, 'face partition'),
            ('duplicate_prop_local', s['entries']+16, '<I', 1, 'prop mesh visibility'),
            ('sun_as_local_entry', s['entries'], '<I', 0, 'prop mesh visibility'),
            ('gpu_or_worldlight_index_not_canonical', s['entries'], '<I', 77, 'prop mesh visibility'),
            ('unknown_encoding', s['entries']+4, '<I', 257, 'prop mesh visibility'),
            ('uniform_has_payload', s['entries']+12, '<I', 1, 'prop mesh visibility'),
            ('dense_wrong_vertex_count', s['entries']+16+12, '<I', 4, 'prop mesh visibility'),
            ('dense_unaligned_offset', s['entries']+16+8, '<I', 1, 'prop mesh visibility'),
            ('dense_overflow_offset', s['entries']+16+8, '<I', 0xffffffff, 'prop mesh visibility'),
            ('prop_ordinal_gap', s['props'], '<I', 1, 'prop partition'),
            ('prop_mesh_prefix', s['props']+8, '<I', 1, 'prop partition'),
            ('prop_reserved', s['props']+24, '<Q', 1, 'prop partition'),
            ('missing_lod_local_entry', s['meshes']+32+16, '<I', 2, 'prop mesh visibility'),
            ('mesh_ordinal_gap', s['meshes']+32, '<I', 7, 'prop mesh visibility'),
            ('mesh_zero_vertices', s['meshes']+8, '<I', 0, 'prop mesh visibility'),
            ('mesh_direct_offset_without_bytes', s['meshes']+24, '<I', 1, 'incomplete prop mesh visibility'),
            ('payload_crc_corrupt', s['payload'], '<B', 99, 'payload CRC'),
            ('manifest_selected_crc', s['record']+4, '<I', 0, 'visibility/manifest selected lights'),
        ]
        for name, at, fmt, value, reason in mutations:
            f = dict(base, data=bytearray(base['data']))
            struct.pack_into(fmt, f['data'], at, value)
            seal(f['data'])
            pair_only = name == 'manifest_selected_crc'
            run(name, f, (True, True, False) if pair_only else (False, True, False), reason)
        for name, at, fmt, value in [('old_manifest_asset_v1', 8, '<I', 1),
                                     ('old_manifest_asset_v2', 8, '<I', 2),
                                     ('old_manifest_asset_v3', 8, '<I', 3),
                                     ('manifest_wrong_sun', 16+36, '<i', -1),
                                     ('manifest_out_of_range_lights', 16+32, '<I', 8193)]:
            f = dict(base, manifest=bytearray(base['manifest']))
            struct.pack_into(fmt, f['manifest'], at, value)
            run(name, f, (True, False, False))
        run('manifest_level_flag_mismatch', base, (True, False, False), flags=1 << 18)
        f = dict(base, data=bytearray(base['data']))
        f['data'][s['payload']] ^= 1
        run('whole_file_crc_corrupt', f, (False, True, False), 'header/version')
        f = dict(base, data=bytearray(base['data'][:-1]))
        seal(f['data'])
        run('truncated_dense_payload', f, (False, True, False), 'visibility section')
        f = dict(base, data=bytearray(base['data'])+b'\0')
        seal(f['data'])
        run('trailing_file_byte', f, (False, True, False), 'trailing file')
        # Nonzero inter-plane padding must fail even after both CRCs are repaired.
        f = dict(base, data=bytearray(base['data']))
        f['data'][s['payload']+3] = 1
        struct.pack_into('<I', f['data'], s['record']+80, crc(f['data'][s['payload']:s['payload']+s['payload_bytes']]))
        seal(f['data'])
        run('dense_nonzero_alignment_padding', f, (False, True, False), 'prop mesh visibility')
        f = fixture(shared=False)
        second = f['locations']['sets'][1]
        struct.pack_into('<I', f['data'], second['record']+4, crc(light_record(0, 0)))
        seal(f['data'])
        run('paired_nonshared_wrong_selected_crc', f, (True, True, False), 'visibility/manifest selected lights')
        f = dict(base, manifest=bytearray(base['manifest']))
        hdr_lights = struct.unpack_from('<I', f['manifest'], 16+64+28)[0]
        struct.pack_into('<f', f['manifest'], hdr_lights+2*116+12, 2)
        run('paired_shared_hdr_manifest_changed', f, (True, True, False), 'visibility/manifest selected lights')
        f = fixture(shared=False)
        struct.pack_into('<I', f['data'], 64+128+112, 0)
        seal(f['data'])
        run('unreferenced_visibility_set', f, (False, True, False), 'visibility identity')
        # A real RGB page is necessary to admit required world visibility. Keep
        # the many prop corruption cases above small; only these need 32 MiB.
        f = fixture(mode_count=1, world=True)
        run('world_uniform_dense_omitted_support', f, inspect=inspect_fixture(f))
        s = f['locations']['sets'][0]
        for name, at, fmt, value in [
            ('missing_required_world_entry', s['faces']+8, '<I', 1),
            ('undeclared_world_entry', s['support'], '<B', 2),
            ('world_dense_wrong_endpoint_count', s['entries']+16+12, '<I', 2),
            ('world_omitted_local_now_required', s['support'], '<B', 14),
            ('world_missing_baked_direct', f['locations']['modes'][0]['faces']+32, '<I', 12),
        ]:
            damaged = dict(f, data=bytearray(f['data']))
            struct.pack_into(fmt, damaged['data'], at, value)
            seal(damaged['data'])
            run(name, damaged, (False, True, False))
        damaged = dict(f, data=bytearray(f['data']), manifest=bytearray(f['manifest']))
        # The omitted third local is now unbounded. Repair the selected CRC to
        # prove admission rejects omission, rather than merely mismatched bytes.
        struct.pack_into('<f', damaged['manifest'], 400+3*116+60, 0)
        struct.pack_into('<I', damaged['data'], s['record']+4, crc(damaged['manifest'][400:]))
        seal(damaged['data'])
        run('unbounded_world_local_cannot_be_omitted', damaged, (True, True, False), 'unbounded local visibility omitted')
        palette = (0, 32, 33, 34)
        fallback = fixture(mode_count=1, world=True, baked_styles=palette, light_styles={2: 35},
                           unbaked_faces=((0, 0, 1),), unbaked_light_indices=(2,))

        def inspect_fallback(observed):
            inspect_fixture(fallback)(observed)
            view = observed['sets'][0]
            require(view['unbaked_faces'] == [dict(face=0, first=0, count=1)] and
                    view['unbaked_light_indices'] == [2], 'one production world fallback local')
            style = struct.unpack_from('<i', fallback['manifest'], 400+2*116+44)[0]
            face = fallback['modes'][0]['faces'][0]
            require(face[9] == 4 and style == 35 and style not in face[10:10+face[9]],
                    'fallback Source style must overflow the complete four-slot baked palette')
            pixel = fallback['locations']['modes'][0]['pixels']+(2048+1)*8
            require(struct.unpack_from('<4e', fallback['data'], pixel) == (.25, .5, .75, 1),
                    'fallback fixture must carry real nonzero RGB')
            require(view['faces'][0][1]['light'] == 2 and view['faces'][0][1]['encoding'] == 256,
                    'unbaked local must retain production dense visibility')

        run('world_sparse_unbaked_style_absent_from_palette', fallback, inspect=inspect_fallback)
        s = fallback['locations']['sets'][0]
        for name, at, fmt, value, reason in [
            ('fallback_face_out_of_range', s['unbaked_faces'], '<I', 2, 'unbaked face partition'),
            ('fallback_unlit_face', s['unbaked_faces'], '<I', 1, 'unbaked face partition'),
            ('fallback_zero_count', s['unbaked_faces']+8, '<I', 0, 'unbaked face partition'),
            ('fallback_count_exceeds_indices', s['unbaked_faces']+8, '<I', 2, 'unbaked face partition'),
            ('fallback_bad_first_prefix', s['unbaked_faces']+4, '<I', 1, 'unbaked face partition'),
            ('fallback_index_count_out_of_domain', s['record']+124, '<I', 9, 'visibility identity'),
            ('fallback_index_section_out_of_range', s['record']+112, '<Q', 0xfffffffffffffff0, 'visibility section'),
            ('fallback_index_out_of_range', s['unbaked_light_indices'], '<I', 4, 'unbaked selected-local'),
            ('fallback_sun_index', s['unbaked_light_indices'], '<I', 0, 'unbaked selected-local'),
            ('fallback_unsupported_index', s['unbaked_light_indices'], '<I', 3, 'unbaked selected-local'),
            ('fallback_support_missing', s['support'], '<B', 2, 'unbaked selected-local'),
            ('fallback_visibility_entry_missing', s['entries']+16, '<I', 3, 'incomplete required face visibility'),
            ('fallback_no_baked_direct_face', fallback['locations']['modes'][0]['faces']+32,
             '<I', 12, 'invalid face density/flags/domain'),
        ]:
            damaged = dict(fallback, data=bytearray(fallback['data']))
            struct.pack_into(fmt, damaged['data'], at, value)
            seal(damaged['data'])
            run(name, damaged, (False, True, False), reason)
        for name, records, indices, reason in [
            ('fallback_absent_index_list', ((0, 0, 1),), (), 'unbaked face partition'),
            ('fallback_orphan_indices', (), (2,), 'orphan unbaked light indices'),
        ]:
            damaged = fixture(mode_count=1, world=True, baked_styles=palette, light_styles={2: 35},
                              unbaked_faces=records, unbaked_light_indices=indices)
            run(name, damaged, (False, True, False), reason)
        damaged = fixture(mode_count=1, world=True, baked_styles=palette[:3], light_styles={2: 35},
                          unbaked_faces=((0, 0, 1),), unbaked_light_indices=(2,))
        run('fallback_palette_has_free_slot', damaged, (False, True, False), 'unbaked face partition')
        two_indices = fixture(mode_count=1, world=True, baked_styles=palette, light_styles={1: 35, 2: 36},
                              unbaked_faces=((0, 0, 2),), unbaked_light_indices=(1, 2))
        run('world_multiple_sorted_unbaked_locals', two_indices, inspect=inspect_fixture(two_indices))
        index_at = two_indices['locations']['sets'][0]['unbaked_light_indices']
        for name, indices in [('fallback_duplicate_indices', (1, 1)), ('fallback_unsorted_indices', (2, 1))]:
            damaged = dict(two_indices, data=bytearray(two_indices['data']))
            struct.pack_into('<2I', damaged['data'], index_at, *indices)
            seal(damaged['data'])
            run(name, damaged, (False, True, False), 'unbaked selected-local range/order/support')
        two_faces = fixture(mode_count=1, world=True, lit_second=True, baked_styles=palette, light_styles={2: 35},
                            unbaked_faces=((0, 0, 1), (1, 1, 1)), unbaked_light_indices=(2, 2))
        run('world_sorted_sparse_face_ranges', two_faces, inspect=inspect_fixture(two_faces))
        face_at = two_faces['locations']['sets'][0]['unbaked_faces']
        for name, records in [
            ('fallback_duplicate_face_records', (0, 0, 1, 0, 1, 1)),
            ('fallback_out_of_order_face_records', (1, 0, 1, 0, 1, 1)),
            ('fallback_bad_later_prefix', (0, 0, 1, 1, 0, 1)),
        ]:
            damaged = dict(two_faces, data=bytearray(two_faces['data']))
            struct.pack_into('<6I', damaged['data'], face_at, *records)
            seal(damaged['data'])
            run(name, damaged, (False, True, False), 'unbaked face partition')
        damaged = dict(fallback, data=bytearray(fallback['data']), manifest=bytearray(fallback['manifest']))
        # Repair the selected-byte CRC so only the baked-palette conflict rejects this pair.
        struct.pack_into('<i', damaged['manifest'], 400+2*116+44, 33)
        struct.pack_into('<I', damaged['data'], s['record']+4, crc(damaged['manifest'][400:]))
        seal(damaged['data'])
        run('fallback_style_present_in_baked_palette', damaged, (True, True, False), 'unbaked local style already baked')
        for name, action, scope in [('deterministic_disk_alpha_immutable_reference', reference_checks, 'independent mathematical reference; not GPU execution'),
                                    ('kernel_receiver_caster_source_contract', source_contract_checks, 'source-contract-only; not runtime validation')]:
            try:
                action()
                row = dict(case=name, scope=scope, passed=True)
            except (AssertionError, OSError, ValueError) as error:
                row = dict(case=name, scope=scope, passed=False, error=str(error))
            results.append(row)
            print(json.dumps(row))
    report = dict(passed=all(r['passed'] for r in results), cases=results,
                  coverage='Production shared-validator byte fixtures; Python visibility parser; independent reference; labelled source contracts. No GPU or live-map evidence.')
    if options.report:
        options.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'], cases=len(results))))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
