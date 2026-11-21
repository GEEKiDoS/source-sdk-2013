"""Single-session Normal/Extra parity captures for ordinary and event post features."""
import json
import re
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, 'C:/l4d2_work/tools')
import bench1050 as bench

out = Path('C:/l4d2_work/perf1050/PostFeatures') / sys.argv[1]
out.mkdir(parents=True, exist_ok=False)
smoke = bench.Smoke('postfeatures_' + str(time.time_ns()), '--dx9' in sys.argv, 1920, 1080)
samples = {}
state_re = re.compile(r'PostFX grade: [^\r\n]+')

def command(text):
    return bench.command(smoke, text)

def state():
    return state_re.search(bench.command(smoke, 'mat_postfx_report_grade', state_re.search))[0]

def phase(label):
    shutil.copy2(smoke.jpeg(label), out)
    samples[label] = {'state': state()}

def window(label):
    if '--dx9' not in sys.argv:
        samples[label]['blocks'] = bench.stats_measure(smoke, 1500)['blocks']
    samples[label]['end_state'] = state()
    (out / 'samples.json').write_text(json.dumps(samples, indent=1))

def controller(input_name, value):
    return f'ent_fire postprocess_controller {input_name} {value}'

cases = [
    ('steady_q2', '', ''),
    ('steady_q1', 'mat_postfx_bloom_quality 1', 'mat_postfx_bloom_quality 2'),
    ('steady_q0', 'mat_postfx_bloom_quality 0', 'mat_postfx_bloom_quality 2'),
    ('contrast', controller('SetLocalContrastStrength', 2), controller('SetLocalContrastStrength', .5)),
    ('grain', controller('SetGrainStrength', 4), controller('SetGrainStrength', 1)),
    ('top', controller('SetTopVignetteStrength', 3), controller('SetTopVignetteStrength', 1)),
    ('blur', controller('SetVignetteBlurStrength', .85), controller('SetVignetteBlurStrength', 0)),
    ('black', controller('SetFadeToBlackStrength', .65), controller('SetFadeToBlackStrength', 0)),
    ('viewfade', 'ent_fire pf_fade Fade', 'ent_fire pf_clear Fade'),
    ('dof', 'ent_fire pf_dof SetFarBlurRadius 9', 'ent_fire pf_dof SetFarBlurRadius 0'),
    ('transition', 'mat_postfx_test_transition 1', 'mat_postfx_test_transition 0'),
    ('vomit', 'cmd it', ''),
]

try:
    smoke.launch('c1m2_streets', ['-noborder', '-dx12stats', '-dx12statsframes', '30', '+fps_max', '60'])
    command(bench.HYGIENE + ';fps_max 0;god 1;r_ssr 0;r_volumetric 0;r_upscaler 3;r_upscaler_scale .667')
    # Activation precedes local player takeover; console echoes alone are not proof.
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        hit = bench.POSITION.search(command('getpos'))
        if hit and any(map(float, hit[1].split())):
            break
        smoke.settle(1)
    else:
        raise RuntimeError('No local camera after server activation')
    command(bench.HYGIENE + ';fps_max 0')
    bench.camera(smoke, ('street', '2294 4890 448.03125', '0 180 0', False))
    command('mat_postfx_bloom_quality 2;mat_postfx_bodycam 0;ent_create postprocess_controller targetname pf_controller spawnflags 1')
    command('mat_dof_enabled 1;ent_create env_dof_controller targetname pf_dof')
    smoke.settle(.5)
    command('ent_fire pf_dof SetNearFocusDepth 20;ent_fire pf_dof SetFarFocusDepth 70;ent_fire pf_dof SetFarBlurDepth 200')
    command('ent_create env_fade targetname pf_fade duration 1 holdtime 8 rendercolor "80 20 100" renderamt 165 spawnflags 0')
    command('ent_create env_fade targetname pf_clear duration 1 holdtime 0 rendercolor "80 20 100" renderamt 165 spawnflags 1')
    command('script NetProps.SetPropEntity(GetListenServerHost(),"m_hPostProcessCtrl",Entities.FindByName(null,"pf_controller"))')
    command(controller('SetFadeTime', .25) + ';' + controller('SetLocalContrastStrength', .5) + ';' + controller('SetVignetteStart', .3) + ';' + controller('SetVignetteEnd', .9))
    command('survivor_it_duration 12;z_vomit_fade_start 9;z_vomit_hit_pitch_min 0;z_vomit_hit_pitch_max 0;z_vomit_hit_yaw_min 0;z_vomit_hit_yaw_max 0;cl_transition_blur_fade_time 1')
    selection = set(sys.argv[2:])
    for effect, enter, leave in cases:
        if selection and effect not in selection:
            continue
        for mode, compact, stride in [('normal', 1, 4), ('extra', 0, 1)]:
            prefix = effect + '_' + mode
            command(f'mat_postfx_compact {compact};mat_postfx_meter_stride {stride}')
            smoke.settle(1)
            phase(prefix + '_before')
            if enter:
                smoke.send(enter)
                phase(prefix + '_entry')
            smoke.settle(1.25)
            phase(prefix + '_active')
            if effect == 'vomit' and mode == 'normal':
                command('host_timescale .01')
                phase('vomit_instance_normal')
                command('mat_postfx_compact 0;mat_postfx_meter_stride 1')
                smoke.settle(.3)
                phase('vomit_instance_extra')
                command('mat_postfx_compact 1;mat_postfx_meter_stride 4;host_timescale 1')
            window(prefix + '_active')
            if leave:
                smoke.send(leave)
            if effect == 'vomit':
                smoke.settle(10)
            phase(prefix + '_exit')
            smoke.settle(1.5)
            phase(prefix + '_after')
    smoke.quit()
finally:
    smoke.finish()
    (out / 'samples.json').write_text(json.dumps(samples, indent=1))
    for name in ('console.log', 'events.log'):
        if (smoke.out / name).exists():
            shutil.copy2(smoke.out / name, out)
