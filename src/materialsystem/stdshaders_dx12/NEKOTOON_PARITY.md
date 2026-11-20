# Left4Neko 2.51.0 material compatibility

Reference: `left4dead2/shaders/fxc/nekotoon.vmt` in the 2.51.0 distribution labels itself version **3.1**. Line numbers below refer to that document, not to screenshots. Status compares the DX12 material binding and native VS/PS with the documented effect; supported is not a claim of identical SM3 output. Salt usage is a case-insensitive scan of uncommented VMTs in the three original VPKs, excluding test dependencies. The docs explicitly exclude Source Phong (line 9).

| Parameter | Doc line | Meaning | DX12 | Salt |
|---|---:|---|---|---|
| `$mask1` | 20 | Matcap 1 and 2 masks in R/B, lightwarp row in G, AO in A | Differs: Secondary matcap not evaluated; single-channel missing-component defaults not repaired. | - |
| `$mask2` | 30 | Rim mask in R, tint and second base masks in G/B | Differs: Only the primary rim mask is read. | - |
| `$lightwarptexture` | 36 | Linear-authored diffuse ramp | Differs: Authored ramp sampled using two probe/direct bands. | yes |
| `$lightwarpmapoffset` | 37 | Offset of ramp V coordinate | Supported | - |
| `$lightwarpscale` | 38 | Ramp brightness multiplier | Supported | - |
| `$lightwarpexponent` | 39 | Ramp contrast exponent | Supported | - |
| `$lightwarphalf` | 40 | Above half intensity, return white instead of sampling the ramp | Supported | - |
| `$lightwarpsrgbtexture` | 41 | sRGB alternative to the linear ramp | Supported | - |
| `$rimlight` | 43 | Enable silhouette highlight | Differs: Bounded highlight for DX12 post. | yes |
| `$rimlightboost` | 44 | Rim intensity | Supported | - |
| `$rimlightviewmodelfactor` | 45 | First-person rim intensity multiplier | Missing | - |
| `$rimlightexponent` | 46 | Fresnel exponent | Supported | - |
| `$rimlightalbedotint` | 47 | Mix painted color into rim highlight | Supported | - |
| `$rimshade` | 49 | Enable silhouette shadow | Missing | - |
| `$rimshadecolor` | 50 | Rim shadow tint | Missing | - |
| `$rimshadeborder` | 51 | Rim shadow boundary | Missing | - |
| `$rimshadeblur` | 52 | Width of rim shadow transition | Missing | - |
| `$rimshadeexponent` | 53 | Exponent of rim shadow | Missing | - |
| `$allowdiffusemodulation` | 55 | Allow entity color modulation | Supported | yes |
| `$color` | 56 | Color modulation | Supported | - |
| `$color2` | 57 | Secondary color modulation | Differs: Inherited modulation; mask2.G tint semantics absent. | - |
| `$srgbtint` | 58 | sRGB paint tint | Missing | - |
| `$hsv` | 59 | Hue, saturation, value adjustment | Missing | - |
| `$backfaceshade` | 60 | Tint inward-facing fabric surfaces | Missing | - |
| `$basetexture` | 62 | Paint/albedo map | Supported | yes |
| `$frame` | 63 | Albedo animation frame | Supported | - |
| `$basetexturetransform` | 64 | Albedo UV transform | Supported | - |
| `$basetexture2` | 66 | Second painted layer | Missing | - |
| `$basetexture2frame` | 67 | Second-layer animation frame | Missing | - |
| `$basetexture2tint` | 68 | Second-layer RGBA intensity | Missing | - |
| `$basetexture2backface` | 69 | Display second layer on backfaces | Missing | - |
| `$basetexture2blendmode` | 70 | Multiply, add, normal or screen blend | Missing | - |
| `$basetexture2lighting` | 71 | Second-layer lighting weight | Missing | - |
| `$basetexture2panosphere` | 72 | Use panoramic UVs for second layer | Missing | - |
| `$basetexture2transform` | 73 | Second-layer UV transform | Missing | - |
| `$basetexture2emissionstrength` | 74 | Second-layer emission intensity | Missing | - |
| `$bumpmap` | 76 | DirectX normal map | Differs: Samples XYZ; L4N packed normal decoding not established. | yes |
| `$bumpframe` | 77 | Normal animation frame | Supported | - |
| `$bumpstrength` | 78 | Normal perturbation strength | Supported | - |
| `$bumptransform` | 79 | Normal UV transform | Supported | - |
| `$bumpmap2` | 80 | Second normal blended with the first | Missing | - |
| `$bumpframe2` | 81 | Second normal animation frame | Missing | - |
| `$bumpstrength2` | 82 | Second normal strength | Missing | - |
| `$bumptransform2` | 83 | Second normal UV transform | Missing | - |
| `$matcap` | 85 | Reflection blend: disabled, multiply, add, normal, screen | Supported | yes |
| `$matcaptexture` | 86 | Reflection color and opacity | Supported | yes |
| `$matcapframe` | 87 | Matcap animation frame | Supported | - |
| `$matcaptint` | 88 | RGBA matcap multiplier | Supported | yes |
| `$matcaplighting` | 89 | Blend lighting into reflection | Supported | - |
| `$matcap2` | 91 | Second reflection mode; requires primary matcap | Missing | - |
| `$matcap2texture` | 91 | Secondary reflection texture | Missing | - |
| `$matcap2tint` | 91 | Secondary reflection tint | Missing | - |
| `$matcap2lighting` | 91 | Secondary reflection lighting weight | Missing | - |
| `$matcap2frame` | 91 | Secondary reflection frame | Missing | - |
| `$outline` | 93 | Enable silhouette hull | Supported | yes |
| `$outlinecolor` | 94 | Outline tint | Supported | yes |
| `$outlinehsv` | 95 | Outline HSV adjustment | Missing | - |
| `$outlinethickness` | 96 | Hull width | Differs: Clamped to prevent camera intersections. | yes |
| `$outlinefixedthickness` | 97 | Screen-constant width | Supported | yes |
| `$outlineviewmodelfactor` | 98 | First-person width multiplier | Missing | - |
| `$outlinezoffset` | 99 | Outline depth bias | Supported | - |
| `$outlinelighting` | 100 | Mix emission and lit outline | Supported | yes |
| `$outlinebasetextureblend` | 101 | Multiply outline by albedo | Supported | - |
| `$outlinetexture` | 102 | Hull mask: ATI1N R, otherwise alpha | Differs: Channel selection matches; DX12 clips hull pixels instead of masking vertex displacement. | yes |
| `$alphatest` | 109 | Texture cutout | Supported | - |
| `$allowalphatocoverage` | 110 | MSAA alpha-to-coverage | Differs: Shared material flag, effective coverage depends on MSAA. | - |
| `$alphatestreference` | 111 | Cutout threshold | Supported | - |
| `$translucent` | 112 | Alpha blending | Supported | yes |
| `$alpha` | 113 | Opacity; authoring it requests blending | Supported | yes |
| `$alpha2` | 114 | Opacity multiplier; when opaque requests cutout/A2C | Supported | yes |
| `$additive` | 115 | Additive blend | Supported | - |
| `$multiply` | 116 | Multiply framebuffer by source color | Supported | - |
| `$nocull` | 117 | Render both sides | Supported | yes |
| `$decal` | 118 | Decal material, recommended only on flat geometry | Differs: Caster suppresses incoming decals; decal drawing is not reproduced. | - |
| `$zoffset` | 119 | Move depth without moving projected UV/XY | Supported | - |
| `$flipbackfacenormal` | 120 | Reverse normals on backfaces | Missing | - |
| `$noflexnormal` | 121 | Do not morph normals with the face | Missing | - |
| `$dualquatskinning` | 122 | Dual quaternion bone interpolation | Missing | - |
| `$eyeuv` | 124 | Eye-tracking texture coordinates | Missing | - |
| `$matcapuv` | 125 | Use reflection coordinates for primary albedo | Missing | - |
| `$eyebrow` | 127 | Enable see-through brow surface | Supported | yes |
| `$eyebrowalpha` | 128 | Proxy-adjustable brow opacity | Supported | yes |
| `$eyebrowzoffset` | 129 | Bounded depth displacement through hair | Supported | yes |
| `$fakeshadow` | 131 | Draw an offset projected shadow | Missing | - |
| `$fakeshadowoffset` | 132 | Fake shadow direction and strength | Missing | - |
| `$fakeshadowcolor` | 133 | Fake shadow tint | Missing | - |
| `$emission` | 136 | 0 disabled, 1 color, 2 mask | Differs: Mode 1 only; mode 2 is not a mask blend. | - |
| `$emissiontint` | 137 | Emission color multiplier | Supported | yes |
| `$emissiontexture` | 138 | Emission color or mask map | Differs: Color lookup only. | - |
| `$emissionframe` | 139 | Emission animation frame | Supported | - |
| `$emissionflashlighttint` | 140 | Additional flashlight-driven emission | Missing | - |
| `$emissionstrength` | 141 | Emission intensity | Supported | - |
| `$emissiontonemapscaleamount` | 142 | Influence of exposure on emission | Missing | - |
| `$cameraao` | 144 | Darken near-camera geometry | Differs: Near-field fade, not the L4N implementation. | yes |
| `$lambertfactor` | 145 | Ambient/flashlight shadow intensity | Missing | - |
| `$darknesslimit` | 146 | Retain visibility in dim light | Differs: Uses the DX12 exposure-independent lighting floor. | - |
| `$lightingsaturation` | 147 | Maximum lighting saturation | Differs: Applied to the color of the probe and direct key. | - |
| `$lightingscale` | 148 | Overall irradiance multiplier | Supported | yes |

