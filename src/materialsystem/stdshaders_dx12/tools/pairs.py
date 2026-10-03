from genmat import native_only, pair
SCREENSPACE = [
    pair('screenspace', 'writez_vs20', None, ['writez_dx9.cpp']),
    pair('screenspace', 'bufferclearobeystencil_vs20', 'bufferclearobeystencil_ps20b', ['BufferClearObeyStencil_dx9.cpp']),
    pair('screenspace', 'debugtextureview_vs20', 'debugtextureview_ps20b', ['DebugTextureView.cpp']),
    pair('screenspace', 'bik_vs20', 'bik_ps20b', ['bik_dx90.cpp']),
    pair('screenspace', 'debugmrttexture_vs20', 'debugmrttexture_ps20b', ['debugmrttexture.cpp']),
    pair('screenspace', 'depthwrite_vs30', 'depthwrite_ps30', ['depthwrite.cpp']),
    pair('screenspace', 'rendertargetblit_vs20', 'rendertargetblit_ps20b', ['rendertargetblit_x360.cpp']),
    pair('screenspace', 'shadow_vs20', 'shadow_ps20b', ['shadow.cpp']),
    pair('screenspace', 'unlitgeneric_vs20', 'shadowbuildtexture_ps20b', ['shadowbuild_dx9.cpp']),
    pair('screenspace', 'shadowmodel_vs20', 'shadowmodel_ps20', ['shadowmodel_dx9.cpp'], no_parity='legacy-translated-pair-crashes-dxilconv(ps reads T0.w the vs never writes)'),
    pair('screenspace', 'showz_vs20', 'showz_ps20b', ['showz.cpp']),
    pair('screenspace', 'compositor_vs20', 'compositor_ps20b', ['compositor.cpp'], no_parity='vs-material-constants-at-engine-registers-c2-c9(legacy-commit-rewrites-c2-camera)'),
    pair('screenspace', 'screenspaceeffect_vs20', None, ['screenspace_general.cpp']),
]

