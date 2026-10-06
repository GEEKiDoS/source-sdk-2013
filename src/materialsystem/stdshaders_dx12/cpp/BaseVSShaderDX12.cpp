//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native SM5 port of materialsystem/stdshaders/BaseVSShader.cpp: the DX9 helpers with every constant
//          write redirected into the legacy-register staging file (see BaseVSShaderDX12.h), plus the staging,
//          native block gather and the Draw() flush. The DX8 passes are not ported.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "lightmappedgeneric_dx9_helper.h"
#include "shaderapi/commandbuffer.h"
#include "mathlib/vmatrix.h"
#include "mathlib/bumpvects.h"
#include "cpp_shader_constant_register_map.h"
#include "convar.h"
#include <algorithm>

#include "lightmappedgeneric_flashlight_vs51.inc"
#include "flashlight_ps51.inc"
#include "depthtodestalpha_vs51.inc"
#include "depthtodestalpha_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Legacy-register staging
//-----------------------------------------------------------------------------
DX12ConstantStaging g_DX12Constants;

#include "native_block_registry.inc"

namespace
{
void MarkDirty( uint32_t *bits, int reg, int count )
{
	for ( int i = reg; i < reg + count; ++i ) bits[i >> 5] |= 1u << ( i & 31 );
}

template< class T, size_t N >
void StageRows( T ( &file )[N][4], uint32_t *dirty, int reg, const T *data, int count )
{
	// Out-of-range registers are dropped like the DX9 runtime drops writes past the register file.
	if ( !data || reg < 0 || count <= 0 || reg >= (int)N ) return;
	count = MIN( count, (int)N - reg );
	memcpy( file[reg], data, sizeof( T ) * 4 * count );
	MarkDirty( dirty, reg, count );
}

void StageBools( int ( &file )[DX12ConstantStaging::kBool], uint32_t *dirty, int reg, const int *data, int count )
{
	if ( !data || reg < 0 || count <= 0 || reg >= DX12ConstantStaging::kBool ) return;
	count = MIN( count, DX12ConstantStaging::kBool - reg );
	for ( int i = 0; i < count; ++i ) file[reg + i] = data[i] ? 1 : 0;
	MarkDirty( dirty, reg, count );
}

// Writes every run of registers staged since the last replay through the given shader API setter.
template< class T, class Setter >
void ReplayRuns( uint32_t *dirty, int count, Setter set )
{
	for ( int reg = 0; reg < count; )
	{
		if ( !( dirty[reg >> 5] & ( 1u << ( reg & 31 ) ) ) ) { ++reg; continue; }
		int end = reg;
		while ( end < count && ( dirty[end >> 5] & ( 1u << ( end & 31 ) ) ) ) ++end;
		set( reg, end - reg );
		reg = end;
	}
	memset( dirty, 0, sizeof( uint32_t ) * ( ( count + 31 ) / 32 ) );
}

void ReplayStage( IShaderDynamicAPI *api, bool vertex )
{
	DX12ConstantStaging &s = g_DX12Constants;
	if ( vertex )
	{
		ReplayRuns< float >( s.vsFloatDirty, DX12ConstantStaging::kVSFloat, [&]( int reg, int n ) { api->SetVertexShaderConstant( reg, s.vsFloat[reg], n ); } );
		ReplayRuns< int >( &s.vsIntDirty, DX12ConstantStaging::kInt, [&]( int reg, int n ) { api->SetIntegerVertexShaderConstant( reg, s.vsInt[reg], n ); } );
		ReplayRuns< int >( &s.vsBoolDirty, DX12ConstantStaging::kBool, [&]( int reg, int n ) { api->SetBooleanVertexShaderConstant( reg, &s.vsBool[reg], n ); } );
	}
	else
	{
		ReplayRuns< float >( s.psFloatDirty, DX12ConstantStaging::kPSFloat, [&]( int reg, int n ) { api->SetPixelShaderConstant( reg, s.psFloat[reg], n ); } );
		ReplayRuns< int >( &s.psIntDirty, DX12ConstantStaging::kInt, [&]( int reg, int n ) { api->SetIntegerPixelShaderConstant( reg, s.psInt[reg], n ); } );
		ReplayRuns< int >( &s.psBoolDirty, DX12ConstantStaging::kBool, [&]( int reg, int n ) { api->SetBooleanPixelShaderConstant( reg, &s.psBool[reg], n ); } );
	}
}
}

void DX12SetVertexShaderConstant( int reg, const float *data, int count, bool ) { StageRows( g_DX12Constants.vsFloat, g_DX12Constants.vsFloatDirty, reg, data, count ); }
void DX12SetPixelShaderConstant( int reg, const float *data, int count, bool ) { StageRows( g_DX12Constants.psFloat, g_DX12Constants.psFloatDirty, reg, data, count ); }
void DX12SetIntegerVertexShaderConstant( int reg, const int *data, int count, bool ) { StageRows( g_DX12Constants.vsInt, &g_DX12Constants.vsIntDirty, reg, data, count ); }
void DX12SetIntegerPixelShaderConstant( int reg, const int *data, int count, bool ) { StageRows( g_DX12Constants.psInt, &g_DX12Constants.psIntDirty, reg, data, count ); }
void DX12SetBooleanVertexShaderConstant( int reg, const int *data, int count, bool ) { StageBools( g_DX12Constants.vsBool, &g_DX12Constants.vsBoolDirty, reg, data, count ); }
void DX12SetBooleanPixelShaderConstant( int reg, const int *data, int count, bool ) { StageBools( g_DX12Constants.psBool, &g_DX12Constants.psBoolDirty, reg, data, count ); }
// Same value as IShaderDynamicAPI::SetDepthFeatheringPixelShaderConstant (shaderapidx8.cpp:5225-5253, PC path):
// x = dest-alpha depth range / scale, the range being 8192 for float HDR and 192 otherwise.
void DX12SetDepthFeatheringPixelShaderConstant( int reg, float depthBlendScale )
{
	const float range = g_pHardwareConfig->GetHDRType() == HDR_TYPE_FLOAT ? 8192.0f : 192.0f;
	const float values[4] = { range / depthBlendScale, 0.0f, 0.0f, 0.0f };
	DX12SetPixelShaderConstant( reg, values, 1 );
}

void DX12NativeShaderName( const char *name, const char *stage, char *out, int outSize )
{
	V_strncpy( out, name ? name : "", outSize );
	static const char *const kSuffixes[] = { "20b", "2x", "xx", "11", "14", "20", "30" };
	const int length = V_strlen( out );
	for ( const char *suffix : kSuffixes )
	{
		const int suffixLength = 1 + 2 + V_strlen( suffix );	// "_" + stage + profile
		if ( length <= suffixLength ) continue;
		char *at = out + length - suffixLength;
		if ( at[0] != '_' || V_strnicmp( at + 1, stage, 2 ) || V_stricmp( at + 3, suffix ) ) continue;
		if ( length - suffixLength + 5 >= outSize ) return;	// no room for "_ps51"
		V_snprintf( at, outSize - int( at - out ), "_%s51", stage );
		return;
	}
}