## Material proxies

| Proxy | neko_proxy.vmt line | Meaning / DX12 status | Salt |
|---|---:|---|---|
| `PlayerBottom` | 3 | Root bone axis vs camera-to-entity direction; uses axis X (axis not specified by docs). | - |
| `Sequence` | 7 | Sequence index. Reads requested animating-overlay layer when available, otherwise base sequence. | - |
| `PlayerAttachment` | 13 | Signed cosine of camera ray and selected attachment axis 1..3. Missing attachment returns zero. | yes |
| `RemapValClamp` | 22 | Map between two ranges, clamp, and accept either literal or material-var endpoints. | yes |
| `ClipAmmo` | 31 | Primary clip on bound weapon; also resolves character and viewmodel. | - |
| `RemainingAmmo` | 35 | Owner reserve for primary ammo type. | - |
| `Cycle` | 39 | Existing L4D2 eased cycle; now reads animating-overlay layer when available. | - |
| `Modulo` | 48 | Floating remainder; zero divisor returns zero. | - |
| `SelectFirstIfNonZero` | 54 | Existing SDK mathproxy selects srcVar1 unless zero. | - |

## Global controls

| Active cvar in readme_l4n.txt | Meaning | DX12 status |
|---|---|---|
| `mat_nekotoon_allow_lightwarp` | Enable lightwarp shading | Missing |
| `mat_nekotoon_lambert_factor` | Scale ambient/flashlight shadow | Missing |
| `mat_nekotoon_lighting_scale` | Global irradiance multiplier | Missing |
| `mat_nekotoon_rimlight_boost` | Scale rim intensity | Missing |
| `mat_nekotoon_rimlight_viewmodel_boost` | Scale first-person rim | Missing |
| `mat_nekotoon_brightness_limit` | Limit peak brightness | Missing |
| `mat_nekotoon_darkness_limit` | Dark-light floor | Missing |
| `mat_nekotoon_lazy_texture_load` | Defer texture load until draw | Missing |
| `mat_nekotoon_ignore_flat_normal` | Skip flat_normal texture, stricter mode 2 | Missing |
| `mat_nekotoon_normalized_lightwarp` | Normalize ramp peak across materials | Missing |

Removed L4N cvars `mat_nekotoon_allow_flat_bumpmap` and `mat_nekotoon_min_ambient_luminance` occur only in changelog entries. Do not register obsolete aliases.

## Verification scope

Salt references two missing VMT includes and a ramp that are not L4N platform assets. No material resolver or packaged stand-ins are part of this implementation. Verification used only a temporary companion VPK. `zc806.vpk` and `松果伊藤小手枪.vpk` in the local download folder contained no NekoToon VMT roots.

`PlayerAttachment` and `RemapValClamp` no longer generate proxy-not-found errors on Salt. Sequence/layer, ammo and PlayerBottom do not occur in that addon and were not visually exercised. Passing proxy lookup does not by itself verify matching L4N output.

GPU scope: proxies are client-side. Eyebrow and ZOffset select a vertex permutation; no additional draw or pixel lookup is needed. Materials without depth displacement compile the original VS path. No GPU time measurement isolated the new code.
