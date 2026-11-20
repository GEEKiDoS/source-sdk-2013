# Left4Neko material loading on DX12

The compatibility boundary is the VMT root. Add-ons can use these names without renaming their VMTs. This is not a port of L4N's post stack or a guarantee that every parameter has the same visual effect.

| VMT root (2.51.0) | DX12 handling | Visible differences |
|---|---|---|
| `NekoToon` | Native studio toon material | Probe and shadow-map lighting, hull outline, eyebrow depth/alpha, primary lightwarp, normal and matcap. Secondary layers, L4N fake shadows and dual quaternion skinning are not emulated. |
| `PBR` | Native metalness/specular adapter | Uses this renderer's lighting and reflection pipeline. Set `$model 1` on studio models. |
| `DigitalDisplay` | Native digit-atlas material | One row of ten glyphs; `$number` saturates to `$numbits` decimal places (1–4). Uses `$basetexture`, frame, modulation and alpha test. |
| `NekoRefract` | `Refract` | Normal, tint and refraction work without the L4N scope controller, inversion or overlay layer. |
| `NekoSky` | `Sky` | Native sky and HDR paths; no L4N overlay layer. |
| `NekoSprite` | `Sprite` | Native sprite orientation, frames and render modes. |
| `NekoGlowColor` | `UnlitGeneric` | Unlit color; does not install a glow compositor. |
| `NekoBlurFilterX`, `NekoBlurFilterY` | `BlurFilterX`, `BlurFilterY` | Native separable kernels. |
| `Neko_Downsample_nohdr` | `Downsample_nohdr` | Native SDR downsample. |
| `Neko_Engine_Post` | `Engine_Post` | Native engine-post controls. |
| `NekoBloom`, `NekoTonemap`, `neko_luminance_compare`, `neko_screenspace_general` | `UnlitGeneric` | Draw the input texture instead of running private SM3 code. DX12 owns bloom, exposure and display mapping. |

**Names.** The 2.51.0 `game_shader_generic_neko.dll` has fourteen namespaced shader classes plus `CNekoGlowColor`. The roots in `shaders/fxc/{nekotoon,pbr,neko_refract,digital_display}.vmt` and the installed `materials/l4n/neko_tonemap.vmt` agree with these names. `outline_vs30`, `neko_emissive_scroll_blended_pass_ps30` and `neko_screenspaceeffect_vs30` are stages, not material roots. `Outline`, `Neko_Refract` and `Digital_Display` are not additional registrations.

**Inputs.** Unknown VMT keys are not shader names. NekoToon consumes only the supported inputs. An absent or error optional ramp falls back to the built-in cel transition. Missing `patch` includes are different: material expansion must succeed before choosing a shader. Salt's two missing includes and ramp are not L4N platform assets; no content-specific files or automatic substitutions ship with the renderer.

**Proxies.** The L4D2 client supplies `PlayerAttachment`, `PlayerBottom`, `Sequence`, `RemapValClamp`, `ClipAmmo`, `RemainingAmmo`, `Cycle`, `Modulo` and `SelectFirstIfNonZero`. These resolve material proxy names without installing L4N scripts or console controls.
