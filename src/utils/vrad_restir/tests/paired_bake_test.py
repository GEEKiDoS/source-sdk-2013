#!/usr/bin/env python3
"""Exercise paired reuse and v4 world/prop local direct on isolated production BSPs.

CPU validator (after building hybrid_visibility_probe.cpp):
  python utils/vrad_restir/tests/hybrid_visibility_test.py --probe ./hybrid_visibility_probe.exe
Actual prop acceptance (pertexel eligibility is checked from compiled BSP flags):
  python utils/vrad_restir/tests/paired_bake_test.py --fixture-dir <isolated-fixtures> `
    --tool-game <isolated-tool-game> --prop-vhv-baseline-producer <pre-v4-producer.exe> `
    --only prop-direct-analytic --only prop-direct-no-per-vertex `
    --only prop-direct-pertexel --only prop-direct-zero-selected
The baseline is the original producer, not ordinary VRAD: its existing selected
direct exclusion and full-source reflected-gather policy must remain unchanged.
"""
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import string
import sys
import struct
import subprocess

from shadowmap_contract_test import KV, Runner, prepare, require, MODE_LUMPS
from hlight_contract_test import (BSP, manifest, asset, pixel, diagnostics as check_diagnostics,
                                  coarse_endpoints, baked_direct_rgb, local_direct_rgb, check_direct_partition,
                                  require_half_rgb, face_visibility, unchanged, local_radiance, dot, crc)

PAIRED_CASES = ('equivalent', 'hdr-default', 'eight-equal', 'zero-equal', 'hdr-zero',
                'hdr-scale', 'hdr-empty-scale', 'ambient-scale', 'eight-different',
                'rad-eight', 'rad-mode', 'rad-occlusion', 'material-hdr')
DIRECT_CASES = ('baked-direct-point', 'baked-direct-spot', 'baked-direct-hard-fade',
                'baked-direct-styles', 'baked-direct-styles-hdr')
OVERFLOW_CASES = ('baked-direct-style-overflow', 'source-style-overflow', 'receiver-style-overflow')
PROP_CASES = ('prop-direct-analytic', 'prop-direct-no-per-vertex', 'prop-direct-pertexel',
              'prop-direct-zero-selected')
CASES = PAIRED_CASES + DIRECT_CASES + OVERFLOW_CASES + PROP_CASES
EQUAL = {'equivalent', 'hdr-default', 'eight-equal', 'zero-equal'}
LIGHTS = ('light', 'light_spot', 'light_environment')
REUSE = 'skipping second GPU transport'


def entities(vmf):
    return [e for e in vmf.blocks('entity') if e.get('classname') in LIGHTS]


def fixture(runner, name):
    vmf = KV.parse((runner.root / 'shadowmap_fixture.vmf').read_text())
    lights = entities(vmf)
    require(lights, 'fixture requires authored analytic lights')
    for e in lights:
        e.set('_light', '255 224 160 250')
        e.set('_lightHDR', '-1 -1 -1 1')
        e.set('_lightscaleHDR', '1')
        e.set('_ambient', '128 144 160 100')
        e.set('_ambientHDR', '-1 -1 -1 1')
        e.set('_AmbientScaleHDR', '1')
    sun = next(e for e in lights if e.get('classname') == 'light_environment')
    if name == 'hdr-default':
        for e in lights:
            e.items = [(k, v) for k, v in e.items if k.lower() not in
                       ('_lighthdr', '_lightscalehdr', '_ambienthdr', '_ambientscalehdr')]
    elif name in ('eight-equal', 'eight-different'):
        sun.set('_light', '255 224 160 250 ' + ('255 224 160 250' if name == 'eight-equal' else '64 128 255 80'))
    elif name == 'zero-equal':
        sun.set('_light', '0')
        sun.set('_lightHDR', '0')
    elif name == 'hdr-zero':
        sun.set('_lightHDR', '0')  # Valid zero overrides, never sentinel fallback.
    elif name == 'hdr-scale':
        sun.set('_lightscaleHDR', '.2')  # Sentinel fallback still applies this scale.
    elif name == 'hdr-empty-scale':
        sun.set('_lightscaleHDR', '')  # Present empty differs from absent/default 1.
    elif name == 'ambient-scale':
        sun.set('_AmbientScaleHDR', '.4')
    elif name == 'material-hdr':
        # Change one actual lit brush material; scene material emission includes
        # the HDR scale before bounce transport, not after final encoding.
        world = vmf.get('world')
        side = list(next(world.blocks('solid')).blocks('side'))[1]  # Visible floor top, not void-facing underside.
        side.set('material', 'shadowmap_test/paired_emitter')
        material = runner.game / 'materials/shadowmap_test/paired_emitter.vmt'
        material.write_text('"UnlitGeneric"\n{\n"$basetexture" "shadowmap_test/neutral"\n"$hdrcolorscale" ".2"\n}\n')
    elif name == 'rad-occlusion':
        for e in vmf.blocks('entity'):
            if e.get('classname') == 'prop_static':
                e.set('disableshadows', '0')
    path = runner.root / ('paired-' + name + '.vmf')
    path.write_text(vmf.text())
    rad = ''
    if name == 'rad-eight':
        rad = 'shadowmap_test/flat 255 224 160 1 64 128 255 .5\n'
    elif name == 'rad-mode':
        rad = 'ldr:shadowmap_test/flat 255 224 160 1\nhdr:shadowmap_test/flat 64 128 255 .5\n'
    elif name == 'rad-occlusion':
        # Mode-filtered noshadow affects model materials, not world brushes.
        # Cover every possible model texture name's alphanumeric characters.
        rad = ''.join('hdr:noshadow ' + c + '\n' for c in string.ascii_lowercase + string.digits + '_')
    return runner.compile(path.stem), rad

