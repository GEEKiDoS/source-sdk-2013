"""Batch driver: regenerates native HLSL sources + per-slice manifests from the pair table below.

Each entry: (slice, vs source, vs profile, vs logical, ps source|None, ps profile, ps logical, cpp files whose engine
register calls feed the pixel shader). A pixel shader is always generated against the vertex shader it pairs with
(its input struct mirrors that vertex output). A logical shader shared by several entries is generated once and
must produce identical text for each pairing.

Manifest "# pair" lines include @ambient=cN, @lights=cN, and @fog=cN from the material C++ engine API calls.
Native-only entries use profile `native` and legacy source `-`; their hand-authored source lives in `native_src/`,
outside the generated tree, and they have no DX9 combo-ABI reference or nativeparity pair. Native-only stages may
be `vs`, `ps`, or `cs`.
"""
import os, re, sys, json, difflib
_HERE = os.path.dirname(os.path.abspath(__file__))
_DX12 = os.path.dirname(_HERE)                                   # materialsystem/stdshaders_dx12
_LEGACY = os.path.join(os.path.dirname(_DX12), 'stdshaders')    # materialsystem/stdshaders
sys.path.insert(0, os.path.dirname(__file__))
import gensource as gs

ROOT = _DX12
LEGACY = _LEGACY
MANIFEST_PROFILE = {'vs20': '20b', 'vs30': '30', 'ps20': '20', 'ps20b': '20b', 'ps30': '30'}

PAIRS = [
    # slice, vs src, vs prof, vs logical, ps src, ps prof, ps logical, cpp (engine register calls)
    ('screenspace', 'writez_vs20.fxc', 'vs20', 'writez_vs20', None, None, None, ['writez_dx9.cpp']),
    ('vertexlit', 'unlittwotexture_vs20.fxc', 'vs20', 'unlittwotexture_vs20', 'unlittwotexture_ps2x.fxc', 'ps20b', 'unlittwotexture_ps20b', ['unlittwotexture_dx9.cpp']),
    ('effects_misc', 'cloak_blended_pass_vs20.fxc', 'vs20', 'cloak_blended_pass_vs20', 'cloak_blended_pass_ps2x.fxc', 'ps20b', 'cloak_blended_pass_ps20b', ['cloak_blended_pass_helper.cpp']),
    ('effects_misc', 'cloak_blended_pass_vs20.fxc', 'vs30', 'cloak_blended_pass_vs30', 'cloak_blended_pass_ps2x.fxc', 'ps30', 'cloak_blended_pass_ps30', ['cloak_blended_pass_helper.cpp']),
    ('lightmapped', 'lightmappedgeneric_flashlight_vs20.fxc', 'vs20', 'lightmappedgeneric_flashlight_vs20', 'flashlight_ps2x.fxc', 'ps20b', 'flashlight_ps20b', ['BaseVSShader.cpp']),
    ('lightmapped', 'depthtodestalpha_vs20.fxc', 'vs20', 'depthtodestalpha_vs20', 'depthtodestalpha_ps20b.fxc', 'ps20b', 'depthtodestalpha_ps20b', ['BaseVSShader.cpp']),
]


_legacy_files = {f.lower(): f for f in os.listdir(LEGACY)}


def source_for(logical):
    """legacy .fxc for a logical name (profile suffix -> candidate source suffixes)."""
    m = re.match(r'(.+)_(vs20|vs30|ps20b|ps20|ps30)$', logical, re.I)
    base, prof = m.group(1), m.group(2).lower()
    cands = {'vs20': ['_vs20', '_vs30', '_vsxx'], 'vs30': ['_vs30', '_vs20', '_vsxx'], 'ps20b': ['_ps2x', '_ps20b', '_ps20'],
             'ps20': ['_ps20', '_ps2x'], 'ps30': ['_ps30', '_ps2x', '_ps20b']}[prof]
    for c in cands:
        f = _legacy_files.get((base + c + '.fxc').lower())
        if f: return f, prof
    raise RuntimeError('no legacy source for ' + logical)