SSE = 'screenspaceeffect_vs20'
POSTFX = [
    pair('postfx', SSE, 'accumbuff4sample_ps20b', ['AccumBuff4Sample.cpp']),
    pair('postfx', SSE, 'bloom_ps20b', ['Bloom.cpp']),
    pair('postfx', 'blurfilter_vs20', 'blurfilter_ps20b', ['BlurFilterX.cpp', 'BlurFilterY.cpp', 'sfm_blurfilterx.cpp', 'sfm_blurfiltery.cpp']),
    pair('postfx', 'downsample_vs20', 'downsample_ps20b', ['Downsample.cpp', 'sfm_downsample.cpp']),
    pair('postfx', SSE, 'engine_post_ps20b', ['Engine_Post_dx9.cpp']),
    pair('postfx', 'hdrcombineto16bit_vs20', 'hdrcombineto16bit_ps20b', ['HDRCombineTo16Bit.cpp']),
    pair('postfx', 'hdrselectrange_vs20', 'hdrselectrange_ps20b', ['HDRSelectRange.cpp']),
    pair('postfx', SSE, 'accumbuff5sample_ps20b', ['accumbuff5sample.cpp']),
    pair('postfx', 'aftershock_vs20', 'aftershock_ps20b', ['aftershock_helper.cpp']),
    pair('postfx', 'color_projection_vs20', 'color_projection_ps20', ['color_projection.cpp']),
    pair('postfx', SSE, 'colorcorrection_ps20b', ['colorcorrection.cpp']),
    pair('postfx', 'downsample_vs20', 'downsample_nohdr_ps20b', ['downsample_nohdr.cpp']),
    pair('postfx', SSE, 'filmdust_ps20', ['filmdust_dx8_dx9.cpp']),
    pair('postfx', SSE, 'filmgrain_ps20', ['filmgrain_dx8_dx9.cpp']),
    pair('postfx', SSE, 'floatcombine_ps20b', ['floatcombine.cpp']),
    pair('postfx', SSE, 'floatcombine_autoexpose_ps20b', ['floatcombine_autoexpose.cpp']),
    pair('postfx', SSE, 'floattoscreen_ps20b', ['floattoscreen.cpp']),
    # $pixshader values of shipped screenspace_general materials (hl2 VPKs)
    pair('screenspace', SSE, 'lpreview1_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'copy_fp_rt_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'appchooser360movie_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'bloomadd_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'luminance_compare_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'constant_color_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'lpreview_output_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'sample4x4maxmin_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'sample4x4delog_ps20b', ['screenspace_general.cpp']),
    pair('screenspace', SSE, 'sample4x4log_ps20b', ['screenspace_general.cpp']),
    pair('postfx', SSE, 'floattoscreen_notonemap_ps20b', ['floattoscreen.cpp']),
    # floattoscreen_vanilla.cpp selects floattoscreen (its vanilla dynamic index is 0 either way, as in DX9)
    pair('postfx', 'filmgrain_vs20', 'hsl_filmgrain_pass1_ps20b', ['hsl_filmgrain_pass1.cpp']),
    pair('postfx', 'filmgrain_vs20', 'hsl_filmgrain_pass2_ps20b', ['hsl_filmgrain_pass2.cpp']),
    pair('postfx', SSE, 'hsv_ps20b', ['hsv.cpp']),
    pair('postfx', SSE, 'introscreenspaceeffect_ps20b', ['introscreenspaceeffect.cpp']),
    pair('postfx', 'motion_blur_vs20', 'motion_blur_ps20b', ['motion_blur_dx9.cpp']),
    pair('postfx', 'pyro_vision_vs30', 'pyro_vision_ps30', ['pyro_vision.cpp']),
    pair('postfx', 'downsample_vs20', 'sample4x4_ps20b', ['sample4x4.cpp']),
    pair('postfx', 'downsample_vs20', 'sample4x4_blend_ps20b', ['sample4x4_blend.cpp']),
    pair('postfx', 'sfm_combine_vs20', 'sfm_integercombine_ps20b', ['sfm_integercombine.cpp']),
    pair('postfx', 'vr_distort_hud_vs30', 'vr_distort_hud_ps30', ['vr_distort_hud.cpp']),
    pair('postfx', 'vr_distort_texture_vs30', 'vr_distort_texture_ps30', ['vr_distort_texture.cpp']),
]

