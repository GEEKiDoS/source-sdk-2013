#include "BaseVSShader.h"
#include "convar.h"
#include "screenspaceeffect_vs20.inc"
#include "terror_depth_of_field_ps20b.inc"
#include "blurgaussian_3x3_ps20b.inc"
#include "terror_dof_clearalpha_ps20b.inc"
#include "tier0/memdbgon.h"

static ConVar mat_dof_max_blur_radius( "mat_dof_max_blur_radius", "10", FCVAR_NONE );

static const float s_Poisson8[8][2] =
{
	{ 0.0f, 0.0f },
	{ 0.527836978f, -0.085868001f },
	{ -0.0400880016f, 0.536086977f },
	{ -0.670445025f, -0.179949f },
	{ -0.419418007f, -0.616038978f },
	{ 0.440452993f, -0.639398992f },
	{ -0.757088006f, 0.349334002f },
	{ 0.574618995f, 0.685878992f },
};

static const float s_Poisson16[16][2] =
{
	{ 0.0746999979f, -0.834100008f },
	{ -0.913800001f, 0.325100005f },
	{ 0.866699994f, -0.302899987f },
	{ -0.46419999f, 0.218700007f },
	{ -0.1505f, 0.731999993f },
	{ 0.731000006f, -0.678600013f },
	{ 0.285899997f, -0.325399995f },
	{ -0.131099999f, -0.229200006f },
	{ 0.351799995f, 0.647000015f },
	{ -0.74849999f, -0.630699992f },
	{ 0.168699995f, 0.187299997f },
	{ -0.360399991f, -0.748300016f },
	{ -0.565800011f, -0.152099997f },
	{ 0.710200012f, 0.0535999984f },
	{ -0.605599999f, 0.774699986f },
	{ 0.779299974f, 0.619400024f },
};

static const float s_Poisson32[32][2] =
{
	{ 0.0854000002f, -0.0644000024f },
	{ 0.87440002f, 0.166500002f },
	{ 0.232899994f, 0.399500012f },
	{ -0.780399978f, 0.548200011f },
	{ -0.457700014f, 0.764699996f },
	{ -0.193599999f, 0.556400001f },
	{ 0.42050001f, -0.576799989f },
	{ -0.0304000005f, -0.904999971f },
	{ -0.521499991f, 0.185399994f },
	{ 0.316100001f, -0.295399994f },
	{ 0.0666000023f, -0.556400001f },
	{ -0.213699996f, -0.00719999988f },
	{ -0.411199987f, -0.331099987f },
	{ 0.64380002f, -0.248400003f },
	{ -0.905499995f, -0.0359999985f },
	{ 0.832300007f, 0.526799977f },
	{ 0.559199989f, 0.345899999f },
	{ -0.679700017f, -0.520099998f },
	{ -0.432500005f, -0.885699987f },
	{ 0.876800001f, -0.419699997f },
	{ 0.308999985f, -0.864600003f },
	{ 0.503400028f, 0.860300004f },
	{ 0.375200003f, 0.0627000034f },
	{ -0.0161000006f, 0.262699991f },
	{ 0.0969000012f, 0.70539999f },
	{ -0.229100004f, -0.659500003f },
	{ -0.588699996f, -0.109999999f },
	{ 0.70480001f, -0.652800024f },
	{ -0.843800008f, 0.270599991f },
	{ -0.506099999f, 0.465299994f },
	{ -0.124499999f, -0.330199987f },
	{ -0.180099994f, 0.84859997f },
};

