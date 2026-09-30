"""Mechanical legacy material shader (.fxc) -> native SM5.1 source for one logical profile.

Pipeline (see sm5 contract):
  1. combo directives resolved for the logical profile (profile qualifiers removed, other profiles dropped);
  2. material includes (non-common) inlined;
  3. register-bound constants hoisted into ONE space-1 material cbuffer named after the legacy source
     (VS b2 / PS b1), every member annotated `// @legacy <stage>:<reg>`;
  4. SHADER_MODEL_* -> DX12_LEGACY_*; samplers/intrinsics via hlslport; material-owned common globals passed
     to the helpers that read them (gencommon.need);
  5. entry:
     VS  output struct: POSITION -> SV_Position, member conditionals flattened (PC), trailing DX12ClipDistances;
         every `return X;` in main -> `return DX12FinishVertex( X );` (clip distances from the uncorrected
         position, D3D9 half-pixel correction, vs20 COLOR saturation like vsconv.cpp:1033-1035);
         input: entry takes DX12_VS_INPUT (integer compressed streams / D3DCOLOR swizzle as the backend layout
         delivers them) converted to the legacy VS_INPUT at the top of main.
     PS  entry takes DX12_PS_INPUT = exact mirror of the paired VS output (types, order, centroid) so the
         signatures link register-for-register; the legacy input struct loses its semantics and is filled by
         semantic at the top of main (VPOS -> floor(SV_Position.xy), VFACE -> +-1); every `return X;` ->
         `return DX12FinishPixel( X, vertexFog );` (alpha test, ps2.x implicit raster fog from FOG/COLOR1.w/0
         exactly like psconv.cpp:1303-1362); COLORn/DEPTH outputs -> SV_Targetn/SV_Depth.
"""
import os, re, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_DX12 = os.path.dirname(_HERE)                                   # materialsystem/stdshaders_dx12
_LEGACY = os.path.join(os.path.dirname(_DX12), 'stdshaders')    # materialsystem/stdshaders
sys.path.insert(0, os.path.dirname(__file__))
import hlslport as hp
import gencommon as gc

SRC = gc.SRC
COMMON_INCLUDES = {c.lower() for c in gc.COMMONS}
PROFILES = {'vs20': ('vs', 'DX12_LEGACY_VS20', 'vs20'), 'vs30': ('vs', 'DX12_LEGACY_VS30', 'vs30'),
            'ps20': ('ps', 'DX12_LEGACY_PS20', 'ps20'), 'ps20b': ('ps', 'DX12_LEGACY_PS20B', 'ps20b'),
            'ps30': ('ps', 'DX12_LEGACY_PS30', 'ps30')}
PROFILE_TAGS = {'vs20', 'vs30', 'ps20', 'ps20b', 'ps30', 'vs11', 'ps11', 'ps14', 'vs14'}

_common_cache = {}


def common_state():
    if not _common_cache:
        out, known, need = gc.translate_all()
        _common_cache.update(out=out, known=known, need=need)
    return _common_cache['known'], _common_cache['need']


# ---------------------------------------------------------------------------------------------------------------
# 1. combo directives
DIRECTIVE = re.compile(r'^(\s*//\s*(STATIC|DYNAMIC|SKIP|CENTROID)\s*:)(.*)$', re.I)


def select_directives(text, profile):
    lines = []
    for line in text.split('\n'):
        m = DIRECTIVE.match(line)
        if not m:
            lines.append(line); continue
        body = m.group(3)
        tags = [t.strip().lower() for t in re.findall(r'\[\s*([A-Za-z0-9_]+)\s*\]', body)]
        if 'xbox' in tags: continue
        prof = [t for t in tags if t in PROFILE_TAGS]
        if prof and profile not in prof: continue
        body = re.sub(r'\[\s*(?:' + '|'.join(PROFILE_TAGS) + r'|PC)\s*\]', '', body, flags=re.I)
        lines.append((m.group(1) + body).rstrip())
    return '\n'.join(lines)


# ---------------------------------------------------------------------------------------------------------------
# 2. material includes
def inline_includes(text, seen=None):
    seen = set() if seen is None else seen
    def repl(m):
        name = m.group(1)
        if name.lower() in COMMON_INCLUDES or name.lower().endswith('.inc'): return m.group(0)
        path = os.path.join(SRC, name)
        if not os.path.exists(path): raise RuntimeError('missing include ' + name)
        if name.lower() in seen: return f'// {name} already inlined\n'
        seen.add(name.lower())
        body = open(path, encoding='latin-1').read()
        return f'// ---- inlined {name} ----\n' + inline_includes(body, seen) + f'\n// ---- end {name} ----\n'
    return re.sub(r'^[ \t]*#\s*include\s+"([^"]+)"[^\n]*$', repl, text, flags=re.M)


# ---------------------------------------------------------------------------------------------------------------
# 3. register constants
TYPE = r'(?:PixelShaderLightInfo|bool|int\d?|uint\d?|float\d?(?:x\d)?|half\d?(?:x\d)?|HALF\d?(?:x\d)?)'
REGDECL = re.compile(r'^[ \t]*(?:static\s+)?(?:const\s+)?(?:uniform\s+)?((?:row_major\s+|column_major\s+)?' + TYPE +
                     r')\s+(\w+)\s*(\[\s*[^\]]+\])?\s*:\s*register\s*\(\s*(\w+)\s*\)\s*;([^\n]*)$', re.M)