void DX12SelectNativeBlockByName( const char *name, uint32_t stage )
{
	const DX12NativeBlockRegistryEntry *begin = kDX12NativeBlockRegistry;
	const DX12NativeBlockRegistryEntry *end = begin + ARRAYSIZE( kDX12NativeBlockRegistry );
	const DX12NativeBlockRegistryEntry *found = name ? std::lower_bound( begin, end, name,
		[]( const DX12NativeBlockRegistryEntry &e, const char *n ) { return V_stricmp( e.logical, n ) < 0; } ) : end;
	if ( found != end && !V_stricmp( found->logical, name ) && found->stage == stage )
	{
		DX12SelectNativeBlockWriter( stage, found->writer );
		return;
	}
	DX12SelectNativeBlockWriter( stage, nullptr );
	if ( name && V_stristr( name, "_shadowmap_" ) )
	{
		// A feature logical is native-only. Never fall through to legacy registers.
		DX12RejectUnsupportedLitShader( name );
		return;
	}
	( stage == dx12native::kStageVertex ? g_DX12Constants.vsPassthrough : g_DX12Constants.psPassthrough ) = true;
}

void DX12GatherLegacyRegisters( const dx12native::NativeCBufferLegacyMapDX12 &map, uint32_t stage,
	const DX12ConstantStaging &staging, void *block )
{
	const bool vertex = stage == dx12native::kStageVertex;
	unsigned char *bytes = static_cast< unsigned char * >( block );
	for ( uint32_t i = 0; i < map.entryCount; ++i )
	{
		const dx12native::NativeCBufferLegacyEntryDX12 &e = map.entries[i];
		for ( uint32_t element = 0; element < e.elementCount; ++element )
		{
			const uint32_t reg = e.reg + element;
			const void *source = nullptr;
			int boolValue = 0;
			switch ( e.bank )
			{
			case dx12native::kLegacyFloat:
				if ( reg < uint32_t( vertex ? DX12ConstantStaging::kVSFloat : DX12ConstantStaging::kPSFloat ) )
					source = ( vertex ? staging.vsFloat[reg] : staging.psFloat[reg] ) + e.component;
				break;
			case dx12native::kLegacyInt:
				if ( reg < uint32_t( DX12ConstantStaging::kInt ) )
					source = ( vertex ? staging.vsInt[reg] : staging.psInt[reg] ) + e.component;
				break;
			case dx12native::kLegacyBool:
				if ( reg < uint32_t( DX12ConstantStaging::kBool ) )
				{
					boolValue = vertex ? staging.vsBool[reg] : staging.psBool[reg];
					source = &boolValue;
				}
				break;
			}
			unsigned char *target = bytes + e.byteOffset + element * e.srcStride;
			if ( source ) memcpy( target, source, e.elementBytes );
			else memset( target, 0, e.elementBytes );
		}
	}
}

void DX12FlushNativeBlocks( IShaderDynamicAPI *api )
{
	DX12ConstantStaging &s = g_DX12Constants;
	if ( s.vsWriter ) s.vsWriter( api, s );
	if ( s.psWriter ) s.psWriter( api, s );
	// A passthrough stage replays only what was staged for THIS draw. Registers staged for earlier draws already
	// reached the backend (its mirror scatters every native block into the legacy register file); replaying them
	// again would land after engine writes made since (e.g. SetPixelShaderStateAmbientLightCube at c5 after a
	// previous material staged c5..c8), which DX9's immediate register writes never did.
	if ( s.vsPassthrough ) ReplayStage( api, true );
	else { memset( s.vsFloatDirty, 0, sizeof( s.vsFloatDirty ) ); s.vsIntDirty = s.vsBoolDirty = 0; }
	if ( s.psPassthrough ) ReplayStage( api, false );
	else { memset( s.psFloatDirty, 0, sizeof( s.psFloatDirty ) ); s.psIntDirty = s.psBoolDirty = 0; }
	s.vsWriter = s.psWriter = nullptr;
	s.vsPassthrough = s.psPassthrough = false;
}

