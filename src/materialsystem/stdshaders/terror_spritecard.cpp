//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "terror_spritecard_vs20.inc"
#include "terror_spritecard_ps20b.inc"

#include "tier0/memdbgon.h"

// Game-owned preparation selects the native outlined-card and simple-colour
// spline domains. Other SpriteCard families retain their existing owner.
BEGIN_VS_SHADER_FLAGS( TerrorSpriteCard, "L4D2 outlined cards and spline particles", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DEPTHBLEND, SHADER_PARAM_TYPE_INTEGER, "0", "Intersection fading" )
		SHADER_PARAM( DEPTHBLENDSCALE, SHADER_PARAM_TYPE_FLOAT, "50.0", "Intersection fade scale" )
		SHADER_PARAM( INVERSEDEPTHBLEND, SHADER_PARAM_TYPE_BOOL, "0", "Inverse intersection fade" )
		SHADER_PARAM( ORIENTATION, SHADER_PARAM_TYPE_INTEGER, "0", "Card orientation" )
		SHADER_PARAM( ORIENTATIONMATRIX, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "Ground-plane orientation" )
		SHADER_PARAM( OVERBRIGHTFACTOR, SHADER_PARAM_TYPE_FLOAT, "1.0", "Texture brightness" )
		SHADER_PARAM( BLENDFRAMES, SHADER_PARAM_TYPE_BOOL, "1", "Blend the two sheet frames" )
		SHADER_PARAM( SPLINETYPE, SHADER_PARAM_TYPE_INTEGER, "0", "0 = ordinary card, 1 = Catmull-Rom spline" )
		SHADER_PARAM( ALPHATRAILFADE, SHADER_PARAM_TYPE_FLOAT, "1", "End alpha scale for a trail or rope" )
		SHADER_PARAM( RADIUSTRAILFADE, SHADER_PARAM_TYPE_FLOAT, "1", "End radius scale for a trail or rope" )
		SHADER_PARAM( MINSIZE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Minimum fractional screen size" )
		SHADER_PARAM( STARTFADESIZE, SHADER_PARAM_TYPE_FLOAT, "10.0", "Start size fade" )
		SHADER_PARAM( ENDFADESIZE, SHADER_PARAM_TYPE_FLOAT, "20.0", "End size fade" )
		SHADER_PARAM( MAXSIZE, SHADER_PARAM_TYPE_FLOAT, "20.0", "Maximum fractional screen size" )
		SHADER_PARAM( MAXDISTANCE, SHADER_PARAM_TYPE_FLOAT, "100000.0", "Maximum distance" )
		SHADER_PARAM( FARFADEINTERVAL, SHADER_PARAM_TYPE_FLOAT, "400.0", "Distance fade interval" )
		SHADER_PARAM( USEINSTANCING, SHADER_PARAM_TYPE_BOOL, "1", "GPU card instancing" )
		SHADER_PARAM( VERTEXFOGAMOUNT, SHADER_PARAM_TYPE_FLOAT, "1", "Vertex fog amount" )
		SHADER_PARAM( DISTANCEALPHA, SHADER_PARAM_TYPE_BOOL, "0", "Distance-coded alpha" )
		SHADER_PARAM( SOFTEDGES, SHADER_PARAM_TYPE_BOOL, "0", "Distance edge softness" )
		SHADER_PARAM( EDGESOFTNESSSTART, SHADER_PARAM_TYPE_FLOAT, "0.6", "Distance edge start" )
		SHADER_PARAM( EDGESOFTNESSEND, SHADER_PARAM_TYPE_FLOAT, "0.5", "Distance edge end" )
		SHADER_PARAM( OUTLINE, SHADER_PARAM_TYPE_BOOL, "0", "Distance outline" )
		SHADER_PARAM( OUTLINECOLOR, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Outline color" )
		SHADER_PARAM( OUTLINEALPHA, SHADER_PARAM_TYPE_FLOAT, "0.0", "Outline alpha" )
		SHADER_PARAM( OUTLINESTART0, SHADER_PARAM_TYPE_FLOAT, "0.0", "Outer start" )
		SHADER_PARAM( OUTLINESTART1, SHADER_PARAM_TYPE_FLOAT, "0.0", "Inner start" )
		SHADER_PARAM( OUTLINEEND0, SHADER_PARAM_TYPE_FLOAT, "0.0", "Inner end" )
		SHADER_PARAM( OUTLINEEND1, SHADER_PARAM_TYPE_FLOAT, "0.0", "Outer end" )
		SHADER_PARAM( PERPARTICLEOUTLINE, SHADER_PARAM_TYPE_BOOL, "0", "Per-particle outline RGBA" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// Original stdshader_dx9.dll:0x33830. The runtime defaults differ
		// from the registration strings for these two parameters.
		if ( !params[USEINSTANCING]->IsDefined() ) params[USEINSTANCING]->SetIntValue( 0 );
		if ( !params[VERTEXFOGAMOUNT]->IsDefined() ) params[VERTEXFOGAMOUNT]->SetFloatValue( 0.0f );
		if ( !params[BLENDFRAMES]->IsDefined() ) params[BLENDFRAMES]->SetIntValue( 1 );
		const int nZero[] = { DISTANCEALPHA, SOFTEDGES, OUTLINE, PERPARTICLEOUTLINE, ORIENTATION, INVERSEDEPTHBLEND, SPLINETYPE };
		for ( int i = 0; i < ARRAYSIZE( nZero ); ++i )
		{
			if ( !params[nZero[i]]->IsDefined() ) params[nZero[i]]->SetIntValue( 0 );
		}
		INIT_FLOAT_PARM( MINSIZE, 0.0f );
		INIT_FLOAT_PARM( MAXSIZE, 20.0f );
		INIT_FLOAT_PARM( STARTFADESIZE, 10.0f );
		INIT_FLOAT_PARM( ENDFADESIZE, 20.0f );
		INIT_FLOAT_PARM( MAXDISTANCE, 100000.0f );
		INIT_FLOAT_PARM( FARFADEINTERVAL, 400.0f );
		INIT_FLOAT_PARM( DEPTHBLENDSCALE, 50.0f );
		INIT_FLOAT_PARM( OVERBRIGHTFACTOR, 1.0f );
		INIT_FLOAT_PARM( ALPHATRAILFADE, 1.0f );
		INIT_FLOAT_PARM( RADIUSTRAILFADE, 1.0f );
		INIT_FLOAT_PARM( OUTLINEALPHA, 1.0f );
		if ( !params[ORIENTATIONMATRIX]->IsDefined() )
		{
			VMatrix identity;
			identity.Identity();
			params[ORIENTATIONMATRIX]->SetMatrixValue( identity );
		}
		SET_FLAGS2( MATERIAL_VAR2_IS_SPRITECARD );
	}

	SHADER_FALLBACK
	{
		return NULL;
	}

	SHADER_INIT
	{
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
	}

	SHADER_DRAW
	{
		const bool bSpline = params[SPLINETYPE]->GetIntValue() != 0;
		const bool bAnimBlend = params[BLENDFRAMES]->GetIntValue() != 0 && !bSpline;
		const bool bDistanceAlpha = params[DISTANCEALPHA]->GetIntValue() != 0 && !bSpline;
		const bool bOutline = bDistanceAlpha && params[OUTLINE]->GetIntValue() != 0;
		const bool bPerParticleOutline = bDistanceAlpha && params[PERPARTICLEOUTLINE]->GetIntValue() != 0;
		const bool bFog = params[VERTEXFOGAMOUNT]->GetFloatValue() != 0.0f;
		if ( pShaderShadow )
		{
			pShaderShadow->EnableCulling( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBWrite( true );
			pShaderShadow->EnableAlphaTest( true );
			// Retail SpriteCard tests strictly greater than zero, not the
			// native SDK shader's 0.01 threshold (original RVA 0x33F60).
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.0f );
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA,
				IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) ? SHADER_BLEND_ONE : SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			static int s_TexCoordSize[6] = { 4, 4, 4, 2, 4, 4 };
			static int s_SplineTexCoordSize[4] = { 4, 4, 4, 4 };
			const int nOrientation = clamp( params[ORIENTATION]->GetIntValue(), 0, 3 );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_COLOR,
				bSpline ? 4 : ( bPerParticleOutline || nOrientation == 3 ? 6 : 5 ),
				bSpline ? s_SplineTexCoordSize : s_TexCoordSize, 0 );
			DECLARE_STATIC_VERTEX_SHADER( terror_spritecard_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( ANIMBLEND, bAnimBlend );
			SET_STATIC_VERTEX_SHADER_COMBO( PERPARTICLEOUTLINE, bPerParticleOutline );
			SET_STATIC_VERTEX_SHADER_COMBO( SPRITECARDVERTEXFOG, bFog );
			SET_STATIC_VERTEX_SHADER_COMBO( SPLINE, bSpline );
			SET_STATIC_VERTEX_SHADER( terror_spritecard_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( terror_spritecard_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( DISTANCEALPHA, bDistanceAlpha );
			SET_STATIC_PIXEL_SHADER_COMBO( OUTLINE, bOutline );
			// Original selects SOFTEDGES from OUTLINE as well; the separate
			// SOFTEDGES parameter is not read here. Preserve that original bug.
			SET_STATIC_PIXEL_SHADER_COMBO( SOFTEDGES, bOutline );
			SET_STATIC_PIXEL_SHADER_COMBO( ANIMBLEND, bAnimBlend );
			SET_STATIC_PIXEL_SHADER( terror_spritecard_ps20b );
		}
		else
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			const int nOrientation = clamp( params[ORIENTATION]->GetIntValue(), 0, 3 );
			if ( nOrientation == 0 )
			{
				LoadModelViewMatrixIntoVertexShaderConstant( 48 );
				LoadProjectionMatrixIntoVertexShaderConstant( 51 );
			}
			else if ( nOrientation == 2 )
			{
				const VMatrix &matrix = params[ORIENTATIONMATRIX]->GetMatrixValue();
				pShaderAPI->SetVertexShaderConstant( 48, matrix.Base(), 3 );
			}
			const float flMaxDistance = params[MAXDISTANCE]->GetFloatValue();
			const float flStartFade = MAX( 1.0f, flMaxDistance - params[FARFADEINTERVAL]->GetFloatValue() );
			float vSize[8] = {
				params[MINSIZE]->GetFloatValue(), params[MAXSIZE]->GetFloatValue(),
				params[STARTFADESIZE]->GetFloatValue(), params[ENDFADESIZE]->GetFloatValue(),
				flStartFade, 1.0f / ( flMaxDistance - flStartFade ),
				bSpline ? params[ALPHATRAILFADE]->GetFloatValue() : 0.0f,
				bSpline ? params[RADIUSTRAILFADE]->GetFloatValue() : 0.0f
			};
			pShaderAPI->SetVertexShaderConstant( 56, vSize, 2 );
			float v[4] = { 0.0f, 0.0f, params[VERTEXFOGAMOUNT]->GetFloatValue(), 0.0f };
			pShaderAPI->SetVertexShaderConstant( 55, v );
			v[0] = 0.0f; v[1] = params[OVERBRIGHTFACTOR]->GetFloatValue(); v[2] = v[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 0, v );
			if ( bDistanceAlpha )
			{
				params[OUTLINECOLOR]->GetVecValue( v, 3 ); v[3] = params[OUTLINEALPHA]->GetFloatValue();
				pShaderAPI->SetPixelShaderConstant( 7, v );
				v[0] = params[OUTLINESTART0]->GetFloatValue(); v[1] = params[OUTLINESTART1]->GetFloatValue();
				v[2] = params[OUTLINEEND0]->GetFloatValue(); v[3] = params[OUTLINEEND1]->GetFloatValue();
				pShaderAPI->SetPixelShaderConstant( 8, v );
				v[0] = params[EDGESOFTNESSSTART]->GetFloatValue(); v[1] = params[EDGESOFTNESSEND]->GetFloatValue();
				v[2] = v[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 9, v );
			}
			DECLARE_DYNAMIC_VERTEX_SHADER( terror_spritecard_vs20 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( ORIENTATION, nOrientation );
			SET_DYNAMIC_VERTEX_SHADER( terror_spritecard_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( terror_spritecard_ps20b );
			SET_DYNAMIC_PIXEL_SHADER( terror_spritecard_ps20b );
		}
		Draw();
	}
END_SHADER
