# PostFX feature audit

| Feature | Normal compact status | Extra/client status |
|---|---|---|
| AgX, exposure, LUTs, grain | Native composite; LUT/grain gated by ordinary grade | Legacy TerrorEnginePost on incompatible views |
| Ordinary vignette and top vignette | Native `finish[2]` / `values[7]` plus `dev/vignette` sampling | Legacy on incompatible views |
| Local contrast | Native packed quarter-res field and radial contrast approximation | Legacy `Terror_PrepareEnginePost` on incompatible views |
| Blurred vignette / distant blur (`values[4]`) | Falls back when nonzero; not currently native | Exact legacy prepare + composite |
| Fade-to-black controller lane (`values[5]`) | Falls back when nonzero; not currently native | Legacy composite |
| Explicit `fadein`/`fadeout` view fade | Falls back; `Terror_SetEnginePostFade` supplies `$fadeColor/$fade` | Legacy composite / view fade |
| Vomit / boomer refraction | Falls back when active; native vomit particle coverage is not in the DX12 composite | Legacy vomit prepare/material |
| Transition blur/desaturation | Separate client effect after the main post; not compact | `Terror_RenderTransitionBlur` |
| `env_dof_controller` / Terror DOF | Existing client DOF runs before motion blur and main post for both modes; not owned by compact | Existing `Terror_DrawDepthOfField` materials |
| Glow outlines | Separate client effect; integrated branch bounds/scissors projected color submissions | `TerrorGlow_Render` |
| Underwater/smoke/screen overlays | Separate ordered client effects | Existing overlay paths |
| Native bloom | Native; bloom quality 1/2 now remains eligible for compact composition and uses populated/rebased mip selection | Legacy bloom disabled when native owns it |

The remaining non-native items are deliberate exact legacy fallbacks rather than silent omission. Bloom-quality fuse restriction was removed in commit `42a318ae49`. This audit is the implementation boundary for this branch; feature-isolated captures remain to be collected for the fallback lanes.