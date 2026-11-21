"""Single-session Normal/Extra parity captures for ordinary and event post features."""
import sys,time,shutil,json
from pathlib import Path
sys.path.insert(0,'C:/l4d2_work/tools');from smoke_mod_launcher import Smoke
MOD=__import__('in_place_runtime',fromlist=['MOD']).MOD
OUT=Path('C:/l4d2_work/perf1050/PostFeatures/captures');OUT.mkdir(parents=True,exist_ok=True)
CASES=[
 ('baseline', 'mat_postfx_compact 1;mat_postfx_bloom_quality 2;mat_postfx_meter_stride 4'),
 ('extra', 'mat_postfx_compact 0;mat_postfx_bloom_quality 2;mat_postfx_meter_stride 1'),
 ('blurred_vignette_normal','mat_postfx_compact 1;mat_local_contrast_enable 1;mat_local_contrast_scale_override 1.0;mat_local_contrast_vignette_start_override 0.4;mat_local_contrast_vignette_end_override 0.8'),
 ('blurred_vignette_extra','mat_postfx_compact 0;mat_local_contrast_enable 1;mat_local_contrast_scale_override 1.0;mat_local_contrast_vignette_start_override 0.4;mat_local_contrast_vignette_end_override 0.8'),
 ('fade_black_normal','mat_postfx_compact 1;fadeout 255 0 0 1'),
 ('fade_black_extra','mat_postfx_compact 0;fadeout 255 0 0 1'),
 ('fade_clear_normal','mat_postfx_compact 1;fadein 1'),
 ('fade_clear_extra','mat_postfx_compact 0;fadein 1'),
]
s=Smoke('postfeatures_'+str(time.time_ns()),False,1920,1080)
try:
 s.launch('c1m1_hotel',['-noborder','-dx12stats','-dx12statsframes','36','+fps_max','0']);s.send('sv_cheats 1;mat_vsync 0;sb_stop 1;director_stop;nb_stop 1;cl_mouseenable 0;hideconsole');s.settle(30)
 s.send('noclip');s.settle(1);s.send('setpos 585.843750 5597.687500 2848;setang 0 165.717773 0');s.settle(5)
 for name,cmd in CASES:
  s.send(cmd);s.settle(5)
  for i in range(3): shutil.copy2(s.jpeg(name+'_'+str(i)),OUT)
  s.send('mat_postfx_compact 1;mat_local_contrast_scale_override 0;mat_local_contrast_vignette_start_override -1;mat_local_contrast_vignette_end_override -1;fadein 0.1');s.settle(2)
 s.changelevel('c1m2_streets');s.settle(8);shutil.copy2(s.jpeg('transition_streets'),OUT);s.quit()
finally:s.finish()
