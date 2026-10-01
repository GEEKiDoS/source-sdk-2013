"""Regenerates materialsystem/stdshaders_dx12/hlsl/common/* from the legacy DX9 headers by mechanical translation.

Rules (sm5-contract "Common HLSL API rule"):
- legacy names/parameter order kept; sampler params become DX12Sampler* pairs (texture + sampler state, same index);
- DX9 texture intrinsics become object methods with identical coordinates (hlslport.convert_intrinsics);
- SHADER_MODEL_{VS,PS}_* selections become DX12_LEGACY_* (exactly one defined by each native source);
- engine-owned register globals are removed; the engine cbuffers declare the same names (aliases where names differ);
- material-owned globals read by helpers become explicit parameters named after the legacy constant, inserted
  before the first defaulted parameter (trailing otherwise); every caller passes the same-named identifier.
"""
import os, re, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_DX12 = os.path.dirname(_HERE)                                   # materialsystem/stdshaders_dx12
_LEGACY = os.path.join(os.path.dirname(_DX12), 'stdshaders')    # materialsystem/stdshaders
sys.path.insert(0, os.path.dirname(__file__))
import hlslport as hp

SRC = _LEGACY
DST = os.path.join(_DX12, 'hlsl', 'common')
COMMONS = ['common_fxc.h', 'common_ps_fxc.h', 'common_vs_fxc.h', 'common_vertexlitgeneric_dx9.h',
           'common_flashlight_fxc.h', 'common_lightmappedgeneric_fxc.h', 'common_hlsl_cpp_consts.h',
           'shader_constant_register_map.h', 'tree_sway.h', 'water_ps2x_helper.h', 'vortwarp_vs20_helper.h', 'common_pragmas.h']

SHADER_MODEL = {
    'SHADER_MODEL_PS_2_0': 'DX12_LEGACY_PS20', 'SHADER_MODEL_PS_2_B': 'DX12_LEGACY_PS20B',
    'SHADER_MODEL_PS_3_0': 'DX12_LEGACY_PS30', 'SHADER_MODEL_VS_2_0': 'DX12_LEGACY_VS20',
    'SHADER_MODEL_VS_3_0': 'DX12_LEGACY_VS30',
}
# Engine-owned legacy globals: declaration removed, value comes from DX12VSEngine/DX12VSBones/DX12PSEngine.
ENGINE = {'cConstants1', 'g_bLightEnabled', 'g_nLightCountRegister', 'cEyePosWaterZ', 'cFlexScale', 'cModelViewProj',
          'cViewProj', 'cModelViewProjZ', 'cViewProjZ', 'cFogParams', 'cViewModel', 'cAmbientCubeX', 'cAmbientCubeY',
          'cAmbientCubeZ', 'cLightInfo', 'cModel', 'g_LinearFogColor', 'cLightScale'}
ALIASES = {'g_bLightEnabled': 'cLightEnabled', 'g_nLightCountRegister': 'cLightCount', 'g_LinearFogColor': 'cLinearFogColor'}
# Material-owned globals declared by legacy common headers: (stage, type, legacy register).
MATERIAL = {'cModulationColor': ('vs', 'float4', 'c47'), 'cFlexWeights': ('vs', 'float4 [512]', 'c1024'),
            'cFlashlightColor': ('ps', 'float4', 'c28'), 'cFlashlightScreenScale': ('ps', 'float4', 'c31'),
            }
# X360-only legacy globals (declared under _X360 in the legacy headers): their readers are dead on PC.
X360_ONLY = {'g_bHighQualityShadows'}

GLOBAL_DECL = re.compile(r'^[ \t]*(?:static\s+)?(?:const\s+)?(?:uniform\s+)?(?:bool|int|float\d?(?:x\d)?|HALF\d?|LightInfo)'
                         r'\s+(\w+)\s*(?:\[[^\]]*\])?\s*:\s*register\s*\(\s*[^)]+?\s*\)\s*;[^\n]*\n', re.M)


def function_bodies(text):
    """[(name, params, params_start, params_end, body_start, body_end)] for every function definition."""
    out = []
    for m in hp.FUNC_DEF.finditer(text):
        try: params, end = hp.split_args(text, m.end() - 1)
        except ValueError: continue
        rest = text[end:end + 400].lstrip()
        if not rest.startswith('{'): continue
        b = text.index('{', end); depth = 0; j = b
        while j < len(text):
            if text[j] == '{': depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        out.append((m.group(1), params, m.end() - 1, end, b, j + 1))
    return out


