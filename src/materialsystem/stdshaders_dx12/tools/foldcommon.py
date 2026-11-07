"""DX12-only runtime overloads for helpers whose legacy counterparts specialize combos.

Run after the ordinary native translation. DX9 common headers stay untouched.
"""
import re


def fold_vertex_common(name, text):
    if name == 'common_vs_fxc.h':
        if 'bool bMorphing, bool bDecal,' in text:
            return text
        # Import lazily: gencommon owns the existing HLSL declaration parser and
        # calls this module after its own translation helpers have been loaded.
        from gencommon import function_bodies, param_name
        edits = []
        for fn, params, ps, pe, bs, be in function_bodies(text):
            if fn != 'ApplyMorph' or not params or 'DX12Sampler2D' not in params[0]:
                continue
            start = text.rfind('\n', 0, ps) + 1
            body = text[bs:be]
            body = re.sub(r'^#if MORPHING\s*$', 'if ( bMorphing )\n{', body, flags=re.M)
            body = re.sub(r'^#if !DECAL\s*$', 'if ( !bDecal )\n{', body, flags=re.M)
            body = re.sub(r'^#else[ \t]*$', '}\nelse\n{', body, flags=re.M)
            body = re.sub(r'^#endif // DECAL[ \t]*$', '}', body, flags=re.M)
            body = re.sub(r'^#else // !?MORPHING[ \t]*$', '}\nelse\n{', body, flags=re.M)
            body = re.sub(r'^#endif[ \t]*$', '}', body, flags=re.M)
            if re.search(r'^\s*#\s*(?:if|else|endif)', body, re.M):
                raise RuntimeError('unrecognized ApplyMorph preprocessor structure')
            signature = text[start:bs]
            overload = signature.replace('ApplyMorph(', 'ApplyMorph( bool bMorphing, bool bDecal,', 1) + body
            args = ', '.join(param_name(p) for p in params)
            wrapper = signature + '{\n\treturn ApplyMorph( MORPHING != 0, DECAL != 0, ' + args + ' );\n}'
            edits.append((start, be, overload + '\n\n' + wrapper))
        if len(edits) != 4:
            raise RuntimeError('expected four VS30 texture ApplyMorph overloads')
        for start, end, replacement in reversed(edits):
            text = text[:start] + replacement + text[end:]
        defaults = ('// Non-folded callers retain compile-time constants; folded callers pass uniforms.\n'
                    '#ifndef MORPHING\n#define MORPHING 0\n#endif\n'
                    '#ifndef DECAL\n#define DECAL 0\n#endif\n\n')
        anchor = '#ifdef DX12_LEGACY_VS30\n\nbool ApplyMorph('
        if text.count(anchor) != 1:
            raise RuntimeError('missing VS30 ApplyMorph declaration anchor')
        text = text.replace(anchor, defaults + anchor, 1)
    elif name == 'tree_sway.h':
        if '#if defined( g_flTime )' in text:
            return text
        # The macros supplying tree constants are only declared by tree-capable
        # callers. They remain valid availability guards without testing a combo.
        if text.count('#if ( TREESWAY )') != 1:
            raise RuntimeError('missing tree-sway helper availability anchor')
        text = text.replace('#if ( TREESWAY )',
                            '#ifndef TREESWAY\n#define TREESWAY 0\n#endif\n\n#if defined( g_flTime )', 1)
        mode = re.compile(r'(?m)^([ \t]*)#if \( TREESWAY == 2 \)\n(.*?)^\1#endif[ \t]*$', re.S)
        text, count = mode.subn(lambda m: m.group(1) + 'if ( TREESWAY == 2 )\n' + m.group(2), text)
        if count != 3:
            raise RuntimeError('expected three tree-sway mode branches')
    elif name == 'common_ps_fxc.h':
        # BlendPixelFog's final `else if ( NONE )` only returns on a compile-time fog type; folded
        # callers pass a uniform, so the trailing branch becomes the unconditional fall-through.
        tail = ('\telse if( iPIXELFOGTYPE == PIXEL_FOG_TYPE_NONE )\n\t{\n\t\treturn vShaderColor;\n\t}\n}')
        if text.count(tail) != 1:
            if '\treturn vShaderColor; // PIXEL_FOG_TYPE_NONE and any folded uniform fog type\n}' in text:
                return text
            raise RuntimeError('missing BlendPixelFog PIXEL_FOG_TYPE_NONE tail anchor')
        text = text.replace(tail, '\treturn vShaderColor; // PIXEL_FOG_TYPE_NONE and any folded uniform fog type\n}', 1)
    return text