VL = 'vertexlit_and_unlit_generic'
VERTEXLIT = [
    pair('vertexlit', VL + '_vs30', 'decalmodulate_ps30', ['DecalModulate_dx9.cpp']),
    pair('vertexlit', 'unlittwotexture_vs20', 'monitorscreen_ps20b', ['MonitorScreen_dx9.cpp']),
    pair('vertexlit', 'treeleaf_vs20', 'treeleaf_ps20b', ['TreeLeaf.cpp']),
    pair('vertexlit', 'cable_vs20', 'cable_ps20b', ['cable_dx9.cpp']),
    pair('vertexlit', 'unlitgeneric_vs20', 'modulate_ps20b', ['modulate_dx9.cpp']),
    pair('vertexlit', 'unlittwotexture_vs20', 'unlittwotexture_ps20b', ['unlittwotexture_dx9.cpp']),
    pair('vertexlit', VL + '_bump_vs30', VL + '_bump_ps30', ['vertexlitgeneric_dx9_helper.cpp']),
    pair('vertexlit', VL + '_vs30', VL + '_ps30', ['vertexlitgeneric_dx9_helper.cpp']),
    pair('vertexlit', 'windowimposter_vs20', 'windowimposter_ps20b', ['windowimposter_dx90.cpp']),
]
WORLD_MISC = [
    pair('world_misc', 'core_vs20', 'core_ps20b', ['core_dx9.cpp']),
    pair('world_misc', 'portal_vs20', 'portal_ps20b', ['portal.cpp']),
    pair('world_misc', 'portalstaticoverlay_vs20', 'portalstaticoverlay_ps20b', ['portalstaticoverlay.cpp']),
    pair('world_misc', 'sky_vs20', 'sky_ps20b', ['sky_dx9.cpp', 'sky_hdr_dx9.cpp']),
    pair('world_misc', 'sky_vs20', 'sky_hdr_compressed_ps20b', ['sky_hdr_dx9.cpp']),
    pair('world_misc', 'sky_vs20', 'sky_hdr_compressed_rgbs_ps20b', ['sky_hdr_dx9.cpp']),
    pair('world_misc', 'vortwarp_vs30', 'vortwarp_ps30', ['vortwarp_dx9.cpp']),
    pair('world_misc', 'warp_vs30', 'warp_ps30', ['warp.cpp']),
]
SPRITES = [
    pair('sprites_particles', 'cloud_vs20', 'cloud_ps20', ['cloud_dx9.cpp']),
    pair('sprites_particles', 'particlesphere_vs20', 'particlesphere_ps20b', ['particlesphere_dx9.cpp']),
    pair('sprites_particles', 'sprite_vs20', 'sprite_ps20b', ['sprite_dx9.cpp']),
    pair('sprites_particles', 'spritecard_vs20', 'spritecard_ps20b', ['spritecard.cpp']),
    pair('sprites_particles', 'splinecard_vs20', 'spritecard_ps20b', ['spritecard.cpp'], layout_from='spritecard_vs20'),
    pair('sprites_particles', 'volume_clouds_vs20', 'volume_clouds_ps20b', ['volume_clouds_helper.cpp']),
]
WATER = [
    pair('water_refract', 'cloak_blended_pass_vs30', 'cloak_blended_pass_ps30', ['cloak_blended_pass_helper.cpp']),
    pair('water_refract', 'cloak_vs30', 'cloak_ps30', ['cloak_dx9_helper.cpp']),
    pair('water_refract', 'portal_refract_vs20', 'portal_refract_ps20b', ['portal_refract_helper.cpp']),
    pair('water_refract', 'refract_vs20', 'refract_ps20b', ['refract_dx9_helper.cpp']),
    pair('water_refract', 'shatteredglass_vs20', 'shatteredglass_ps20b', ['shatteredglass.cpp']),
    pair('water_refract', 'water_vs20', 'water_ps20b', ['water.cpp']),
    pair('water_refract', 'watercheap_vs20', 'watercheap_ps20b', ['water.cpp']),
]
EYES = [
    pair('eyes_teeth_skin', 'eye_refract_vs30', 'eye_refract_ps30', ['eye_refract_helper.cpp']),
    pair('eyes_teeth_skin', 'eyeglint_vs20', 'eyeglint_ps20b', ['eyeglint_dx9.cpp']),
    pair('eyes_teeth_skin', 'eyes_flashlight_vs30', 'eyes_flashlight_ps30', ['eyes_dx8_dx9_helper.cpp']),
    pair('eyes_teeth_skin', 'eyes_vs30', 'eyes_ps30', ['eyes_dx8_dx9_helper.cpp']),
    pair('eyes_teeth_skin', 'skin_vs30', 'skin_ps30', ['skin_dx9_helper.cpp']),
    pair('eyes_teeth_skin', 'teeth_bump_vs30', 'teeth_bump_ps30', ['teeth.cpp']),
    pair('eyes_teeth_skin', 'teeth_vs30', 'teeth_ps30', ['teeth.cpp']),
    pair('eyes_teeth_skin', 'teeth_flashlight_vs30', 'teeth_flashlight_ps30', ['teeth.cpp']),
]
LMG = 'lightmappedgeneric'
LIGHTMAPPED = [
    pair('lightmapped', LMG + '_flashlight_vs20', 'flashlight_ps20b', ['BaseVSShader.cpp']),
    pair('lightmapped', 'depthtodestalpha_vs20', 'depthtodestalpha_ps20b', ['BaseVSShader.cpp']),
    pair('lightmapped', LMG + '_decal_vs20', LMG + '_decal_ps20b', ['DecalBaseTimesLightmapAlphaBlendSelfIllum_dx9.cpp']),
    pair('lightmapped', LMG + '_vs20', 'decalbasetimeslightmapalphablendselfillum2_ps20b', ['DecalBaseTimesLightmapAlphaBlendSelfIllum_dx9.cpp']),
    pair('lightmapped', LMG + '_vs20', LMG + '_ps20b', ['lightmappedgeneric_dx9_helper.cpp']),
    pair('lightmapped', 'lightmappedreflective_vs20', 'lightmappedreflective_ps20b', ['lightmappedreflective.cpp']),
    pair('lightmapped', LMG + '_vs20', 'worldtwotextureblend_ps20b', ['worldtwotextureblend.cpp']),
]
MORPH = [
    pair('morph_debug', 'debugmorphaccumulator_vs30', 'debugmorphaccumulator_ps30', ['debugmorphaccumulator_dx9.cpp']),
    pair('morph_debug', 'morphaccumulate_vs30', 'morphaccumulate_ps30', ['morphaccumulate_dx9.cpp']),
    pair('morph_debug', 'morphweight_vs30', 'morphweight_ps30', ['morphweight_dx9.cpp']),
]
EFFECTS = [
    pair('effects_misc', 'emissive_scroll_blended_pass_vs30', 'emissive_scroll_blended_pass_ps30', ['emissive_scroll_blended_pass_helper.cpp']),
    pair('effects_misc', 'flesh_interior_blended_pass_vs20', 'flesh_interior_blended_pass_ps20b', ['flesh_interior_blended_pass_helper.cpp']),
    pair('effects_misc', 'weapon_sheen_pass_vs30', 'weapon_sheen_pass_ps30', ['weapon_sheen_pass_helper.cpp']),
]