def check_fallback_log(text, modes):
    records = re.findall(r'^Hlight: baked local direct fallback faces=(\d+) faceLights=(\d+) \((LDR|HDR)\)\s*$',
                         text, re.M)
    require(len(records) == len(modes) and len({tag for _, _, tag in records}) == len(modes),
            'missing/duplicate per-mode baked-direct fallback summary')
    counts = {tag: (int(faces), int(lights)) for faces, lights, tag in records}
    for mode in modes:
        visibility = mode['visibility']
        tag = 'LDR' if mode['lighting'] == 8 else 'HDR'
        require(counts.get(tag) == (len(visibility['unbaked_faces']), len(visibility['unbaked_light_indices'])),
                'per-mode fallback log differs from actual sparse face/local accounting')



def bake(runner, original, path, mode='both', enhanced=True, rad='', reject=False,
         bounces=1, rejection_pattern=None):
    shutil.copyfile(original, path)
    if rad:
        path.with_suffix('.rad').write_text(rad)
    base = path.with_suffix('.diagnostics')
    options = runner.options(mode, enhanced, bounces=bounces, diagnostics=base,
                             extra=('-restir_iterations', '4', '-restir_hlight_density', '2'))
    before = path.read_bytes()
    if reject:
        # Use the production subprocess/log plumbing but allow any explicit
        # failed stage: the assertion is rollback, not a particular parser error.
        import subprocess
        command = [str(runner.tools['vrad_restir']), *options, '-game', str(runner.game), str(path)]
        proc = subprocess.run(command, env=runner.env, capture_output=True, text=True, errors='replace')
        path.with_suffix('.failure.log').write_text(proc.stdout + proc.stderr)
        require(proc.returncode != 0, 'invalid bake input was accepted')
        if rejection_pattern:
            require(re.search(rejection_pattern, proc.stdout+proc.stderr, re.I), 'missing explicit highres face/style overflow rejection')
        require(path.read_bytes() == before, 'failed paired bake published one mode')
        require(not list(path.parent.glob(path.name + '.restir-paired.*.bsp')), 'failed pair leaked transaction BSP')
        return None, proc.stdout+proc.stderr, None
    text = runner.command('vrad_restir', options, path)
    result = BSP(path)
    if enhanced:
        asset_path, modes = manifest(result)
        _, file_modes = asset(result, asset_path, modes, 2)  # Mode tags, native CRC/geometry identities and pages.
        check_fallback_log(text, file_modes)
    diagnostics = {}
    for key in (('ldr', 'hdr') if mode == 'both' or enhanced else (mode,)):
        diagnostic = Path(str(base) + '.' + key + '.json')
        require(diagnostic.is_file(), 'missing mode-owned diagnostic ' + key)
        diagnostics[key] = json.loads(diagnostic.read_text())
        require(diagnostics[key]['mode'] == key, 'diagnostic mode identity copied blindly')
    return result, text, diagnostics


