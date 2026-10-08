#!/usr/bin/env python3
"""Standalone production baked-direct math regressions (CPU only).

Build from src in an x64 MSVC developer shell (PowerShell):
  cl /nologo /std:c++17 /EHsc /MT /O2 /fp:precise /DCOMPILER_MSVC /DCOMPILER_MSVC64 `
    /DWIN32 /D_WIN32 /DWIN64 /D_WIN64 /DPLATFORM_64BITS /DPROTECTED_THINGS_DISABLE `
    /Ipublic /Ipublic/tier0 /Ipublic/tier1 /Iutils/common /Iutils/vrad_restir `
    utils/vrad_restir/tests/baked_direct_probe.cpp `
    lib/public/x64/tier1.lib lib/public/x64/tier0.lib lib/public/x64/vstdlib.lib `
    lib/public/x64/mathlib.lib `
    /Febaked_direct_probe.exe
Build baked_receiver_probe.cpp with the same flags/libs, /Febaked_receiver_probe.exe.
  python utils/vrad_restir/tests/baked_direct_test.py --probe ./baked_direct_probe.exe `
    --receiver-probe ./baked_receiver_probe.exe

The probe calls the production inline helper and hlight R8 decoder. Python math
is an independent analytic oracle; style accumulation is probe harness code,
not evidence of writer wiring. Binary16 checks use Python's IEEE round-to-even
oracle, NOT the production page encoder. No bake, engine or GPU is exercised.
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

ROOT = Path(__file__).resolve().parents[3]
BASIS = ((0.81649661064147949, 0, 0.57735025882720947),
         (-0.40824833512306213, 0.70710676908493042, 0.57735025882720947),
         (-0.40824821591377258, -0.7071068286895752, 0.57735025882720947))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    length = math.sqrt(dot(a, a))
    return tuple(x/length for x in a) if length else tuple(a)


def sat(x):
    return min(1, max(0, x))


def frame(case):
    # Independent reference TangentSpaceSurfaceSetup / ComputeBasis equations.
    s = unit(cross(case['N'], unit(case['T'])))
    t = unit(cross(s, case['N']))
    if dot(case['flat'], cross(unit(case['S']), unit(case['T']))) > 0:
        s = tuple(-x for x in s)
    bases = [tuple(b[0]*s[c]+b[1]*t[c]+b[2]*case['N'][c] for c in range(3)) for b in BASIS]
    return s, t, bases


def light(**changes):
    result = dict(type=1, style=0, encoding=255, byte=255, scale=1,
                  origin=(0, 0, 8), intensity=(2, .5, .25), normal=(0, 0, -1),
                  constant=1, linear=0, quadratic=0, radius=0, cap=0,
                  inner=.9, outer=.5, exponent=1, start=0, end=0)
    result.update(changes)
    return result


def radiance(item, position):
    delta = tuple(o-p for o, p in zip(item['origin'], position))
    distance = math.sqrt(dot(delta, delta))
    hard = item['end'] > item['start']
    if not distance or (item['radius'] > 0 and distance > item['radius']) or (hard and distance > item['end']):
        return (0, 0, 0), (0, 0, 0)
    direction = tuple(x/distance for x in delta)
    clamped = max(distance, 1)
    evaluated = min(clamped, item['cap']) if item['cap'] > 0 else clamped
    denom = item['constant']+evaluated*item['linear']+evaluated**2*item['quadratic']
    attenuation = 1/denom if denom > 0 else 0
    if item['type'] == 2:
        cosine = -dot(direction, item['normal'])
        cone = 1
        if cosine <= item['inner']:
            cone = sat((cosine-item['outer'])/(item['inner']-item['outer'])) if item['inner'] != item['outer'] else 0
            if item['exponent'] not in (0, 1):
                cone **= item['exponent']
        attenuation = 0 if cosine <= item['outer'] else attenuation*cosine*cone
    if hard:
        t = 1-sat((clamped-item['start'])/(item['end']-item['start']))
        attenuation *= 6*t**5-15*t**4+10*t**3
    return tuple(255*x*attenuation for x in item['intensity']), direction


def expected(case):
    s, t, bases = frame(case)
    styles, lights, scales = {}, [], {}
    for item in case['lights']:
        rgb, direction = radiance(item, case['position'])
        visibility = (item['byte'] if item['encoding'] == 256 else item['encoding'])/255
        angular = [sat(dot(direction, n)) for n in [case['N'], *bases]]
        lights.append(dict(radiance=rgb, L=direction, visibility=visibility, angular=angular))
        planes = styles.setdefault(item['style'], [[0.0]*3 for _ in range(4)])
        scales[item['style']] = item['scale']
        for p in range(4):
            for c in range(3):
                planes[p][c] += rgb[c]*visibility*angular[p]
    plain = [sum(planes[0][c]*scales[style]/255 for style, planes in styles.items()) for c in range(3)]
    bumped = [sum(planes[p+1][c]*case['weights'][p]*scales[style]/255
                  for style, planes in styles.items() for p in range(3)) for c in range(3)]
    return dict(S=s, T=t, bases=bases, lights=lights,
                styles=[dict(style=i, planes=styles[i]) for i in sorted(styles)], plain=plain, bumped=bumped)


def close(actual, reference, label='root'):
    if isinstance(reference, dict):
        for key, value in reference.items():
            close(actual[key], value, label+'.'+str(key))
    elif isinstance(reference, (list, tuple)):
        if len(actual) != len(reference):
            raise AssertionError(f'{label}: length {len(actual)} != {len(reference)}')
        for i, value in enumerate(reference):
            close(actual[i], value, f'{label}[{i}]')
    elif not math.isclose(actual, reference, rel_tol=8e-6, abs_tol=2e-5):
        raise AssertionError(f'{label}: {actual} != {reference}')


def serialize(case):
    rows = [' '.join(str(x) for key in ('position', 'N', 'flat', 'S', 'T', 'weights') for x in case[key]),
            str(len(case['lights']))]
    for item in case['lights']:
        fields = [item[k] for k in ('type', 'style', 'encoding', 'byte', 'scale')]
        fields += [x for k in ('origin', 'intensity', 'normal') for x in item[k]]
        fields += [item[k] for k in ('constant', 'linear', 'quadratic', 'radius', 'cap', 'inner', 'outer', 'exponent', 'start', 'end')]
        rows.append(' '.join(map(str, fields)))
    return '\n'.join(rows)+'\n'


def cases():
    base = dict(position=(0, 0, 0), N=(0, 0, 1), flat=(0, 0, 1),
                S=(1, 0, 0), T=(0, -1, 0), weights=(.2, .3, .5))
    def make(name, lights=None, **changes):
        result = copy.deepcopy(base)
        result.update(changes, name=name, lights=lights if lights is not None else [light()])
        return result
    result = [make('plain_and_ssbump_weighted_sum'),
              make('zero_selected_locals', []),
              make('zero_distance', [light(origin=(0, 0, 0))]),
              make('subunit_distance_clamp', [light(origin=(0, 0, .25), constant=2, linear=3, quadratic=4)]),
              make('cap_before_attenuation', [light(cap=2, constant=2, linear=3, quadratic=4)]),
              make('cap_below_one', [light(cap=.5, constant=2, linear=3, quadratic=4)]),
              make('negative_cap_uncapped', [light(cap=-1, quadratic=1)]),
              make('nonpositive_denominator', [light(constant=-1)]),
              make('radius_boundary', [light(radius=8)]),
              make('radius_outside', [light(radius=7.999)]),
              make('quintic_midpoint', [light(start=4, end=12)]),
              make('fade_start', [light(start=8, end=12)]),
              make('fade_end', [light(start=4, end=8)]),
              make('fade_beyond', [light(start=4, end=7)]),
              make('fade_uses_clamped_not_cap', [light(cap=2, start=4, end=12, quadratic=1)]),
              make('fade_subunit_uses_one', [light(origin=(0, 0, .25), start=0, end=2)]),
              make('disabled_fade', [light(start=12, end=4)]),
              make('backface', N=(0, 0, -1)),
              make('angular_saturates_upper', N=(0, 0, 2)),
              make('mirrored_texture', T=(0, 1, 0)),
              make('skewed_texture_not_vrad_frame', S=(2, .4, .3), T=(.6, -3, .8)),
              make('tilted_vertex_frame', N=unit((.4, -.2, 1)), T=(.4, -2, .1)),
              make('nonunit_shader_normal', N=(.1, .2, .8)),
              make('blocked_R8', [light(encoding=0)]),
              make('uniform_mid_R8', [light(encoding=128)]),
              make('dense_mid_R8', [light(encoding=256, byte=128)]),
              make('multiple_per_style', [light(style=33, scale=.5), light(style=33, scale=.5, intensity=(.2, 1, 3)),
                                         light(style=37, scale=0), light(style=0, encoding=256, byte=31)])]
    for cosine in (.499, .5, .501, .7, .9, .901, 1):
        for exponent in (0, 1, 2, .5):
            result.append(make(f'spot_{cosine}_{exponent}',
                               [light(type=2, normal=(math.sqrt(1-cosine*cosine), 0, -cosine), exponent=exponent)]))
    result.append(make('hard_cone_edge', [light(type=2, normal=(math.sqrt(.75), 0, -.5), inner=.5, outer=.5)]))
    result.append(make('hard_cone_inside', [light(type=2, inner=.5, outer=.5)]))
    # Normal-map join: saturate squared dot then normalize weights; SSBUMP above
    # uses unnormalized texture weights (also explicitly test sums != 1).
    for n in ((0, 0, 1), unit((.3, .1, .9)), unit((-.6, -.1, .7))):
        dp = [sat(dot(n, b))**2 for b in BASIS]
        result.append(make('normalmap_'+str(n), weights=tuple(x/sum(dp) for x in dp)))
    result.append(make('ssbump_nonunit_weights', weights=(.7, .8, .2)))
    return result


def quantization_oracle(row, case):
    # Verify quantized plane join error with a componentwise binary16 half-ULP
    # bound, plus exact zero/endpoints and round-to-nearest-even tie fixtures.
    for key, bumped in (('plain', False), ('bumped', True)):
        decoded, bound = [0.0]*3, [0.0]*3
        scales = {item['style']: item['scale'] for item in case['lights']}
        for style in row['styles']:
            for p in (range(1, 4) if bumped else (0,)):
                coefficient = scales[style['style']]*(case['weights'][p-1] if bumped else 1)
                for c, source in enumerate(style['planes'][p]):
                    linear = source/255
                    encoded = struct.pack('<e', linear)
                    half = struct.unpack('<e', encoded)[0]
                    word, = struct.unpack('<H', encoded)
                    half_ulp = 2**(-25 if word < 0x400 else ((word >> 10)-15-11))
                    decoded[c] += coefficient*half
                    bound[c] += abs(coefficient)*half_ulp
        for c in range(3):
            if abs(decoded[c]-row[key][c]) > bound[c]+2e-6*max(1, abs(row[key][c])):
                raise AssertionError(f'{case["name"]}: binary16 join error exceeds bound')
    for source, expected_word in ((0, 0), (1, 0x3c00), (65504, 0x7bff),
                                  (1+2**-11, 0x3c00), (1+3*2**-11, 0x3c02), (2**-25, 0)):
        if struct.unpack('<H', struct.pack('<e', source))[0] != expected_word:
            raise AssertionError('binary16 round-to-even oracle')
    for hits in range(33):
        byte = math.floor(255*hits/32+.5)
        if abs(byte/255-hits/32) > .5/255+1e-12:
            raise AssertionError('R8 nearest quantization oracle')


def receiver_cases(probe, env):
    def run(operation, numbers):
        process = subprocess.run([str(probe.resolve()), operation],
                                 input=' '.join(map(str, numbers))+'\n', text=True,
                                 capture_output=True, check=True, env=env)
        return json.loads(process.stdout)

    # Unit vertex outputs become nonunit pixel outputs. Check that interpolation
    # precedes saturation and does not rebuild tangents from the interpolated N.
    normals = [unit((0, 0, 1)), unit((.7, .2, 1)), unit((-.3, .6, 1))]
    vertices = []
    for i, n in enumerate(normals):
        item = dict(N=n, flat=(0, 0, 1), S=(2, .4, .3), T=(.6, -3, .8))
        s, t, _ = frame(item)
        vertices.append(((i % 2, i // 2, .2*i), n, s, t))
    total = 0
    for weights in ((1, 0, 0), (.2, .3, .5), (.5, .5, 0)):
        numbers = [*weights, *(x for vertex in vertices for vec in vertex for x in vec)]
        row = run('frame', numbers)
        blended = [tuple(sum(weights[i]*vertices[i][field][c] for i in range(3)) for c in range(3))
                   for field in range(4)]
        p, n, s, t = blended
        bases = [tuple(b[0]*s[c]+b[1]*t[c]+b[2]*n[c] for c in range(3)) for b in BASIS]
        close(row, dict(position=p, N=n, bases=bases), 'interpolated_shader_frame')
        total += 1

    # Fan and authored tessellation produce different barycentric ownership.
    corners = ((0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0))
    for triangles, samples in (
            (((0, 1, 2), (0, 2, 3)),
             (((1.5, .5, 0), 0, (.25, .5, .25)), ((.5, 1.5, 0), 1, (.25, .25, .5)),
              ((3, .5, 0), 0, (0, .75, .25)))),
            (((0, 1, 3), (1, 2, 3)),
             (((.5, .5, 0), 0, (.5, .25, .25)), ((1.5, 1.5, 0), 1, (.25, .5, .25))))):
        for point, selected, weights in samples:
            row = run('brush', [4, 2, *point, *(x for p in corners for x in p),
                                *(x for tri in triangles for x in tri)])
            close(row, dict(triangle=selected, weights=weights), 'brush_triangle_weights')
            total += 1

    # Independent determinant oracle over explicit two-triangle cells, including
    # odd/even diagonals, vertices, edges and exact endpoint 1 (no epsilon shrink).
    for power in (2, 3, 4):
        side = 1 << power
        width = side+1
        for x, y in ((0, 0), (1, 0), (0, 1), (side-1, side-1)):
            for fu, fv in ((0, 0), (.25, .75), (.75, .25), (.5, .5), (1, 1)):
                u, v = (x+fu)/side, (y+fv)/side
                row = run('disp', [power, u, v])
                reconstructed = (sum((index % width)*w for index, w in zip(row['indices'], row['weights']))/side,
                                 sum((index // width)*w for index, w in zip(row['indices'], row['weights']))/side)
                close(reconstructed, (u, v), 'disp_UV_reconstruction')
                close(sum(row['weights']), 1, 'disp_weight_sum')
                if min(row['weights']) < -1e-6 or max(row['indices']) >= width*width:
                    raise AssertionError('displacement indices/weights outside full-res domain')
                cx, cy = min(int(u*side), side-1), min(int(v*side), side-1)
                base = cy*width+cx
                tris = ((base, base+width, base+1), (base+1, base+width, base+width+1)) if base % 2 else (
                    (base, base+width, base+width+1), (base, base+width+1, base+1))
                expected_map = None
                for tri in tris:
                    a, b, c = [(i % width, i // width) for i in tri]
                    denominator = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
                    wa = ((b[1]-c[1])*(u*side-c[0])+(c[0]-b[0])*(v*side-c[1]))/denominator
                    wb = ((c[1]-a[1])*(u*side-c[0])+(a[0]-c[0])*(v*side-c[1]))/denominator
                    ws = (wa, wb, 1-wa-wb)
                    if min(ws) >= -1e-7:
                        expected_map = {i: w for i, w in zip(tri, ws) if w > 1e-7}
                        break
                actual_map = {i: w for i, w in zip(row['indices'], row['weights']) if w > 1e-7}
                if expected_map is None or actual_map.keys() != expected_map.keys():
                    raise AssertionError('displacement triangle differs from checkerboard renderer oracle')
                close(actual_map, expected_map, 'disp_triangle_weights')
                total += 1
    print(json.dumps(dict(receiver_geometry_cases=total, production_interpolation=True,
                          live_renderer=False, dynamic_displacement_LOD=False, passed=True)))
    capacity_cases = [
        (0, 17, 64, 128),
        (1 << 30, (1 << 30)+1, (1 << 30)+3, 65536),
        (1 << 30, 1128807948, 1128807900, 48),
        ((1 << 31)-1, (1 << 31)-1, (1 << 31)-32, 16),
        (1 << 30, 1 << 31, (1 << 31)-16, 16),
        (-1, 0, 64, 0),
    ]
    maximum = (1 << 31)-1
    for allocated, required, size, section in capacity_cases:
        row = run('bytecapacity', (allocated, required, size, section))
        fits = allocated >= 0 and required <= maximum
        capacity = max(allocated, required, min(2*allocated, maximum)) if fits and required > allocated else allocated if fits else -1
        aligned = (size+15) & ~15
        section_fits = aligned <= maximum and section <= maximum-aligned
        close(row, dict(fits=fits, capacity=capacity, sectionFits=section_fits,
                        total=aligned+section if section_fits else size), 'byte_capacity')
    print(json.dumps(dict(byte_capacity_cases=len(capacity_cases), production_growth_helper=True,
                          large_allocation=False, passed=True)))
    total += len(capacity_cases)
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--receiver-probe', type=Path, required=True)
    parser.add_argument('--dll-dir', type=Path, action='append', default=[])
    parser.add_argument('--large-byte-growth', action='store_true',
                        help='exercise real shared-vector growth past 1 GiB (allocation failure skips)')
    args = parser.parse_args()
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p) for p in [ROOT/'../game/bin/x64', *args.dll_dir])+os.pathsep+env['PATH']
    total = 0
    for case in cases():
        process = subprocess.run([str(args.probe.resolve())], input=serialize(case), text=True,
                                 capture_output=True, check=True, env=env)
        row = json.loads(process.stdout)
        close(row, expected(case), case['name'])
        quantization_oracle(row, case)
        print(json.dumps(dict(case=case['name'], production_math=True, production_R8_decode=True,
                              writer_exercised=False, quantization='independent IEEE oracle', passed=True)))
        total += 1
    total += receiver_cases(args.receiver_probe, env)
    if args.large_byte_growth:
        process = subprocess.run([str(args.receiver_probe.resolve()), 'largebytegrowth'], text=True,
                                 capture_output=True, check=True, env=env, timeout=120)
        row = json.loads(process.stdout)
        if not row.get('skipped'):
            assert row['passed'] and row['required'] > 1 << 30 and row['allocated'] >= row['required']
            total += 1
        print(json.dumps(dict(case='real_shared_vector_growth_past_1GiB', **row)))
    print(json.dumps(dict(passed=True, cases=total, bake=False, renderer=False, GPU=False)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