NATIVE_ONLY = [
    native_only('postfx', 'motion_blur_mv_ps51.fxc', 'ps', 'motion_blur_mv_ps51'),
    native_only('gtao', 'gtao_prefilter_cs51.fxc', 'cs', 'gtao_prefilter_cs51'),
    native_only('gtao', 'gtao_viewdepth_cs51.fxc', 'cs', 'gtao_viewdepth_cs51'),
    native_only('gtao', 'gtao_main_cs51.fxc', 'cs', 'gtao_main_cs51'),
    native_only('gtao', 'gtao_denoise_cs51.fxc', 'cs', 'gtao_denoise_cs51'),
    native_only('gtao', 'gtao_apply_ps51.fxc', 'ps', 'gtao_apply_ps51'),
    native_only('postfx', 'postfx_downsample_cs51.fxc', 'cs', 'postfx_downsample_cs51'),
    native_only('postfx', 'postfx_upsample_cs51.fxc', 'cs', 'postfx_upsample_cs51'),
    native_only('postfx', 'postfx_flare_cs51.fxc', 'cs', 'postfx_flare_cs51'),
    native_only('postfx', 'postfx_kawase_down_cs51.fxc', 'cs', 'postfx_kawase_down_cs51'),
    native_only('postfx', 'postfx_kawase_up_cs51.fxc', 'cs', 'postfx_kawase_up_cs51'),
    native_only('postfx', 'postfx_glare_cs51.fxc', 'cs', 'postfx_glare_cs51'),
    native_only('postfx', 'postfx_composite_ps51.fxc', 'ps', 'postfx_composite_ps51'),
    native_only('postfx', 'postfx_rcas_ps51.fxc', 'ps', 'postfx_rcas_ps51'),
]
ALL = SCREENSPACE + POSTFX + VERTEXLIT + WORLD_MISC + SPRITES + WATER + EYES + LIGHTMAPPED + MORPH + EFFECTS

# Logical shaders the SDK ships only as compiled VCS (no .fxc source): legacy records via passthrough.
LEGACY_ONLY = {
    'particlelit_generic_vs30': ('vs', 'no-fxc-source-in-sdk'),
    'particlelit_generic_ps30': ('ps', 'no-fxc-source-in-sdk'),
    # WorldVertexAlpha's draw is unreachable (SHADER_FALLBACK always returns WorldVertexAlpha_DX8, as in the SDK's
    # DX9 DLL); its vertex shader is assembly, so the pair keeps the legacy records.
    'WorldVertexAlpha': ('vs', 'assembly-vs-unreachable-draw'),
    'worldvertexalpha_ps20b': ('ps', 'pairs-with-assembly-vs-unreachable-draw'),
    # Hammer-only (UsingEditor) pass of WorldVertexTransition: DX9 DLL draws it with assembly shaders by name.
    'WorldVertexTransition': ('vs', 'assembly-vs-editor-only'),
}
