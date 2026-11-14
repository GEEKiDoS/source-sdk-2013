//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Left 4 Dead 2's SpriteCard. The L4D2 client routes its SpriteCard materials here: the retail shader weights
// $addself differently from the SDK's, and adds $mod2x, $cropfactor, $vertexfogamount and $orientationmatrix
// (native_src/l4d2_spritecard_*.fxc; retail stdshader_dx9.dll 0x33F60). Its ADDSELF cards are also written for
// the display tone curve instead of as scene light. Everything else is cpp/spritecard.cpp.
//
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "convar.h"
#include "renderparm.h"

#include "l4d2_spritecard_ps51.inc"
#include "l4d2_spritecard_vs51.inc"
#include "splinecard_vs51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

int GetDefaultDepthFeatheringValue( void );	// spritecard.cpp

// An ADDSELF card is written as the scene colour the tone curve maps back to its retail colour. A white card would
// need the curve's white point, 16.29 times its peak, and everything above mat_postfx_bloom_threshold feeds the
// bloom; the default stops at that threshold's default, which keeps fire as crisp as retail's.
static ConVar mat_l4d2_spritecard_ceiling( "mat_l4d2_spritecard_ceiling", "1.25", FCVAR_ARCHIVE,
	"Largest scene value an emissive (ADDSELF) L4D2 SpriteCard takes under the DX12 tone curve, as a multiple of its peak: 16.29 reaches display white, values above mat_postfx_bloom_threshold make fire glow", true, 1.0f, true, 16.29f );

BEGIN_VS_SHADER_FLAGS( L4D2_SpriteCard, "Left 4 Dead 2 SpriteCard", SHADER_NOT_EDITABLE )