def param_name(p):
    p = re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', p, flags=re.S)).strip()
    p = p.split('=')[0].strip()
    m = re.search(r'(\w+)\s*(?:\[[^\]]*\])?$', p)
    return m.group(1) if m else ''


def has_default(p):
    return '=' in re.sub(r'//[^\n]*', '', p)


def reads_global(body, name):
    return re.search(r'\b' + re.escape(name) + r'\b', body) is not None

def material_params(texts):
    """function -> [(insert index, type, name)] including transitive propagation through callers."""
    need = {}
    changed = True
    while changed:
        changed = False
        for text in texts.values():
            for name, params, *_, bs, be in function_bodies(text):
                body = text[bs:be]
                pnames = {param_name(p) for p in params if p.strip()}
                wanted = [g for g in MATERIAL if reads_global(body, g) and g not in pnames]
                for callee, extra in list(need.items()):
                    if re.search(r'\b' + callee + r'\s*\(', body):
                        wanted += [g for _, _, g in extra if g not in pnames]
                for g in dict.fromkeys(wanted):
                    have = need.setdefault(name, [])
                    if g not in [x[2] for x in have]:
                        first_default = next((i for i, p in enumerate(params) if has_default(p)), len(params))
                        have.append((first_default, MATERIAL[g][1], g)); changed = True
    return need


def insert_params(text, need):
    """Adds material parameters to definitions and arguments to calls of every function in `need`."""
    for fn, extra in need.items():
        pat = re.compile(r'\b' + re.escape(fn) + r'\s*\(')
        out, i = [], 0
        for m in pat.finditer(text):
            if m.start() < i: continue
            args, end = hp.split_args(text, m.end() - 1)
            line_start = text.rfind('\n', 0, m.start()) + 1
            is_def = re.match(r'[ \t]*(?:static\s+|inline\s+)*[\w<>]+\s+$', text[line_start:m.start()]) is not None
            new = list(args) if args != [''] else []
            for index, typ, name in reversed(sorted(extra, key=lambda e: e[0])):
                pos = min(index, len(new))
                new.insert(pos, f'{typ} {name}' if is_def else name)
            out.append(text[i:m.end() - 1]); out.append('( ' + ', '.join(a.strip() for a in new) + ' )'); i = end
        out.append(text[i:]); text = ''.join(out)
    return text


def rename_shader_models(text):
    for k, v in SHADER_MODEL.items(): text = re.sub(r'\b' + k + r'\b', v, text)
    return text


def strip_globals(text):
    def repl(m):
        name = m.group(1)
        if name in ALIASES: return f'#define {name} {ALIASES[name]}\t// engine-owned (dx12_engine_cbuffers.h)\n'
        if name in ENGINE: return f'// {name}: engine-owned (dx12_engine_cbuffers.h)\n'
        if name in X360_ONLY: return f'// {name}: X360-only legacy constant (dead on PC)\n'
        if name in MATERIAL: return f'// {name}: material-owned; declared by each source that reads it\n'
        return m.group(0)
    return GLOBAL_DECL.sub(repl, text)


def ambient_cube(text):
    base = {'X': 0, 'Y': 2, 'Z': 4}
    return re.sub(r'\bcAmbientCube([XYZ])\s*\[\s*([^\]]+?)\s*\]',
                  lambda m: f'cAmbientCube[ {base[m.group(1)]} + {m.group(2)} ].xyz', text)


PREAMBLE = '''
// ---- DX12 native preamble (not in the legacy header) ----------------------------------------------------------
#if defined(DX12_LEGACY_VS20) || defined(DX12_LEGACY_VS30)
#define DX12_STAGE_VERTEX 1
#elif defined(DX12_LEGACY_PS20) || defined(DX12_LEGACY_PS20B) || defined(DX12_LEGACY_PS30) || defined(DX12_NATIVE_PS)
#define DX12_STAGE_PIXEL 1
#else
#error Native sources must define a vertex or pixel DX12 stage macro
#endif
#include "dx12_engine_cbuffers.h"
#include "dx12_raster_emulation.h"
''' + hp.SAMPLER_STRUCTS + '''// D3D9 arithmetic edge cases the shipped VCS rely on (the DX12 legacy translator reproduces them in
// thirdparty/dx12_shaderconv context.cpp Translate_NRM/Translate_RSQ): rsq/nrm of 0 use FLT_MAX instead of +inf,
// so normalize(0) is 0 (not NaN) and rsqrt(0) is FLT_MAX. Native sources route normalize/rsqrt through these.
float DX12Rsq( float x ) { const float a = abs( x ); return a != 0.0f ? rsqrt( a ) : 3.402823466e+38f; }
float2 DX12Rsq( float2 x ) { return float2( DX12Rsq( x.x ), DX12Rsq( x.y ) ); }
float3 DX12Rsq( float3 x ) { return float3( DX12Rsq( x.x ), DX12Rsq( x.y ), DX12Rsq( x.z ) ); }
float4 DX12Rsq( float4 x ) { return float4( DX12Rsq( x.x ), DX12Rsq( x.y ), DX12Rsq( x.z ), DX12Rsq( x.w ) ); }
float2 DX12Normalize( float2 v ) { return v * DX12Rsq( dot( v, v ) ); }
float3 DX12Normalize( float3 v ) { return v * DX12Rsq( dot( v, v ) ); }
float4 DX12Normalize( float4 v ) { return v * DX12Rsq( dot( v, v ) ); }
#define normalize DX12Normalize
#define rsqrt DX12Rsq
// ---------------------------------------------------------------------------------------------------------------
'''