NO_PARITY = {}   # (vs, ps) -> reason the nativeparity comparison cannot run for the pair
LAYOUT_FROM = {}  # vs logical -> legacy source whose VS_OUTPUT it adopts
SHADOWMAP_PAIRS = set()  # feature generation is a separate pass; ordinary conversion is untouched


def pair(slice_, vs, ps, cpps, no_parity=None, layout_from=None, shadowmaps=False):
    """PAIRS entry from logical names alone (sources and profiles derived)."""
    vsrc, vprof = source_for(vs)
    if no_parity: NO_PARITY[(vs, ps)] = no_parity
    if layout_from: LAYOUT_FROM[vs] = source_for(layout_from)[0]
    if shadowmaps:
        if not ps: raise ValueError('shadow-map receiver pair needs a pixel shader')
        SHADOWMAP_PAIRS.add((vs, ps))
    if ps:
        psrc, pprof = source_for(ps)
        return (slice_, vsrc, vprof, vs, psrc, pprof, ps, cpps)
    return (slice_, vsrc, vprof, vs, None, None, None, cpps)
def native_only(slice_, source, stage, logical):
    """Register a hand-authored native-only logical compiled from native_src/."""
    if stage not in ('vs', 'ps', 'cs'):
        raise ValueError('native-only shader stage must be vs, ps, or cs')
    return (slice_, source, stage, logical)



LITERAL = {'true': 1, 'false': 0}


def fixed_vs_values(vs_logical, cpps):
    """VS combo -> literal value when every SET_*_VERTEX_SHADER_COMBO of it after DECLARE_*_VERTEX_SHADER( vs ) in the
    material C++ uses the same literal."""
    vals = {}
    for c in cpps:
        text = open(os.path.join(LEGACY, c), encoding='latin-1').read()
        current = None
        for m in re.finditer(r'DECLARE_(?:STATIC|DYNAMIC)_VERTEX_SHADER\s*\(\s*(\w+)\s*\)|SET_(?:STATIC|DYNAMIC)_VERTEX_SHADER_COMBO\s*\(\s*(\w+)\s*,\s*([^;]*?)\s*\)\s*;', text):
            if m.group(1): current = m.group(1).lower(); continue
            if current != vs_logical.lower(): continue
            v = m.group(3).strip()
            v = LITERAL.get(v, v)
            vals.setdefault(m.group(2), set()).add(str(v))
    return {k: next(iter(v)) for k, v in vals.items() if len(v) == 1 and re.fullmatch(r'-?\d+', next(iter(v)))}


def native_name(logical):
    """Native logical of a legacy logical: <base>_vs51 / <base>_ps51 (the SM5.1 profile the source is compiled for).
    The legacy name (profile suffix vs20/vs30/ps20/ps20b/ps30) stays the combo-ABI reference and the shaders/fxc name."""
    m = re.fullmatch(r'(\w+?)_(vs|ps)(20b|20|30|2x|xx)', logical, re.I)
    if not m: raise RuntimeError('logical without a shader-model suffix: ' + logical)
    return f'{m.group(1)}_{m.group(2).lower()}51'

def shadowmap_logical(logical):
    """Keep the legacy profile suffix internally; native_name maps it to the feature SM5.1 name."""
    m = re.fullmatch(r'(.+)_(vs20|vs30|ps20|ps20b|ps30)', logical)
    if not m: raise RuntimeError('invalid shadow-map legacy logical: ' + logical)
    return f'{m.group(1)}_shadowmap_{m.group(2)}'



