"""Explicit BSP lightmap joins, independently generated beside ordinary/old receiver variants.
The role catalog names sampled native pages, not shader-name admission in the renderer.
Alpha-only replay and refraction-only passes deliberately keep their original samples.
"""
import os
import re
import gensource as gs

SAMPLER_ROLES = {
    'lightmappedgeneric_ps20b': 2,
    'worldtwotextureblend_ps20b': 2,
    'lightmappedreflective_ps20b': 8,
    'lightmappedgeneric_decal_ps20b': 14,
    'water_ps20b': 8,
    'shatteredglass_ps20b': 2,
    'pyro_vision_ps30': 2,
}
EXTRA_VERTEX = {'water_vs20', 'shatteredglass_vs20', 'pyro_vision_vs30'}


def prepare(text, logical, stage):
    if stage == 'ps' and logical == 'water_ps20b':
        path = os.path.join(gs.SRC, 'water_ps2x_helper.h')
        with open(path, encoding='latin-1') as source:
            helper = source.read().replace('\r\n', '\n')
        text = gs.shadow_replace(text, '#include "water_ps2x_helper.h"', helper)
    return text


def extra_vertex(text, logical):
    st = gs.find_struct(text, 'VS_OUTPUT')
    used = set()
    for line in text[st[1]:st[2]].splitlines():
        member = gs.MEMBER.match(line)
        if member:
            match = re.fullmatch(r'TEXCOORD(\d+)', member.group(6), re.I)
            if match: used.add(int(match.group(1)))
    decl = []
    for typ, name in [('float2', 'shadowBaseLightmapUV'), ('float3', 'highresPosition'),
                      ('float3', 'highresNormal'), ('float3', 'highresTangentS'), ('float3', 'highresTangentT')]:
        slot = next(n for n in range(32) if n not in used)
        used.add(slot)
        interpolation = 'centroid ' if name == 'shadowBaseLightmapUV' else ''
        decl.append(f'\t{interpolation}{typ} {name} : TEXCOORD{slot};')
    text = text[:st[2]] + '\n' + '\n'.join(decl) + '\n' + text[st[2]:]
    for headers, bs, be in reversed(gs.main_defs(text)):
        arg = gs.param_parts(headers[0][1][0])[2]
        pos = f'float4( {arg}.vPos.xyz, 1 )'
        if logical == 'pyro_vision_vs30':
            normal = f'cross( {arg}.vTangentS, {arg}.vTangentT )'
        else:
            normal = 'hlightObjectNormal'
        code = f'o.shadowBaseLightmapUV = {arg}.vLightmapTexCoord.xy;\n'
        if logical != 'pyro_vision_vs30':
            code += f'float3 hlightObjectNormal; DecompressVertex_Normal( {arg}.vNormal, hlightObjectNormal );\n'
        code += f'o.highresPosition = mul( {pos}, cModel[0] );\no.highresNormal = normalize( mul( {normal}, (float3x3)cModel[0] ) );\n'
        if logical == 'shatteredglass_vs20':
            code += 'o.highresTangentS = 0; o.highresTangentT = 0;\n'
        else:
            code += f'o.highresTangentS = normalize( mul( {arg}.vTangentS, (float3x3)cModel[0] ) );\no.highresTangentT = normalize( mul( {arg}.vTangentT, (float3x3)cModel[0] ) );\n'
        body = text[bs + 1:be]
        returns = list(re.finditer(r'\breturn\s+o\s*;', gs.mask(body)))
        if len(returns) != 1: raise RuntimeError('highres vertex needs final o return: ' + logical)
        ret = returns[0]
        body = body[:ret.start()] + code + body[ret.start():]
        text = text[:bs + 1] + body + text[be:]
    return text


def replace_samples(text, logical):
    # Original sampling is retained as the alpha argument, even on HDR/native alpha consumers.
    calls = list(re.finditer(r'\b(LightMapSample|tex2D)\s*\(\s*(LightmapSampler|LightMap[012]Sampler)\s*,\s*([^()]+?)\s*\)', text))
    expected = {'lightmappedgeneric_ps20b': 4, 'worldtwotextureblend_ps20b': 4,
                'lightmappedreflective_ps20b': 3, 'lightmappedgeneric_decal_ps20b': 3,
                'water_ps20b': 3, 'shatteredglass_ps20b': 1, 'pyro_vision_ps30': 3}[logical]
    if len(calls) != expected: raise RuntimeError(f'highres sample contract drift: {logical}: {len(calls)} != {expected}')
    for index, match in reversed(list(enumerate(calls))):
        plane = index + 1 if index < 3 and logical not in {'shatteredglass_ps20b', 'pyro_vision_ps30'} else 0
        original = match.group(0)
        if match.group(1) == 'LightMapSample':
            # PC LightMapSample returns ordinary decoded RGB; use original tex2D only to preserve A.
            original = f'tex2D( {match.group(2)}, {match.group(3)} )'
        replacement = f'HighresLightmap_Plane( hlr, {plane}, {original} )'
        if match.group(1) == 'LightMapSample': replacement += '.rgb'
        text = text[:match.start()] + replacement + text[match.end():]
    return text


