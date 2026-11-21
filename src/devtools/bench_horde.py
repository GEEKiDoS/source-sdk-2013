"""Run via gpurun.py --quiet-cpu --deploy WORKTREE -- py -3.12 -B bench_horde.py OUT [--pix]."""
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, 'C:/l4d2_work/tools')
import bench1050 as bench

out = Path(sys.argv[1]).resolve()
pix = '--pix' in sys.argv
camera = bench.camera
launch = bench.Smoke.launch
_, pos, ang, noclip = bench.VIEWS['c1m2_streets'][0]
bench.VIEWS['c1m2_streets'] = [(name, pos, ang, noclip) for name in ('empty', 'horde')]
bench.QUALITY += 'r_pbr_lean_lighting r_csm_cascades r_shadowmap_viewmodel_self mat_postfx_meter_stride mat_postfx_compact'.split()


def scene(s, view):
    result = camera(s, view)
    bench.command(s, 'ent_fire infected kill; director_stop; sb_stop 1; nb_stop 1')
    s.settle(1)
    if view[0] == 'horde':
        bench.command(s, 'god 1; z_common_limit 31; setang 30 180 0; z_spawn mob; setang 0 180 0')
        s.settle(2)
    inventory = bench.command(s, 'report_entities; getpos', bench.POSITION.search)
    found = re.search(r'Class: infected \((\d+)\)', inventory)
    count = int(found[1]) if found else 0
    (out / (view[0] + '_inventory.txt')).write_text(inventory, encoding='utf-8')
    if (view[0] == 'empty' and count) or (view[0] == 'horde' and not 28 <= count <= 33):
        raise RuntimeError(f'{view[0]} fixture has {count} common infected')
    result['infected'] = count
    if pix and count:
        bench.command(s, f'dx12_pix_draw_markers 1; dx12_pix_capture "{out / "horde.wpix"}"')
        s.settle(5)
    return result


def pix_launch(s, name, args):
    return launch(s, name, args + ['-dx12pix'])


bench.camera = scene
if pix:
    bench.Smoke.launch = pix_launch
sets = '|'.join(label + '=r_upscaler 6;r_upscaler_scale 0.8333;r_pbr_light_passes ' + mode for label, mode in [('split', '1'), ('singleModel', '3')])
sys.argv = [sys.argv[0], str(out), '--maps', 'c1m2_streets', '--sets', sets, '--reapply-tier', '--reps', '1' if pix else '3', '--window', '2.5']
bench.main()
shots = out.parent / 'shots'
shots.mkdir(exist_ok=True)
for image in out.glob('*.jpg'):
    shutil.copy2(image, shots / (out.name + '_' + image.name))
