"""Run via gpurun.py --quiet-cpu --deploy WORKTREE -- py -3.12 -B bench_horde.py OUT [--pix]."""
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, 'C:/l4d2_work/tools')
import bench1050 as bench

out = Path(sys.argv[1]).resolve()
pix = '--pix' in sys.argv
active = '--active' in sys.argv
combat = '--combat' in sys.argv
combat_view = None
camera = bench.camera
launch = bench.Smoke.launch
_, pos, ang, noclip = bench.VIEWS['c1m2_streets'][0]
bench.VIEWS['c1m2_streets'] = [(name, pos, ang, noclip) for name in ('horde', 'empty')]
if active:
    bench.VIEWS['c1m2_streets'] = [('live', pos, ang, noclip)]
bench.QUALITY += 'r_pbr_lean_lighting r_csm_cascades r_shadowmap_viewmodel_self mat_postfx_meter_stride mat_postfx_compact'.split()


def scene(s, view):
    global combat_view
    if combat_view and view[0] == 'empty':
        view = ('empty', *combat_view, True)
    result = camera(s, view)
    if active or (combat and view[0] == 'horde'):
        bench.set_noclip(s, False)
        bench.command(s, 'god; sb_stop 0; nb_stop 0; director_start; director_force_panic_event; z_spawn mob')
        s.settle(3)
        inventory = bench.command(s, 'report_entities; getpos', bench.POSITION.search)
        if active:
            (out / 'live_inventory.txt').write_text(inventory, encoding='utf-8')
            return result
        bench.command(s, 'nb_stop 1; sb_stop 1; director_stop')
        inventory = bench.command(s, 'report_entities; getpos', bench.POSITION.search)
        combat_view = (bench.POSITION.search(inventory)[1], bench.ANGLE.search(inventory)[1])
        result = camera(s, ('horde', *combat_view, True))
    else:
        bench.command(s, 'ent_fire infected kill; director_stop; sb_stop 1; nb_stop 1')
        s.settle(1)
    if view[0] == 'horde' and not combat:
        bench.command(s, 'z_spawn_const_distance 384; z_spawn_const_ang 0; z_common_limit 31')
        for index in range(31):
            x = 2010 - 56 * (index // 7)
            y = 4890 + 42 * (index % 7 - 3)
            bench.command(s, f'z_spawn_const_pos {x} {y} 475; z_spawn common')
        bench.command(s, 'z_spawn_const_pos; z_spawn_const_ang; z_spawn_const_distance -1')
        s.settle(2)
    inventory = bench.command(s, 'report_entities; getpos', bench.POSITION.search)
    found = re.search(r'Class: infected \((\d+)\)', inventory)
    count = int(found[1]) if found else 0
    (out / (view[0] + '_inventory.txt')).write_text(inventory, encoding='utf-8')
    if (view[0] == 'empty' and count) or (view[0] == 'horde' and not (26 <= count <= 40 if combat else 31 <= count <= 35)):
        raise RuntimeError(f'{view[0]} fixture has {count} common infected')
    result['infected'] = count
    if pix and count:
        bench.command(s, f'dx12_pix_draw_markers 1; dx12_pix_capture {(out / "horde.wpix").as_posix()}')
        s.settle(5)
    return result


def pix_launch(s, name, args):
    return launch(s, name, args + ['-dx12pix'])


bench.camera = scene
# No allocation dump during gameplay; the fixture needs only GPU event queries.
bench.memory = lambda *args: {}
if pix:
    bench.Smoke.launch = pix_launch
sets = '|'.join(label + '=r_upscaler 6;r_upscaler_scale 0.8333;r_pbr_light_passes ' + mode for label, mode in [('split', '1'), ('singleModel', '3')])
if '--infected-only' in sys.argv:
    sets = sets.replace('singleModel=', 'singleInfected=').replace('r_pbr_light_passes 3', 'r_pbr_light_passes 4')
if active:
    mode = '4' if '--infected-only' in sys.argv else '1'
    sets = 'live=r_upscaler 6;r_upscaler_scale 0.8333;r_pbr_light_passes ' + mode
sys.argv = [sys.argv[0], str(out), '--maps', 'c1m2_streets', '--sets', sets, '--reapply-tier', '--reps', '1' if pix else '3', '--window', '2.5']
sys.argv.extend(['--launch', '+mat_queue_mode 0 +fps_max 60'])
bench.main()
shots = out.parent / 'shots'
shots.mkdir(exist_ok=True)
for image in out.glob('*.jpg'):
    shutil.copy2(image, shots / (out.name + '_' + image.name))