BEGIN_VS_SHADER_FLAGS( DepthOfField, "Native L4D2 four-quality depth of field", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SMALLFB, SHADER_PARAM_TYPE_TEXTURE, "_rt_SmallFB1", "Downsampled backbuffer" )
		SHADER_PARAM( NEARPLANE, SHADER_PARAM_TYPE_FLOAT, "0", "Near plane depth" )
		SHADER_PARAM( FARPLANE, SHADER_PARAM_TYPE_FLOAT, "0", "Far plane depth" )
		SHADER_PARAM( NEARBLURDEPTH, SHADER_PARAM_TYPE_FLOAT, "0", "Near blur plane depth" )
		SHADER_PARAM( NEARFOCUSDEPTH, SHADER_PARAM_TYPE_FLOAT, "0", "Near focus plane depth" )
		SHADER_PARAM( FARFOCUSDEPTH, SHADER_PARAM_TYPE_FLOAT, "0", "Far focus plane depth" )
		SHADER_PARAM( FARBLURDEPTH, SHADER_PARAM_TYPE_FLOAT, "0", "Far blur plane depth" )
		SHADER_PARAM( NEARBLURRADIUS, SHADER_PARAM_TYPE_FLOAT, "0", "Max near blur radius" )
		SHADER_PARAM( FARBLURRADIUS, SHADER_PARAM_TYPE_FLOAT, "0", "Max far blur radius" )
		SHADER_PARAM( QUALITY, SHADER_PARAM_TYPE_INTEGER, "0", "Quality level. Selects different algorithms." )
	END_SHADER_PARAMS
	SHADER_FALLBACK { return NULL; }
	SHADER_INIT { LoadTexture( BASETEXTURE ); LoadTexture( SMALLFB ); }
	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( terror_depth_of_field_ps20b );
			SET_STATIC_PIXEL_SHADER( terror_depth_of_field_ps20b );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			BindTexture( SHADER_SAMPLER1, SMALLFB, -1 );
			const float nearBlur = params[NEARBLURDEPTH]->GetFloatValue();
			const float nearFocus = params[NEARFOCUSDEPTH]->GetFloatValue();
			const float farFocus = params[FARFOCUSDEPTH]->GetFloatValue();
			const float farBlur = params[FARBLURDEPTH]->GetFloatValue();
			const float maximum = mat_dof_max_blur_radius.GetFloat();
			const float nearRadius = params[NEARBLURRADIUS]->GetFloatValue();
			const float farRadius = params[FARBLURRADIUS]->GetFloatValue();
			const float inverseMaximum = 1.0f / maximum;
			const float nearLimited = maximum <= nearRadius ? maximum : nearRadius;
			float farLimited = maximum;
			if ( maximum > farRadius ) farLimited = farRadius;
			const float nearPlane = params[NEARPLANE]->GetFloatValue();
			const float farPlane = params[FARPLANE]->GetFloatValue();
			const float depthScale = ( ( farPlane - nearPlane ) * 8192.0f ) / farPlane;
			float constants[16] = { nearBlur, nearFocus, farFocus, farBlur,
				maximum, inverseMaximum * nearLimited, inverseMaximum * farLimited, 0,
				nearPlane, farPlane, depthScale, 0,
				depthScale / ( nearBlur - nearFocus ), ( nearPlane - nearFocus ) / ( nearBlur - nearFocus ),
				depthScale / ( farBlur - farFocus ), ( nearPlane - farFocus ) / ( farBlur - farFocus ) };
			pShaderAPI->SetPixelShaderConstant( 0, constants, 4 );
			const int quality = params[QUALITY]->GetIntValue();
			if ( quality < 0 || quality > 3 )
			{
				Warning( "Invalid mat_dof_quality value. Resetting to 0.\n" );
				static ConVarRef mat_dof_quality( "mat_dof_quality" );
				mat_dof_quality.SetValue( 0 );
				// Native continues with an invalid dynamic-combo index after resetting
				// only the ConVar. Approved minimal invalid-access adaptation: skip
				// that draw; material QUALITY is refreshed normally next frame.
				return;
			}
			const int count = quality == 0 ? 8 : quality == 3 ? 32 : 16;
			const float ( *poisson )[2] = quality == 0 ? s_Poisson8 : quality == 3 ? s_Poisson32 : s_Poisson16;
			ITexture *full = params[BASETEXTURE]->GetTextureValue();
			const float invWidth = 1.0f / full->GetActualWidth();
			const float invHeight = 1.0f / full->GetActualHeight();
			float taps[64];
			for ( int i = 0; i < count; ++i ) { taps[i*2] = poisson[i][0] * invWidth; taps[i*2+1] = poisson[i][1] * invHeight; }
			for ( int i = 1; i < count; i += 2 ) { const float swap = taps[i*2]; taps[i*2] = taps[i*2+1]; taps[i*2+1] = swap; }
			pShaderAPI->SetPixelShaderConstant( 4, taps, count / 2 );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( terror_depth_of_field_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, quality );
			SET_DYNAMIC_PIXEL_SHADER( terror_depth_of_field_ps20b );
		}
		Draw();
	}
END_SHADER

BEGIN_VS_SHADER_FLAGS( TerrorDOFBlurGaussian, "Native DOF quarter-buffer Gaussian", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( C0_X, SHADER_PARAM_TYPE_FLOAT, "0", "Tap0 X" )
		SHADER_PARAM( C0_Y, SHADER_PARAM_TYPE_FLOAT, "0", "Tap0 Y" )
		SHADER_PARAM( C1_X, SHADER_PARAM_TYPE_FLOAT, "0", "Tap1 X" )
		SHADER_PARAM( C1_Y, SHADER_PARAM_TYPE_FLOAT, "0", "Tap1 Y" )
	END_SHADER_PARAMS
	SHADER_FALLBACK { return NULL; }
	SHADER_INIT { LoadTexture( BASETEXTURE ); }
	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( blurgaussian_3x3_ps20b );
			SET_STATIC_PIXEL_SHADER( blurgaussian_3x3_ps20b );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			float constants[8] = { params[C0_X]->GetFloatValue(), params[C0_Y]->GetFloatValue(), 0, 0,
				params[C1_X]->GetFloatValue(), params[C1_Y]->GetFloatValue(), 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 0, constants, 2 );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( blurgaussian_3x3_ps20b );
			SET_DYNAMIC_PIXEL_SHADER( blurgaussian_3x3_ps20b );
		}
		Draw();
	}
END_SHADER

BEGIN_VS_SHADER_FLAGS( TerrorDOFClearAlpha, "Native post-skybox alpha1 clear", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS
	SHADER_INIT
	{
		// CLEAR_ALPHA=1 reads no textures or instance parameters.
	}
	SHADER_FALLBACK { return NULL; }
	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableColorWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( terror_dof_clearalpha_ps20b );
			SET_STATIC_PIXEL_SHADER( terror_dof_clearalpha_ps20b );
		}
		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( terror_dof_clearalpha_ps20b );
			SET_DYNAMIC_PIXEL_SHADER( terror_dof_clearalpha_ps20b );
		}
		Draw();
	}
END_SHADER