def equal_combos(vs_logical, ps_logical, cpps):
    """Combo variables the material C++ sets from one identical expression in the vertex and the pixel shader: those
    must hold EQUAL values in a rendered pair (enums such as pyro_vision's EFFECT), not merely the same truth."""
    exprs = {'VERTEX': {}, 'PIXEL': {}}
    for c in cpps:
        text = open(os.path.join(LEGACY, c), encoding='latin-1').read()
        current = {'VERTEX': None, 'PIXEL': None}
        for m in re.finditer(r'DECLARE_(?:STATIC|DYNAMIC)_(VERTEX|PIXEL)_SHADER\s*\(\s*(\w+)\s*\)|'
                             r'SET_(?:STATIC|DYNAMIC)_(VERTEX|PIXEL)_SHADER_COMBO\s*\(\s*(\w+)\s*,\s*([^;]*?)\s*\)\s*;', text):
            if m.group(1): current[m.group(1)] = m.group(2).lower(); continue
            stage = m.group(3)
            want = vs_logical if stage == 'VERTEX' else ps_logical
            if current[stage] != want.lower(): continue
            exprs[stage].setdefault(m.group(4), set()).add(re.sub(r'\s+', '', m.group(5)))
    return sorted(n for n, e in exprs['VERTEX'].items() if n in exprs['PIXEL'] and e == exprs['PIXEL'][n] and len(e) == 1
                  and not re.fullmatch(r'-?\d+|true|false', next(iter(e))))