def transform(text, logical, stage, folded_names):
    text = gs.shadow_feature_skips(text, logical)
    if stage == 'vs':
        if logical in EXTRA_VERTEX: return extra_vertex(text, logical)
        return gs.shadow_vertex(text, logical, highres=True)
    if logical in gs.SHADOW_LIGHTMAPPED:
        text = gs.shadow_pixel_joins(text, logical, folded_names)
    text = replace_samples(text, logical)
    if logical == 'lightmappedgeneric_ps20b':
        # Explicit material modulation preserves colored/zero-red tint without recovering it by division.
        text = text.replace('g_TintValuesAndLightmapScale.rgb', '(cHlightRoute.x != 0 ? g_ShadowDirectTint : g_TintValuesAndLightmapScale.rgb)')
    elif logical == 'worldtwotextureblend_ps20b':
        text = gs.shadow_replace(text, 'diffuseLighting *= g_OverbrightFactor;', 'diffuseLighting *= cHlightRoute.x != 0 ? 1.0f : g_OverbrightFactor;')
    elif logical in {'lightmappedreflective_ps20b', 'water_ps20b'}:
        text = gs.shadow_replace(text, 'diffuseLighting *= LIGHT_MAP_SCALE / sum;', 'diffuseLighting *= (cHlightRoute.x != 0 ? 1.0f : LIGHT_MAP_SCALE) / sum;')
    elif logical == 'shatteredglass_ps20b':
        text = gs.shadow_replace(text, 'diffuseLighting *= LIGHT_MAP_SCALE;', 'diffuseLighting *= cHlightRoute.x != 0 ? 1.0f : LIGHT_MAP_SCALE;\n diffuseLighting += hld.diffuse * g_DiffuseModulation;')
    if logical == 'water_ps20b':
        text = gs.shadow_replace(text, 'struct DrawWater_params_t\n{',
            'struct DrawWater_params_t\n{\n#if BASETEXTURE\n HlightReceiver hlightReceiver;\n ShadowMapReceiver hlightShadow;\n float3 hlightTangentS, hlightTangentT, hlightNormal;\n#endif')
        text = gs.shadow_replace(text, 'params.lightmapTexCoord1And2 = i.lightmapTexCoord1And2;',
            'params.hlightReceiver = hlr; params.hlightShadow = hls;\n params.hlightTangentS = dx12In.highresTangentS; params.hlightTangentT = dx12In.highresTangentT; params.hlightNormal = dx12In.highresNormal;\n params.lightmapTexCoord1And2 = i.lightmapTexCoord1And2;')
        text = gs.shadow_replace(text, 'float4 baseSample = tex2D( BaseTextureSampler, i.vBumpTexCoord.xy );',
            'HlightReceiver hlr = i.hlightReceiver;\n float4 baseSample = tex2D( BaseTextureSampler, i.vBumpTexCoord.xy );')
        basis = ', '.join(f'mul( bumpBasis[{n}], float3x3( i.hlightTangentS, i.hlightTangentT, i.hlightNormal ) )' for n in range(3))
        text = gs.shadow_replace(text, 'diffuseLighting *= (cHlightRoute.x != 0 ? 1.0f : LIGHT_MAP_SCALE) / sum;',
            'diffuseLighting *= (cHlightRoute.x != 0 ? 1.0f : LIGHT_MAP_SCALE) / sum;\n ShadowMapDirect hld = ShadowMap_GatherDirect( i.hlightShadow, ShadowMap_ShadeBumped( ' + basis + ', dp / sum ) );\n diffuseLighting += hld.diffuse;')
    elif logical == 'pyro_vision_ps30':
        # The effect's authored color transforms remain after linear irradiance replacement.
        text = text.replace('HighresLightmap_Plane( hlr, 0, tex2D( LightmapSampler, i.vLightmapBlendTexCoord.xy ) )',
            '( HighresLightmap_NativeScalePlane( hlr, 0, tex2D( LightmapSampler, i.vLightmapBlendTexCoord.xy ), LIGHT_MAP_SCALE ) + float4( hld.diffuse, 0 ) )')
    # Unknown owners and explicitly neutral native fullbright pages receive no
    # selected direct. Model/VHV shaders have their independent runtime-only route.
    text = text.replace('smd.diffuse', '(smd.diffuse * hlr.valid)')
    text = text.replace('hld.diffuse', '(hld.diffuse * hlr.valid)')
    if logical == 'water_ps20b':
        # DrawWater consumes baked diffuse only in reflection-without-refraction.
        # Keep !BASETEXTURE fog/depth policy unchanged, but remove the unused
        # positive base-texture blocks so validation cannot keep dead owners live.
        text = re.sub(r'(?m)^([ \t]*#\s*if\s+)BASETEXTURE[ \t]*$',
                      r'\1BASETEXTURE && REFLECT && !REFRACT', text)
    return text