def compare_native(pair, control, mode):
    for lump in MODE_LUMPS[mode]:
        require(pair.lumps[lump] == control.lumps[lump], f'independent mode {mode} native lump {lump} differs')
    prefix = 'sp_hdr_' if mode else 'sp_'
    vhv = [n for n in control.pak if n.startswith(prefix) and n.endswith('.vhv') and (mode or not n.startswith('sp_hdr_'))]
    require(vhv, 'fixture has no actual VHV secondary lighting')
    for name in vhv:
        require(pair.pak.get(name) == control.pak[name], 'independent VHV differs: ' + name)
    require(pair.game(('dplt', 'dplh')[mode]) == control.game(('dplt', 'dplh')[mode]), 'mode detail style extension differs')
    if mode:
        require(pair.game('dprp') == control.game('dprp'), 'shared detail lighting is not HDR-final')
        for name in control.pak:
            if name.startswith('texelslighting_') and name.endswith('.ppl'):
                require(pair.pak.get(name) == control.pak[name], 'shared PPL is not HDR-final')


def direct_fixture(runner, name):
    if name != 'baked-direct-styles-hdr':
        return runner.compile(name)
    vmf = KV.parse((runner.root / 'baked-direct-styles.vmf').read_text())
    for e in entities(vmf):
        e.set('_lightscaleHDR', '.25')
    (runner.root / (name+'.vmf')).write_text(vmf.text())
    return runner.compile(name)


def reject_partition_mutations(report, mode, face, contributing, unbaked):
    """Prove the independent oracle catches both missing and spuriously unbaked IDs."""
    baked = sorted(contributing-set(unbaked))
    require(unbaked and baked, 'mutation probe requires real baked and runtime fallback locals')
    for indices, label in ((unbaked[:-1], 'missing'), (tuple(sorted((*unbaked, baked[0]))), 'extra')):
        bad = dict(mode)
        bad['visibility'] = dict(mode['visibility'])
        bad['visibility']['unbaked_by_face'] = dict(mode['visibility']['unbaked_by_face'])
        bad['visibility']['unbaked_by_face'][face['dface']] = indices
        try:
            check_direct_partition(report, bad, face)
        except AssertionError:
            continue
        raise AssertionError('partition oracle accepted '+label+' fallback ID')