BEGIN_SHADER_PARAMS
SHADER_PARAM( DEPTHBLEND, SHADER_PARAM_TYPE_INTEGER, "0", "fade at intersection boundaries" )
SHADER_PARAM( DEPTHBLENDSCALE, SHADER_PARAM_TYPE_FLOAT, "50.0", "Amplify or reduce DEPTHBLEND fading. Lower values make harder edges." )
SHADER_PARAM( ORIENTATION, SHADER_PARAM_TYPE_INTEGER, "0", "0 = always face camera, 1 = rotate around z, 2= parallel to ground" )
SHADER_PARAM( ORIENTATIONMATRIX, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "plane of orientation 2" )
SHADER_PARAM( ADDBASETEXTURE2, SHADER_PARAM_TYPE_FLOAT, "0.0", "amount to blend second texture into frame by" )
SHADER_PARAM( OVERBRIGHTFACTOR, SHADER_PARAM_TYPE_FLOAT, "1.0", "overbright factor for texture. For HDR effects.")
SHADER_PARAM( DUALSEQUENCE, SHADER_PARAM_TYPE_INTEGER, "0", "blend two separate animated sequences.")
SHADER_PARAM( SEQUENCE_BLEND_MODE, SHADER_PARAM_TYPE_INTEGER, "0", "defines the blend mode between the images un dual sequence particles. 0 = avg, 1=alpha from first, rgb from 2nd, 2= first over second" )
SHADER_PARAM( MAXLUMFRAMEBLEND1, SHADER_PARAM_TYPE_INTEGER, "0", "instead of blending between animation frames for the first sequence, select pixels based upon max luminance" )
SHADER_PARAM( MAXLUMFRAMEBLEND2, SHADER_PARAM_TYPE_INTEGER, "0", "instead of blending between animation frames for the 2nd sequence, select pixels based upon max luminance" )
SHADER_PARAM( RAMPTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "if specified, then the red value of the image is used to index this ramp to produce the output color" )
SHADER_PARAM( ZOOMANIMATESEQ2, SHADER_PARAM_TYPE_FLOAT, "1.0", "amount to gradually zoom between frames on the second sequence. 2.0 will double the size of a frame over its lifetime.")
SHADER_PARAM( EXTRACTGREENALPHA, SHADER_PARAM_TYPE_INTEGER, "0", "grayscale data sitting in green/alpha channels")
SHADER_PARAM( ADDOVERBLEND, SHADER_PARAM_TYPE_INTEGER, "0", "use ONE:INVSRCALPHA blending")
SHADER_PARAM( ADDSELF, SHADER_PARAM_TYPE_FLOAT, "0.0", "amount of base texture to additively blend in" )
SHADER_PARAM( MOD2X, SHADER_PARAM_TYPE_BOOL, "0", "modulate the destination, with 0.5 as the identity" )
SHADER_PARAM( BLENDFRAMES, SHADER_PARAM_TYPE_BOOL, "1", "whether or not to smoothly blend between animated frames" )
SHADER_PARAM( CROPFACTOR, SHADER_PARAM_TYPE_VEC2, "[1 1]", "fraction of the card and of each sheet frame to draw, about the centre" )
SHADER_PARAM( VERTEXFOGAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0.0", "amount of the fog to fade alpha by" )
SHADER_PARAM( MINSIZE, SHADER_PARAM_TYPE_FLOAT, "0.0", "minimum screen fractional size of particle")
SHADER_PARAM( STARTFADESIZE, SHADER_PARAM_TYPE_FLOAT, "10.0", "screen fractional size to start fading particle out")
SHADER_PARAM( ENDFADESIZE, SHADER_PARAM_TYPE_FLOAT, "20.0", "screen fractional size to finish fading particle out")
SHADER_PARAM( MAXSIZE, SHADER_PARAM_TYPE_FLOAT, "20.0", "maximum screen fractional size of particle")
SHADER_PARAM( USEINSTANCING, SHADER_PARAM_TYPE_BOOL, "1", "whether to use GPU vertex instancing (submit 1 vert per particle quad)")
SHADER_PARAM( SPLINETYPE, SHADER_PARAM_TYPE_INTEGER, "0", "spline type 0 = none,  1=ctamull rom")
SHADER_PARAM( MAXDISTANCE, SHADER_PARAM_TYPE_FLOAT, "100000.0", "maximum distance to draw particles at")
SHADER_PARAM( FARFADEINTERVAL, SHADER_PARAM_TYPE_FLOAT, "400.0", "interval over which to fade out far away particles")
END_SHADER_PARAMS

SHADER_INIT_PARAMS()
{
	INIT_FLOAT_PARM( MAXDISTANCE, 100000.0);
	INIT_FLOAT_PARM( FARFADEINTERVAL, 400.0);
	INIT_FLOAT_PARM( MAXSIZE, 20.0 );
	INIT_FLOAT_PARM( ENDFADESIZE, 20.0 );
	INIT_FLOAT_PARM( STARTFADESIZE, 10.0 );
	INIT_FLOAT_PARM( DEPTHBLENDSCALE, 50.0 );
	INIT_FLOAT_PARM( OVERBRIGHTFACTOR, 1.0 );
	INIT_FLOAT_PARM( ADDBASETEXTURE2, 0.0 );
	INIT_FLOAT_PARM( ADDSELF, 0.0 );
	INIT_FLOAT_PARM( ZOOMANIMATESEQ2, 0.0 );
	INIT_FLOAT_PARM( VERTEXFOGAMOUNT, 0.0 );

	if ( !params[CROPFACTOR]->IsDefined() )
	{
		params[CROPFACTOR]->SetVecValue( 1.0f, 1.0f );
	}
	if ( !params[ORIENTATIONMATRIX]->IsDefined() )
	{
		VMatrix identity;
		identity.Identity();
		params[ORIENTATIONMATRIX]->SetMatrixValue( identity );
	}
	if ( !params[DEPTHBLEND]->IsDefined() )
	{
		params[ DEPTHBLEND ]->SetIntValue( GetDefaultDepthFeatheringValue() );
	}
	static const int nZero[] = { DUALSEQUENCE, MAXLUMFRAMEBLEND1, MAXLUMFRAMEBLEND2, EXTRACTGREENALPHA, ADDOVERBLEND, MOD2X, USEINSTANCING };
	for ( int i = 0; i < ARRAYSIZE( nZero ); ++i )
	{
		if ( !params[nZero[i]]->IsDefined() )
		{
			params[nZero[i]]->SetIntValue( 0 );
		}
	}
	if ( !params[BLENDFRAMES]->IsDefined() )
	{
		params[ BLENDFRAMES ]->SetIntValue( 1 );
	}
	SET_FLAGS2( MATERIAL_VAR2_IS_SPRITECARD );
}

SHADER_FALLBACK
{
	return 0;
}

SHADER_INIT
{
	SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );

	if ( params[BASETEXTURE]->IsDefined() )
	{
		LoadTexture( BASETEXTURE, params[EXTRACTGREENALPHA]->GetIntValue() == 0 ? TEXTUREFLAGS_SRGB : 0 );
	}
	if ( params[RAMPTEXTURE]->IsDefined() )
	{
		LoadTexture( RAMPTEXTURE, TEXTUREFLAGS_SRGB );
	}
}

