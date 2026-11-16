//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXBC/DXIL shader reflection (see shader_reflect_dx12.h).
//
//=============================================================================//
#include "shader_reflect_dx12.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <cstring>
#include <mutex>

namespace shaderapidx12
{

namespace
{

bool ReadU32( const uint8_t *pBytes, size_t nBytes, size_t nOffset, uint32_t &value )
{
	if ( nOffset > nBytes || nBytes - nOffset < sizeof( uint32_t ) )
		return false;
	memcpy( &value, pBytes + nOffset, sizeof( value ) );
	return true;
}

// dxcompiler.dll is loaded once from the directory of this module (game bin, or the ShaderCompiler3 exe directory).
struct DxcLibrary
{
	HMODULE module = nullptr;
	Microsoft::WRL::ComPtr<IDxcUtils> utils;
};

DxcLibrary &Dxc()
{
	static DxcLibrary s_Library;
	static std::once_flag s_Once;
	std::call_once( s_Once, []
	{
		HMODULE self = nullptr;
		wchar_t path[MAX_PATH] = {};
		if ( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		         reinterpret_cast<LPCWSTR>( &ReflectShaderDX12 ), &self ) ||
		     !GetModuleFileNameW( self, path, MAX_PATH ) )
			return;
		wchar_t *slash = wcsrchr( path, L'\\' );
		if ( !slash || size_t( slash - path ) + 16 >= MAX_PATH )
			return;
		wcscpy( slash + 1, L"dxcompiler.dll" );
		s_Library.module = LoadLibraryExW( path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
		if ( !s_Library.module )
			return;
		auto create = reinterpret_cast<DxcCreateInstanceProc>( GetProcAddress( s_Library.module, "DxcCreateInstance" ) );
		if ( create )
			create( CLSID_DxcUtils, IID_PPV_ARGS( &s_Library.utils ) );
	} );
	return s_Library;
}

}

ShaderBytecodeKindDX12 ClassifyShaderBytecodeDX12( const void *pData, size_t nBytes, uint32_t *pProgramKind )
{
	const uint8_t *pBytes = static_cast<const uint8_t *>( pData );
	uint32_t nTotal = 0, nParts = 0;
	if ( !pBytes || nBytes < 32 || memcmp( pBytes, "DXBC", 4 ) || !ReadU32( pBytes, nBytes, 24, nTotal ) ||
	     nTotal != nBytes || !ReadU32( pBytes, nBytes, 28, nParts ) || !nParts || nParts > ( nBytes - 32 ) / 4 )
		return ShaderBytecodeKindDX12::Invalid;
	ShaderBytecodeKindDX12 kind = ShaderBytecodeKindDX12::Invalid;
	uint32_t nProgramKind = 0;
	for ( uint32_t part = 0; part < nParts; ++part )
	{
		uint32_t nOffset = 0, nLength = 0;
		if ( !ReadU32( pBytes, nBytes, 32 + part * 4, nOffset ) || ( nOffset & 3 ) ||
		     !ReadU32( pBytes, nBytes, size_t( nOffset ) + 4, nLength ) || nLength > nBytes - nOffset - 8 )
			return ShaderBytecodeKindDX12::Invalid;
		const bool dxbc = !memcmp( pBytes + nOffset, "SHDR", 4 ) || !memcmp( pBytes + nOffset, "SHEX", 4 );
		const bool dxil = !memcmp( pBytes + nOffset, "DXIL", 4 );
		if ( !dxbc && !dxil )
			continue;
		// Both program headers start with the SM4-style version token: kind in bits 16-31, major in bits 4-7.
		uint32_t nVersion = 0;
		if ( kind != ShaderBytecodeKindDX12::Invalid || nLength < 8 || !ReadU32( pBytes, nBytes, size_t( nOffset ) + 8, nVersion ) )
			return ShaderBytecodeKindDX12::Invalid;
		const uint32_t nMajor = ( nVersion >> 4 ) & 0xfu;
		if ( dxbc ? nMajor != 5 : nMajor != 6 )
			return ShaderBytecodeKindDX12::Invalid;
		kind = dxbc ? ShaderBytecodeKindDX12::Dxbc : ShaderBytecodeKindDX12::Dxil;
		nProgramKind = nVersion >> 16;
	}
	if ( pProgramKind )
		*pProgramKind = nProgramKind;
	return kind;
}

bool DxilReflectionAvailableDX12()
{
	return Dxc().utils != nullptr;
}

HRESULT ReflectShaderDX12( const void *pBytes, size_t nBytes, ID3D12ShaderReflection **ppReflection )
{
	if ( !ppReflection )
		return E_POINTER;
	*ppReflection = nullptr;
	switch ( ClassifyShaderBytecodeDX12( pBytes, nBytes ) )
	{
	case ShaderBytecodeKindDX12::Dxbc:
		return D3DReflect( pBytes, nBytes, IID_PPV_ARGS( ppReflection ) );
	case ShaderBytecodeKindDX12::Dxil:
	{
		IDxcUtils *utils = Dxc().utils.Get();
		if ( !utils )
			return HRESULT_FROM_WIN32( ERROR_MOD_NOT_FOUND );
		const DxcBuffer buffer = { pBytes, nBytes, 0 };
		return utils->CreateReflection( &buffer, IID_PPV_ARGS( ppReflection ) );
	}
	default:
		return E_INVALIDARG;
	}
}

}
