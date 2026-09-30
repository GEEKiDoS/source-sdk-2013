"""Mechanical legacy (SM2/3, DX9 intrinsics) -> SM5.1 HLSL translation used for the stdshader_dx12 rewrite.

Only syntax changes: samplers become typed texture/sampler pairs wrapped in DX12Sampler* structs, DX9 texture
intrinsics become object methods with the same coordinates, and nothing else in a function body is touched.
Semantics/cbuffers of material entry points are handled by the caller (per material)."""
import re, sys

SAMPLER_DECL = re.compile(r'^([ \t]*)(?:const\s+)?sampler(1D|2D|3D|CUBE)?\s+(\w+)\s*(\[\s*\d+\s*\])?\s*:\s*register\s*\(\s*s(\d+)\s*\)\s*;(.*)$', re.M)
SAMPLER_PARAM = re.compile(r'\b(?:(in|const)\s+)?sampler(1D|2D|3D|CUBE)?\s+(\w+)\b')
INTRINSICS = {
    'tex2D': '2D', 'tex2Dproj': '2D', 'tex2Dlod': '2D', 'tex2Dbias': '2D', 'tex2Dgrad': '2D',
    'texCUBE': 'CUBE', 'texCUBElod': 'CUBE', 'texCUBEbias': 'CUBE', 'texCUBEproj': 'CUBE',
    'tex3D': '3D', 'tex3Dlod': '3D', 'tex3Dproj': '3D', 'tex3Dbias': '3D',
    'tex1D': '1D', 'tex1Dlod': '1D',
}
STRUCT = {'2D': 'DX12Sampler2D', 'CUBE': 'DX12SamplerCube', '3D': 'DX12Sampler3D', '1D': 'DX12Sampler1D', 'SHADOW': 'DX12ShadowSampler'}
TEXTURE = {'2D': 'Texture2D', 'CUBE': 'TextureCube', '3D': 'Texture3D', '1D': 'Texture2D', 'SHADOW': 'Texture2D'}
# Hardware shadow-depth samplers (IShaderShadow::SetShadowDepthFiltering) bind a comparison sampler state.
STATE = {'SHADOW': 'SamplerComparisonState'}


