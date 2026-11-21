# PostFX feature audit

| Feature | Normal compact status | Extra/client status |
|---|---|---|
| AgX, exposure, LUTs, grain | Native composite; LUT/grain gated by ordinary grade | Legacy TerrorEnginePost on incompatible views |
| Ordinary vignette and top vignette | Native `finish[2]` / `values[7]` plus `dev/vignette` sampling | Legacy on incompatible views |
| Local contrast | Native packed quarter-res field and radial contrast approximation | Legacy `Terror_PrepareEnginePost` on incompatible views |
| Blurred vignette / distant blur (`values[4]`) | Native radial blend toward blurred scene luminance, after local contrast, before LUT | Legacy prepare + composite |
| Fade-to-black controller lane (`values[5]`) | Falls back when nonzero; not currently native | Legacy composite |
| Explicit `fadein`/`fadeout` view fade | Falls back; `Terror_SetEnginePostFade` supplies `$fadeColor/$fade` | Legacy composite / view fade |
| Vomit / boomer refraction | The real first-person PCF supplies quarter-resolution RG offset and B coverage. Native composite mirrors displaced UVs, applies both authored tints and luma, then performs the controller grade | Legacy prepare/PCF/grade |
| Transition blur/desaturation | Separate client effect after the main post; not compact | `Terror_RenderTransitionBlur` |
| `env_dof_controller` / Terror DOF | The global controller always transmits. Both qualities pack actual DX12 scene depth with color before the existing DOF pass. DX9 retains its original alpha-depth input | Same focus planes, blur radii, Gaussian and Poisson materials |
| Glow outlines | Separate client effect; integrated branch bounds/scissors projected color submissions | `TerrorGlow_Render` |
| Underwater/smoke/screen overlays | Separate ordered client effects | Existing overlay paths |
| Native bloom | Native; bloom quality 1/2 now remains eligible for compact composition and uses populated/rebased mip selection | Legacy bloom disabled when native owns it |

Fade states retain the legacy grade; transition blur stays in its original client stage. Ineligible/debug/camera views retain their existing composition. `feature_capture.py` captures producer state, Normal/Extra pairs, transition phases and GPU timestamps. See the measured results in `report.md`.