LPREVIEW_FINISH = '''
// DX12: alpha test and ps2.x implicit raster fog apply to render target 0 only (psconv.cpp:1175-1186).
LPREVIEW_PS_OUT DX12FinishPixel( LPREVIEW_PS_OUT o, float vertexFog )
{
	o.color = DX12FinishPixel( o.color, vertexFog );
	return o;
}'''


def colour_depth_overloads(text):
    """DX12Sampler2D overload after every helper taking a DX12ShadowSampler: materials that bind a colour texture
    as the flashlight depth sampler (worldtwotextureblend) read it without comparison, as D3D9 did."""
    out, pos = [], 0
    for name, params, ps, pe, bs, be in function_bodies(text):
        if not any(re.search(r'\bDX12ShadowSampler\b', p) for p in params): continue
        line_start = text.rfind('\n', 0, ps) + 1
        head = text[line_start:ps]
        # defaults kept: callers omit trailing arguments (worldtwotextureblend's DoFlashlight call)
        decl_params = [(re.sub(r'\bDX12ShadowSampler\b', 'DX12Sampler2D', re.sub(r'//[^\n]*|/\*.*?\*/', '', p, flags=re.S).strip())) for p in params]
        clone = head + '( ' + ', '.join(p.strip() for p in decl_params) + ' )' + text[pe:be]
        out.append(text[pos:be]); out.append('\n' + clone); pos = be
    out.append(text[pos:])
    return ''.join(out)


