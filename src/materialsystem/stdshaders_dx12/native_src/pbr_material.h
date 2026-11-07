// Material constants shared by pbr_world_* and pbr_model_* (PBR-only; never included by legacy sources).
// Staged by the material DLL through the legacy register vocabulary named in each @legacy annotation; the packer
// generates dx12cb::PBRMaterialPS / dx12cb::PBRMaterialVS from these blocks. This is a .h on purpose: the packer's
// annotation scan covers only .h/.fxc files, and each annotation must end its line. Bare member names must not
// collide with any @legacy member elsewhere in hlsl/ or native_src/ (the packer merges both scans), hence
// g_ReflectionTint / g_EmissionTint.
#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#if defined( DX12_STAGE_PIXEL )
cbuffer PBRMaterialPS : register( b1, space1 )
{
	// rgb linear modulation, a alpha
	float4 g_BaseTint; // @legacy ps:c0
	// x metalness, y roughness, z ao (multipliers of MRAO.rgb when hasMrao), w workflow (0 metalness, 1 specular)
	float4 g_Surface; // @legacy ps:c1
	// rgb linear F0 (specular workflow without texture, legacy Phong); w unused (alpha test is engine state)
	float4 g_SpecularColor; // @legacy ps:c2
	// x hasMrao, y hasBump (0 none, 1 normal map, 2 ssbump), z hasSpecTex, w emissionMode (0 none, 1 base alpha * tint, 2 emission texture)
	float4 g_Flags0; // @legacy ps:c3
	// x hasEnvmap, y envmapMask (0 none, 1 texture rgb, 2 1-base alpha, 3 normal alpha), z hasDetail, w detailBlendMode (0 mod2x, 1 additive)
	float4 g_Flags1; // @legacy ps:c4
	// rgb linear envmap tint, w parallaxEnabled
	float4 g_ReflectionTint; // @legacy ps:c5
	// parallax box: world -> unit cube [0,1]^3 rows
	float4 g_Parallax0; // @legacy ps:c6
	float4 g_Parallax1; // @legacy ps:c7
	float4 g_Parallax2; // @legacy ps:c8
	// xyz cubemap capture origin
	float4 g_EnvmapOrigin; // @legacy ps:c9
	// rgb emission tint, w draw-local light count (model)
	float4 g_EmissionTint; // @legacy ps:c10
	// x subsurface, y backlight, z thickness, w anisotropy (signed)
	float4 g_Options; // @legacy ps:c11
	// rgb subsurface tint, w hasThicknessTex
	float4 g_SubsurfaceTint; // @legacy ps:c12
	// rgb backlight tint, w anisotropy rotation cos
	float4 g_BacklightTint; // @legacy ps:c13
	// x anisotropy rotation sin, y halfLambert (model), z legacy Phong bits (1 Phong, 2 exponent texture, 4 mask = base alpha), w $blendmodulatetexture present (world)
	float4 g_Misc; // @legacy ps:c14
	// rgb $detailtint (world: raw, models: linear), w $detailblendfactor
	float4 g_PbrDetail; // @legacy ps:c30
	// x 1: $nodiffusebumplighting (diffuse from the flat lightmap plane and the vertex normal; the normal map shades reflections only)
	// y 1: legacy-replacement material gloss (roughness = 1 - environment mask; the mask no longer scales the reflection)
	// z 1 (world): $basetexture2 layer; w 1 (world): legacy-replacement material (LightmappedGeneric / WorldVertexTransition)
	float4 g_PbrFlags2; // @legacy ps:c31
	// World: x 1 $maskedblending (layer blend = 0.5, or $blendmodulatetexture.g, instead of the vertex alpha), y $alpha2 factor (1 unless
	// $alpha2 > 0), z 1 $bumpmap2 layer, w 1 $bumpmask (two scrolling normal maps under a static mask)
	float4 g_PbrBlend; // @legacy ps:c32
	// World: the texture transform hlsl/lightmappedgeneric_vs51.fxc keeps in one register for the $envmapmask texture and the second
	// normal map of $bumpmask: $envmapmasktransform when the material has an $envmapmask texture, else $bumptransform2. Rows (a b _ tx)
	// and (c d _ ty), applied to the untransformed UV in the pixel shader.
	float4 g_PbrMaskTransform[2]; // @legacy ps:c33
};
#endif

#if defined( DX12_STAGE_VERTEX )
cbuffer PBRMaterialVS : register( b2, space1 )
{
	// VERTEX_SHADER_SHADER_SPECIFIC_CONST_0/1
	float4 g_BaseTransform[2]; // @legacy vs:c48
	float4 g_BumpTransform[2]; // @legacy vs:c50
	float4 g_DetailTransform[2]; // @legacy vs:c52
	// world: $blendmodulatetexture transform
	float4 g_BlendTransform[2]; // @legacy vs:c54
	// hardware morph (SetHWMorphVertexShaderState)
	float3 g_MorphTargetTextureDim; // @legacy vs:c14
	float4 g_MorphSubrect; // @legacy vs:c15
	// x 1: $vertexcolor (the mesh COLOR stream scales albedo and alpha)
	float4 g_PbrVertexColor; // @legacy vs:c56
};
#endif

// Two-row texture transform (a b _ tx / c d _ ty) applied to a mesh UV.
float2 PBR_TransformUV( float2 uv, float4 transform[2] )
{
	return float2( dot( uv, transform[0].xy ) + transform[0].w, dot( uv, transform[1].xy ) + transform[1].w );
}

#endif