def define_map(texts):
    defs = {}
    for t in texts:
        for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+([A-Za-z_]\w*|[cbi]\d+)[ \t]*(?://[^\n]*)?$', t, re.M):
            defs.setdefault(m.group(1), []).append(m.group(2))
    return defs


def resolve_register(name, defs, depth=0):
    if re.fullmatch(r'[cbi]\d+', name): return name
    if depth > 8 or name not in defs: raise RuntimeError('unresolved register ' + name)
    # SHADER_SPECIFIC_CONST_n has a vs11 branch first and the DX9 branch last: the DX9 (last) definition wins.
    return resolve_register(defs[name][-1], defs, depth + 1)


def hoist_constants(text, stage, cbname, defs, extra_members=(), engine_regs=None):
    members, order = {}, []
    by_reg = {}
    aliases = []
    engine_aliases = {}
    first_decl_pos = []
    removed = [0]
    first = REGDECL.search(text)
    if first: first_decl_pos.append(first.start())
    def collect(m):
        typ, name, arr, reg, tail = m.groups()
        reg = resolve_register(reg, defs)
        if engine_regs and reg in engine_regs and engine_shape_matches(engine_regs[reg], typ, arr):
            # written by an engine API at a material-chosen register -> DX12PSEngine member
            if name != engine_regs[reg]: engine_aliases[name] = engine_regs[reg]
            return ''
        key = name
        # A combo branch may define the same name as a literal (`static const g_EnvmapContrast = ...` on the fast
        # path): the register member gets a private cbuffer name and the legacy name maps to it only where the
        # register declaration stood, i.e. under the same conditional.
        mapped = ''
        if name in static_names:
            key = f'{name}_{reg}'
            mapped = f'#define {name} {key}'
        if key in members:
            if members[key][1] != reg or members[key][0] != typ.strip() or members[key][2] != (arr or ''):
                raise RuntimeError(f'{cbname}: {name} declared twice differently ({members[key]} vs {typ} {reg} {arr})')
            return mapped
        # Several names may share a legacy register (declared in different combo branches): each member gathers
        # the same staged registers in C++, so the backend mirror scatters identical values for all of them.
        members[key] = (typ.strip(), reg, arr or '', tail.strip()); order.append(key); by_reg[reg] = key
        return mapped
    static_names = set(re.findall(r'^[ \t]*static\s+const\s+\w+\s+(\w+)\s*(?:\[[^\]]*\])?\s*=', text, re.M))
    text = REGDECL.sub(collect, text)
    for name, (typ, reg) in extra_members:
        if name in members: continue
        arr = ''
        if ' [' in typ: typ, arr = typ.split(' ', 1)
        members[name] = (typ, reg, arr, ''); order.append(name)
    engine_lines = ''.join(f'#define {a} {b}\t// engine-owned (DX12PSEngine)\n' for a, b in engine_aliases.items())
    if not members:
        if engine_lines:
            incs = [m for m in re.finditer(r'^[ \t]*#\s*include\s+"[^"]+"[^\n]*\n', text, re.M)]
            pos = incs[-1].end() if incs else 0
            text = text[:pos] + engine_lines + text[pos:]
        return text, None
    bank = {'c': 0, 'i': 1, 'b': 2}
    first = {n: i for i, n in enumerate(order)}
    order.sort(key=lambda n: (bank[members[n][1][0]], int(members[n][1][1:]), first[n]))
    slot = 'b2' if stage == 'vs' else 'b1'
    lines = [f'cbuffer {cbname} : register( {slot}, space1 )', '{']
    converted = []
    for n in order:
        typ, reg, arr, tail = members[n]
        # int/bool declared at a float register: the DX9 compiler reads the float register directly (compares and
        # arithmetic on c#.x, no integer conversion; see compositor_ps2x), so the member is the same-width float.
        mm = re.match(r'(u?int|bool)(\d?)$', typ.split()[-1])
        if reg[0] == 'c' and mm:
            typ = re.sub(r'(u?int|bool)(\d?)$', lambda q: 'float' + q.group(2), typ)
        lines.append(f'\t{typ} {n}{arr}; // @legacy {stage}:{reg}')
    lines.append('};')
    lines += converted
    for a, b in aliases: lines.append(f'#define {a} {b}\t// same legacy register as {b}')
    block = '\n'.join(lines) + '\n' + engine_lines
    # where the first register constant was declared (hoisted out of any enclosing conditional), so helpers
    # included later (tree_sway.h, ...) see the members exactly as they saw the legacy globals
    pos = first_decl_pos[0] if first_decl_pos else 0
    pos = outermost_block_start(text, pos)
    return text[:pos] + '\n' + block + text[pos:], [(n, members[n]) for n in order]


def outermost_block_start(text, pos):
    """Start of the line opening the outermost #if block that contains pos (pos itself at depth 0)."""
    stack = []
    for m in re.finditer(r'^[ \t]*#\s*(if|ifdef|ifndef|endif)\b[^\n]*', text[:pos], re.M):
        if m.group(1) == 'endif': stack.pop()
        else: stack.append(m.start())
    return stack[0] if stack else text.rfind('\n', 0, pos) + 1


def preproc_depth(text, pos):
    depth = 0
    for m in re.finditer(r'^[ \t]*#\s*(if|ifdef|ifndef|endif)\b', text[:pos], re.M):
        depth += -1 if m.group(1) == 'endif' else 1
    return depth


# ---------------------------------------------------------------------------------------------------------------
# helpers for structs / main
def find_struct(text, name):
    m = re.search(r'^[ \t]*struct\s+' + re.escape(name) + r'\s*\{', text, re.M)
    if not m: return None
    j = text.index('{', m.start()); depth = 0; k = j
    while k < len(text):
        if text[k] == '{': depth += 1
        elif text[k] == '}':
            depth -= 1
            if depth == 0: break
        k += 1
    end = text.index(';', k) + 1
    return m.start(), j + 1, k, end  # struct start, body start, body end ('}'), after ';'


MEMBER = re.compile(r'^([ \t]*)((?:centroid\s+|linear\s+|noperspective\s+|nointerpolation\s+)*)([\w]+)\s+(\w+)\s*(\[[^\]]*\])?\s*:\s*(\w+)\s*;([^\n]*)$')


def eval_pc_condition(cond):
    """True/False for conditions that only involve _X360 / SHADER_MODEL / DX12_LEGACY; None if combo dependent."""
    c = re.sub(r'//.*|/\*.*?\*/', '', cond).strip()
    if re.search(r'\b(?!defined\b)(?!_X360\b)(?!SHADER_MODEL_\w+)(?!DX12_LEGACY_\w+)[A-Za-z_]\w*', c): return None
    c = re.sub(r'defined\s*\(\s*_X360\s*\)|defined\s+_X360', '0', c)
    c = re.sub(r'defined\s*\(\s*(SHADER_MODEL_\w+|DX12_LEGACY_\w+)\s*\)', '0', c)
    c = c.replace('!', ' not ').replace('&&', ' and ').replace('||', ' or ')
    try: return bool(eval(c))
    except Exception: return None


def struct_members(body, flatten=True):
    """[(prefix, type, name, array, semantic, comment)] with _X360 conditionals evaluated (PC) and combo-dependent
    conditionals flattened (union of all branches)."""
    out, stack = [], []
    for line in body.split('\n'):
        s = line.strip()
        pm = re.match(r'#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', s)
        if pm:
            kind, cond = pm.group(1), pm.group(2)
            if kind == 'ifdef': cond = f'defined({cond.strip()})'
            if kind == 'ifndef': cond = f'!defined({cond.strip()})'
            if kind in ('if', 'ifdef', 'ifndef'): stack.append(eval_pc_condition(cond))
            elif kind == 'else': stack[-1] = None if stack[-1] is None else (not stack[-1])
            elif kind == 'elif': stack[-1] = None
            elif kind == 'endif': stack.pop()
            continue
        if any(v is False for v in stack): continue
        m = MEMBER.match(line)
        if m: out.append((m.group(2).strip(), m.group(3), m.group(4), m.group(5) or '', m.group(6), m.group(7).strip()))
        elif s and not s.startswith('//'): raise RuntimeError('unparsed struct line: ' + s)
    return out


def norm_sem(sem):
    s = sem.upper()
    if s in ('POSITION', 'POSITION0', 'SV_POSITION'): return 'SV_POSITION'
    m = re.fullmatch(r'([A-Z_]+?)(\d*)', s)
    return m.group(1) + (m.group(2) or '0')


def mask(text):
    """Same-length copy with comments, string literals and `#if 0` blocks blanked (newlines kept)."""
    out = list(text); i = 0; n = len(text)
    def blank(a, b):
        for k in range(a, b):
            if out[k] != '\n': out[k] = ' '
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i); j = n if j < 0 else j; blank(i, j); i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2); j = n if j < 0 else j + 2; blank(i, j); i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] not in '"\n': j += 2 if text[j] == '\\' else 1
            blank(i, min(j + 1, n)); i = j + 1
        else: i += 1
    masked = ''.join(out)
    for m in re.finditer(r'^[ \t]*#[ \t]*if[ \t]+0\b', masked, re.M):
        depth = 0
        for d in re.finditer(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b', masked[m.start():], re.M):
            depth += -1 if d.group(1) == 'endif' else 1
            if depth == 0: blank(m.start(), m.start() + d.end()); break
    return ''.join(out)


def find_mains(text):
    masked = mask(text)
    heads = [m for m in re.finditer(r'\bmain\s*\(', masked)]
    if not heads: raise RuntimeError('no main')
    groups = []
    for h in heads:
        args, end = hp.split_args(text, h.end() - 1)
        nb = masked.index('{', end)
        groups.append((h, args, end, nb))
    # body: from the brace after the last header
    body_start = groups[-1][3]
    depth = 0; k = body_start
    while k < len(text):
        if text[k] == '{': depth += 1
        elif text[k] == '}':
            depth -= 1
            if depth == 0: break
        k += 1
    return groups, body_start, k


def main_defs(text):
    """[(headers [(match, args, end, nb)], body_start, body_end)] per distinct main body (headers that share one body,
    e.g. #if/#else alternative signatures, are grouped)."""
    groups, _, _ = find_mains(text)
    masked = mask(text)
    defs = {}
    for g in groups: defs.setdefault(g[3], []).append(g)
    out = []
    for nb, hs in sorted(defs.items()):
        depth = 0; k = nb
        while k < len(masked):
            if masked[k] == '{': depth += 1
            elif masked[k] == '}':
                depth -= 1
                if depth == 0: break
            k += 1
        out.append((hs, nb, k))
    return out


def replace_returns(text, start, end, fmt):
    body = text[start:end]
    mbody = mask(text)[start:end]   # comments/strings/#if 0 ignored
    out, i = [], 0
    for m in re.finditer(r'\breturn\b', mbody):
        if m.start() < i: continue
        j = m.end(); depth = 0
        while j < len(body):
            c = mbody[j]
            if c in '([{': depth += 1
            elif c in ')]}': depth -= 1
            elif c == ';' and depth == 0: break
            j += 1
        expr = body[m.end():j].strip()
        out.append(body[i:m.start()]); out.append('return ' + fmt(expr)); i = j  # keeps the original ';'
    out.append(body[i:])
    return text[:start] + ''.join(out) + text[end:]


def param_parts(p):
    p = re.sub(r'//[^\n]*', '', p).strip()
    m = re.match(r'((?:(?:in|out|inout|const|uniform)\s+)*)(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*(?::\s*(\w+))?\s*$', p)
    if not m: raise RuntimeError('unparsed main parameter: ' + p)
    return m.group(1).strip(), m.group(2), m.group(3), m.group(4) or '', m.group(5)


# ---------------------------------------------------------------------------------------------------------------
# 5a. vertex entry
INT_STREAMS = {'NORMAL0': 'uint4', 'BLENDWEIGHT0': 'int2', 'TANGENT0': 'uint4'}
SWAP_STREAMS = {'COLOR0', 'COLOR1', 'BLENDINDICES0'}


def width(typ):
    m = re.search(r'(\d)$', typ)
    return int(m.group(1)) if m and 'x' not in typ else (1 if re.fullmatch(r'(float|half|HALF|int|uint|bool)', typ) else 4)


def fit(expr, typ):
    n = width(typ)
    return f'( {expr} ).{"xyzw"[:n]}' if n < 4 else expr


def vertex_entry(text, profile, centroid_semantics=(), flatten=frozenset()):
    text = structure_params(text, 'DX12_LEGACY_VS_INPUT', 'dx12Legacy')
    groups, bs, be = find_mains(text)
    if len(groups) != 1: raise RuntimeError('vertex shader with several main headers')
    h, args, end, nb = groups[0]
    line_start = text.rfind('\n', 0, h.start()) + 1
    ret = text[line_start:h.start()].split()[-1]
    if len(args) != 1: raise RuntimeError('vertex main must take one input struct')
    q, in_type, in_name, _, _ = param_parts(args[0])
    # output struct
    st = find_struct(text, ret)
    if not st: raise RuntimeError('vertex output struct not found: ' + ret)
    s0, b0, b1, s1 = st
    csem = {norm_sem(c) for c in centroid_semantics}
    # Conditionals are kept: the paired pixel shader mirrors this exact text, so both compile the same members
    # for the same combo values (the generator checks the pixel shader has every macro the conditionals use).
    lines, fin_body, pos_name, interp_lines, members = [], [], None, [], []
    flat_stack = []   # per open #if: True when that conditional is flattened (its branches are all kept)
    path, counter, keyed = [], [0], set()   # kept-conditional branch path of every member (duplicate detection)
    conds = []   # parallel to path: [expression, taken-branch polarity] of each kept conditional
    member_conds = {}
    for line in text[b0:b1].split('\n'):
        st_ = line.strip()
        if st_.startswith('#'):
            pm = re.match(r'#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', st_)
            kind = pm.group(1) if pm else ''
            if kind in ('if', 'ifdef', 'ifndef'):
                used = set(re.findall(r'\b[A-Z_][A-Z0-9_]*\b', re.sub(r'//.*', '', pm.group(2))))
                flat_stack.append(bool(used & set(flatten)))
                if not flat_stack[-1]:
                    counter[0] += 1; path.append((counter[0], 0))
                    expr = re.sub(r'//.*', '', pm.group(2)).strip()
                    if kind == 'ifdef': expr = f'defined({expr})'
                    if kind == 'ifndef': expr = f'!defined({expr})'
                    conds.append([expr, True])
                else: continue
            elif flat_stack and flat_stack[-1]:
                if kind == 'endif': flat_stack.pop()
                continue
            elif kind in ('else', 'elif') and path:
                path[-1] = (path[-1][0], path[-1][1] + 1)
                conds[-1] = [conds[-1][0], False] if kind == 'else' else [None, None]
            elif kind == 'endif':
                if flat_stack: flat_stack.pop()
                if path: path.pop(); conds.pop()
            lines.append(line); fin_body.append(line); interp_lines.append(line); continue
        mm = MEMBER.match(line)
        if not mm:
            if st_ and not st_.startswith('//'): raise RuntimeError('unparsed vertex output line: ' + st_)
            continue
        ind, pre, typ, name, arr, sem, com = mm.groups()
        ns = norm_sem(sem)
        key = (tuple(path), ns)
        if key in keyed: raise RuntimeError('flattening vertex output conditionals duplicates a semantic: ' + sem)
        keyed.add(key)
        if ns == 'SV_POSITION':
            if pos_name: raise RuntimeError('vertex output declares POSITION twice')
            pos_name = name; sem = 'SV_Position'
        pre = (('centroid ' if ns in csem else '') + pre.strip() + ' ').lstrip()
        decl = f'\t{pre}{typ} {name}{arr or ""} : {sem};' + (f' {com}' if com else '')
        lines.append(decl); interp_lines.append(f'\t{pre}{typ} {name}{arr or ""} : {sem};')
        members.append((pre, typ, name, arr or '', sem))
        member_conds[len(members) - 1] = [tuple(c) for c in conds]
        if ns.startswith('COLOR') and profile == 'vs20':
            fin_body.append(f'\to.{name} = saturate( o.{name} );\t// vs_2_0 oD# clamp (vsconv.cpp:1033-1035)')
    if not pos_name: raise RuntimeError('vertex output has no POSITION')
    lines.append('\tDX12ClipDistances dx12Clip;\t// SV_ClipDistance0/1 (trailing; pixel inputs omit it)')
    macros_used = set()
    for l in interp_lines:
        if l.strip().startswith('#'):
            macros_used |= set(re.findall(r'\b(?!defined\b)([A-Z_][A-Z0-9_]*)\b', re.sub(r'//.*', '', l.strip()[1:].split(None, 1)[1] if len(l.strip()[1:].split(None, 1)) > 1 else '')))
    macros_used = {m for m in macros_used if not m.startswith(('SHADER_MODEL_', 'DX12_LEGACY_'))} - {'_X360', 'IF', 'IFDEF', 'IFNDEF', 'ELIF', 'ELSE', 'ENDIF'}
    interp = {'lines': interp_lines, 'members': members, 'macros': macros_used, 'conds': member_conds}
    new_struct = f'struct {ret}\n{{\n' + '\n'.join(lines) + '\n};\n'
    fin = [f'{ret} DX12FinishVertex( {ret} o )', '{',
           f'\to.dx12Clip = DX12UserClipDistances( o.{pos_name} );',
           f'\to.{pos_name} = DX12Position( o.{pos_name} );'] + [l for l in fin_body if l.strip()] + ['\treturn o;', '}']
    # drop preprocessor-only noise (empty #if/#endif pairs are harmless)
    text = text[:s0] + new_struct + '\n'.join(fin) + '\n' + text[s1:]
    # input struct
    st = find_struct(text, in_type)
    if not st: raise RuntimeError('vertex input struct not found: ' + in_type)
    s0, b0, b1, s1 = st
    body = text[b0:b1]
    dx_lines, conv = [], []
    # D3D9 let two members share one input semantic (e.g. vTangentS/vUserData : TANGENT); SM5 declares the element
    # once (widest type) and every legacy member reads it.
    widest, first_name, names = {}, {}, {}
    for line in body.split('\n'):
        m = MEMBER.match(line)
        if m:
            ns_ = norm_sem(m.group(6))
            if width(m.group(3)) >= widest.get(ns_, (0, ''))[0]: widest[ns_] = (width(m.group(3)), m.group(3))
            first_name.setdefault(ns_, m.group(4))
            names.setdefault(ns_, set()).add(m.group(4))
    # A shared semantic's members usually sit in opposite branches of a combo conditional (#if !MODEL / #else), so
    # its single SM5 element is declared unconditionally ahead of the conditional members.
    shared = {ns_ for ns_, n_ in names.items() if len(n_) > 1}
    for ns_ in sorted(shared, key=lambda n_: body.index(first_name[n_])):
        src_name, wide = first_name[ns_], widest[ns_][1]
        sem_ = next(m.group(6) for m in map(MEMBER.match, body.split('\n')) if m and m.group(4) == src_name)
        if ns_ in INT_STREAMS: dx_lines += ['#if COMPRESSED_VERTS', f'\t{INT_STREAMS[ns_]} {src_name} : {sem_};', '#else', f'\t{wide} {src_name} : {sem_};', '#endif']
        else: dx_lines.append(f'\t{wide} {src_name} : {sem_};')
    for line in body.split('\n'):
        s = line.strip()
        if re.match(r'#', s): dx_lines.append(line); conv.append(line); continue
        m = MEMBER.match(line)
        if not m:
            if s and not s.startswith('//'): raise RuntimeError('unparsed input line: ' + s)
            continue
        ind, pre, typ, name, arr, sem, com = m.groups()
        ns = norm_sem(sem)
        if ns in shared:
            src_name = first_name[ns]
            if ns in INT_STREAMS:
                n = int(INT_STREAMS[ns][-1])
                full = 'float4( (float2)i.%s, 0, 1 )' % src_name if n == 2 else f'(float4)i.{src_name}'
                conv += ['#if COMPRESSED_VERTS', f'\tl.{name} = {fit(full, typ)};', '#else', f'\tl.{name} = {fit("i." + src_name, typ) if width(typ) < widest[ns][0] else "i." + src_name};', '#endif']
            else:
                conv.append(f'\tl.{name} = {fit("i." + src_name, typ) if width(typ) < widest[ns][0] else "i." + src_name};')
            continue
        if widest[ns][1] != typ and width(widest[ns][1]) > width(typ):
            wide_typ = widest[ns][1]
        else:
            wide_typ = typ
        if ns in INT_STREAMS:
            it = INT_STREAMS[ns]
            dx_lines += ['#if COMPRESSED_VERTS', f'\t{it} {name} : {sem};', '#else', f'\t{wide_typ} {name} : {sem};', '#endif']
            n = int(it[-1])
            full = 'float4( (float2)i.%s, 0, 1 )' % name if n == 2 else f'(float4)i.{name}'
            conv += ['#if COMPRESSED_VERTS', f'\tl.{name} = {fit(full, typ)};', '#else', f'\tl.{name} = {fit("i." + name, typ) if wide_typ != typ else "i." + name};', '#endif']
        elif ns in SWAP_STREAMS:
            # D3DCOLOR streams: the backend swaps the bytes in the vertex buffer (resources_dx12.h SwapOffsets), so
            # the R8G8B8A8_UNORM fetch already equals the D3D9 D3DCOLOR fetch.
            dx_lines.append(f'\tfloat4 {name} : {sem};')
            conv.append(f'\tl.{name} = {fit(f"i.{name}", typ)};')
        else:
            dx_lines.append(f'\t{wide_typ} {name}{arr or ""} : {sem};')
            conv.append(f'\tl.{name} = {fit("i." + name, typ) if wide_typ != typ else "i." + name};')
    dx_struct = (f'// Entry input as the DX12 input layout delivers it (vertex_layout_dx12.cpp): integer compressed\n'
                 f'// streams and D3DCOLOR byte order are converted exactly like the D3D9 declaration types.\n'
                 f'struct DX12_{in_type}\n{{\n' + '\n'.join(dx_lines) + '\n};\n'
                 f'{in_type} DX12ConvertInput( DX12_{in_type} i )\n{{\n\t{in_type} l = ({in_type})0;\n' + '\n'.join(conv) + '\n\treturn l;\n}\n')
    # strip semantics in legacy input struct (plain struct)
    plain = re.sub(r'\s*:\s*\w+\s*;', ';', text[b0:b1])
    text = text[:b0] + plain + text[b1:s1] + '\n' + dx_struct + text[s1:]
    groups, bs, be = find_mains(text)
    h, args, end, nb = groups[0]
    # an output local declared without initializer is zero-initialized (members a shader leaves unwritten were
    # undefined in D3D9; SM5 requires every output written)
    body = text[bs:be]
    body2 = re.sub(r'(\b' + re.escape(ret) + r'\s+(\w+))\s*;', lambda m: f'{m.group(1)} = ({ret})0;', body, count=1)
    text = text[:bs] + body2 + text[be:]
    groups, bs, be = find_mains(text)
    text = replace_returns(text, bs + 1, be, lambda e: f'DX12FinishVertex( {e} )')
    groups, bs, be = find_mains(text)
    h, args, end, nb = groups[0]
    new_head = f'main( const DX12_{in_type} dx12In )'
    text = text[:h.start()] + new_head + text[end:bs + 1] + f'\n\t{"const " if "const" in q else ""}{in_type} {in_name} = DX12ConvertInput( dx12In );' + text[bs + 1:]
    return text, interp


# ---------------------------------------------------------------------------------------------------------------
# 5b. pixel entry
def output_semantic(sem):
    ns = norm_sem(sem)
    if ns.startswith('COLOR'): return 'SV_Target' + ns[5:]
    if ns.startswith('DEPTH'): return 'SV_Depth'
    return sem


def structure_params(text, struct_name, var_name):
    """main( type a : SEM, ... ) -> main( struct_name var_name ) with a synthesized input struct and locals, so
    parameter-list entry points go through the same struct path. Returns text unchanged for struct mains."""
    groups, bs, be = find_mains(text)
    if len(groups) != 1: return text
    h, args, end, nb = groups[0]
    parts = [param_parts(a) for a in args if a.strip()]
    if len(parts) == 1 and parts[0][4] is None: return text
    if any(q and 'out' in q.split() for q, *_ in parts): raise RuntimeError('main with out parameters needs manual translation')
    if any(sem is None for *_, sem in parts): raise RuntimeError('main parameter without semantic')
    members = ''.join(f'\t{t} {n}{a} : {sem};\n' for q, t, n, a, sem in parts)
    locals_ = ''.join(f'\n\t{t} {n}{a} = {var_name}.{n};' for q, t, n, a, sem in parts)
    line_start = text.rfind('\n', 0, h.start()) + 1
    text = (text[:line_start] + f'struct {struct_name}\n{{\n{members}}};\n\n' + text[line_start:h.start()] +
            f'main( {struct_name} {var_name} )' + text[end:bs + 1] + locals_ + text[bs + 1:])
    return text


def member_live(conds, fixed):
    """False when some enclosing conditional of a vertex output member is decided against it by `fixed` values."""
    for expr, polarity in conds:
        if expr is None: continue
        e = re.sub(r'//.*', '', expr)
        e = re.sub(r'defined\s*\(?\s*(\w+)\s*\)?', lambda m: '1' if m.group(1) in fixed else ('0' if m.group(1) == '_X360' else 'None'), e)
        names = set(re.findall(r'\b[A-Za-z_]\w*\b', e)) - {'None'}
        if any(n not in fixed for n in names): continue
        for n in names: e = re.sub(r'\b' + n + r'\b', str(fixed[n]), e)
        e = e.replace('!', ' not ').replace('&&', ' and ').replace('||', ' or ').replace('not =', '!=')
        try: v = bool(eval(e))
        except Exception: continue
        if v != polarity: return False
    return True


def pixel_entry(text, profile, interp, fixed=None):
    text = structure_params(text, 'DX12_LEGACY_PS_INPUT', 'dx12Legacy')
    groups, bs, be = find_mains(text)
    # legacy input: single struct parameter shared by every header
    in_types = set()
    for h, args, end, nb in groups:
        if len(args) != 1: raise RuntimeError('pixel main must take one input struct')
        in_types.add(param_parts(args[0])[1:3])
    if len(in_types) != 1: raise RuntimeError('pixel main headers disagree on input')
    in_type, in_name = in_types.pop()
    st = find_struct(text, in_type)
    s0, b0, b1, s1 = st
    # DX12 input mirrors the vertex output text, conditionals included
    ps_macros = set(re.findall(r'^\s*//\s*(?:STATIC|DYNAMIC)\s*:\s*"(\w+)"', text, re.M | re.I))
    ps_macros |= set(re.findall(r'^\s*#\s*define\s+(\w+)', text, re.M))
    fixed = fixed or {}
    missing = interp['macros'] - ps_macros - set(fixed)
    if missing: raise MissingMacros(missing)
    dx = interp['lines']
    members = interp['members']
    fog = next((f'dx12In.{name}' + ('' if width(typ) == 1 else '.x') for pre, typ, name, arr, sem in members if norm_sem(sem) == 'FOG0'), None)
    if fog is None:
        fog = next((f'dx12In.{name}.w' for pre, typ, name, arr, sem in members if norm_sem(sem) == 'COLOR1'), '0.0f')
    interp_by_sem = {}
    for idx, (pre, typ, name, arr, sem) in enumerate(members): interp_by_sem.setdefault(norm_sem(sem), []).append((typ, name, idx))
    conv, face = [], False
    if 'SV_POSITION' not in interp_by_sem: raise RuntimeError('paired vertex output lacks a position')
    pos = interp_by_sem['SV_POSITION'][0][1]
    fixed = fixed or {}
    # line by line so members inside combo conditionals are converted under the same conditionals
    for line in text[b0:b1].split('\n'):
        st_ = line.strip()
        if st_.startswith('#'): conv.append(line); continue
        mm = MEMBER.match(line)
        if not mm:
            if st_ and not st_.startswith('//'): raise RuntimeError('unparsed pixel input line: ' + st_)
            continue
        typ, name, sem = mm.group(3), mm.group(4), mm.group(6)
        ns = norm_sem(sem)
        if ns == 'VPOS0':
            conv.append(f'\tl.{name} = {fit(f"floor( dx12In.{pos}.xy )", typ)};\t// VPOS: round_ni (psconv.cpp:1115-1124)')
        elif ns == 'VFACE0':
            face = True
            conv.append(f'\tl.{name} = dx12FrontFace ? 1.0f : -1.0f;\t// VFACE (psconv.cpp:1126-1137)')
        elif ns == 'SV_POSITION':
            conv.append(f'\t// {name} : {sem} (ps2.x cannot read POSITION)')
        elif ns in interp_by_sem:
            cands = interp_by_sem[ns]
            if len(cands) > 1 and fixed:
                # drop vertex members whose conditionals are false for the values the material always uses
                live = [c for c in cands if member_live(interp['conds'].get(c[2], []), fixed) is not False]
                cands = live or cands
            declarations = cands
            cands = list({(c[0], c[1]): c for c in declarations}.values())
            # D3D9 links by semantic, not member name. A combo can rename the vertex member (vertexlit's
            # SeamlessTexCoord/baseTexCoord); read every alternative under its declaration's guards.
            for it, iname, iidx in cands:
                wl, wi = width(typ), width(it)
                if 'x' in typ or 'x' in it: read = f'\tl.{name} = dx12In.{iname};'
                elif wl < wi: read = f'\tl.{name} = {fit("dx12In." + iname, typ)};'
                elif wl > wi: read = f'\tl.{name}.{"xyzw"[:wi]} = dx12In.{iname};'
                else: read = f'\tl.{name} = dx12In.{iname};'
                # Repeated same-name declarations are live in the UNION of their branches.
                # Fixed values are #defined around DX12ConvertInput, so their guards resolve there too.
                alternatives = [interp['conds'].get(c[2], []) for c in declarations if c[:2] == (it, iname)]
                if any(e is None for conds in alternatives for e, _ in conds):
                    raise RuntimeError(f'{name} : {sem} reads a vertex member under #elif')
                if all(alternatives):
                    guards = [' && '.join(f'( {e} )' if p else f'!( {e} )' for e, p in conds) for conds in alternatives]
                    conv += ['#if ' + ' || '.join(f'( {g} )' for g in guards), read, '#endif']
                elif len(cands) == 1:
                    conv.append(read)
                else:
                    raise RuntimeError(f'{name} : {sem} matches several unguarded vertex outputs {cands}')
        else:
            conv.append(f'\t// {name} : {sem} is not written by the paired vertex shader (reads 0)')
    fixed_defs = ''.join(f'#define {k} {v}\t// value the material always gives the paired vertex shader\n' for k, v in sorted(fixed.items()))
    fixed_undefs = ''.join(f'#undef {k}\n' for k in sorted(fixed))
    dx_struct = ('// Exact mirror of the paired vertex output (same types, order and interpolation) so the signatures\n'
                 '// link register-for-register.\n' + fixed_defs + 'struct DX12_PS_INPUT\n{\n' + '\n'.join(dx) + '\n};\n'
                 f'{in_type} DX12ConvertInput( DX12_PS_INPUT dx12In{", bool dx12FrontFace" if face else ""} )\n{{\n'
                 f'\t{in_type} l = ({in_type})0;\n' + '\n'.join(conv) + '\n\treturn l;\n}\n' + fixed_undefs)
    plain = re.sub(r'\s*:\s*\w+\s*;', ';', text[b0:b1])
    text = text[:b0] + plain + text[b1:s1] + '\n' + dx_struct + text[s1:]
    # output structs used as return types: semantics -> SV_Target/SV_Depth + finish overload
    groups, bs, be = find_mains(text)
    finish = []
    for h, args, end, nb in groups:
        line_start = text.rfind('\n', 0, h.start()) + 1
        ret = text[line_start:h.start()].split()[-1]
        if ret in ('float4', 'HALF4', 'half4', 'LPREVIEW_PS_OUT') or ret in [f[0] for f in finish]: continue
        st = find_struct(text, ret)
        if not st: raise RuntimeError('pixel output struct not found: ' + ret)
        s0, b0, b1, s1 = st
        mem = struct_members(text[b0:b1])
        c0 = next((n + ('[0]' if a else '') for pre, t, n, a, sem, com in mem if norm_sem(sem) == 'COLOR0'), None)
        body = re.sub(r':\s*(\w+)\s*;', lambda m: f': {output_semantic(m.group(1))};', text[b0:b1])
        over = (f'{ret} DX12FinishPixel( {ret} o, float vertexFog )\n{{\n'
                + (f'\to.{c0} = DX12FinishPixel( o.{c0}, vertexFog );\n' if c0 else '') + '\treturn o;\n}\n')
        text = text[:b0] + body + text[b1:s1] + '\n' + over + text[s1:]
        finish.append((ret,))
        groups, bs, be = find_mains(text)
    extra = ', bool dx12FrontFace : SV_IsFrontFace' if face else ''
    for k in reversed(range(len(main_defs(text)))):
        hs, bs, be = main_defs(text)[k]
        text = replace_returns(text, bs + 1, be, lambda e: f'DX12FinishPixel( {e}, {fog} )')
        hs, bs, be = main_defs(text)[k]
        text = text[:bs + 1] + f'\n\t{in_type} {in_name} = DX12ConvertInput( dx12In{", dx12FrontFace" if face else ""} );' + text[bs + 1:]
        # headers: rename param, fix return semantic
        for h, args, end, nb in reversed(hs):
            line_start = text.rfind('\n', 0, h.start()) + 1
            ret = text[line_start:h.start()].split()[-1]
            sem = ' : SV_Target0' if ret in ('float4', 'HALF4', 'half4') else ''
            m = re.match(r'\s*:\s*\w+', text[end:])
            after = end + (m.end() if m else 0)
            text = text[:h.start()] + f'main( DX12_PS_INPUT dx12In{extra} ){sem}' + text[after:]
    return text


# ---------------------------------------------------------------------------------------------------------------
def centroid_semantics(source, profile):
    """TEXCOORDn named by `// CENTROID:` directives of a legacy source (material includes inlined)."""
    text = open(os.path.join(SRC, source), encoding='latin-1').read().replace('\r\n', '\n')
    text = inline_includes(select_directives(text, PROFILES[profile][2]))
    return {m.group(1) for m in re.finditer(r'^\s*//\s*CENTROID\s*:\s*"?(\w+)"?', text, re.M | re.I)}


class MissingMacros(RuntimeError):
    def __init__(self, macros): super().__init__('vertex output conditionals use macros the pixel shader lacks: ' + ', '.join(sorted(macros))); self.macros = set(macros)


# Shipped combo contracts that predate the repository sources. Apply these to BOTH the native translation and
# the generated legacy_reference source compiled -dynamic, so the packer's range/order/skip check stays strict.
ABI_PATCHES = {
    'worldtwotextureblend_ps2x.fxc': [
        ('// DYNAMIC: "PIXELFOGTYPE"\t\t\t\t\t"0..2"', '// DYNAMIC: "PIXELFOGTYPE"\t\t\t\t\t"0..1"'),
    ],
}


def legacy_reference(source):
    text = open(os.path.join(SRC, source), encoding='latin-1').read().replace('\r\n', '\n')
    for old, new in ABI_PATCHES.get(source, []):
        if text.count(old) != 1: raise RuntimeError(f'ABI patch for {source} does not apply exactly once: {old[:40]}')
        text = text.replace(old, new)
    return text


# Source-level fixes applied to the legacy text before translation, for constructs the mechanical rules cannot
# express. Each entry must apply exactly once.
PATCHES = {
    # The shipped VCS (D3DX9 5.04) compiled the per-component `s_rgb < 0` select as one scalar select on s_rgb.r
    # (`cmp r1.w, r2.x, c7.z, c7.w`, both color_projection_ps20.vcs and _ps20b.vcs); DX9 rendered that, so the native
    # source reproduces it.
    'color_projection_ps2x.fxc': [('\tfloat3\tadj_rgb = ( d_rgb != 0.0f ? ( ( s_rgb < 0.0f ? 0.0f : 1.0f ) - s_rgb / d_rgb ) : 0.0f );',
        '\tfloat3\tadj_rgb = ( d_rgb != 0.0f ? ( ( s_rgb.r < 0.0f ? 0.0f : 1.0f ) - s_rgb / d_rgb ) : 0.0f );')],
    # Core's distant sphere causes cancellation in the discriminant. Keep the shipped D3DX9
    # reciprocal-rsq square roots; native sqrt changes dependent point-sampled UVs.
    'core_ps2x.fxc': [
        ('flDiscrim = sqrt( flDiscrim );', 'flDiscrim = 1.0f / rsqrt( flDiscrim );'),
        ('1.05f * sqrt( dot( tmp, tmp ) )', '1.05f * ( 1.0f / rsqrt( dot( tmp, tmp ) ) )'),
    ],
    # X360 vertex-index parameter macros around main: the PC expansion is `const VS_INPUT v` (spritecard_vsxx.fxc).
    'spritecard_vsxx.fxc': [('VS_OUTPUT main( CONST_PC VS_INPUT v\n\t\t\t    VERTEX_INDEX_PARAM_360 )', 'VS_OUTPUT main( const VS_INPUT v )')],
    # The adopted spritecard layout adds TEXCOORD4, which the legacy spline VS does not emit. Its PS input
    # therefore gets the D3D9 undeclared-TEXCOORD default (0,0,0,1), not the output struct's zero initializer
    # (psconv.cpp MergeInputDecls + context.hpp InitializeTempRegistersForUndeclaredInputs).
    'splinecard_vsxx.fxc': [('\tVS_OUTPUT o;', '\tVS_OUTPUT o;\n\to.blendfactor1 = float4( 0, 0, 0, 1 );')],
    # Eye ray-sphere UVs feed point-sampled textures. The shipped ps30 sqrt is rsq + rcp;
    # native sqrt rounding can move the sphere intersection across a texel boundary.
    'eye_refract_ps2x.fxc': [
        ('return (D > 0) ? (-B - sqrt(D)) : 0;', 'return (D > 0) ? (-B - rcp( rsqrt( D ) )) : 0;'),
    ],
    # One sampler read as 2D and as CUBE in the same compile: DX9 declared a cube sampler whenever ISCUBEMAP; the 2D
    # fetch result is overwritten in that combo, so the typed declaration follows the combo.
    'DebugTextureView_ps2x.fxc': [
        ('sampler g_tSampler : register( s0 );',
         '#if ISCUBEMAP\nsamplerCUBE g_tSampler : register( s0 );\n#else\nsampler2D g_tSampler : register( s0 );\n#endif'),
        ('\tfloat4 sample = tex2D( g_tSampler, i.texCoord );',
         '#if ISCUBEMAP\n\tfloat4 sample = texCUBE( g_tSampler, float3( i.texCoord, 0 ) );\n#else\n\tfloat4 sample = tex2D( g_tSampler, i.texCoord );\n#endif'),
    ],
}


ENGINE_CALLS = {'SetPixelShaderFogParams': 'cPixelFogParams', 'SetPixelShaderStateAmbientLightCube': 'cAmbientCube',
                'CommitPixelShaderLighting': 'cLightInfo'}


def engine_shape_matches(member, typ, arr):
    """The engine APIs write a fixed shape at the material's register; another declaration at the same register (a
    combo-exclusive material constant, e.g. vertexlit's g_DepthFeatheringConstants at c13 beside cLightInfo) stays
    a material member."""
    typ = typ.strip().lower().replace('half', 'float')
    count = re.sub(r'\s', '', arr or '')
    if member == 'cPixelFogParams': return typ == 'float4' and not count
    if member == 'cAmbientCube': return typ == 'float3' and count == '[6]'
    if member == 'cLightInfo': return typ == 'pixelshaderlightinfo' and count == '[3]'
    raise RuntimeError('unknown engine member ' + member)


def engine_regs_from_cpp(cpp_texts):
    """legacy PS register -> DX12PSEngine member for every engine API the material calls with a register."""
    commons = [open(os.path.join(SRC, c), encoding='latin-1').read() for c in gc.COMMONS]
    defs = define_map(commons)
    regs = {}
    for t in cpp_texts:
        for call, member in ENGINE_CALLS.items():
            for m in re.finditer(r'\b' + call + r'\s*\(\s*([A-Za-z_]\w*|\d+)', t):
                arg = m.group(1)
                if arg in ('int', 'nReg', 'nConst', 'reg'): continue
                reg = 'c' + arg if arg.isdigit() else resolve_register(arg, defs)
                if regs.get(reg, member) != member: raise RuntimeError(f'register {reg} used by two engine APIs')
                regs[reg] = member
    return regs


def convert(source, profile, logical, interp=None, centroid=(), engine_regs=None, flatten=frozenset(), fixed=None,
            output_struct_from=None):
    stage, define, tag = PROFILES[profile]
    known, need = common_state()
    text = legacy_reference(source)
    for old, new in PATCHES.get(source, []):
        if text.count(old) != 1: raise RuntimeError(f'patch for {source} does not apply exactly once: {old[:40]}')
        text = text.replace(old, new)
    text = select_directives(text, tag)
    centroid = list(centroid) or [m.group(1).strip().strip('"') for m in re.finditer(r'^\s*//\s*CENTROID\s*:\s*"?(\w+)"?', text, re.M | re.I)]
    text = inline_includes(text)
    if output_struct_from:
        # Several vertex shaders feeding one pixel shader must emit one output layout: this vertex shader adopts the
        # other's VS_OUTPUT declaration (its own members are a subset; the rest stay zero).
        other = inline_includes(select_directives(open(os.path.join(SRC, output_struct_from), encoding='latin-1').read().replace('\r\n', '\n'), tag))
        a, b = find_struct(text, 'VS_OUTPUT'), find_struct(other, 'VS_OUTPUT')
        text = text[:a[0]] + other[b[0]:b[3]] + text[a[3]:]
    commons = [open(os.path.join(SRC, c), encoding='latin-1').read() for c in gc.COMMONS]
    defs = define_map(commons + [text])
    text = gc.rename_shader_models(text)
    # material-owned common globals referenced by this source (directly or via a parameterized helper)
    extra = []
    for g, (st, typ, reg) in gc.MATERIAL.items():
        if st != stage: continue
        direct = re.search(r'\b' + g + r'\b', text) is not None
        via = any(re.search(r'\b' + fn + r'\s*\(', text) and any(x[2] == g for x in ex) for fn, ex in need.items())
        if direct or via: extra.append((g, (typ, reg)))
    cbname = os.path.splitext(os.path.basename(source))[0]
    text, members = hoist_constants(text, stage, cbname, defs, extra, engine_regs if stage == 'ps' else None)
    text = hp.convert(text, known)
    text = gc.insert_params(text, need)
    if stage == 'vs':
        text, interp_out = vertex_entry(text, tag, centroid, flatten)
    else:
        if interp is None: raise RuntimeError('pixel conversion needs the paired vertex interpolators')
        text = pixel_entry(text, tag, interp, fixed)
        interp_out = None
    head = (f'// Native SM5.1 rewrite of materialsystem/stdshaders/{source} for logical {logical} (legacy profile {tag});\n'
            f'// generated mechanically by gensource.py, then reviewed. Combo directives are the {tag} set.\n'
            f'#define {define} 1\n#include "dx12_preamble.h"\n')
    text = re.sub(r'\n{4,}', '\n\n\n', text)
    return head + text, interp_out, members