def finish_pixels(text):
    # Argument evaluation completes material/fog derivatives before the failure UAV write.
    for _, bs, be in reversed(gs.main_defs(text)):
        text = gs.replace_returns(text, bs + 1, be, lambda e: f'HighresLightmap_Finish( {e} )')
    if 'LPREVIEW_PS_OUT' in text:
        inputs = gs.find_struct(text, 'DX12_PS_INPUT')
        overload = ('\nLPREVIEW_PS_OUT HighresLightmap_Finish(LPREVIEW_PS_OUT color)\n'
                    '{ HighresLightmap_Report(); return color; }\n')
        text = text[:inputs[3]] + overload + text[inputs[3]:]
    return text


def pixel_entry(text, logical, folded_names):
    if logical in gs.SHADOW_LIGHTMAPPED:
        text = gs.shadow_pixel_entry(text, logical, folded_names)
    inputs = gs.find_struct(text, 'DX12_PS_INPUT')
    position = next((name for _, _, name, _, semantic, _ in gs.struct_members(text[inputs[1]:inputs[2]])
                     if gs.norm_sem(semantic) == 'SV_POSITION'), None)
    if position is None: raise RuntimeError('missing highres screen position: ' + logical)
    if logical in gs.SHADOW_LIGHTMAPPED:
        pattern = re.compile(r'ShadowMapReceiver smr = ShadowMap_BeginLightmappedReceiver\([^;]+;')
        matches = list(pattern.finditer(text))
        if not matches: raise RuntimeError('missing highres receiver entry: ' + logical)
        for match in reversed(matches):
            code = ('HlightReceiver hlr = HighresLightmap_Begin( dx12In.shadowBaseLightmapUV );\n'
                    f' ShadowMapReceiver smr = ShadowMap_BeginReceiver( dx12In.worldPos, dx12In.worldNormal, dx12In.{position}.xy );\n'
                    ' smr.bakedSunVisibility = hlr.sun;')
            text = text[:match.start()] + code + text[match.end():]
        return finish_pixels(text)
    guard = {'water_ps20b': 'BASETEXTURE && REFLECT && !REFRACT', 'shatteredglass_ps20b': '1',
             'pyro_vision_ps30': 'VERTEX_LIT == 0 && ( EFFECT == 0 || EFFECT == 1 )'}[logical]
    for headers, bs, be in reversed(gs.main_defs(text)):
        body = text[bs + 1:be]
        conv = re.search(r'\bDX12ConvertInput\s*\([^;]+;', body)
        if not conv: raise RuntimeError('missing highres input conversion: ' + logical)
        code = ('\n#if ' + guard + '\n HlightReceiver hlr = HighresLightmap_Begin( dx12In.shadowBaseLightmapUV );\n'
                f' ShadowMapReceiver hls = ShadowMap_BeginReceiver( dx12In.highresPosition, dx12In.highresNormal, dx12In.{position}.xy );\n'
                ' hls.bakedSunVisibility = hlr.sun;\n')
        if logical != 'water_ps20b':
            code += ' ShadowMapDirect hld = ShadowMap_GatherDirect( hls, ShadowMap_ShadeLambert( dx12In.highresNormal ) );\n'
        code += '#endif\n'
        body = body[:conv.end()] + code + body[conv.end():]
        text = text[:bs + 1] + body + text[be:]
    return finish_pixels(text)
