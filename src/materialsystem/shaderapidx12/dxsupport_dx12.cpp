//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: dxsupport.cfg profile selection, adapted from shaderdevicebase.cpp;
//			no legacy device ABI.
//
//=============================================================================//

#include "dxsupport_dx12.h"
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include "tier0/icommandline.h"
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include <windows.h>
#include <stdlib.h>
#include <limits.h>

namespace shaderapidx12
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Loads a KeyValues profile after validating its structure.
//			KeyValues::LoadFromBuffer reports syntax errors but still returns true,
//			so validate the key/value and brace structure before allowing partial profiles.
//-----------------------------------------------------------------------------
bool LoadProfile( KeyValues *pValues, IFileSystem *pFileSystem, const char *pszName, const char *pszPath )
{
	FileHandle_t hFile = pFileSystem->Open( pszName, "rb", pszPath );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return false;
	const unsigned nLength = pFileSystem->Size( hFile );
	if ( nLength > static_cast<unsigned>( INT_MAX - 1 ) )
	{
		pFileSystem->Close( hFile );
		return false;
	}
	CUtlBuffer data( 0, static_cast<int>( nLength + 1 ), CUtlBuffer::TEXT_BUFFER );
	const int nRead = pFileSystem->Read( data.Base(), static_cast<int>( nLength ), hFile );
	pFileSystem->Close( hFile );
	if ( nRead != static_cast<int>( nLength ) )
		return false;
	data.SeekPut( CUtlBuffer::SEEK_HEAD, nRead );
	const char *pszText = static_cast<const char *>( data.Base() );
	const size_t nSize = nLength;

	// One entry per open brace; an entry is true while a key is waiting for its value.
	const int kMaxDepth = 128;
	bool bExpectsValue[kMaxDepth];
	int nDepth = 1;
	bExpectsValue[0] = false;
	bool bSawRoot = false;
	for ( size_t i = 0; i < nSize; )
	{
		const char c = pszText[i];
		if ( V_isspace( c ) )
		{
			++i;
			continue;
		}
		if ( c == '/' && i + 1 < nSize && pszText[i + 1] == '/' )
		{
			while ( i < nSize && pszText[i] != '\n' )
				++i;
			continue;
		}
		if ( c == '{' )
		{
			if ( !bExpectsValue[nDepth - 1] || nDepth >= kMaxDepth )
				return false;
			bExpectsValue[nDepth - 1] = false;
			bExpectsValue[nDepth++] = false;
			bSawRoot = true;
			++i;
			continue;
		}
		if ( c == '}' )
		{
			if ( nDepth == 1 || bExpectsValue[nDepth - 1] )
				return false;
			--nDepth;
			++i;
			continue;
		}
		if ( c == '[' )
		{
			while ( i < nSize && pszText[i] != ']' )
				++i;
			if ( i == nSize )
				return false;
			++i;
			continue;
		}
		if ( c == '"' )
		{
			++i;
			while ( i < nSize && pszText[i] != '"' )
				++i;
			if ( i == nSize )
				return false;
			++i;
		}
		else
		{
			const size_t nStart = i;
			while ( i < nSize && !V_isspace( pszText[i] ) && pszText[i] != '{' && pszText[i] != '}' )
				++i;
			if ( i == nStart || !c )
				return false;
		}
		bExpectsValue[nDepth - 1] = !bExpectsValue[nDepth - 1];
	}
	if ( !bSawRoot || nDepth != 1 || bExpectsValue[0] )
		return false;
	return pValues->LoadFromBuffer( pszName, data, pFileSystem, pszPath );
}

//-----------------------------------------------------------------------------
// Purpose: Parses a hexadecimal key; returns -1 when missing or malformed
//-----------------------------------------------------------------------------
int Hex( KeyValues *pGroup, const char *pszKey )
{
	const char *pszText = pGroup->GetString( pszKey, nullptr );
	if ( !pszText )
		return -1;
	char *pszEnd = nullptr;
	long nValue = strtol( pszText, &pszEnd, 16 );
	return pszEnd != pszText && !*pszEnd && nValue >= 0 && nValue <= INT_MAX ? static_cast<int>( nValue ) : -1;
}