SHADER_DRAW
{
	bool bUseRampTexture = params[RAMPTEXTURE]->IsDefined();
	bool bZoomSeq2 = params[ZOOMANIMATESEQ2]->GetFloatValue() > 1.0;
	bool bDepthBlend = params[DEPTHBLEND]->GetIntValue() != 0;
	bool bAdditive2ndTexture = params[ADDBASETEXTURE2]->GetFloatValue() != 0.0;
	int nSplineType = params[SPLINETYPE]->GetIntValue();

	SHADOW_STATE
	{
		bool bSecondSequence = params[DUALSEQUENCE]->GetIntValue() != 0;
		bool bAddOverBlend = params[ADDOVERBLEND]->GetIntValue() != 0;
		bool bExtractGreenAlpha = params[EXTRACTGREENALPHA]->GetIntValue() != 0;
		bool bBlendFrames = params[BLENDFRAMES]->GetIntValue() != 0;
		if ( nSplineType )
		{
			bBlendFrames = false;
		}
		bool bAddSelf = params[ADDSELF]->GetFloatValue() != 0.0;
		bool bMod2X = params[MOD2X]->GetIntValue() != 0;

		// draw back-facing because of yaw spin
		pShaderShadow->EnableCulling( false );

		// Be sure not to write to dest alpha
		pShaderShadow->EnableAlphaWrites( false );

		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		if ( bUseRampTexture )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
		}

		if ( bDepthBlend )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
		}

		pShaderShadow->EnableAlphaTest( !bAdditive2ndTexture && !bAddSelf );
		// Retail keeps every nonzero-alpha texel (the SDK tests against 0.01).
		pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.0f );

		if ( bMod2X )
		{
			EnableAlphaBlending( SHADER_BLEND_DST_COLOR, SHADER_BLEND_SRC_COLOR );
		}
		else if ( bAdditive2ndTexture || bAddOverBlend || bAddSelf )
		{
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}
		else if ( IS_FLAG_SET(MATERIAL_VAR_ADDITIVE) )
		{
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE );
		}
		else
		{
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}

		unsigned int flags = VERTEX_POSITION | VERTEX_COLOR;
		static int s_TexCoordSize[8]={4,				// 0 = sheet bounding uvs, frame0
			4,				// 1 = sheet bounding uvs, frame 1
			4,				// 2 = frame blend, rot, radius, ???
			2,				// 3 = corner identifier ( 0/0,1/0,1/1, 1/0 )
			4,				// 4 = texture 2 bounding uvs
			4,				// 5 = second sequence bounding uvs, frame0
			4,				// 6 = second sequence bounding uvs, frame1
			4,				// 7 = second sequence frame blend, ?,?,?
		};
		static int s_TexCoordSizeSpline[]={4,				// 0 = sheet bounding uvs, frame0
			4,				// 1 = sheet bounding uvs, frame 1
			4,				// 2 = frame blend, rot, radius, ???
			4,				// 3 = corner identifier ( 0/0,1/0,1/1, 1/0 )
			4,				// 4 = texture 2 bounding uvs
			4,				// 5 = second sequence bounding uvs, frame0
			4,				// 6 = second sequence bounding uvs, frame1
			4,				// 7 = second sequence frame blend, ?,?,?
		};

		// the whole shebang - 2 sequences, with a possible multi-image sequence first
		int numTexCoords = bSecondSequence ? 8 : 5;
		pShaderShadow->VertexShaderVertexFormat( flags,
			numTexCoords,
			nSplineType? s_TexCoordSizeSpline : s_TexCoordSize, 0 );

		if ( nSplineType )
		{
			DECLARE_STATIC_VERTEX_SHADER( splinecard_vs51 );
			SET_STATIC_VERTEX_SHADER( splinecard_vs51 );
		}
		else
		{
			DECLARE_STATIC_VERTEX_SHADER( l4d2_spritecard_vs51 );
			SET_STATIC_VERTEX_SHADER_COMBO( ZOOM_ANIMATE_SEQ2, bZoomSeq2 );
			SET_STATIC_VERTEX_SHADER_COMBO( DUALSEQUENCE, bSecondSequence );
			SET_STATIC_VERTEX_SHADER_COMBO( EXTRACTGREENALPHA, bExtractGreenAlpha );
			SET_STATIC_VERTEX_SHADER( l4d2_spritecard_vs51 );
		}

		// The second-texture path is the SDK's and excludes the two retail colour paths.
		DECLARE_STATIC_PIXEL_SHADER( l4d2_spritecard_ps51 );
		SET_STATIC_PIXEL_SHADER_COMBO( DUALSEQUENCE, bSecondSequence );
		SET_STATIC_PIXEL_SHADER_COMBO( SEQUENCE_BLEND_MODE, bSecondSequence ? params[SEQUENCE_BLEND_MODE]->GetIntValue() : 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( ADDBASETEXTURE2, bAdditive2ndTexture );
		SET_STATIC_PIXEL_SHADER_COMBO( MAXLUMFRAMEBLEND1, params[MAXLUMFRAMEBLEND1]->GetIntValue() );
		SET_STATIC_PIXEL_SHADER_COMBO( MAXLUMFRAMEBLEND2, bSecondSequence? params[MAXLUMFRAMEBLEND1]->GetIntValue() : 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( EXTRACTGREENALPHA, bExtractGreenAlpha );
		SET_STATIC_PIXEL_SHADER_COMBO( COLORRAMP, bUseRampTexture );
		SET_STATIC_PIXEL_SHADER_COMBO( ANIMBLEND, bBlendFrames );
		SET_STATIC_PIXEL_SHADER_COMBO( ADDSELF, bAddSelf && !bAdditive2ndTexture );
		SET_STATIC_PIXEL_SHADER_COMBO( MOD2X, bMod2X && !bAdditive2ndTexture );
		SET_STATIC_PIXEL_SHADER_COMBO( DEPTHBLEND, bDepthBlend );
		SET_STATIC_PIXEL_SHADER( l4d2_spritecard_ps51 );

		pShaderShadow->EnableSRGBWrite( true );
		if( !bExtractGreenAlpha )
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
	}
	DYNAMIC_STATE
	{
		BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );

		if ( bUseRampTexture )
		{
			BindTexture( SHADER_SAMPLER1, RAMPTEXTURE, FRAME );
		}

		if ( bDepthBlend )
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_FRAME_BUFFER_FULL_DEPTH );
		}

		LoadViewportTransformScaledIntoVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10 );

		// Retail's orientation 3 (the particle's normal) needs a vertex stream the particle renderers here do
		// not write; it draws as orientation 2.
		int nOrientation = clamp( params[ORIENTATION]->GetIntValue(), 0, 2 );

		if ( nOrientation == 0 )
		{
			// We need these only when screen-orienting
			LoadModelViewMatrixIntoVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			LoadProjectionMatrixIntoVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3 );
		}
		else if ( nOrientation == 2 )
		{
			const VMatrix &matrix = params[ORIENTATIONMATRIX]->GetMatrixValue();
			DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, matrix.Base(), 3 );
		}

		const float *pCrop = params[CROPFACTOR]->GetVecValue();
		float vCrop[4] = { pCrop[0], pCrop[1], 0.5f - 0.5f * pCrop[0], 0.5f - 0.5f * pCrop[1] };
		DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, vCrop, 1 );

		// The vertex fog constants describe range fog whether or not the scene has any, so only it fades cards.
		float flZScale = bZoomSeq2 ? 1.0f / params[ZOOMANIMATESEQ2]->GetFloatValue() : 1.0f;
		float flVertexFog = pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR ? params[VERTEXFOGAMOUNT]->GetFloatValue() : 0.0f;
		float vScale[4] = { 0.5f * ( 1.0f + flZScale ), flZScale, flVertexFog, 0.0f };
		DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_7, vScale, 1 );

		// set fade constants in vsconsts 8 and 9
		float flMaxDistance = params[MAXDISTANCE]->GetFloatValue();
		float flStartFade = max( 1.f, flMaxDistance - params[FARFADEINTERVAL]->GetFloatValue() );

		float VC0[8]={ params[MINSIZE]->GetFloatValue(), params[MAXSIZE]->GetFloatValue(),
			params[STARTFADESIZE]->GetFloatValue(), params[ENDFADESIZE]->GetFloatValue(),
			flStartFade, (float)(1.0/(flMaxDistance-flStartFade)),
			0,0 };

		DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, VC0, ARRAYSIZE(VC0)/4 );

		DX12SetDepthFeatheringPixelShaderConstant( 2, params[DEPTHBLENDSCALE]->GetFloatValue() );

		float C0[4]={ params[ADDBASETEXTURE2]->GetFloatValue(),
			params[OVERBRIGHTFACTOR]->GetFloatValue(),
			params[ADDSELF]->GetFloatValue(),
			0.0f };

		DX12SetPixelShaderConstant( 0, C0, ARRAYSIZE(C0)/4 );

		// This frame's tone curve, the ceiling as the curve's input and as the scene value that becomes it.
		const Vector vDisplay = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_DX12_DISPLAY_TRANSFORM );
		const float flCeiling = mat_l4d2_spritecard_ceiling.GetFloat() * vDisplay.x;
		float vCurve[4] = { vDisplay.x, vDisplay.y, flCeiling, flCeiling * exp2f( -vDisplay.y ) };
		DX12SetPixelShaderConstant( 1, vCurve, 1 );

		if ( nSplineType )
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( splinecard_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( splinecard_vs51 );
		}
		else
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_spritecard_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( ORIENTATION, nOrientation );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_spritecard_vs51 );
		}

		DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_spritecard_ps51 );
		SET_DYNAMIC_PIXEL_SHADER( l4d2_spritecard_ps51 );
	}
	Draw( );
}
END_SHADER