void DX12ExecuteCommandBuffer( IShaderDynamicAPI *api, uint8 *buffer )
{
	if ( !buffer ) return;
	uint8 *returns[20]; int depth = 0;
	const auto readInt = []( const uint8 *p ) { int value; memcpy( &value, p, sizeof( value ) ); return value; };
	const auto readPointer = []( const uint8 *p ) { uint8 *value; memcpy( &value, p, sizeof( value ) ); return value; };
	for ( uint8 *pc = buffer; pc; )
	{
		const int opcode = readInt( pc ); pc += sizeof( int );
		switch ( opcode )
		{
		case CBCMD_END: if ( !depth ) return; pc = returns[--depth]; break;
		case CBCMD_JUMP: pc = readPointer( pc ); break;
		case CBCMD_JSR:
			if ( depth == ARRAYSIZE( returns ) ) { Warning( "DX12ExecuteCommandBuffer: call stack overflow\n" ); return; }
			returns[depth++] = pc + sizeof( uint8 * ); pc = readPointer( pc ); break;
		case CBCMD_SET_PIXEL_SHADER_FLOAT_CONST:
		case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST:
		{
			const int reg = readInt( pc ), count = readInt( pc + sizeof( int ) ); pc += 2 * sizeof( int );
			const float *values = reinterpret_cast< const float * >( pc );
			if ( opcode == CBCMD_SET_PIXEL_SHADER_FLOAT_CONST ) DX12SetPixelShaderConstant( reg, values, count );
			else DX12SetVertexShaderConstant( reg, values, count );
			pc += size_t( count ) * 4 * sizeof( float );
			break;
		}
		case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST_REF:
		{
			const int reg = readInt( pc ), count = readInt( pc + sizeof( int ) ); pc += 2 * sizeof( int );
			const float *values; memcpy( &values, pc, sizeof( values ) ); pc += sizeof( values );
			DX12SetVertexShaderConstant( reg, values, count );
			break;
		}
		case CBCMD_STORE_EYE_POS_IN_PSCONST:
		{
			// Same value the shader API stores (camera position, w = 1).
			float eye[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			api->GetWorldSpaceCameraPosition( eye );
			eye[3] = 1.0f;
			DX12SetPixelShaderConstant( readInt( pc ), eye, 1 ); pc += sizeof( int );
			break;
		}
		case CBCMD_SET_DEPTH_FEATHERING_CONST:
		{
			const int reg = readInt( pc ); float scale; memcpy( &scale, pc + sizeof( int ), sizeof( scale ) );
			pc += sizeof( int ) + sizeof( scale );
			DX12SetDepthFeatheringPixelShaderConstant( reg, scale );
			break;
		}
		case CBCMD_SETPIXELSHADERFOGPARAMS: api->SetPixelShaderFogParams( readInt( pc ) ); pc += sizeof( int ); break;
		case CBCMD_COMMITPIXELSHADERLIGHTING: api->CommitPixelShaderLighting( readInt( pc ) ); pc += sizeof( int ); break;
		case CBCMD_SETPIXELSHADERSTATEAMBIENTLIGHTCUBE: api->SetPixelShaderStateAmbientLightCube( readInt( pc ), false ); pc += sizeof( int ); break;
		case CBCMD_SETAMBIENTCUBEDYNAMICSTATEVERTEXSHADER: api->SetVertexShaderStateAmbientLightCube(); break;
		case CBCMD_BIND_STANDARD_TEXTURE:
		{
			const int sampler = readInt( pc ), id = readInt( pc + sizeof( int ) ); pc += 2 * sizeof( int );
			api->BindStandardTexture( Sampler_t( sampler ), StandardTextureId_t( id ) );
			break;
		}
		case CBCMD_BIND_SHADERAPI_TEXTURE_HANDLE:
		{
			// IShaderDynamicAPI has no raw-handle bind; the shader API's own decoder executes this one command.
			uint8 single[3 * sizeof( int ) + sizeof( ShaderAPITextureHandle_t )];
			const int end = CBCMD_END;
			memcpy( single, pc - sizeof( int ), 2 * sizeof( int ) + sizeof( ShaderAPITextureHandle_t ) );
			memcpy( single + 2 * sizeof( int ) + sizeof( ShaderAPITextureHandle_t ), &end, sizeof( end ) );
			api->ExecuteCommandBuffer( single );
			pc += sizeof( int ) + sizeof( ShaderAPITextureHandle_t );
			break;
		}
		case CBCMD_SET_PSHINDEX: api->SetPixelShaderIndex( readInt( pc ) ); pc += sizeof( int ); break;
		case CBCMD_SET_VSHINDEX: api->SetVertexShaderIndex( readInt( pc ) ); pc += sizeof( int ); break;
		default:
			Warning( "DX12ExecuteCommandBuffer: unsupported command %d\n", opcode );
			return;
		}
	}
}

void CBaseVSShaderDX12::Draw( bool bMakeActualDrawCall )
{
	if ( IsSnapshotting() )
	{
		// Index objects built while snapshotting must not leak into the next dynamic pass.
		DX12SelectNativeBlockWriter( dx12native::kStageVertex, nullptr );
		DX12SelectNativeBlockWriter( dx12native::kStagePixel, nullptr );
	}
	else if ( bMakeActualDrawCall && s_pShaderAPI )
	{
		IShaderAPIDX12Lighting *lighting = DX12ShadowmapLighting();
		const int lightingFlags = MATERIAL_VAR2_LIGHTING_VERTEX_LIT | MATERIAL_VAR2_LIGHTING_LIGHTMAP | MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP;
		if ( !DX12HighresMap() && lighting && lighting->ReceiverFeatureGeneration() != 0 && !DX12ShadowmapFullbright() &&
			( s_ppParams[FLAGS2]->GetIntValue() & lightingFlags ) != 0 &&
			!g_DX12Constants.shadowmapPassAdmitted && !UsingFlashlight( s_ppParams ) )
		{
			Warning( "DX12 shadowmaps: lit shader %s drew without selecting its receiver variant (flags2=0x%x)\n", GetName(), s_ppParams[FLAGS2]->GetIntValue() );
			lighting->RejectUnsupportedLitShader( GetName() );
			bMakeActualDrawCall = false;
		}
		DX12FlushNativeBlocks( s_pShaderAPI );
	}
	else
	{
		DX12SelectNativeBlockWriter( dx12native::kStageVertex, nullptr );
		DX12SelectNativeBlockWriter( dx12native::kStagePixel, nullptr );
	}
	CBaseShader::Draw( bMakeActualDrawCall );
}

void CBaseVSShaderDX12::DrawElements( IMaterialVar **params, int nModulationFlags, IShaderShadow *pShaderShadow, IShaderDynamicAPI *pShaderAPI,
	VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContext )
{
	// One admission per DrawElements: the primary pass selects the receiver variant; secondary passes of the
	// same material (DrawEqualDepthToDestAlpha, ...) must not be mistaken for an unadmitted lit draw, and an
	// earlier material cannot admit a later one.
	g_DX12Constants.shadowmapPassAdmitted = false;
	CBaseShader::DrawElements( params, nModulationFlags, pShaderShadow, pShaderAPI, vertexCompression, pContext );
	g_DX12Constants.shadowmapPassAdmitted = false;
}

static ConVar mat_fullbright( "mat_fullbright","0", FCVAR_CHEAT );

// Defined by eyes_dx8_dx9_helper.cpp in the DX9 tree; every flashlight-aware material reads it.
ConVar r_flashlight_version2( "r_flashlight_version2", "0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );

// These functions are to be called from the shaders.

//-----------------------------------------------------------------------------
// Pixel and vertex shader constants....
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetPixelShaderConstant( int pixelReg, int constantVar, int constantVar2 )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1) || (constantVar2 == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );
	IMaterialVar* pPixelVar2 = s_ppParams[constantVar2];
	Assert( pPixelVar2 );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
	{
		pPixelVar->GetVecValue( val, 3 );
	}
	else
	{
		val[0] = val[1] = val[2] = pPixelVar->GetFloatValue();
	}

	val[3] = pPixelVar2->GetFloatValue();
	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetPixelShaderConstantGammaToLinear( int pixelReg, int constantVar, int constantVar2 )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1) || (constantVar2 == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );
	IMaterialVar* pPixelVar2 = s_ppParams[constantVar2];
	Assert( pPixelVar2 );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
	{
		pPixelVar->GetVecValue( val, 3 );
	}
	else
	{
		val[0] = val[1] = val[2] = pPixelVar->GetFloatValue();
	}

	val[3] = pPixelVar2->GetFloatValue();
	val[0] = val[0] > 1.0f ? val[0] : GammaToLinear( val[0] );
	val[1] = val[1] > 1.0f ? val[1] : GammaToLinear( val[1] );
	val[2] = val[2] > 1.0f ? val[2] : GammaToLinear( val[2] );

	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetPixelShaderConstant_W( int pixelReg, int constantVar, float fWValue )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
		pPixelVar->GetVecValue( val, 4 );
	else
		val[0] = val[1] = val[2] = val[3] = pPixelVar->GetFloatValue();
	val[3]=fWValue;
	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetPixelShaderConstant( int pixelReg, int constantVar )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
		pPixelVar->GetVecValue( val, 4 );
	else
		val[0] = val[1] = val[2] = val[3] = pPixelVar->GetFloatValue();
	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetPixelShaderConstantGammaToLinear( int pixelReg, int constantVar )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
		pPixelVar->GetVecValue( val, 4 );
	else
		val[0] = val[1] = val[2] = val[3] = pPixelVar->GetFloatValue();

	val[0] = val[0] > 1.0f ? val[0] : GammaToLinear( val[0] );
	val[1] = val[1] > 1.0f ? val[1] : GammaToLinear( val[1] );
	val[2] = val[2] > 1.0f ? val[2] : GammaToLinear( val[2] );

	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetVertexShaderConstantGammaToLinear( int var, float const* pVec, int numConst, bool bForce )
{
	int i;
	for( i = 0; i < numConst; i++ )
	{
		float vec[4];
		vec[0] = pVec[i*4+0] > 1.0f ? pVec[i*4+0] : GammaToLinear( pVec[i*4+0] );
		vec[1] = pVec[i*4+1] > 1.0f ? pVec[i*4+1] : GammaToLinear( pVec[i*4+1] );
		vec[2] = pVec[i*4+2] > 1.0f ? pVec[i*4+2] : GammaToLinear( pVec[i*4+2] );
		vec[3] = pVec[i*4+3];

		DX12SetVertexShaderConstant( var + i, vec, 1, bForce );
	}
}

void CBaseVSShaderDX12::SetPixelShaderConstantGammaToLinear( int var, float const* pVec, int numConst, bool bForce )
{
	int i;
	for( i = 0; i < numConst; i++ )
	{
		float vec[4];
		vec[0] = pVec[i*4+0] > 1.0f ? pVec[i*4+0] : GammaToLinear( pVec[i*4+0] );
		vec[1] = pVec[i*4+1] > 1.0f ? pVec[i*4+1] : GammaToLinear( pVec[i*4+1] );
		vec[2] = pVec[i*4+2] > 1.0f ? pVec[i*4+2] : GammaToLinear( pVec[i*4+2] );

		vec[3] = pVec[i*4+3];

		DX12SetPixelShaderConstant( var + i, vec, 1, bForce );
	}
}