//-----------------------------------------------------------------------------
// Purpose: Copies one typed value key into pDest
//-----------------------------------------------------------------------------
void AddKey( KeyValues *pDest, KeyValues *pSrc )
{
	switch ( pSrc->GetDataType() )
	{
	case KeyValues::TYPE_STRING:
		pDest->SetString( pSrc->GetName(), pSrc->GetString() );
		break;
	case KeyValues::TYPE_INT:
		pDest->SetInt( pSrc->GetName(), pSrc->GetInt() );
		break;
	case KeyValues::TYPE_FLOAT:
		pDest->SetFloat( pSrc->GetName(), pSrc->GetFloat() );
		break;
	case KeyValues::TYPE_PTR:
		pDest->SetPtr( pSrc->GetName(), pSrc->GetPtr() );
		break;
	case KeyValues::TYPE_WSTRING:
		pDest->SetWString( pSrc->GetName(), pSrc->GetWString() );
		break;
	case KeyValues::TYPE_COLOR:
		pDest->SetColor( pSrc->GetName(), pSrc->GetColor() );
		break;
	case KeyValues::TYPE_UINT64:
		pDest->SetUint64( pSrc->GetName(), pSrc->GetUint64() );
		break;
	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Recursively overrides values of pDest with those of pSrc
//-----------------------------------------------------------------------------
void OverrideValues( KeyValues *pDest, KeyValues *pSrc )
{
	for ( KeyValues *pValue = pSrc->GetFirstValue(); pValue; pValue = pValue->GetNextValue() )
		AddKey( pDest, pValue );
	for ( KeyValues *pDir = pSrc->GetFirstTrueSubKey(); pDir; pDir = pDir->GetNextTrueSubKey() )
	{
		KeyValues *pMatch = pDest->FindKey( pDir->GetName() );
		if ( pMatch && pMatch->GetDataType() == KeyValues::TYPE_NONE )
			OverrideValues( pMatch, pDir );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Applies dxsupport_override.cfg groups to the matching dxsupport.cfg groups
//-----------------------------------------------------------------------------
void MergeOverrides( KeyValues *pDest, KeyValues *pSrc )
{
	for ( KeyValues *pOverrideGroup = pSrc->GetFirstTrueSubKey(); pOverrideGroup; pOverrideGroup = pOverrideGroup->GetNextTrueSubKey() )
	{
		const char *pszName = pOverrideGroup->GetString( "name", nullptr );
		const int nVendor = Hex( pOverrideGroup, "VendorID" );
		const int nMinDevice = Hex( pOverrideGroup, "MinDeviceID" );
		const int nMaxDevice = Hex( pOverrideGroup, "MaxDeviceID" );
		for ( KeyValues *pGroup = pDest->GetFirstTrueSubKey(); pGroup; pGroup = pGroup->GetNextTrueSubKey() )
		{
			if ( pszName && Q_stricmp( pszName, pGroup->GetString( "name", "" ) ) )
				continue;
			if ( nVendor >= 0 && nVendor != Hex( pGroup, "VendorID" ) )
				continue;
			if ( nMinDevice >= 0 && nMaxDevice >= 0 && ( Hex( pGroup, "MinDeviceID" ) < nMinDevice || Hex( pGroup, "MaxDeviceID" ) < 0 || Hex( pGroup, "MaxDeviceID" ) > nMaxDevice ) )
				continue;
			OverrideValues( pGroup, pOverrideGroup );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Finds the DX level group, optionally restricted to one vendor
//-----------------------------------------------------------------------------
KeyValues *FindDXLevel( KeyValues *pRoot, int nLevel, int nVendor = -1 )
{
	for ( KeyValues *pGroup = pRoot->GetFirstTrueSubKey(); pGroup; pGroup = pGroup->GetNextTrueSubKey() )
		if ( pGroup->GetInt( "name", 0 ) == nLevel && ( nVendor < 0 || Hex( pGroup, "VendorID" ) == nVendor ) )
			return pGroup;
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Finds the card group whose vendor/device range contains the adapter
//-----------------------------------------------------------------------------
KeyValues *FindCard( KeyValues *pRoot, unsigned nVendor, unsigned nDevice )
{
	for ( KeyValues *pGroup = pRoot->GetFirstTrueSubKey(); pGroup; pGroup = pGroup->GetNextTrueSubKey() )
	{
		const int nMinDevice = Hex( pGroup, "MinDeviceID" );
		const int nMaxDevice = Hex( pGroup, "MaxDeviceID" );
		if ( Hex( pGroup, "VendorID" ) == static_cast<int>( nVendor ) && nMinDevice >= 0 && nMaxDevice >= nMinDevice && nDevice >= static_cast<unsigned>( nMinDevice ) && nDevice <= static_cast<unsigned>( nMaxDevice ) )
			return pGroup;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Finds the group whose [min, max) key range contains nValue
//-----------------------------------------------------------------------------
KeyValues *FindRange( KeyValues *pRoot, const char *pszMinKey, const char *pszMaxKey, uint64_t nValue, const char *pszName = nullptr )
{
	for ( KeyValues *pGroup = pRoot->GetFirstTrueSubKey(); pGroup; pGroup = pGroup->GetNextTrueSubKey() )
	{
		if ( pszName && !Q_stristr( pGroup->GetString( "name", "" ), pszName ) )
			continue;
		const int nMin = pGroup->GetInt( pszMinKey, -1 );
		const int nMax = pGroup->GetInt( pszMaxKey, -1 );
		if ( nMin >= 0 && nMax >= 0 && nValue >= static_cast<uint64_t>( nMin ) && nValue < static_cast<uint64_t>( nMax ) )
			return pGroup;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Calls apply for the DX level profile, the card profile and, for
//			catch-all cards, the vendor-specific DX level profile
//-----------------------------------------------------------------------------
template <typename Apply>
void ApplyGPUProfiles( KeyValues *pRoot, int nLevel, unsigned nVendor, unsigned nDevice, Apply apply )
{
	apply( FindDXLevel( pRoot, nLevel ) );
	KeyValues *pCard = FindCard( pRoot, nVendor, nDevice );
	apply( pCard );
	// A precise card profile supersedes the vendor/level profile. A catch-all does not.
	if ( pCard && Hex( pCard, "MinDeviceID" ) == 0 && Hex( pCard, "MaxDeviceID" ) == 0xffff )
		apply( FindDXLevel( pRoot, nLevel, static_cast<int>( nVendor ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Prints a KeyValues tree for -debugdxsupport
//-----------------------------------------------------------------------------
void Dump( KeyValues *pValues )
{
	CUtlBuffer text;
	pValues->RecursiveSaveToFile( text, 0 );
	Warning( "%s\n", static_cast<const char *>( text.Base() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Copies every key of pGroup into pDest
//-----------------------------------------------------------------------------
void LoadConfig( KeyValues *pGroup, KeyValues *pDest )
{
	if ( !pGroup )
		return;
	for ( KeyValues *pValue = pGroup->GetFirstSubKey(); pValue; pValue = pValue->GetNextKey() )
		AddKey( pDest, pValue );
}
} // namespace

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
CDXSupportDX12::~CDXSupportDX12()
{
	Clear();
}

//-----------------------------------------------------------------------------
// Purpose: Releases the loaded configuration
//-----------------------------------------------------------------------------
void CDXSupportDX12::Clear()
{
	if ( m_pConfig )
		m_pConfig->deleteThis();
	m_pConfig = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Loads dxsupport.cfg and merges dxsupport_override.cfg into it
//-----------------------------------------------------------------------------
bool CDXSupportDX12::Load( IFileSystem *pFileSystem )
{
	Clear();
	if ( CommandLine()->CheckParm( "-ignoredxsupportcfg" ) )
		return true;
	if ( !pFileSystem->FileExists( "dxsupport.cfg", "EXECUTABLE_PATH" ) )
		return true;
	KeyValues *pBase = new KeyValues( "dxsupport" );
	if ( !LoadProfile( pBase, pFileSystem, "dxsupport.cfg", "EXECUTABLE_PATH" ) )
	{
		Warning( "shaderapidx12: malformed dxsupport.cfg; retaining hardware capabilities\n" );
		pBase->deleteThis();
		return false;
	}
	if ( pFileSystem->FileExists( "dxsupport_override.cfg", "GAME" ) )
	{
		KeyValues *pOverrides = new KeyValues( "dxsupport_override" );
		if ( !LoadProfile( pOverrides, pFileSystem, "dxsupport_override.cfg", "GAME" ) )
		{
			Warning( "shaderapidx12: malformed dxsupport_override.cfg; discarding configuration overrides\n" );
			pOverrides->deleteThis();
			pBase->deleteThis();
			return false;
		}
		MergeOverrides( pBase, pOverrides );
		pOverrides->deleteThis();
	}
	m_pConfig = pBase;
	if ( CommandLine()->CheckParm( "-debugdxsupport" ) )
		Dump( m_pConfig );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Applies the per-GPU capability overrides for nDXLevel
//-----------------------------------------------------------------------------
void CDXSupportDX12::ReadHardwareCaps( DXSupportCapsDX12 &caps, int nDXLevel ) const
{
	if ( m_pConfig )
		ApplyGPUProfiles( m_pConfig, nDXLevel, caps.vendor, caps.device, [&]( KeyValues *pGroup )
		    {
			    if ( !pGroup )
				    return;
			    caps.fastClipping = pGroup->GetInt( "NoUserClipPlanes", caps.fastClipping ? 1 : 0 ) != 0;
			    caps.centroidHack = pGroup->GetInt( "CentroidHack", caps.centroidHack ? 1 : 0 ) != 0;
			    caps.disableShaderOptimizations = pGroup->GetInt( "DisableShaderOptimizations", caps.disableShaderOptimizations ? 1 : 0 ) != 0;
		    } );
	if ( CommandLine()->CheckParm( "-nouserclip" ) )
		caps.fastClipping = true;
}

//-----------------------------------------------------------------------------
// Purpose: Builds the recommended convar configuration for the adapter in caps
//-----------------------------------------------------------------------------
bool CDXSupportDX12::GetRecommendedConfigurationInfo( const DXSupportCapsDX12 &caps, int nDXLevel, KeyValues *pConfiguration ) const
{
	return GetRecommendedConfigurationInfo( caps, nDXLevel, caps.vendor, caps.device, pConfiguration );
}

//-----------------------------------------------------------------------------
// Purpose: Builds the recommended convar configuration for an explicit vendor/device
//-----------------------------------------------------------------------------
bool CDXSupportDX12::GetRecommendedConfigurationInfo( const DXSupportCapsDX12 &caps, int nDXLevel, unsigned nVendor, unsigned nDevice, KeyValues *pConfiguration ) const
{
	// Old card profiles cannot downgrade the native renderer's material tier.
	nDXLevel = kSourceDXLevel;
	pConfiguration->SetInt( "ConVar.mat_dxlevel", nDXLevel );
	pConfiguration->SetInt( "ConVar.mat_hdr_level", 2 );
	if ( !m_pConfig )
		return true;
	ApplyGPUProfiles( m_pConfig, nDXLevel, nVendor, nDevice, [&]( KeyValues *pGroup )
	    {
		    LoadConfig( pGroup, pConfiguration );
	    } );
	const CPUInformation &cpu = *GetCPUInformation();
	const uint64_t nMHz = static_cast<uint64_t>( cpu.m_Speed / 1000000 );
	LoadConfig( FindRange( m_pConfig, "min megahertz", "max megahertz", nMHz, Q_stristr( cpu.m_szProcessorID, "amd" ) ? "AMD" : "Intel" ), pConfiguration );
	MEMORYSTATUSEX memory{};
	memory.dwLength = sizeof( memory );
	if ( GlobalMemoryStatusEx( &memory ) )
		LoadConfig( FindRange( m_pConfig, "min megabytes", "max megabytes", memory.ullTotalPhys / ( 1024ull * 1024 ) ), pConfiguration );
	const uint64_t nVideoMB = caps.memory / ( 1024ull * 1024 );
	KeyValues *pVideo = FindRange( m_pConfig, "min megatexels", "max megatexels", nVideoMB );
	if ( pVideo && caps.memory )
	{
		KeyValues *pPicmip = pVideo->FindKey( "ConVar.mat_picmip" );
		if ( pPicmip )
			pConfiguration->SetInt( "ConVar.mat_picmip", Max( pPicmip->GetInt(), pConfiguration->GetInt( "ConVar.mat_picmip", 0 ) ) );
	}
	pConfiguration->SetInt( "ConVar.mat_dxlevel", nDXLevel );
	pConfiguration->SetInt( "ConVar.mat_hdr_level", 2 );
	if ( CommandLine()->CheckParm( "-debugdxsupport" ) )
		Dump( pConfiguration );
	return true;
}
} // namespace shaderapidx12