def translate_all(texts=None):
    texts = texts or {c: open(os.path.join(SRC, c), encoding='latin-1').read() for c in COMMONS}
    known = {}
    # Shadow-map depth parameters of the flashlight helpers are hardware-compare samplers.
    for c, t in texts.items():
        for name, params, *_ in function_bodies(t):
            for i, prm in enumerate(params):
                if re.search(r'\bsampler\s+\w*DepthSampler\s*$', prm.strip()) and c == 'common_flashlight_fxc.h':
                    known.setdefault(name, {})[i] = 'SHADOW'
    if 'common_flashlight_fxc.h' in texts:
        t = texts['common_flashlight_fxc.h']
        t = re.sub(r'\btex2Dproj(\s*\(\s*DepthSampler\b)', r'DX12ShadowCompareProj\1', t)
        t = re.sub(r'\btex2D(\s*\(\s*DepthSampler\b)', r'DX12ShadowRaw\1', t)
        texts = dict(texts, **{'common_flashlight_fxc.h': t})
    for _ in range(3):
        for t in texts.values(): hp.function_sampler_params(t, known)
    need = material_params(texts)
    out = {}
    for c, t in texts.items():
        t = re.sub(r'^[ \t]*#pragma\s+def\b[^\n]*\n', '// #pragma def removed: cConstants0 is DX12VSEngine.cConstants0\n', t, flags=re.M)
        t = strip_globals(t)
        t = ambient_cube(t)
        t = rename_shader_models(t)
        t = hp.convert(t, known)
        t = insert_params(t, need)
        if c == 'common_flashlight_fxc.h': t = colour_depth_overloads(t)
        if c == 'tree_sway.h':
            # Shipped VS30 expands sin to a range-reduced cosine polynomial with MADs.
            # Keep its folded frequencies and allow contraction: precise splits the
            # height cancellation and moves animated edges across raster sample points.
            helper = '''float4 DX12TreeSinePhase( float4 phase )
{
    float4 x = mad( phase, 6.28318548f, -3.14159274f );
    float4 x2 = x * x;
    float4 p = mad( x2, -2.52398507e-7f, 2.47609005e-5f );
    p = mad( x2, p, -0.00138883968f );
    p = mad( x2, p, 0.0416666418f );
    p = mad( x2, p, -0.5f );
    return mad( x2, p, 1.0f );
}
float4 DX12TreeSine( float4 angle ) { return DX12TreeSinePhase( frac( mad( angle, 0.159154937f, 0.25f ) ) ); }
float4 DX12TreeSwaySines( float slowTime, float fastScale )
{
    float fastTime = slowTime * fastScale;
    float4 times = float4( slowTime, slowTime, fastTime, fastTime );
    float4 phase = frac( mad( times, float4( 0.159154937f, 0.367647916f, 0.159154937f, 0.340591609f ), 0.25f ) );
    return DX12TreeSinePhase( phase );
}
'''
            t = t.replace('#if ( TREESWAY )', helper + '\n#if ( TREESWAY )', 1)
            t = re.sub(r'\bsin\s*\(', 'DX12TreeSine(', t)
            t = t.replace('float3 ComputeTreeSway', 'float3 DX12TreeSine( float3 angle ) { return DX12TreeSine( float4( angle, 0 ) ).xyz; }\n\tfloat3 ComputeTreeSway', 1)
            t = t.replace('DX12TreeSine( float4( 1.0, 2.31, g_flFastSwaySpeedScale, 2.14 * g_flFastSwaySpeedScale ) * flSlowSwayTime.xxxx )',
                          'DX12TreeSwaySines( flSlowSwayTime, g_flFastSwaySpeedScale )')
        if c == 'vortwarp_vs20_helper.h':
            # Shipped VS30 range-reduces this intro sine before SINCOS. At flashlight
            # projection w near zero, a one-ulp position change magnifies wrapped cookie UVs.
            t = t.replace('sin( t )', 'sin( mad( frac( mad( t, 0.159154937f, 0.5f ) ), 6.28318548f, -3.14159274f ) )')
        if c == 'water_ps2x_helper.h':
            # Shipped HLSL 10.1 VCS aliases a scalar temporary in this combination: its second
            # reflection UV warp multiplies alpha * normal.x * normal.y instead of alpha * normal.y.
            old = '\tfloat4 vDependentTexCoords = vN * vNormal.a * reflectRefractScale;'
            assert t.count(old) == 1
            t = t.replace(old, old + '\n'
                '#if MULTITEXTURE && REFLECT && !REFRACT && !BASETEXTURE && PIXELFOGTYPE == 2 && (!ABOVEWATER || BLURRY_REFRACT)\n'
                '\tvDependentTexCoords.y *= vNormal.x;\n'
                '#endif')
        if c == 'common_ps_fxc.h':
            m = re.search(r'struct\s+LPREVIEW_PS_OUT\s*\{[^}]*\};', t)
            body = re.sub(r':\s*COLOR(\d)\s*;', r': SV_Target\1;', m.group(0))
            t = t[:m.start()] + body + LPREVIEW_FINISH + t[m.end():]
        if c == 'common_vertexlitgeneric_dx9.h':
            old = 'struct PixelShaderLightInfo\n{\n\tfloat4 color;\n\tfloat4 pos;\n};\n'
            assert t.count(old) == 1
            t = t.replace(old, '#if !defined( DX12_STAGE_PIXEL )	// pixel stage: declared with DX12PSEngine.cLightInfo\n' + old + '#endif\n')
        if c == 'common_fxc.h':
            # after the include guard
            m = re.search(r'#define\s+COMMON_FXC_H_?\s*\n', t)
            t = t[:m.end()] + '#include "dx12_preamble.h"\n' + t[m.end():]
        header = (f'// Native SM5.1 mechanical translation of materialsystem/stdshaders/{c} (generated by gencommon.py; '
                  f'see sm5 contract "Common HLSL API rule").\n')
        out[c] = header + t
    out['dx12_preamble.h'] = ('// DX12 native preamble shared by every native source (generated by gencommon.py): stage selection,\n'
                              '// backend-owned engine cbuffers, raster emulation and the typed texture/sampler pairs.\n'
                              '#ifndef DX12_PREAMBLE_H\n#define DX12_PREAMBLE_H\n' + PREAMBLE + '#endif\n')
    return out, known, need


if __name__ == '__main__':
    out, known, need = translate_all()
    for c, t in out.items():
        open(os.path.join(DST, c), 'w', newline='\n', encoding='latin-1').write(t)
    print('material params:', need)