// GR - special version with fix for const/lerp issue
void CBaseVSShaderDX12::SetPixelShaderConstantFudge( int pixelReg, int constantVar )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1))
		return;

	IMaterialVar* pPixelVar = s_ppParams[constantVar];
	Assert( pPixelVar );

	float val[4];
	if (pPixelVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
	{
		pPixelVar->GetVecValue( val, 4 );
		val[0] = val[0] * 0.992f + 0.0078f;
		val[1] = val[1] * 0.992f + 0.0078f;
		val[2] = val[2] * 0.992f + 0.0078f;
		val[3] = val[3] * 0.992f + 0.0078f;
	}
	else
		val[0] = val[1] = val[2] = val[3] = pPixelVar->GetFloatValue() * 0.992f + 0.0078f;
	DX12SetPixelShaderConstant( pixelReg, val );	
}

void CBaseVSShaderDX12::SetVertexShaderConstant( int vertexReg, int constantVar )
{
	Assert( !IsSnapshotting() );
	if ((!s_ppParams) || (constantVar == -1))
		return;

	IMaterialVar* pVertexVar = s_ppParams[constantVar];
	Assert( pVertexVar );

	float val[4];
	if (pVertexVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
		pVertexVar->GetVecValue( val, 4 );
	else
		val[0] = val[1] = val[2] = val[3] = pVertexVar->GetFloatValue();
	DX12SetVertexShaderConstant( vertexReg, val );	
}

//-----------------------------------------------------------------------------
// Sets normalized light color for pixel shaders.
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetPixelShaderLightColors( int pixelReg )
{
	int i;
	int maxLights = s_pShaderAPI->GetMaxLights();
	for( i = 0; i < maxLights; i++ )
	{
		const LightDesc_t & lightDesc = s_pShaderAPI->GetLight( i );
		if( lightDesc.m_Type != MATERIAL_LIGHT_DISABLE )
		{
			Vector color( lightDesc.m_Color[0], lightDesc.m_Color[1], lightDesc.m_Color[2] );
			VectorNormalize( color );
			float val[4] = { color[0], color[1], color[2], 1.0f };
			DX12SetPixelShaderConstant( pixelReg + i, val, 1 );
		}
		else
		{
			float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			DX12SetPixelShaderConstant( pixelReg + i, zero, 1 );
		}
	}
}


//-----------------------------------------------------------------------------
// Sets vertex shader texture transforms
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetVertexShaderTextureTranslation( int vertexReg, int translationVar )
{
	float offset[2] = {0, 0};

	IMaterialVar* pTranslationVar = s_ppParams[translationVar];
	if (pTranslationVar)
	{
		if (pTranslationVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pTranslationVar->GetVecValue( offset, 2 );
		else
			offset[0] = offset[1] = pTranslationVar->GetFloatValue();
	}

	Vector4D translation[2];
	translation[0].Init( 1.0f, 0.0f, 0.0f, offset[0] );
	translation[1].Init( 0.0f, 1.0f, 0.0f, offset[1] );
	DX12SetVertexShaderConstant( vertexReg, translation[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetVertexShaderTextureScale( int vertexReg, int scaleVar )
{
	float scale[2] = {1, 1};

	IMaterialVar* pScaleVar = s_ppParams[scaleVar];
	if (pScaleVar)
	{
		if (pScaleVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pScaleVar->GetVecValue( scale, 2 );
		else if (pScaleVar->IsDefined())
			scale[0] = scale[1] = pScaleVar->GetFloatValue();
	}

	Vector4D scaleMatrix[2];
	scaleMatrix[0].Init( scale[0], 0.0f, 0.0f, 0.0f );
	scaleMatrix[1].Init( 0.0f, scale[1], 0.0f, 0.0f );
	DX12SetVertexShaderConstant( vertexReg, scaleMatrix[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetVertexShaderTextureTransform( int vertexReg, int transformVar )
{
	Vector4D transformation[2];
	IMaterialVar* pTransformationVar = s_ppParams[transformVar];
	if (pTransformationVar && (pTransformationVar->GetType() == MATERIAL_VAR_TYPE_MATRIX))
	{
		const VMatrix &mat = pTransformationVar->GetMatrixValue();
		transformation[0].Init( mat[0][0], mat[0][1], mat[0][2], mat[0][3] );
		transformation[1].Init( mat[1][0], mat[1][1], mat[1][2], mat[1][3] );
	}
	else
	{
		transformation[0].Init( 1.0f, 0.0f, 0.0f, 0.0f );
		transformation[1].Init( 0.0f, 1.0f, 0.0f, 0.0f );
	}
	DX12SetVertexShaderConstant( vertexReg, transformation[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetVertexShaderTextureScaledTransform( int vertexReg, int transformVar, int scaleVar )
{
	Vector4D transformation[2];
	IMaterialVar* pTransformationVar = s_ppParams[transformVar];
	if (pTransformationVar && (pTransformationVar->GetType() == MATERIAL_VAR_TYPE_MATRIX))
	{
		const VMatrix &mat = pTransformationVar->GetMatrixValue();
		transformation[0].Init( mat[0][0], mat[0][1], mat[0][2], mat[0][3] );
		transformation[1].Init( mat[1][0], mat[1][1], mat[1][2], mat[1][3] );
	}
	else
	{
		transformation[0].Init( 1.0f, 0.0f, 0.0f, 0.0f );
		transformation[1].Init( 0.0f, 1.0f, 0.0f, 0.0f );
	}

	Vector2D scale( 1, 1 );
	IMaterialVar* pScaleVar = s_ppParams[scaleVar];
	if (pScaleVar)
	{
		if (pScaleVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pScaleVar->GetVecValue( scale.Base(), 2 );
		else if (pScaleVar->IsDefined())
			scale[0] = scale[1] = pScaleVar->GetFloatValue();
	}

	// Apply the scaling
	transformation[0][0] *= scale[0];
	transformation[0][1] *= scale[1];
	transformation[1][0] *= scale[0];
	transformation[1][1] *= scale[1];
	transformation[0][3] *= scale[0];
	transformation[1][3] *= scale[1];
	DX12SetVertexShaderConstant( vertexReg, transformation[0].Base(), 2 ); 
}


//-----------------------------------------------------------------------------
// Sets pixel shader texture transforms
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetPixelShaderTextureTranslation( int pixelReg, int translationVar )
{
	float offset[2] = {0, 0};

	IMaterialVar* pTranslationVar = s_ppParams[translationVar];
	if (pTranslationVar)
	{
		if (pTranslationVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pTranslationVar->GetVecValue( offset, 2 );
		else
			offset[0] = offset[1] = pTranslationVar->GetFloatValue();
	}

	Vector4D translation[2];
	translation[0].Init( 1.0f, 0.0f, 0.0f, offset[0] );
	translation[1].Init( 0.0f, 1.0f, 0.0f, offset[1] );
	DX12SetPixelShaderConstant( pixelReg, translation[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetPixelShaderTextureScale( int pixelReg, int scaleVar )
{
	float scale[2] = {1, 1};

	IMaterialVar* pScaleVar = s_ppParams[scaleVar];
	if (pScaleVar)
	{
		if (pScaleVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pScaleVar->GetVecValue( scale, 2 );
		else if (pScaleVar->IsDefined())
			scale[0] = scale[1] = pScaleVar->GetFloatValue();
	}

	Vector4D scaleMatrix[2];
	scaleMatrix[0].Init( scale[0], 0.0f, 0.0f, 0.0f );
	scaleMatrix[1].Init( 0.0f, scale[1], 0.0f, 0.0f );
	DX12SetPixelShaderConstant( pixelReg, scaleMatrix[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetPixelShaderTextureTransform( int pixelReg, int transformVar )
{
	Vector4D transformation[2];
	IMaterialVar* pTransformationVar = s_ppParams[transformVar];
	if (pTransformationVar && (pTransformationVar->GetType() == MATERIAL_VAR_TYPE_MATRIX))
	{
		const VMatrix &mat = pTransformationVar->GetMatrixValue();
		transformation[0].Init( mat[0][0], mat[0][1], mat[0][2], mat[0][3] );
		transformation[1].Init( mat[1][0], mat[1][1], mat[1][2], mat[1][3] );
	}
	else
	{
		transformation[0].Init( 1.0f, 0.0f, 0.0f, 0.0f );
		transformation[1].Init( 0.0f, 1.0f, 0.0f, 0.0f );
	}
	DX12SetPixelShaderConstant( pixelReg, transformation[0].Base(), 2 ); 
}

void CBaseVSShaderDX12::SetPixelShaderTextureScaledTransform( int pixelReg, int transformVar, int scaleVar )
{
	Vector4D transformation[2];
	IMaterialVar* pTransformationVar = s_ppParams[transformVar];
	if (pTransformationVar && (pTransformationVar->GetType() == MATERIAL_VAR_TYPE_MATRIX))
	{
		const VMatrix &mat = pTransformationVar->GetMatrixValue();
		transformation[0].Init( mat[0][0], mat[0][1], mat[0][2], mat[0][3] );
		transformation[1].Init( mat[1][0], mat[1][1], mat[1][2], mat[1][3] );
	}
	else
	{
		transformation[0].Init( 1.0f, 0.0f, 0.0f, 0.0f );
		transformation[1].Init( 0.0f, 1.0f, 0.0f, 0.0f );
	}

	Vector2D scale( 1, 1 );
	IMaterialVar* pScaleVar = s_ppParams[scaleVar];
	if (pScaleVar)
	{
		if (pScaleVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pScaleVar->GetVecValue( scale.Base(), 2 );
		else if (pScaleVar->IsDefined())
			scale[0] = scale[1] = pScaleVar->GetFloatValue();
	}

	// Apply the scaling
	transformation[0][0] *= scale[0];
	transformation[0][1] *= scale[1];
	transformation[1][0] *= scale[0];
	transformation[1][1] *= scale[1];
	transformation[0][3] *= scale[0];
	transformation[1][3] *= scale[1];
	DX12SetPixelShaderConstant( pixelReg, transformation[0].Base(), 2 ); 
}


//-----------------------------------------------------------------------------
// Moves a matrix into vertex shader constants 
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetVertexShaderMatrix2x4( int vertexReg, int matrixVar )
{
	IMaterialVar* pTranslationVar = s_ppParams[ matrixVar ];
	if ( pTranslationVar )
	{
		DX12SetVertexShaderConstant( vertexReg, &pTranslationVar->GetMatrixValue()[ 0 ][ 0 ], 2 );
	}
	else
	{
		VMatrix matrix;
		MatrixSetIdentity( matrix );
		DX12SetVertexShaderConstant( vertexReg, &matrix[ 0 ][ 0 ], 2 );
	}
}

void CBaseVSShaderDX12::SetVertexShaderMatrix3x4( int vertexReg, int matrixVar )
{
	IMaterialVar* pTranslationVar = s_ppParams[matrixVar];
	if (pTranslationVar)
	{
		DX12SetVertexShaderConstant( vertexReg, &pTranslationVar->GetMatrixValue( )[0][0], 3 ); 
	}
	else
	{
		VMatrix matrix;
		MatrixSetIdentity( matrix );
		DX12SetVertexShaderConstant( vertexReg, &matrix[0][0], 3 ); 
	}
}

void CBaseVSShaderDX12::SetVertexShaderMatrix4x4( int vertexReg, int matrixVar )
{
	IMaterialVar* pTranslationVar = s_ppParams[matrixVar];
	if (pTranslationVar)
	{
		DX12SetVertexShaderConstant( vertexReg, &pTranslationVar->GetMatrixValue( )[0][0], 4 ); 
	}
	else
	{
		VMatrix matrix;
		MatrixSetIdentity( matrix );
		DX12SetVertexShaderConstant( vertexReg, &matrix[0][0], 4 ); 
	}
}


//-----------------------------------------------------------------------------
// Loads the view matrix into pixel shader constants
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadViewMatrixIntoVertexShaderConstant( int vertexReg )
{
	VMatrix mat, transpose;
	s_pShaderAPI->GetMatrix( MATERIAL_VIEW, mat.m[0] );

	MatrixTranspose( mat, transpose );
	DX12SetVertexShaderConstant( vertexReg, transpose.m[0], 3 );
}


//-----------------------------------------------------------------------------
// Loads the projection matrix into pixel shader constants
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadProjectionMatrixIntoVertexShaderConstant( int vertexReg )
{
	VMatrix mat, transpose;
	s_pShaderAPI->GetMatrix( MATERIAL_PROJECTION, mat.m[0] );

	MatrixTranspose( mat, transpose );
	DX12SetVertexShaderConstant( vertexReg, transpose.m[0], 4 );
}


//-----------------------------------------------------------------------------
// Loads the projection matrix into pixel shader constants
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadModelViewMatrixIntoVertexShaderConstant( int vertexReg )
{
	VMatrix view, model, modelView;
	s_pShaderAPI->GetMatrix( MATERIAL_MODEL, model.m[0] );
	MatrixTranspose( model, model );
	s_pShaderAPI->GetMatrix( MATERIAL_VIEW, view.m[0] );
	MatrixTranspose( view, view );

	MatrixMultiply( view, model, modelView );
	DX12SetVertexShaderConstant( vertexReg, modelView.m[0], 3 );
}

//-----------------------------------------------------------------------------
// Loads a scale/offset version of the viewport transform into the specified constant.
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadViewportTransformScaledIntoVertexShaderConstant( int vertexReg )
{
	ShaderViewport_t viewport;

	s_pShaderAPI->GetViewports( &viewport, 1 );

	int bbWidth = 0, 
		bbHeight = 0;

	s_pShaderAPI->GetBackBufferDimensions( bbWidth, bbHeight );

	// (x, y, z, w) = (Width / bbWidth, Height / bbHeight, MinX / bbWidth, MinY / bbHeight)
	Vector4D viewportTransform( 
		1.0f * viewport.m_nWidth / bbWidth,
		1.0f * viewport.m_nHeight / bbHeight,
		1.0f * viewport.m_nTopLeftX / bbWidth, 
		1.0f * viewport.m_nTopLeftY / bbHeight
	);

	DX12SetVertexShaderConstant( vertexReg, viewportTransform.Base() );
}



//-----------------------------------------------------------------------------
// Loads bump lightmap coordinates into the pixel shader
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadBumpLightmapCoordinateAxes_PixelShader( int pixelReg )
{
	Vector4D basis[3];
	for (int i = 0; i < 3; ++i)
	{
		memcpy( &basis[i], &g_localBumpBasis[i], 3 * sizeof(float) );
		basis[i][3] = 0.0f;
	}
	DX12SetPixelShaderConstant( pixelReg, (float*)basis, 3 );
}


//-----------------------------------------------------------------------------
// Loads bump lightmap coordinates into the pixel shader
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::LoadBumpLightmapCoordinateAxes_VertexShader( int vertexReg )
{
	Vector4D basis[3];

	// transpose
	int i;
	for (i = 0; i < 3; ++i)
	{
		basis[i][0] = g_localBumpBasis[0][i];
		basis[i][1] = g_localBumpBasis[1][i];
		basis[i][2] = g_localBumpBasis[2][i];
		basis[i][3] = 0.0f;
	}
	DX12SetVertexShaderConstant( vertexReg, (float*)basis, 3 );
	for (i = 0; i < 3; ++i)
	{
		memcpy( &basis[i], &g_localBumpBasis[i], 3 * sizeof(float) );
		basis[i][3] = 0.0f;
	}
	DX12SetVertexShaderConstant( vertexReg + 3, (float*)basis, 3 );
}


//-----------------------------------------------------------------------------
// Helper methods for pixel shader overbrighting
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::EnablePixelShaderOverbright( int reg, bool bEnable, bool bDivideByTwo )
{
	// can't have other overbright values with pixel shaders as it stands.
	float v[4];
	if( bEnable )
	{
		v[0] = v[1] = v[2] = v[3] = bDivideByTwo ? OVERBRIGHT / 2.0f : OVERBRIGHT;
	}
	else
	{
		v[0] = v[1] = v[2] = v[3] = bDivideByTwo ? 1.0f / 2.0f : 1.0f;
	}
	DX12SetPixelShaderConstant( reg, v, 1 );
}


//-----------------------------------------------------------------------------
// Helper for dealing with modulation
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetModulationVertexShaderDynamicState()
{
 	float color[4] = { 1.0, 1.0, 1.0, 1.0 };
	ComputeModulationColor( color );
	DX12SetVertexShaderConstant( VERTEX_SHADER_MODULATION_COLOR, color );
}

void CBaseVSShaderDX12::SetModulationPixelShaderDynamicState( int modulationVar )
{
	float color[4] = { 1.0, 1.0, 1.0, 1.0 };
	ComputeModulationColor( color );
	DX12SetPixelShaderConstant( modulationVar, color );
}

void CBaseVSShaderDX12::SetModulationPixelShaderDynamicState_LinearColorSpace( int modulationVar )
{
	float color[4] = { 1.0, 1.0, 1.0, 1.0 };
	ComputeModulationColor( color );
	color[0] = color[0] > 1.0f ? color[0] : GammaToLinear( color[0] );
	color[1] = color[1] > 1.0f ? color[1] : GammaToLinear( color[1] );
	color[2] = color[2] > 1.0f ? color[2] : GammaToLinear( color[2] );

	DX12SetPixelShaderConstant( modulationVar, color );
}

void CBaseVSShaderDX12::SetModulationPixelShaderDynamicState_LinearColorSpace_LinearScale( int modulationVar, float flScale )
{
	float color[4] = { 1.0, 1.0, 1.0, 1.0 };
	ComputeModulationColor( color );
	color[0] = ( color[0] > 1.0f ? color[0] : GammaToLinear( color[0] ) ) * flScale;
	color[1] = ( color[1] > 1.0f ? color[1] : GammaToLinear( color[1] ) ) * flScale;
	color[2] = ( color[2] > 1.0f ? color[2] : GammaToLinear( color[2] ) ) * flScale;

	DX12SetPixelShaderConstant( modulationVar, color );
}


//-----------------------------------------------------------------------------
// Converts a color + alpha into a vector4
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::ColorVarsToVector( int colorVar, int alphaVar, Vector4D &color )
{
	color.Init( 1.0, 1.0, 1.0, 1.0 ); 
	if ( colorVar != -1 )
	{
		IMaterialVar* pColorVar = s_ppParams[colorVar];
		if ( pColorVar->GetType() == MATERIAL_VAR_TYPE_VECTOR )
		{
			pColorVar->GetVecValue( color.Base(), 3 );
		}
		else
		{
			color[0] = color[1] = color[2] = pColorVar->GetFloatValue();
		}
	}
	if ( alphaVar != -1 )
	{
		float flAlpha = s_ppParams[alphaVar]->GetFloatValue();
		color[3] = clamp( flAlpha, 0.0f, 1.0f );
	}
}


//-----------------------------------------------------------------------------
// Sets a color + alpha into shader constants
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetColorVertexShaderConstant( int nVertexReg, int colorVar, int alphaVar )
{
	Vector4D color;
	ColorVarsToVector( colorVar, alphaVar, color );
	DX12SetVertexShaderConstant( nVertexReg, color.Base() );
}

void CBaseVSShaderDX12::SetColorPixelShaderConstant( int nPixelReg, int colorVar, int alphaVar )
{
	Vector4D color;
	ColorVarsToVector( colorVar, alphaVar, color );
	DX12SetPixelShaderConstant( nPixelReg, color.Base() );
}

#ifdef _DEBUG
ConVar mat_envmaptintoverride( "mat_envmaptintoverride", "-1" );
ConVar mat_envmaptintscale( "mat_envmaptintscale", "-1" );
#endif

//-----------------------------------------------------------------------------
// Helpers for dealing with envmap tint
//-----------------------------------------------------------------------------
// set alphaVar to -1 to ignore it.
void CBaseVSShaderDX12::SetEnvMapTintPixelShaderDynamicState( int pixelReg, int tintVar, int alphaVar, bool bConvertFromGammaToLinear )
{
	float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if( g_pConfig->bShowSpecular && mat_fullbright.GetInt() != 2 )
	{
		IMaterialVar* pAlphaVar = NULL;
		if( alphaVar >= 0 )
		{
			pAlphaVar = s_ppParams[alphaVar];
		}
		if( pAlphaVar )
		{
			color[3] = pAlphaVar->GetFloatValue();
		}

		IMaterialVar* pTintVar = s_ppParams[tintVar];
#ifdef _DEBUG
		pTintVar->GetVecValue( color, 3 );

		float envmapTintOverride = mat_envmaptintoverride.GetFloat();
		float envmapTintScaleOverride = mat_envmaptintscale.GetFloat();

		if( envmapTintOverride != -1.0f )
		{
			color[0] = color[1] = color[2] = envmapTintOverride;
		}
		if( envmapTintScaleOverride != -1.0f )
		{
			color[0] *= envmapTintScaleOverride;
			color[1] *= envmapTintScaleOverride;
			color[2] *= envmapTintScaleOverride;
		}

		if( bConvertFromGammaToLinear )
		{
			color[0] = color[0] > 1.0f ? color[0] : GammaToLinear( color[0] );
			color[1] = color[1] > 1.0f ? color[1] : GammaToLinear( color[1] );
			color[2] = color[2] > 1.0f ? color[2] : GammaToLinear( color[2] );
		}
#else
		if( bConvertFromGammaToLinear )
		{
			pTintVar->GetLinearVecValue( color, 3 );
		}
		else
		{
			pTintVar->GetVecValue( color, 3 );
		}
#endif
	}
	else
	{
		color[0] = color[1] = color[2] = color[3] = 0.0f;
	}
	DX12SetPixelShaderConstant( pixelReg, color, 1 );
}

void CBaseVSShaderDX12::SetAmbientCubeDynamicStateVertexShader( )
{
	s_pShaderAPI->SetVertexShaderStateAmbientLightCube();
}

float CBaseVSShaderDX12::GetAmbientLightCubeLuminance( )
{
	return s_pShaderAPI->GetAmbientLightCubeLuminance();
}


//-----------------------------------------------------------------------------
// Sets up hw morphing state for the vertex shader
//-----------------------------------------------------------------------------
void CBaseVSShaderDX12::SetHWMorphVertexShaderState( int nDimConst, int nSubrectConst, VertexTextureSampler_t morphSampler )
{
#ifndef _X360
	if ( !s_pShaderAPI->IsHWMorphingEnabled() )
		return;

	int nMorphWidth, nMorphHeight;
	s_pShaderAPI->GetStandardTextureDimensions( &nMorphWidth, &nMorphHeight, TEXTURE_MORPH_ACCUMULATOR );

	int nDim = s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_MORPH_ACCUMULATOR_4TUPLE_COUNT );
	float pMorphAccumSize[4] = { (float)nMorphWidth, (float)nMorphHeight, (float)nDim, 0.0f };
	DX12SetVertexShaderConstant( nDimConst, pMorphAccumSize );

	int nXOffset = s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_MORPH_ACCUMULATOR_X_OFFSET );
	int nYOffset = s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_MORPH_ACCUMULATOR_Y_OFFSET );
	int nWidth = s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_MORPH_ACCUMULATOR_SUBRECT_WIDTH );
	int nHeight = s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_MORPH_ACCUMULATOR_SUBRECT_HEIGHT );
	float pMorphAccumSubrect[4] = { (float)nXOffset, (float)nYOffset, (float)nWidth, (float)nHeight };
	DX12SetVertexShaderConstant( nSubrectConst, pMorphAccumSubrect );

	s_pShaderAPI->BindStandardVertexTexture( morphSampler, TEXTURE_MORPH_ACCUMULATOR );
#endif
}

//-----------------------------------------------------------------------------
// GR - translucency query
//-----------------------------------------------------------------------------
BlendType_t CBaseVSShaderDX12::EvaluateBlendRequirements( int textureVar, bool isBaseTexture,
													  int detailTextureVar )
{
	// Either we've got a constant modulation
	bool isTranslucent = IsAlphaModulating();

	// Or we've got a vertex alpha
	isTranslucent = isTranslucent || (CurrentMaterialVarFlags() & MATERIAL_VAR_VERTEXALPHA);

	// Or we've got a texture alpha (for blending or alpha test)
	isTranslucent = isTranslucent || ( TextureIsTranslucent( textureVar, isBaseTexture ) &&
		                               !(CurrentMaterialVarFlags() & MATERIAL_VAR_ALPHATEST ) );

	if ( ( detailTextureVar != -1 ) && ( ! isTranslucent ) )
	{
		isTranslucent = TextureIsTranslucent( detailTextureVar, isBaseTexture );
	}

	if ( CurrentMaterialVarFlags() & MATERIAL_VAR_ADDITIVE )
	{	
		return isTranslucent ? BT_BLENDADD : BT_ADD;	// Additive
	}
	else
	{
		return isTranslucent ? BT_BLEND : BT_NONE;		// Normal blending
	}
}


void CBaseVSShaderDX12::SetFlashlightVertexShaderConstants( bool bBump, int bumpTransformVar, bool bDetail, int detailScaleVar, bool bSetTextureTransforms )
{
	Assert( !IsSnapshotting() );

	VMatrix worldToTexture;
	const FlashlightState_t &flashlightState = s_pShaderAPI->GetFlashlightState( worldToTexture );

	// Set the flashlight origin
	float pos[4];
	pos[0] = flashlightState.m_vecLightOrigin[0];
	pos[1] = flashlightState.m_vecLightOrigin[1];
	pos[2] = flashlightState.m_vecLightOrigin[2];
	pos[3] = 1.0f / ( ( 0.6f * flashlightState.m_FarZ ) - flashlightState.m_FarZ );		// DX8 needs this

	DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, pos, 1 );

	DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, worldToTexture.Base(), 4 );

	// Set the flashlight attenuation factors
	float atten[4];
	atten[0] = flashlightState.m_fConstantAtten;
	atten[1] = flashlightState.m_fLinearAtten;
	atten[2] = flashlightState.m_fQuadraticAtten;
	atten[3] = flashlightState.m_FarZ;
	DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_5, atten, 1 );

	if ( bDetail )
	{
		SetVertexShaderTextureScaledTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, BASETEXTURETRANSFORM, detailScaleVar );
	}

	if( bSetTextureTransforms )
	{
		SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_6, BASETEXTURETRANSFORM );
		if( !bDetail && bBump && bumpTransformVar != -1 )
		{
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, bumpTransformVar ); // aliased on top of detail transform
		}
	}
}

void CBaseVSShaderDX12::DrawFlashlight_dx90( IMaterialVar** params, IShaderDynamicAPI *pShaderAPI, 
										IShaderShadow* pShaderShadow, DrawFlashlight_dx90_Vars_t &vars )
{
	// The DX9 non-lightmapped variant drew with the DX8 vertexlitgeneric_flashlight_vs11 assembly shader; no
	// stdshader material uses it (DecalBaseTimesLightmapAlphaBlendSelfIllum and LightmappedGeneric pass true).
	if( !vars.m_bLightmappedGeneric )
	{
		Error( "DrawFlashlight_dx90: only the lightmapped flashlight pass exists in stdshader_dx12\n" );
	}
	bool bBump2 = vars.m_bWorldVertexTransition && vars.m_bBump && vars.m_nBumpmap2Var != -1 && params[vars.m_nBumpmap2Var]->IsTexture();
	bool bSeamless = vars.m_fSeamlessScale != 0.0;
	bool bDetail = vars.m_bLightmappedGeneric && (vars.m_nDetailVar != -1) && params[vars.m_nDetailVar]->IsDefined() && (vars.m_nDetailScale != -1);

	int nDetailBlendMode = 0;
	if ( bDetail )
	{
		nDetailBlendMode = GetIntParam( vars.m_nDetailTextureCombineMode, params );
		nDetailBlendMode = nDetailBlendMode > 1 ? 1 : nDetailBlendMode;
	}

	if( pShaderShadow )
	{
		SetInitialShadowState();
		pShaderShadow->EnableDepthWrites( false );
		pShaderShadow->EnableAlphaWrites( false );

		// Alpha blend
		SetAdditiveBlendingShadowState( BASETEXTURE, true );

		// Alpha test
		pShaderShadow->EnableAlphaTest( IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) );
		if ( vars.m_nAlphaTestReference != -1 && params[vars.m_nAlphaTestReference]->GetFloatValue() > 0.0f )
		{
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, params[vars.m_nAlphaTestReference]->GetFloatValue() );
		}

		// Spot sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );

		// Base sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );

		// Normalizing cubemap sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );

		// Normalizing cubemap sampler2 or normal map sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

		// RandomRotation sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );

		// Flashlight depth sampler
		pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
		pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER7 );

		if( vars.m_bWorldVertexTransition )
		{
			// $basetexture2
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, true );
		}
		if( bBump2 )
		{
			// Normalmap2 sampler
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
		}
		if( bDetail )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );				// detail sampler
			if ( nDetailBlendMode != 0 ) //Not Mod2X
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, true );
		}
		
		pShaderShadow->EnableSRGBWrite( true );

		{
			lightmappedgeneric_flashlight_vs51_Static_Index	vshIndex;
			vshIndex.SetWORLDVERTEXTRANSITION( vars.m_bWorldVertexTransition );
			vshIndex.SetNORMALMAP( vars.m_bBump );
			vshIndex.SetSEAMLESS( bSeamless );
			vshIndex.SetDETAIL( bDetail );
			pShaderShadow->SetVertexShader( "lightmappedgeneric_flashlight_vs51", vshIndex.GetIndex() );

			unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL;
			if( vars.m_bBump )
			{
				flags |= VERTEX_TANGENT_S | VERTEX_TANGENT_T;
			}
			int numTexCoords = 1;
			if( vars.m_bWorldVertexTransition )
			{
				flags |= VERTEX_COLOR;
				numTexCoords = 2; // need lightmap texcoords to get alpha.
			}
			pShaderShadow->VertexShaderVertexFormat( flags, numTexCoords, 0, 0 );
		}

		int nBumpMapVariant = 0;
		if ( vars.m_bBump )
		{
			nBumpMapVariant = ( vars.m_bSSBump ) ? 2 : 1;
		}
		{
			int nShadowFilterMode = g_pHardwareConfig->GetShadowFilterMode();

			flashlight_ps51_Static_Index	pshIndex;
			pshIndex.SetNORMALMAP( nBumpMapVariant );
			pshIndex.SetNORMALMAP2( bBump2 );
			pshIndex.SetWORLDVERTEXTRANSITION( vars.m_bWorldVertexTransition );
			pshIndex.SetSEAMLESS( bSeamless );
			pshIndex.SetDETAILTEXTURE( bDetail );
			pshIndex.SetDETAIL_BLEND_MODE( nDetailBlendMode );
			pshIndex.SetFLASHLIGHTDEPTHFILTERMODE( nShadowFilterMode );
			pShaderShadow->SetPixelShader( "flashlight_ps51", pshIndex.GetIndex() );
		}
		FogToBlack();
	}
	else
	{
		VMatrix worldToTexture;
		ITexture *pFlashlightDepthTexture;
		FlashlightState_t flashlightState = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pFlashlightDepthTexture );

		SetFlashLightColorFromState( flashlightState, pShaderAPI );

		BindTexture( SHADER_SAMPLER0, flashlightState.m_pSpotlightTexture, flashlightState.m_nSpotlightTextureFrame );

		if( pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && flashlightState.m_bEnableShadows )
		{
			BindTexture( SHADER_SAMPLER7, pFlashlightDepthTexture, 0 );
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D );

			// Tweaks associated with a given flashlight
			float tweaks[4];
			tweaks[0] = ShadowFilterFromState( flashlightState );
			tweaks[1] = ShadowAttenFromState( flashlightState );
			HashShadow2DJitter( flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
			DX12SetPixelShaderConstant( PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks, 1 );

			// Dimensions of screen, used for screen-space noise map sampling
			float vScreenScale[4] = {1280.0f / 32.0f, 720.0f / 32.0f, 0, 0};
			int nWidth, nHeight;
			pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
			vScreenScale[0] = (float) nWidth  / 32.0f;
			vScreenScale[1] = (float) nHeight / 32.0f;
			DX12SetPixelShaderConstant( PSREG_FLASHLIGHT_SCREEN_SCALE, vScreenScale, 1 );
		}

		if( params[BASETEXTURE]->IsTexture() && mat_fullbright.GetInt() != 2 )
		{
			BindTexture( SHADER_SAMPLER1, BASETEXTURE, FRAME );
		}
		else
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_GREY );
		}
		if( vars.m_bWorldVertexTransition )
		{
			Assert( vars.m_nBaseTexture2Var >= 0 && vars.m_nBaseTexture2FrameVar >= 0 );
			BindTexture( SHADER_SAMPLER4, vars.m_nBaseTexture2Var, vars.m_nBaseTexture2FrameVar );
		}
		pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_NORMALIZATION_CUBEMAP );
		if( vars.m_bBump )
		{
			BindTexture( SHADER_SAMPLER3, vars.m_nBumpmapVar, vars.m_nBumpmapFrame );
		}
		else
		{
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_NORMALIZATION_CUBEMAP );
		}

		if( bDetail )
		{
			BindTexture( SHADER_SAMPLER8, vars.m_nDetailVar );
		}

		if( vars.m_bWorldVertexTransition )
		{
			if( bBump2 )
			{
				BindTexture( SHADER_SAMPLER6, vars.m_nBumpmap2Var, vars.m_nBumpmap2Frame );
			}
		}

		{
			DECLARE_DYNAMIC_VERTEX_SHADER( lightmappedgeneric_flashlight_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER( lightmappedgeneric_flashlight_vs51 );
			if ( bSeamless )
			{
				float const0[4]={ vars.m_fSeamlessScale,0,0,0};
				DX12SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_6, const0 );
			}

			if ( bDetail )
			{
				float vDetailConstants[4] = {1,1,1,1};

				if ( vars.m_nDetailTint != -1 )
				{
					params[vars.m_nDetailTint]->GetVecValue( vDetailConstants, 3 );
				}

				if ( vars.m_nDetailTextureBlendFactor != -1 )
				{
					vDetailConstants[3] = params[vars.m_nDetailTextureBlendFactor]->GetFloatValue();
				}

				DX12SetPixelShaderConstant( 0, vDetailConstants, 1 );
			}
		}

		pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );

		float vEyePos_SpecExponent[4];
		pShaderAPI->GetWorldSpaceCameraPosition( vEyePos_SpecExponent );
		vEyePos_SpecExponent[3] = 0.0f;
		DX12SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1 );

		{
			DECLARE_DYNAMIC_PIXEL_SHADER( flashlight_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE,  pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, flashlightState.m_bEnableShadows && ( pFlashlightDepthTexture != NULL ) );
			SET_DYNAMIC_PIXEL_SHADER( flashlight_ps51 );
		}

		float atten[4];										// Set the flashlight attenuation factors
		atten[0] = flashlightState.m_fConstantAtten;
		atten[1] = flashlightState.m_fLinearAtten;
		atten[2] = flashlightState.m_fQuadraticAtten;
		atten[3] = flashlightState.m_FarZ;
		DX12SetPixelShaderConstant( PSREG_FLASHLIGHT_ATTENUATION, atten, 1 );

		SetFlashlightVertexShaderConstants( vars.m_bBump, vars.m_nBumpTransform, bDetail, vars.m_nDetailScale,  bSeamless ? false : true );
	}
	Draw();
}