def split_args(text, start):
    """text[start] == '('; returns (args, end_index_after_close_paren)."""
    depth, i, args, cur = 0, start, [], []
    while i < len(text):
        c = text[i]
        if c == '(':
            depth += 1
            if depth > 1: cur.append(c)
        elif c == ')':
            depth -= 1
            if depth == 0:
                args.append(''.join(cur).strip())
                return args, i + 1
            cur.append(c)
        elif c == ',' and depth == 1:
            args.append(''.join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    raise ValueError('unbalanced parentheses near: ' + text[start:start + 80])


def sampler_usage(text):
    """identifier -> dimension from direct intrinsic use."""
    use = {}
    for m in re.finditer(r'\b(' + '|'.join(sorted(INTRINSICS, key=len, reverse=True)) + r')\s*\(', comment_mask(text)):
        args, _ = split_args(text, m.end() - 1)
        name = args[0].strip()
        if re.fullmatch(r'\w+', name): use.setdefault(name, INTRINSICS[m.group(1)])
    return use


FUNC_DEF = re.compile(r'^[ \t]*(?:static\s+|inline\s+)*[\w<>]+\s+(\w+)\s*\(', re.M)


def function_sampler_params(text, known):
    """function name -> {param index: dim} for sampler parameters, resolved through direct use and callees."""
    funcs = {}
    for m in FUNC_DEF.finditer(text):
        name = m.group(1)
        try: params, end = split_args(text, m.end() - 1)
        except ValueError: continue
        rest = text[end:end + 400].lstrip()
        if not rest.startswith('{'): continue
        # body extent by brace matching
        b = text.index('{', end); depth = 0; j = b
        while j < len(text):
            if text[j] == '{': depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        body = text[b:j + 1]
        sp = {}
        for idx, p in enumerate(params):
            pm = SAMPLER_PARAM.search(p)
            if pm: sp[idx] = (pm.group(3), pm.group(2))
        if sp: funcs.setdefault(name, []).append((sp, body))
    changed = True
    while changed:
        changed = False
        for name, overloads in funcs.items():
            for sp, body in overloads:
                use = sampler_usage(body)
                # propagate through calls to other functions with known sampler params
                for callee, cinfo in known.items():
                    for cm in re.finditer(r'\b' + re.escape(callee) + r'\s*\(', body):
                        try: args, _ = split_args(body, cm.end() - 1)
                        except ValueError: continue
                        for ci, dim in cinfo.items():
                            if ci < len(args) and re.fullmatch(r'\w+', args[ci].strip()):
                                use.setdefault(args[ci].strip(), dim)
                info = known.setdefault(name, {})
                for idx, (pname, decl) in sp.items():
                    dim = decl or use.get(pname)
                    if dim and info.get(idx) != dim:
                        info[idx] = dim; changed = True
    return known


def comment_mask(text):
    """Same-length copy with // and /* */ comments blanked (so intrinsic names in comments are not rewritten)."""
    return re.sub(r'//[^\n]*|/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)


def convert_intrinsics(text):
    out, i = [], 0
    pat = re.compile(r'\b(' + '|'.join(sorted(INTRINSICS, key=len, reverse=True)) + r')\s*\(')
    masked = comment_mask(text)
    while True:
        m = pat.search(masked, i)
        if not m:
            out.append(text[i:]); break
        out.append(text[i:m.start()])
        args, end = split_args(text, m.end() - 1)
        args = [convert_intrinsics(a) for a in args]
        fn, s = m.group(1), args[0]
        t, smp = f'{s}.tex', f'{s}.smp'
        if fn in ('tex2D', 'texCUBE', 'tex3D'):
            rep = f'{t}.Sample( {smp}, {args[1]} )' if len(args) == 2 else f'{t}.SampleGrad( {smp}, {args[1]}, {args[2]}, {args[3]} )'
        elif fn == 'tex1D':
            rep = f'{t}.Sample( {smp}, ( {args[1]} ).xx )'
        elif fn in ('tex2Dproj', 'tex3Dproj', 'texCUBEproj'):
            sw = {'tex2Dproj': 'xy', 'tex3Dproj': 'xyz', 'texCUBEproj': 'xyz'}[fn]
            rep = f'{t}.Sample( {smp}, ( {args[1]} ).{sw} / ( {args[1]} ).w )'
        elif fn in ('tex2Dlod', 'tex3Dlod', 'texCUBElod', 'tex1Dlod'):
            sw = {'tex2Dlod': 'xy', 'tex3Dlod': 'xyz', 'texCUBElod': 'xyz', 'tex1Dlod': 'xx'}[fn]
            rep = f'{t}.SampleLevel( {smp}, ( {args[1]} ).{sw}, ( {args[1]} ).w )'
        elif fn in ('tex2Dbias', 'tex3Dbias', 'texCUBEbias'):
            sw = {'tex2Dbias': 'xy', 'tex3Dbias': 'xyz', 'texCUBEbias': 'xyz'}[fn]
            rep = f'{t}.SampleBias( {smp}, ( {args[1]} ).{sw}, ( {args[1]} ).w )'
        elif fn == 'tex2Dgrad':
            rep = f'{t}.SampleGrad( {smp}, {args[1]}, {args[2]}, {args[3]} )'
        else:
            raise ValueError(fn)
        out.append(rep); i = end
    return ''.join(out)


def convert(text, known):
    from collections import Counter
    direct = sampler_usage(text)
    needs = {}
    # every intrinsic use is a need of its dimension (one D3D9 sampler may be read as 2D and as CUBE in different
    # combo branches: vertexlit's CUBEMAP_SPHERE_LEGACY, weapon_sheen's effect index)
    masked_ = comment_mask(text)
    for m in re.finditer(r'\b(' + '|'.join(sorted(INTRINSICS, key=len, reverse=True)) + r')\s*\(', masked_):
        try: args, _ = split_args(text, m.end() - 1)
        except ValueError: continue
        if re.fullmatch(r'\w+', args[0].strip()): needs.setdefault(args[0].strip(), Counter())[INTRINSICS[m.group(1)]] += 1
    # typed helper parameters that receive each global sampler
    for callee, cinfo in known.items():
        for cm in re.finditer(r'\b' + re.escape(callee) + r'\s*\(', comment_mask(text)):
            try: args, _ = split_args(text, cm.end() - 1)
            except ValueError: continue
            for ci, dim in cinfo.items():
                if ci < len(args) and re.fullmatch(r'\w+', args[ci].strip()): needs.setdefault(args[ci].strip(), Counter())[dim] += 1
    primary = {}
    for name in set(direct) | set(needs):
        # a colour sampler also passed as a depth sampler stays colour (the shadow helpers overload DX12Sampler2D)
        ranked = [d for d, _ in needs.get(name, Counter()).most_common() if d != 'SHADOW'] or ['SHADOW']
        primary[name] = direct.get(name) or ranked[0]
    aliases = {}   # (sampler, dim) for helper arguments of another dimension (D3D9 passed one sampler everywhere)

    def decl(m):
        indent, dim, name, arr, reg, tail = m.groups()
        dim = dim or primary.get(name) or '2D'
        primary[name] = dim
        if arr: raise ValueError('sampler arrays need manual translation: ' + name)
        out = (f'{indent}{TEXTURE[dim]} {name}_Texture : register( t{reg} );\n'
               f'{indent}{STATE.get(dim, "SamplerState")} {name}_Sampler : register( s{reg} );{tail}\n'
               f'{indent}static const {STRUCT[dim]} {name} = {{ {name}_Texture, {name}_Sampler }};')
        if dim == 'SHADOW' and len(needs.get(name, {})) > 1:
            raise ValueError('depth sampler also passed as a colour sampler: ' + name)
        # an explicitly typed declaration (sampler2D / samplerCUBE, e.g. declared per combo branch) is only read as
        # its own type; per-use views are for untyped D3D9 `sampler` objects
        for other in ([] if m.group(2) else sorted(set(needs.get(name, {})) - {dim, 'SHADOW'})):
            # Same t/s register viewed with the dimension that helper parameter declares; D3D9 accepted one sampler for
            # both (the other use is a dead dummy argument), SM5 needs a typed view per use.
            aliases[(name, other)] = True
            state = STATE.get(other, 'SamplerState')
            smp = f'{name}_Sampler' if state == STATE.get(dim, 'SamplerState') else f'{name}_Sampler{other}'
            if smp != f'{name}_Sampler': out += f'\n{indent}{state} {smp} : register( s{reg} );'
            out += (f'\n{indent}{TEXTURE[other]} {name}_Texture{other} : register( t{reg} );'
                    f'\n{indent}static const {STRUCT[other]} {name}_{other} = {{ {name}_Texture{other}, {smp} }};')
        return out
    text = SAMPLER_DECL.sub(decl, text)

    # helper calls whose argument sampler has another dimension than the parameter take the typed alias
    if aliases:
        for callee, cinfo in known.items():
            out, i = [], 0
            masked = comment_mask(text)
            for cm in re.finditer(r'\b' + re.escape(callee) + r'\s*\(', masked):
                if cm.start() < i: continue
                try: args, end = split_args(text, cm.end() - 1)
                except ValueError: continue
                changed = False
                for ci, dim in cinfo.items():
                    if ci < len(args) and (args[ci].strip(), dim) in aliases and primary.get(args[ci].strip()) != dim:
                        args[ci] = f' {args[ci].strip()}_{dim}'; changed = True
                if not changed: continue
                out.append(text[i:cm.end() - 1]); out.append('(' + ','.join(args) + ' )'); i = end
            out.append(text[i:]); text = ''.join(out)

    # typed sampler params, per function overload
    out, i = [], 0
    for m in FUNC_DEF.finditer(text):
        name = m.group(1)
        try: args, end = split_args(text, m.end() - 1)
        except ValueError: continue
        if not any(SAMPLER_PARAM.search(a) for a in args): continue
        info = known.get(name, {})
        # retype in place so preprocessor lines inside the parameter list (water_ps2x_helper.h) keep their newlines
        region = text[m.end() - 1:end]
        def arg_index(pos):
            depth = idx = 0
            for ch in region[:pos]:
                if ch in '([': depth += 1
                elif ch in ')]': depth -= 1
                elif ch == ',' and depth == 1: idx += 1
            return idx
        def retype(pm):
            dim = pm.group(2) or info.get(arg_index(pm.start())) or '2D'
            return f'{STRUCT[dim]} {pm.group(3)}'
        out.append(text[i:m.end() - 1]); out.append(SAMPLER_PARAM.sub(retype, region)); i = end
    out.append(text[i:]); text = ''.join(out)
    if aliases:
        out, i = [], 0
        masked_ = comment_mask(text)
        for m in re.finditer(r'\b(' + '|'.join(sorted(INTRINSICS, key=len, reverse=True)) + r')\s*\(', masked_):
            if m.start() < i: continue
            try: args, end = split_args(text, m.end() - 1)
            except ValueError: continue
            name, dim = args[0].strip(), INTRINSICS[m.group(1)]
            if (name, dim) not in aliases or primary.get(name) == dim: continue
            first = text.index(name, m.end())
            out.append(text[i:first]); out.append(f'{name}_{dim}'); i = first + len(name)
        out.append(text[i:]); text = ''.join(out)
    text = convert_intrinsics(text)
    return text


SAMPLER_STRUCTS = '''// Typed texture/sampler pairs replacing DX9 `sampler` objects; t<n>/s<n> keep the legacy sampler index.
struct DX12Sampler2D { Texture2D tex; SamplerState smp; };
struct DX12SamplerCube { TextureCube tex; SamplerState smp; };
struct DX12Sampler3D { Texture3D tex; SamplerState smp; };
// D3D9 has no 1D textures: sampler1D binds a 2D texture and tex1D replicates u (texld r, r.xxxx).
struct DX12Sampler1D { Texture2D tex; SamplerState smp; };
// Shadow depth maps: tex2Dproj is the D3D9 hardware compare (ref = z/w, LESS_EQUAL, filtered by the sampler);
// tex2D reads the stored depth (only non-PCF shadow modes, which the DX12 config never selects).
struct DX12ShadowSampler { Texture2D tex; SamplerComparisonState smp; };
float4 DX12ShadowCompareProj( DX12ShadowSampler s, float4 p ) { return s.tex.SampleCmpLevelZero( s.smp, p.xy / p.w, p.z / p.w ).xxxx; }
float4 DX12ShadowRaw( DX12ShadowSampler s, float2 uv )
{
	uint width, height;
	s.tex.GetDimensions( width, height );
	// Point read with clamp addressing (shadow depth textures are created clamped).
	return s.tex.Load( int3( clamp( int2( floor( uv * float2( width, height ) ) ), int2( 0, 0 ), int2( width, height ) - 1 ), 0 ) );
}
// A colour texture passed as the depth sampler (worldtwotextureblend binds its flashlight cookie there) has no
// comparison in D3D9 either: texldp/texld on a non-depth format are plain projective/regular samples. The shadow
// helpers have DX12Sampler2D overloads (common_flashlight_fxc.h) that end here.
float4 DX12ShadowCompareProj( DX12Sampler2D s, float4 p ) { return s.tex.Sample( s.smp, p.xy / p.w ); }
float4 DX12ShadowRaw( DX12Sampler2D s, float2 uv ) { return s.tex.Sample( s.smp, uv ); }
'''

if __name__ == '__main__':
    import json
    sources = sys.argv[1:]
    known = {}
    texts = [open(s, errors='replace').read() for s in sources]
    for t in texts: function_sampler_params(t, known)
    for t in texts: function_sampler_params(t, known)
    print(json.dumps(known, indent=1))