def run_direct(runner, name):
    source = direct_fixture(runner, name)
    path = runner.root / (name+'-enhanced.bsp')
    result, log, reports = bake(runner, source, path, bounces=0)
    equal = name != 'baked-direct-styles-hdr'
    overflow = name == 'baked-direct-style-overflow'
    require((REUSE in log) == equal, 'local-only paired transport reuse mismatch')
    asset_path, selected = manifest(result)
    data, modes = asset(result, asset_path, selected, 2)
    unchanged(BSP(source), result, asset_path)
    require((modes[0]['visibility_set'] == modes[1]['visibility_set']) == equal,
            'local-only visibility reuse did not follow paired scene identity')
    positive_by_mode = []
    for mode, report in zip(modes, (reports['ldr'], reports['hdr'])):
        require(all(light['type'] in (1, 2) for light in mode['selected']), 'analytic fixture gained sun/nonlocal direct')
        if name == 'baked-direct-spot':
            require(all(light['type'] == 2 and light['stopdot'] > light['stopdot2'] and
                        light['exponent'] == 2 and light['radius'] == 480 for light in mode['selected']),
                    'analytic spotlight lost cone/exponent/radius authoring')
        elif name == 'baked-direct-hard-fade':
            require(all(0 < light['fade_start'] < light['fade_end'] == 480 for light in mode['selected']),
                    'analytic hard-fade fixture did not retain quintic fade interval')
        require(all(not any(rgb) for rgb in report['receiverRadiance']),
                'local-only zero-bounce receiver transport must isolate baked direct')
        require(any(any(rgb) for rgb in report['sourceRadiance']), 'selected source direct was not actually solved')
        require(mode['receiver_styles'] == {0}, 'selected styles leaked into native receiver style domain')
        seen_styles, runtime_styles, positive_planes, visibility_values = set(), set(), set(), set()
        positives = fallback_faces = 0
        for face in report['faces']:
            disk = mode['faces'][face['dface']]
            require(all(s == 0 for s in mode['native'][face['dface']]['styles']),
                    'native BSP gained highres direct-only styles')
            contributing, unbaked = check_direct_partition(report, mode, face)
            if overflow and contributing:
                require(contributing == set(range(5)) and len(unbaked) == 1 and disk[9] == 4,
                        'co-located five-style probe must bake four and retain exactly one runtime fallback')
                remaining = tuple(li for li in range(5) if mode['selected'][li]['style'] not in disk[10:14])
                require(unbaked == remaining and mode['selected'][unbaked[0]]['style'] != 0,
                        'fallback is not exactly the remaining selected-local index/style')
                require(set(disk[10:14]) | {mode['selected'][unbaked[0]]['style']} == {0, 32, 33, 34, 35},
                        'five selected styles were not all accounted for by baked and unbaked partitions')
                if not fallback_faces:
                    reject_partition_mutations(report, mode, face, contributing, unbaked)
                fallback_faces += 1
            elif not overflow:
                require(not unbaked, 'no-overflow analytic fixture unexpectedly used runtime fallback')
            for local in range(face['luxelW']*face['luxelH']):
                visibility_values.update(face_visibility(mode, face['dface'], local).values())
                for li in unbaked:
                    light = mode['selected'][li]
                    require(li in face_visibility(mode, face['dface'], local), 'fallback lost its R8 visibility field')
                    for plane in range(face['numChannels']):
                        require(not any(baked_direct_rgb(report, mode, face, light['style'], plane, local)),
                                'unbaked selected style leaked into baked RGB oracle')
                        if any(local_direct_rgb(report, mode, face, li, plane, local)):
                            runtime_styles.add(light['style'])
                for slot, style in enumerate(disk[10:10+disk[9]]):
                    for plane in range(face['numChannels']):
                        expected = baked_direct_rgb(report, mode, face, style, plane, local)
                        actual = pixel(data, mode, disk, slot, plane, local % face['luxelW'], local // face['luxelW'])
                        require_half_rgb(actual[:3], expected, 'isolated analytic selected direct')
                        if any(expected):
                            seen_styles.add(style)
                            positive_planes.add(plane)
                            positives += 1
        wanted = {0, 32, 33, 34, 35} if overflow else (
            {0, 32, 33, 34} if name.startswith('baked-direct-styles') else {0})
        require(seen_styles | runtime_styles == wanted, 'not every selected local style has baked RGB or full runtime fallback')
        if overflow:
            require(fallback_faces > 0 and runtime_styles and len(seen_styles) == 4 and
                    seen_styles.isdisjoint(runtime_styles), 'forced overflow did not exercise real RGB exclusion')
        else:
            require(seen_styles == wanted and not runtime_styles,
                    'not every selected local style contributed isolated baked RGB')
        require(positive_planes == {0, 1, 2, 3}, 'analytic probe did not exercise Lambert and every bump plane')
        require(0 in visibility_values and any(v > 0 for v in visibility_values),
                'analytic probe did not exercise both occluded and visible local visibility')
        positive_by_mode.append(positives)
    base = path.with_suffix('.diagnostics')
    check_diagnostics(base, None, modes, data, 2, True)
    coarse_endpoints(result, data, modes, 2, base)
    if equal:
        for key in ('receiverRadiance', 'sourceRadiance', 'localDirectPositions', 'luxelNormals', 'luxelBumpNormals', 'bakedDirectRadiance'):
            require(reports['ldr'][key] == reports['hdr'][key], 'paired local-only diagnostic mismatch: '+key)
        require(positive_by_mode[0] == positive_by_mode[1], 'equal paired local coverage differs')
        for ldr, hdr in zip(MODE_LUMPS[0], MODE_LUMPS[1]):
            require(result.lumps[ldr] == result.lumps[hdr], 'equal local paired native payload differs')
    else:
        require(reports['ldr']['bakedDirectRadiance'] != reports['hdr']['bakedDirectRadiance'],
                'HDR scale lost from isolated baked direct')
        for ldr, hdr in zip(modes[0]['selected'], modes[1]['selected']):
            require(ldr['style'] == hdr['style'] and all(abs(h-.25*l) <= abs(l)*2e-7+1e-9
                    for l, h in zip(ldr['intensity'], hdr['intensity'])), 'HDR local manifest scale was not applied')


def run_overflow(runner, name):
    if name == 'baked-direct-style-overflow':
        return run_direct(runner, name)
    source = runner.compile(name)
    if name == 'receiver-style-overflow':
        # Native receiver transport still has a hard four-slot limit. Failure
        # must leave both modes and transaction scratch BSPs unpublished.
        bake(runner, source, runner.root / (name+'-rejected.bsp'), reject=True, bounces=0,
             rejection_pattern=r'Hlight:[^\n]*(?:face[^\n]*style|style[^\n]*face)')
        return
    path = runner.root / (name+'-enhanced.bsp')
    result, log, reports = bake(runner, source, path, bounces=0)
    require(REUSE in log, 'equal source-overflow modes did not share GPU transport')
    asset_path, selected = manifest(result)
    data, modes = asset(result, asset_path, selected, 2)
    unchanged(BSP(source), result, asset_path)
    require(modes[0]['visibility'] is modes[1]['visibility'],
            'equal source-overflow pair did not share the exact visibility/fallback directory')
    for mode, report in zip(modes, (reports['ldr'], reports['hdr'])):
        require(mode['receiver_styles'] == {0, 1, 2, 3} and mode['local_styles'] == {3, 32, 34, 63},
                'source-overflow fixture lost mixed native/selected style authoring')
        fallback_styles, mixed_styles, overflow_faces = set(), set(), 0
        for face in report['faces']:
            contributing, unbaked = check_direct_partition(report, mode, face)
            if not unbaked:
                continue
            disk = mode['faces'][face['dface']]
            require(set(disk[10:14]) == {0, 1, 2, 3} and
                    {mode['selected'][li]['style'] for li in unbaked} == {32, 34, 63},
                    'source-overflow must preserve four receiver styles and all three runtime-only locals')
            require({mode['selected'][li]['style'] for li in contributing} == {3, 32, 34, 63},
                    'source-overflow probe did not exercise every selected-local contribution')
            mixed_styles.update(mode['selected'][li]['style'] for li in contributing-set(unbaked))
            fallback_styles.update(mode['selected'][li]['style'] for li in unbaked)
            if not overflow_faces:
                reject_partition_mutations(report, mode, face, contributing, unbaked)
            overflow_faces += 1
        require(overflow_faces > 0 and mixed_styles == {3} and fallback_styles == {32, 34, 63},
                'source-overflow did not preserve its baked mixed style and full runtime fallback partition')
    base = path.with_suffix('.diagnostics')
    check_diagnostics(base, None, modes, data, 2, True, require_source_overflow=True)
    coarse_endpoints(result, data, modes, 2, base)


def prop_mesh_visibility(visibility, mesh, vertex):
    return {li: (encoding if encoding < 256 else visibility['payload'][offset+vertex])/255
            for li, encoding, offset, samples in visibility['entries'][mesh[3]:mesh[3]+mesh[4]]}


def prop_local_rgb(mode, geometry, mesh, light_index, plane, vertex):
    light = mode['selected'][light_index]
    if light['type'] not in (1, 2):
        return (0, 0, 0)
    rgb, direction = local_radiance(light, geometry['positions'][vertex])
    cosine = dot(geometry['normals'][vertex], direction)
    angular = max(0, min(1, cosine)) if plane == 0 else max(0, min(1, .5*cosine+.5))**2
    visibility = prop_mesh_visibility(mode['visibility'], mesh, vertex)
    require(light_index in visibility, 'prop selected-local R8 missing')
    return tuple(value*angular*visibility[light_index] for value in rgb)


def check_prop_partition(mode, geometry, mesh, direct):
    """Derive admission solely from authored geometry, manifest lights and R8."""
    palette, contributing, fallback = [0], set(), []
    for li, light in enumerate(mode['selected']):
        if light['type'] not in (1, 2):
            continue
        if not any(any(prop_local_rgb(mode, geometry, mesh, li, plane, vertex))
                   for plane in range(2) for vertex in range(mesh[2])):
            continue
        contributing.add(li)
        if light['style'] not in palette:
            if len(palette) == 4:
                fallback.append(li)
                continue
            palette.append(light['style'])
    require(direct is not None and direct['styles'] == tuple(palette) and
            direct['unbaked'] == tuple(fallback), 'prop canonical actual-nonzero style/fallback admission')
    return contributing, tuple(fallback)


def check_prop_diagnostics(mode, report, skip_reason=''):
    visibility = mode['visibility']
    require(report['selectedLightCount'] == len(mode['selected']), 'prop diagnostic canonical selected count')
    require(len(report['props']) == len(visibility['props']) > 0, 'actual prop diagnostic/serialized domain')
    nonzero = fallback_meshes = pixels_checked = retained_open_r8 = 0
    for prop, diagnostic in zip(visibility['props'], report['props']):
        ordinal, checksum, first, count, pose, reserved = prop
        require(diagnostic['prop'] == ordinal and diagnostic['skipReason'] == skip_reason,
                'prop direct eligibility must match authored skip policy')
        require(len(diagnostic['meshes']) == count > 0, 'prop every LOD/stripgroup diagnostic coverage')
        require(bool(diagnostic['flags'] & 0x40) == (skip_reason == 'NO_PER_VERTEX_LIGHTING') and
                bool(diagnostic['flags'] & 0x100) == (skip_reason != 'pertexel'),
                'compiled prop flags did not actually exercise authored pervertex/pertexel policy')
        authored_by_source = {}
        for index, geometry in enumerate(diagnostic['meshes']):
            mesh_index = first+index
            mesh = visibility['meshes'][mesh_index]
            vertices = mesh[2]
            require((geometry['mesh'], geometry['lod'], geometry['vertexCount']) == (index, mesh[1], vertices),
                    'prop LOD stripgroup vertex counts/order')
            # R8 is always retained, including both full-runtime skip classes.
            require([e['light'] for e in geometry['visibility']] ==
                    [li for li in range(visibility['lights']) if li != visibility['sun']],
                    'prop diagnostic canonical visibility order')
            for entry in geometry['visibility']:
                expected = [round(prop_mesh_visibility(visibility, mesh, v)[entry['light']]*255)
                            for v in range(vertices)]
                require(entry['r8'] == expected, 'prop serialized R8 hardware scatter differs from diagnostics')
                retained_open_r8 += sum(value > 0 for value in expected)
            direct = visibility['prop_direct'][mesh_index]
            if skip_reason:
                require(direct is None and geometry['styles'] == [] and geometry['unbakedLightIndices'] == [],
                        'ineligible prop must remain full runtime direct without replacing immutable R8')
                continue
            require(len(geometry['sourceIndices']) == vertices and
                    crc(struct.pack('<'+'I'*vertices, *geometry['sourceIndices'])) == mesh[5],
                    'prop original source-index uint32 VHV/R8/direct scatter CRC')
            require(all(len(geometry[key]) == vertices and all(len(v) == 3 and
                        all(math.isfinite(x) for x in v) for v in geometry[key])
                        for key in ('positions', 'normals')), 'prop authored world geometry aligned to hardware vertices')
            for source, position, normal in zip(geometry['sourceIndices'], geometry['positions'], geometry['normals']):
                prior = authored_by_source.setdefault(source, (position, normal))
                require(prior == (position, normal), 'prop duplicate source scatter changed authored geometry across LODs')
            if not mode['selected'] and direct is None:
                require(not geometry['styles'] and not geometry['unbakedLightIndices'],
                        'zero-selected prop diagnostics claimed baked direct')
                continue
            contributing, fallback = check_prop_partition(mode, geometry, mesh, direct)
            require(geometry['styles'] == list(direct['styles']) and
                    geometry['unbakedLightIndices'] == list(fallback), 'prop diagnostic/direct block palette disagreement')
            if mode['selected'] and contributing:
                require(contributing == set(range(5)) and len(fallback) == 1 and len(direct['styles']) == 4,
                        'co-located five-style prop must contribute every selected local and exactly one fallback')
                require(set(direct['styles']) | {mode['selected'][li]['style'] for li in fallback} ==
                        {0, 32, 33, 34, 35}, 'prop direct/fallback lost authored style')
            fallback_meshes += bool(fallback)
            for slot, style in enumerate(direct['styles']):
                for plane in range(2):
                    for vertex in range(vertices):
                        expected = tuple(sum(prop_local_rgb(mode, geometry, mesh, li, plane, vertex)[c]
                                             for li in contributing-set(fallback)
                                             if mode['selected'][li]['style'] == style) for c in range(3))
                        actual = direct['pixels'][(slot*2+plane)*vertices+vertex]
                        require_half_rgb(actual[:3], expected, 'prop analytic normalized RGBA16F direct')
                        require(actual[3] == 0, 'prop direct reserved alpha')
                        nonzero += bool(any(expected))
                        pixels_checked += 1
            if fallback:
                # Require the independent oracle to reject missing and spuriously
                # unbaked IDs, not just compare a producer-supplied fallback list.
                baked = sorted(contributing-set(fallback))
                require(baked, 'prop fallback mutation requires a genuine baked contributor')
                for indices in (fallback[:-1], tuple(sorted((*fallback, baked[0])))):
                    bad = dict(direct, unbaked=indices)
                    try:
                        check_prop_partition(mode, geometry, mesh, bad)
                    except AssertionError:
                        continue
                    raise AssertionError('prop independent oracle accepted wrong runtime fallback partition')
                for li in fallback:
                    require(mode['selected'][li]['style'] not in direct['styles'] and
                            any(any(prop_local_rgb(mode, geometry, mesh, li, plane, vertex))
                                for plane in range(2) for vertex in range(vertices)),
                            'prop fallback must retain nonzero full runtime diffuse and be excluded from baked RGB')
    if skip_reason or not mode['selected']:
        require(nonzero == fallback_meshes == 0, 'full-runtime/zero-selected prop unexpectedly baked local radiance')
        if skip_reason:
            require(retained_open_r8 > 0, 'full-runtime skipped props did not retain actual nonzero local visibility')
    else:
        require(nonzero > 0 and fallback_meshes > 0 and pixels_checked > 0,
                'actual producer did not exercise nonzero prop RGB plus fallback')
    return dict(nonzero=nonzero, fallbackMeshes=fallback_meshes, checked=pixels_checked,
                openR8=retained_open_r8)


def compare_prop_vhv_baseline(runner, source, name, result):
    producer = runner.args.prop_vhv_baseline_producer
    require(producer and producer.is_file(), 'prop VHV byte regression requires preserved --prop-vhv-baseline-producer')
    path = runner.root/(name+'-original-producer.bsp')
    shutil.copyfile(source, path)
    options = runner.options('both', True, bounces=0,
                             extra=('-restir_iterations', '4', '-restir_hlight_density', '2'))
    proc = subprocess.run([str(producer.resolve()), *options, '-game', str(runner.game), str(path)],
                          env=runner.env, capture_output=True, text=True, errors='replace')
    path.with_suffix('.log').write_text(proc.stdout+proc.stderr)
    require(proc.returncode == 0, 'original producer native policy control failed')
    baseline = BSP(path)
    def vhvs(bsp):
        return {key: data for key, data in bsp.pak.items() if re.fullmatch(r'sp_(?:hdr_)?\d+\.vhv', key)}
    require(vhvs(result) == vhvs(baseline), 'new prop direct modified native VHV byte contents versus original producer')
    if name in ('prop-direct-analytic', 'prop-direct-zero-selected'):
        require(vhvs(result), 'VHV byte control did not exercise actual native VHV')
    if name == 'prop-direct-analytic':
        rebake = runner.root/(name+'-checked-v3-rebake.bsp')
        shutil.copyfile(path, rebake)
        log = runner.command('vrad_restir', options, rebake)
        require('checked v3 input; discarding previous RGB for complete v4 prop-direct rebake' in log,
                'original v3 producer output must pass checked rebake-only import')
        imported = BSP(rebake)
        asset_path, selected = manifest(imported)
        asset(imported, asset_path, selected, 2)
        unchanged(baseline, imported, asset_path)
        require(vhvs(imported) == vhvs(result), 'checked v3 rebake changed native VHV policy')


def run_prop_direct(runner, name):
    source = runner.compile(name)
    path = runner.root/(name+'-enhanced.bsp')
    result, log, reports = bake(runner, source, path, bounces=0)
    asset_path, selected = manifest(result)
    data, modes = asset(result, asset_path, selected, 2)
    unchanged(BSP(source), result, asset_path)
    require(REUSE in log and modes[0]['visibility'] is modes[1]['visibility'],
            'equal prop analytic LDR/HDR must share visibility including mixed direct payload')
    skip_reason = {'prop-direct-no-per-vertex': 'NO_PER_VERTEX_LIGHTING',
                   'prop-direct-pertexel': 'pertexel'}.get(name, '')
    for mode, tag in zip(modes, ('ldr', 'hdr')):
        require(not any(any(rgb) for rgb in reports[tag]['receiverRadiance']),
                'prop analytic zero-bounce fixture gained unselected receiver transport')
        require({light['style'] for light in mode['selected']} ==
                (set() if name == 'prop-direct-zero-selected' else {0, 32, 33, 34, 35}),
                'prop fixture selected styles do not match actual manifest')
        report = json.loads(Path(str(path.with_suffix('.diagnostics'))+'.props.'+tag+'.json').read_text())
        require(runner.config['propDirectLods'] > 1 and
                all({mesh['lod'] for mesh in prop['meshes']} == set(range(runner.config['propDirectLods']))
                    for prop in report['props']), 'real prop direct fixture must exercise every authored LOD')
        evidence = check_prop_diagnostics(mode, report, skip_reason)
        if skip_reason:
            for prop in report['props']:
                reason = 'NO_PER_VERTEX_LIGHTING' if skip_reason == 'NO_PER_VERTEX_LIGHTING' else 'active pertexel lighting'
                require(f'{tag.upper()} prop {prop["prop"]} selected-local direct skipped: {reason}' in log,
                        'missing actual prop ID/reason full-runtime skip diagnostic')
        print(json.dumps(dict(case=name, mode=tag, propDirect=evidence, actualProducer=True)))
    compare_prop_vhv_baseline(runner, source, name, result)


def run(runner, name):
    if name in PROP_CASES:
        return run_prop_direct(runner, name)
    if name in DIRECT_CASES:
        return run_direct(runner, name)
    if name in OVERFLOW_CASES:
        return run_overflow(runner, name)
    source, rad = fixture(runner, name)
    equal = name in EQUAL
    paired, log, diagnostics = bake(runner, source, runner.root / (name + '-enhanced.bsp'), rad=rad)
    require((REUSE in log) == equal, 'wrong reuse admission for ' + name)
    asset_path, selected_modes = manifest(paired)
    _, file_modes = asset(paired, asset_path, selected_modes, 2)
    set_ids = [m['visibility_set'] for m in file_modes]
    require((set_ids[0] == set_ids[1]) == equal,
            'visibility sets must share iff whole-scene paired equality admitted ' + name)
    if equal:
        require(file_modes[0]['visibility'] is file_modes[1]['visibility'],
                'paired equal modes did not retain the exact shared visibility set')
    else:
        require(len(set(set_ids)) == 2, 'independent modes aliased visibility despite unequal scene inputs')
    dispatches = len(re.findall(r'^VRAD ReSTIR: baking lightmaps\s*$', log, re.M))
    require(dispatches == (1 if equal else 2), 'second actual transport not bypassed/retained')
    a, b = diagnostics['ldr'], diagnostics['hdr']
    if equal:
        for key in ('sourceRadiance', 'receiverRadiance', 'luxelValid', 'faces', 'luxelPositions'):
            require(a[key] == b[key], 'equivalent mode solved data differs: ' + key)
        for ldr, hdr in zip(MODE_LUMPS[0], MODE_LUMPS[1]):
            require(paired.lumps[ldr] == paired.lumps[hdr], 'equivalent native mode payload differs')
        for key in paired.pak:
            if re.fullmatch(r'sp_\d+\.vhv', key):
                require(paired.pak.get(key.replace('sp_', 'sp_hdr_', 1)) == paired.pak[key], 'paired VHV names/data differ')
    elif name != 'rad-occlusion':
        require(a['sourceRadiance'] != b['sourceRadiance'] or a['faces'] != b['faces'], 'different authored energy lost in transport')
    # Ordinary controls permit genuine independent single-mode solves. Compare
    # all native/ambient/worldlight outputs and VHV/detail/PPL contracts, not just luxels.
    ordinary, ordinary_log, _ = bake(runner, source, runner.root / (name + '-ordinary.bsp'), enhanced=False, rad=rad)
    require((REUSE in ordinary_log) == equal, 'ordinary paired admission differs')
    for mode, key in enumerate(('ldr', 'hdr')):
        control, control_log, _ = bake(runner, source, runner.root / (name + '-control-' + key + '.bsp'), key, False, rad)
        require(REUSE not in control_log, 'genuine single mode incorrectly reused pair')
        require(not control.game('rshd'), 'ordinary single mode gained enhanced metadata')
        compare_native(ordinary, control, mode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-dir', type=Path, required=True)
    parser.add_argument('--tool-game', type=Path, required=True)
    parser.add_argument('--only', choices=CASES, action='append')
    parser.add_argument('--prop-vhv-baseline-producer', type=Path,
                        help='preserved original producer executable for byte-exact unchanged native VHV policy control')
    args = parser.parse_args()
    args.runtime_report = None
    config = prepare(args.fixture_dir, args.tool_game)
    runner = Runner(args, config)
    failures = []
    for name in args.only or CASES:
        try:
            run(runner, name)
            print('PASS', name)
        except (AssertionError, OSError, ValueError, KeyError, IndexError) as error:
            failures.append(name)
            print('FAIL', name, str(error), file=sys.stderr)
    # Invalid HDR must not publish even a successful LDR into the input BSP.
    vmf = KV.parse((runner.root / 'paired-equivalent.vmf').read_text()) if (runner.root / 'paired-equivalent.vmf').exists() else None
    if vmf is None:
        fixture(runner, 'equivalent')
        vmf = KV.parse((runner.root / 'paired-equivalent.vmf').read_text())
    next(e for e in entities(vmf) if e.get('classname') == 'light_environment').set('_lightscaleHDR', 'nan')
    (runner.root / 'paired-invalid-hdr.vmf').write_text(vmf.text())
    try:
        bake(runner, runner.compile('paired-invalid-hdr'), runner.root / 'rollback.bsp', reject=True)
        print('PASS rollback')
    except (AssertionError, OSError, ValueError, KeyError, IndexError) as error:
        failures.append('rollback')
        print('FAIL rollback', str(error), file=sys.stderr)
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