// Take 0..1 seed and map to (u, v) coordinate to be used in shadow filter jittering...
void CBaseVSShaderDX12::HashShadow2DJitter( const float fJitterSeed, float *fU, float* fV )
{
	const int nTexRes = 32;
	int nSeed = fmod (fJitterSeed, 1.0f) * nTexRes * nTexRes;

	int nRow = nSeed / nTexRes;
	int nCol = nSeed % nTexRes;

	// Div and mod to get an individual texel in the fTexRes x fTexRes grid
	*fU = nRow / (float) nTexRes;	// Row
	*fV = nCol / (float) nTexRes;	// Column
}


void CBaseVSShaderDX12::DrawEqualDepthToDestAlpha( void )
{
	bool bMakeActualDrawCall = false;
	if( s_pShaderShadow )
	{
		s_pShaderShadow->EnableColorWrites( false );
		s_pShaderShadow->EnableAlphaWrites( true );
		s_pShaderShadow->EnableDepthWrites( false );
		s_pShaderShadow->EnableAlphaTest( false );
		s_pShaderShadow->EnableBlending( false );

		s_pShaderShadow->DepthFunc( SHADER_DEPTHFUNC_EQUAL );

		s_pShaderShadow->SetVertexShader( "depthtodestalpha_vs51", 0 );
		s_pShaderShadow->SetPixelShader( "depthtodestalpha_ps51", 0 );
	}
	if( s_pShaderAPI )
	{
		// Dynamic index objects select each logical shader's native block (these shaders have no combos).
		depthtodestalpha_vs51_Dynamic_Index vshIndex;
		depthtodestalpha_ps51_Dynamic_Index pshIndex;
		s_pShaderAPI->SetVertexShaderIndex( vshIndex.GetIndex() );
		s_pShaderAPI->SetPixelShaderIndex( pshIndex.GetIndex() );

		bMakeActualDrawCall = s_pShaderAPI->ShouldWriteDepthToDestAlpha();
	}
	Draw( bMakeActualDrawCall );
}