def run(only_slices=None):
    import importlib, pairs as _pairs
    importlib.reload(_pairs)
    # pairs.py registers through the importable `genmat` module (not __main__ when run as a script)
    import genmat as _gm
    LAYOUT_FROM, NO_PARITY = _gm.LAYOUT_FROM, _gm.NO_PARITY
    generated, manifests, errors = {}, {}, []
    vs_cache = {}
    # Shared logicals keep the first pair-table slice as their manifest owner,
    # including partial regeneration where that slice is not selected.
    owners = {}
    for slice_, _, _, vlog, _, _, plog, _ in _pairs.ALL:
        owners.setdefault(vlog, slice_)
        if plog:
            owners.setdefault(plog, slice_)
    entries = [e for e in _pairs.ALL if not only_slices or e[0] in only_slices]
    native_entries = [e for e in getattr(_pairs, 'NATIVE_ONLY', []) if not only_slices or e[0] in only_slices]
    # Vertex output conditionals on macros a paired pixel shader lacks are flattened (union of members, the
    # vertex shader zero-initializes the ones it does not write).
    # centroid interpolation is declared by the pixel shaders; the vertex output (mirrored by every paired pixel
    # input) carries the union so both sides pack identically.
    centroid = {}
    for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
        if psrc: centroid.setdefault(vlog, set()).update(gs.centroid_semantics(psrc, pprof))
    flatten, fixed = {}, {}
    # An adopted output also needs the union for macros its vertex shader does not define (splinecard has
    # no DUALSEQUENCE combo). Apply that union to every user of the layout, including the shared pixel input.
    for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
        adopted = LAYOUT_FROM.get(vlog)
        if not adopted: continue
        _, interp, _ = gs.convert(vsrc, vprof, vlog, output_struct_from=adopted)
        own = gs.select_directives(open(os.path.join(LEGACY, vsrc), encoding='latin-1').read(), gs.PROFILES[vprof][2])
        macros = set(re.findall(r'^\s*//\s*(?:STATIC|DYNAMIC)\s*:\s*"(\w+)"', own, re.M | re.I))
        macros |= set(re.findall(r'^\s*#\s*define\s+(\w+)', own, re.M))
        missing = interp['macros'] - macros
        for _, other_src, _, other_log, _, _, _, _ in entries:
            if other_src == adopted or LAYOUT_FROM.get(other_log) == adopted:
                flatten.setdefault(other_log, set()).update(missing)
    for _ in range(3):
        changed = False
        for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
            if not psrc: continue
            try:
                _, interp, _ = gs.convert(vsrc, vprof, vlog, centroid=sorted(centroid.get(vlog, ())), flatten=frozenset(flatten.get(vlog, ())), output_struct_from=LAYOUT_FROM.get(vlog))
                gs.convert(psrc, pprof, plog, interp=interp, engine_regs={}, fixed=fixed.get((vlog, plog)))
            except gs.MissingMacros as mm:
                lits = fixed_vs_values(vlog, cpps)
                for mac in mm.macros:
                    if mac in lits: fixed.setdefault((vlog, plog), {})[mac] = lits[mac]
                    else: flatten.setdefault(vlog, set()).add(mac)
                changed = True
            except Exception:
                pass
        if not changed: break
    for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
        try:
            if vlog not in vs_cache:
                text, interp, _ = gs.convert(vsrc, vprof, vlog, centroid=sorted(centroid.get(vlog, ())), flatten=frozenset(flatten.get(vlog, ())), output_struct_from=LAYOUT_FROM.get(vlog))
                vs_cache[vlog] = (text, interp)
                generated[vlog] = text
                if owners[vlog] == slice_:
                    manifests.setdefault(slice_, []).append(f'{native_name(vlog)}.fxc vs {native_name(vlog)} {MANIFEST_PROFILE[vprof]} {vsrc}')
            interp = vs_cache[vlog][1]
            if psrc:
                cpp_texts = [open(os.path.join(LEGACY, c), encoding='latin-1').read() for c in cpps]
                regs = gs.engine_regs_from_cpp(cpp_texts)
                text, _, _ = gs.convert(psrc, pprof, plog, interp=interp, engine_regs=regs, fixed=fixed.get((vlog, plog)))
                if plog in generated and generated[plog] != text:
                    open(os.path.join(os.path.dirname(__file__), 'mismatch_' + plog + '.txt'), 'w', encoding='latin-1').write(
                        '\n'.join(difflib.unified_diff(generated[plog].splitlines(), text.splitlines(), lineterm='')))
                    raise RuntimeError(f'{plog} differs between its vertex pairings')
                if plog not in generated:
                    generated[plog] = text
                    if owners[plog] == slice_:
                        manifests.setdefault(slice_, []).append(f'{native_name(plog)}.fxc ps {native_name(plog)} {MANIFEST_PROFILE[pprof]} {psrc}')
            if (vlog, plog) in NO_PARITY:
                manifests.setdefault(slice_, []).append(f'# noparity {native_name(vlog)} {native_name(plog) if plog else "-"} {NO_PARITY[(vlog, plog)]}')
            else:
                engine_tags = {'cAmbientCube': 'ambient', 'cLightInfo': 'lights', 'cPixelFogParams': 'fog'}
                # Parity must perform the material's engine commits as well as its material-block writes.
                engine_state = ''.join(f' @{engine_tags[member]}={reg}' for reg, member in sorted(regs.items()) if member in engine_tags) if psrc else ''
                vs_vars = set(re.findall(r'^\s*//\s*(?:STATIC|DYNAMIC)\s*:\s*"(\w+)"', generated[vlog], re.M | re.I))
                ps_vars = set(re.findall(r'^\s*//\s*(?:STATIC|DYNAMIC)\s*:\s*"(\w+)"', generated[plog], re.M | re.I)) if plog else set()
                shared = vs_vars & ps_vars
                # C++ can still set an obsolete combo absent from the shipped source (WTB's VS FLASHLIGHT).
                equal = [k for k in equal_combos(vlog, plog, cpps) if k in shared] if plog else []
                manifests.setdefault(slice_, []).append(f'# pair {native_name(vlog)} {native_name(plog) if plog else "-"}' + ''.join(f' {k}={v}' for k, v in sorted(fixed.get((vlog, plog), {}).items())) + ''.join(f' ={k}' for k in equal) + engine_state)
        except Exception as e:
            errors.append(f'{slice_} {vlog}/{plog}: {e}')
    # Never mutate an ordinary result/interpolator: feature variants independently translate the SAME source
    # with the SAME resolved flatten/fixed/centroid tables. Combo directives and @legacy annotations survive.
    feature_vs = {}
    for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
        if (vlog, plog) not in _gm.SHADOWMAP_PAIRS: continue
        try:
            fv, fp = shadowmap_logical(vlog), shadowmap_logical(plog)
            if fv not in feature_vs:
                text, interp, _ = gs.convert(vsrc, vprof, vlog, centroid=sorted(centroid.get(vlog, ())),
                    flatten=frozenset(flatten.get(vlog, ())), output_struct_from=LAYOUT_FROM.get(vlog), shadowmaps=True)
                feature_vs[fv] = (text, interp)
                generated[fv] = text
                manifests.setdefault(slice_, []).extend([
                    f'{native_name(fv)}.fxc vs {native_name(fv)} native -',
                    f'# noparity {native_name(fv)} - shadowmap-feature-variant'])
            cpp_texts = [open(os.path.join(LEGACY, c), encoding='latin-1').read() for c in cpps]
            text, _, _ = gs.convert(psrc, pprof, plog, interp=feature_vs[fv][1],
                engine_regs=gs.engine_regs_from_cpp(cpp_texts), fixed=fixed.get((vlog, plog)), shadowmaps=True)
            if fp in generated and generated[fp] != text:
                raise RuntimeError(f'{fp} differs between its vertex pairings')
            if fp not in generated:
                generated[fp] = text
                manifests.setdefault(slice_, []).extend([
                    f'{native_name(fp)}.fxc ps {native_name(fp)} native -',
                    f'# noparity {native_name(fp)} - shadowmap-feature-variant'])
        except Exception as e:
            errors.append(f'{slice_} shadowmap {vlog}/{plog}: {e}')
    # Every actual BSP-lightmap consumer has an explicit RGB join. Do not reuse the old
    # shadow receiver whitelist: water, glass and world pyro consume native lightmaps too.
    import genhighres
    import gencommon
    earlydepth_common, _, _ = gencommon.translate_all()
    highres_vs = {}
    for slice_, vsrc, vprof, vlog, psrc, pprof, plog, cpps in entries:
        if plog not in genhighres.SAMPLER_ROLES: continue
        try:
            fv = shadowmap_logical(vlog).replace('_shadowmap_', '_highres_')
            fp = shadowmap_logical(plog).replace('_shadowmap_', '_highres_')
            if fv not in highres_vs:
                text, interp, _ = gs.convert(vsrc, vprof, vlog, centroid=sorted(centroid.get(vlog, ())),
                    flatten=frozenset(flatten.get(vlog, ())), output_struct_from=LAYOUT_FROM.get(vlog), highres=True)
                highres_vs[fv] = (text, interp)
                generated[fv] = text
                manifests.setdefault(slice_, []).extend([
                    f'{native_name(fv)}.fxc vs {native_name(fv)} native -',
                    f'# noparity {native_name(fv)} - highres-feature-variant'])
            cpp_texts = [open(os.path.join(LEGACY, c), encoding='latin-1').read() for c in cpps]
            text, _, _ = gs.convert(psrc, pprof, plog, interp=highres_vs[fv][1],
                engine_regs=gs.engine_regs_from_cpp(cpp_texts), fixed=fixed.get((vlog, plog)), highres=True)
            if fp in generated and generated[fp] != text:
                raise RuntimeError(f'{fp} differs between its vertex pairings')
            if fp not in generated:
                generated[fp] = text
                manifests.setdefault(slice_, []).extend([
                    f'{native_name(fp)}.fxc ps {native_name(fp)} native -',
                    f'# noparity {native_name(fp)} - highres-feature-variant'])
                hazards = genhighres.earlydepth_hazards(text, native_name(fp), earlydepth_common, ROOT)
                if hazards:
                    manifests.setdefault(slice_, []).append(
                        f'# earlydepth-excluded {native_name(fp)} ' + '; '.join(hazards))
                else:
                    early = fp.replace('_highres_', '_highres_earlydepth_')
                    generated[early] = genhighres.earlydepth_pixels(text)
                    manifests.setdefault(slice_, []).extend([
                        f'{native_name(early)}.fxc ps {native_name(early)} native -',
                        f'# noparity {native_name(early)} - highres-earlydepth-feature-variant',
                        f'# earlydepth {native_name(fp)} {native_name(early)}'])
        except Exception as e:
            errors.append(f'{slice_} highres {vlog}/{plog}: {e}')
    # Fail before publication/deletion: a missing receiver rule must not leave a partial shader pack.
    if errors: raise RuntimeError('\n'.join(errors))
    for slice_, source, stage, logical in native_entries:
        native_path = os.path.join(ROOT, 'native_src', source)
        if not os.path.isfile(native_path):
            errors.append(f'{slice_} native-only {logical}: missing {native_path}')
            continue
        if logical in generated:
            errors.append(f'{slice_} native-only {logical}: duplicate generated logical')
            continue
        manifests.setdefault(slice_, []).append(f'{source} {stage} {logical} native -')
        manifests.setdefault(slice_, []).append(f'# noparity {logical} - native-only')
        if stage == 'ps' and '_highres_' in logical:
            with open(native_path, encoding='latin-1') as authored:
                text = authored.read()
            hazards = genhighres.earlydepth_hazards(text, logical, earlydepth_common, ROOT)
            if hazards:
                manifests.setdefault(slice_, []).append(
                    f'# earlydepth-excluded {logical} ' + '; '.join(hazards))
            else:
                early = logical.replace('_ps51', '_earlydepth_ps51')
                early_source = early + '.fxc'
                if not os.path.isfile(os.path.join(ROOT, 'native_src', early_source)):
                    errors.append(f'{slice_} native-only {early}: missing authored twin')
                    continue
                manifests.setdefault(slice_, []).extend([
                    f'{early_source} ps {early} native -',
                    f'# noparity {early} - native-only-highres-earlydepth',
                    f'# earlydepth {logical} {early}'])
    if errors: raise RuntimeError('\n'.join(errors))
    if not only_slices:
        # the native sources and manifests are generator-owned: drop what the table no longer produces
        for f in os.listdir(os.path.join(ROOT, 'hlsl')):
            if f.endswith('.fxc') and f[:-4] not in {native_name(n) for n in generated}: os.remove(os.path.join(ROOT, 'hlsl', f))
        for f in os.listdir(os.path.join(ROOT, 'manifests')):
            if f.endswith('.txt') and f != '00_common.txt' and f[:-4] not in manifests: os.remove(os.path.join(ROOT, 'manifests', f))
    names = {}
    for name in generated:
        if names.setdefault(native_name(name), name) != name:
            raise RuntimeError(f'{name} and {names[native_name(name)]} map to one native logical {native_name(name)}')
    for name, text in generated.items():
        open(os.path.join(ROOT, 'hlsl', native_name(name) + '.fxc'), 'w', newline='\n', encoding='latin-1').write(text)
    # Keep the legacy filename/logical name: only the shipped combo directives differ from the SDK source.
    reference_sources = {source for _, vsrc, _, _, psrc, _, _, _ in entries for source in (vsrc, psrc)
                         if source in gs.ABI_PATCHES}
    if reference_sources:
        reference_root = os.path.join(ROOT, 'legacy_reference')
        os.makedirs(reference_root, exist_ok=True)
        for source in sorted(reference_sources):
            with open(os.path.join(reference_root, source), 'w', newline='\n', encoding='latin-1') as reference:
                reference.write(gs.legacy_reference(source))
    for logical, (stage, reason) in getattr(_pairs, 'LEGACY_ONLY', {}).items():
        manifests.setdefault('zz_legacy', []).append(f'# legacy {logical} {stage} {reason}')
    for slice_, lines in manifests.items():
        head = ['// <native source> <vs|ps|cs> <logical name> <legacy profile 20b|30|native> <legacy DX9 source or ->',
                '// generated by genmat.py from the pair table; "# pair" lines feed the nativeparity smoke case']
        open(os.path.join(ROOT, 'manifests', slice_ + '.txt'), 'w', newline='\n', encoding='latin-1').write('\n'.join(head + lines) + '\n')
    return generated, manifests, errors


if __name__ == '__main__':
    g, m, e = run(sys.argv[1:] or None)
    print(len(g), 'sources;', {k: len(v) for k, v in m.items()})
    print('\n'.join(e))
