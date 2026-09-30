"""Fast compile check of generated native sources: each file with every combo at its minimum and at its maximum
(skips ignored), SM5.1, in parallel. Prints the first error lines per failing shader."""
import os, re, sys, subprocess, concurrent.futures as cf
_HERE = os.path.dirname(os.path.abspath(__file__))
_DX12 = os.path.dirname(_HERE)                                   # materialsystem/stdshaders_dx12
_LEGACY = os.path.join(os.path.dirname(_DX12), 'stdshaders')    # materialsystem/stdshaders
ROOT = os.path.join(_DX12, 'hlsl')
COMMON = os.path.join(ROOT, 'common')
FXC = os.environ.get('FXC', r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe')


def combos(text):
    out = []
    for m in re.finditer(r'^\s*//\s*(STATIC|DYNAMIC)\s*:\s*"(\w+)"\s*"(\d+)\.\.(\d+)"', text, re.M | re.I):
        out.append((m.group(2), int(m.group(3)), int(m.group(4))))
    return out


def skips(text):
    out = []
    for m in re.finditer(r'^\s*//\s*SKIP\s*:\s*(.+?)\s*(?:\[(\w+)\])?\s*$', text, re.M):
        if m.group(2) and m.group(2).upper() not in ('PC', 'VS20', 'PS20B', 'VS30', 'PS30'): continue
        e = m.group(1)
        e = re.sub(r'\bdefined\s*\$(\w+)', 'True', e)
        e = e.replace('&&', ' and ').replace('||', ' or ')
        e = re.sub(r'!(?!=)', ' not ', e)
        e = re.sub(r'\$(\w+)', r'V["\1"]', e)
        out.append(e)
    return out


def valid(cs, sk, values):
    V = {n: v for (n, _, _), v in zip(cs, values)}
    for e in sk:
        try:
            if eval(e, {}, {'V': V}): return False
        except (KeyError, NameError, SyntaxError): continue
    return True


def picks(cs, sk, count=int(os.environ.get('QC_COUNT', '6'))):
    import random
    rnd = random.Random(len(cs))
    out = []
    for pick in [[lo for _, lo, _ in cs], [hi for _, _, hi in cs]] + [[rnd.randint(lo, hi) for _, lo, hi in cs] for _ in range(count * 200)]:
        if valid(cs, sk, pick) and pick not in out: out.append(pick)
        if len(out) >= count: break
    return out


def check(name):
    path = os.path.join(ROOT, name)
    text = open(path, encoding='latin-1').read()
    prof = 'vs_5_1' if re.search(r'_vs\d', name) else 'ps_5_1'
    errors = []
    cs = combos(text)
    for pick in picks(cs, skips(text)):
        defs = [f'/D{n}={v}' for (n, _, _), v in zip(cs, pick)]
        r = subprocess.run([FXC, '/nologo', '/T' + prof, '/Emain', '/I', COMMON, '/I', ROOT] + defs + [path, '/Fo', os.devnull],
                           capture_output=True, text=True)
        if r.returncode:
            errors += [' '.join(defs)] + [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l][:3]; break
    return name, errors


if __name__ == '__main__':
    names = sorted(f for f in os.listdir(ROOT) if f.endswith('.fxc') and (len(sys.argv) < 2 or any(a in f for a in sys.argv[1:])))
    bad = 0
    with cf.ThreadPoolExecutor(16) as ex:
        for name, errors in ex.map(check, names):
            if errors:
                bad += 1
                print(name); print('\n'.join('   ' + e for e in errors))
    print(f'{len(names)} checked, {bad} failing')
