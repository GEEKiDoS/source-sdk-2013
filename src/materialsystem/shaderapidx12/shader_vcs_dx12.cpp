//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CPU-only, bounded reader for Source VCS shader files.
//
//=============================================================================//
#include "shader_vcs_dx12.h"

#include "filesystem.h"
#include "materialsystem/shader_vcs_version.h"
#include "tier0/threadtools.h"
#include "tier1/diff.h"
#include "tier1/fmtstr.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/strtools.h"
#include "thirdparty/bzip2/bzlib.h"

#include <cstdio>
#include <cstring>

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: Reads a little-endian uint32 at nOffset. All file integers are LE,
//          including unaligned records in compressed blocks.
//-----------------------------------------------------------------------------
static bool U32( const uint8_t *pData, size_t nSize, size_t nOffset, uint32_t &nValue )
{
	if ( nOffset > nSize || nSize - nOffset < sizeof( nValue ) )
		return false;
	memcpy( &nValue, pData + nOffset, sizeof( nValue ) );
#if defined( PLATFORM_BIG_ENDIAN )
	nValue = ( nValue >> 24 ) | ( ( nValue >> 8 ) & 0x0000ff00u ) |
	    ( ( nValue << 8 ) & 0x00ff0000u ) | ( nValue << 24 );
#endif
	return true;
}

static bool Span( size_t nSize, size_t nStart, size_t nCount )
{
	return nStart <= nSize && nCount <= nSize - nStart;
}

static bool Product( size_t nA, size_t nB, size_t &nOut )
{
	if ( nB && nA > SIZE_MAX / nB )
		return false;
	nOut = nA * nB;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: ApplyDiffs does unchecked pointer arithmetic on both source and output.
//          Model its cursor transitions using signed offsets before invoking it.
//-----------------------------------------------------------------------------
static bool ValidDiff( const uint8_t *pDiff, size_t nLength, size_t nOldSize, size_t &nOutputSize )
{
	size_t nPos = 0;
	size_t nOut = 0;
	int64_t nSource = 0;
	while ( nPos < nLength )
	{
		const uint8_t nOp = pDiff[nPos++];
		size_t nCount = 0;
		int64_t nDelta = 0;
		bool bFromOld = false;
		if ( nOp == 0 )
		{
			if ( !Span( nLength, nPos, 4 ) )
				return false;
			nCount = static_cast<size_t>( pDiff[nPos] ) | ( static_cast<size_t>( pDiff[nPos + 1] ) << 8 );
			const uint16_t nOfs = static_cast<uint16_t>( pDiff[nPos + 2] | ( pDiff[nPos + 3] << 8 ) );
			nDelta = static_cast<int16_t>( nOfs );
			nPos += 4;
			bFromOld = true;
		}
		else if ( nOp & 0x80 )
		{
			nCount = nOp & 0x7f;
			if ( !nCount )
			{
				if ( !Span( nLength, nPos, 1 ) )
					return false;
				nCount = pDiff[nPos++];
				if ( !nCount )
				{
					if ( !Span( nLength, nPos, 3 ) )
						return false;
					nCount = static_cast<size_t>( pDiff[nPos] ) |
					    ( static_cast<size_t>( pDiff[nPos + 1] ) << 8 ) |
					    ( static_cast<size_t>( pDiff[nPos + 2] ) << 16 );
					nPos += 3;
					if ( !Span( nLength, nPos, nCount ) )
						return false;
					nPos += nCount;
				}
				else
				{
					if ( !Span( nLength, nPos, 2 ) )
						return false;
					const uint16_t nOfs = static_cast<uint16_t>( pDiff[nPos] | ( pDiff[nPos + 1] << 8 ) );
					nDelta = static_cast<int16_t>( nOfs );
					nPos += 2;
					bFromOld = true;
				}
			}
			else
			{
				if ( !Span( nLength, nPos, 1 ) )
					return false;
				nDelta = static_cast<int8_t>( pDiff[nPos++] );
				bFromOld = true;
			}
		}
		else
		{
			nCount = nOp;
			if ( !Span( nLength, nPos, nCount ) )
				return false;
			nPos += nCount;
		}
		if ( !nCount || nCount > nOldSize - nOut )
			return false;
		nOut += nCount;
		if ( bFromOld )
		{
			nSource += nDelta;
			if ( nSource < 0 || static_cast<uint64_t>( nSource ) > nOldSize ||
			    nCount > nOldSize - static_cast<size_t>( nSource ) )
				return false;
			nSource += static_cast<int64_t>( nCount );
		}
	}
	nOutputSize = nOut;
	return nOut != 0;
}

//-----------------------------------------------------------------------------
// Purpose: Binary search over records sorted by m_nId; returns the index of the
//          record whose m_nId equals nId, or -1.
//-----------------------------------------------------------------------------
template <class T>
static int FindRecord( const T *pRecords, int nCount, uint32_t nId )
{
	int nLow = 0;
	int nHigh = nCount;
	while ( nLow < nHigh )
	{
		const int nMid = nLow + ( nHigh - nLow ) / 2;
		if ( pRecords[nMid].m_nId < nId )
			nLow = nMid + 1;
		else
			nHigh = nMid;
	}
	return nLow < nCount && pRecords[nLow].m_nId == nId ? nLow : -1;
}

// One line of shaders/native_dx12_legacy_names.txt; m_Key is "<vs|ps>:<native lowercase>".
struct LegacyNameDX12
{
	CUtlString m_Key;
	CUtlString m_Legacy;
};

static int __cdecl CompareLegacyNames( const LegacyNameDX12 *pLeft, const LegacyNameDX12 *pRight )
{
	const int nKey = V_strcmp( pLeft->m_Key.Get(), pRight->m_Key.Get() );
	return nKey ? nKey : V_strcmp( pLeft->m_Legacy.Get(), pRight->m_Legacy.Get() );
}

//-----------------------------------------------------------------------------
// Purpose: Binary search over the sorted legacy-name table; returns the first
//          entry whose key equals pszKey, or -1.
//-----------------------------------------------------------------------------
static int FindLegacyName( const CUtlVector<LegacyNameDX12> &names, const char *pszKey )
{
	int nLow = 0;
	int nHigh = names.Count();
	while ( nLow < nHigh )
	{
		const int nMid = nLow + ( nHigh - nLow ) / 2;
		if ( V_strcmp( names[nMid].m_Key.Get(), pszKey ) < 0 )
			nLow = nMid + 1;
		else
			nHigh = nMid;
	}
	return nLow < names.Count() && !V_strcmp( names[nLow].m_Key.Get(), pszKey ) ? nLow : -1;
}

//-----------------------------------------------------------------------------
// Purpose: Constructor / destructor
//-----------------------------------------------------------------------------
ShaderVcsFile::ShaderVcsFile()
    : m_Cache( DefLessFunc( uint32_t ) ), m_LoadedStatics( DefLessFunc( uint32_t ) )
{
}

ShaderVcsFile::~ShaderVcsFile()
{
	m_LoadedStatics.Purge();
	m_Cache.PurgeAndDeleteElements();
}

//-----------------------------------------------------------------------------
// Purpose: Formats "<path> (<stage>): <message>" into error; always returns false.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::Fail( const char *pszMessage, CUtlString &error ) const
{
	error = m_Path + " (" + ( m_Stage == VcsStage::Vertex ? "VS" : "PS" ) + "): " + pszMessage;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Parsed once per process from GAME shaders/native_dx12_legacy_names.txt:
//          "<native> <vs|ps> <legacy>" per line, or "<native> <vs|ps>" for a
//          native-only logical, which maps to kNativeOnlyMarker (no shaders/fxc fallback).
//-----------------------------------------------------------------------------
CUtlString ShaderVcsFile::LegacyShaderName( IFileSystem &filesystem, const char *pszName, VcsStage stage )
{
	static CThreadFastMutex s_Mutex;
	static bool s_bLoaded = false;
	static CUtlVector<LegacyNameDX12> s_Names;
	{
		AUTO_LOCK( s_Mutex );
		if ( !s_bLoaded )
		{
			s_bLoaded = true;
			FileHandle_t hFile = filesystem.Open( "shaders/native_dx12_legacy_names.txt", "rb", "GAME" );
			if ( hFile != FILESYSTEM_INVALID_HANDLE )
			{
				const unsigned int nSize = filesystem.Size( hFile );
				CUtlVector<char> text;
				text.SetCount( static_cast<int>( nSize ) + 1 );
				memset( text.Base(), 0, nSize + 1 );
				filesystem.Read( text.Base(), static_cast<int>( nSize ), hFile );
				filesystem.Close( hFile );
				char *pText = text.Base();
				char *pTextEnd = pText + nSize;
				for ( char *pLine = pText; pLine < pTextEnd; )
				{
					char *pLineEnd = static_cast<char *>( memchr( pLine, '\n', static_cast<size_t>( pTextEnd - pLine ) ) );
					if ( !pLineEnd )
						pLineEnd = pTextEnd;
					*pLineEnd = '\0';
					char szLogical[256] = {};
					char szStage[8] = {};
					char szLegacy[256] = {};
					const int nFields = sscanf( pLine, "%255s %7s %255s", szLogical, szStage, szLegacy );
					if ( nFields >= 2 )
					{
						char szKey[sizeof( szStage ) + sizeof( szLogical ) + 1];
						V_snprintf( szKey, sizeof( szKey ), "%s:%s", szStage, szLogical );
						V_strlower( szKey );
						const int i = s_Names.AddToTail();
						s_Names[i].m_Key = szKey;
						s_Names[i].m_Legacy = nFields == 3 ? szLegacy : kNativeOnlyMarker;
					}
					pLine = pLineEnd + 1;
				}
				s_Names.Sort( CompareLegacyNames );
			}
		}
	}
	CUtlString key( stage == VcsStage::Vertex ? "vs:" : "ps:" );
	key += pszName;
	V_strlower( key.GetForModify() );
	const int nFound = FindLegacyName( s_Names, key.Get() );
	return nFound >= 0 ? s_Names[nFound].m_Legacy : CUtlString( pszName );
}

//-----------------------------------------------------------------------------
// Purpose: Opens shaders/<vsh|psh>/<name>.vcs, falling back to the legacy
//          shaders/fxc logical unless the name is native-only.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::Open( IFileSystem &filesystem, const char *pszName, VcsStage stage, CUtlString &error )
{
	if ( !pszName || !*pszName || strchr( pszName, '/' ) || strchr( pszName, '\\' ) ||
	    V_strstr( pszName, ".." ) || strchr( pszName, ':' ) )
	{
		m_Path = pszName ? pszName : "<null>";
		m_Stage = stage;
		return Fail( "invalid shader name", error );
	}
	const CUtlString filename = CUtlString( "shaders/" ) +
	    ( stage == VcsStage::Vertex ? "vsh/" : "psh/" ) + pszName + ".vcs";
	// A native logical whose native record is absent normally resolves to its legacy DX9 logical.
	// A two-column native-only marker deliberately has no shaders/fxc fallback.
	const CUtlString legacyName = LegacyShaderName( filesystem, pszName, stage );
	const bool bNativeOnly = !V_strcmp( legacyName.Get(), kNativeOnlyMarker );
	const CUtlString fallback = CUtlString( "shaders/fxc/" ) + legacyName + ".vcs";
	FileHandle_t hFile = filesystem.Open( filename.Get(), "rb", "GAME" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE && bNativeOnly )
	{
		m_Path = filename;
		m_Stage = stage;
		return Fail( "native-only shader file not found in GAME", error );
	}
	const CUtlString &selected = hFile == FILESYSTEM_INVALID_HANDLE ? fallback : filename;
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		hFile = filesystem.Open( fallback.Get(), "rb", "GAME" );
	m_Path = selected;
	m_Stage = stage;
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return Fail( "file not found in GAME", error );
	const unsigned int nSize = filesystem.Size( hFile );
	if ( nSize < 28 || nSize > static_cast<unsigned int>( INT_MAX ) )
	{
		filesystem.Close( hFile );
		return Fail( "invalid file size", error );
	}
	CUtlVector<uint8> bytes;
	bytes.SetCount( static_cast<int>( nSize ) );
	const int nRead = filesystem.Read( bytes.Base(), static_cast<int>( nSize ), hFile );
	filesystem.Close( hFile );
	if ( nRead != static_cast<int>( nSize ) )
		return Fail( "short GAME file read", error );
	if ( !ParseBytes( bytes.Base(), nSize, stage, selected.Get(), error ) )
		return false;
	m_File.Swap( bytes );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Parses and takes a copy of an in-memory VCS image.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::OpenBytes( const uint8_t *pBytes, size_t nLength, VcsStage stage,
    const char *pszLabel, CUtlString &error )
{
	if ( !ParseBytes( pBytes, nLength, stage, pszLabel, error ) )
		return false;
	m_File.CopyArray( pBytes, static_cast<int>( nLength ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Validates the header and combo directory; resets all decoded state.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::ParseBytes( const uint8_t *pBytes, size_t nLength, VcsStage stage,
    const char *pszLabel, CUtlString &error )
{
	m_Path = pszLabel ? pszLabel : "<memory>";
	m_Stage = stage;
	m_nVersion = m_nTotalCount = m_nDynamicCount = m_nFlags = m_nCentroidMask = m_nSourceCRC = 0;
	m_nPayloadStart = 0;
	m_nHeaderBytes = 28;
	m_LoadedStatics.Purge();
	m_Cache.PurgeAndDeleteElements();
	m_File.RemoveAll();
	m_Reference.RemoveAll();
	m_Records.RemoveAll();
	m_Aliases.RemoveAll();
	if ( !pBytes || nLength < 24 )
		return Fail( "truncated VCS header", error );
	// The bytes are kept in a CUtlVector, whose element count is an int.
	if ( nLength > static_cast<size_t>( INT_MAX ) )
		return Fail( "VCS file exceeds 32-bit on-disk offsets", error );
	uint32_t nVersion, nTotal, nDynamic, nFlags, nCentroid, nField, nCRC = 0;
	if ( !U32( pBytes, nLength, 0, nVersion ) || !U32( pBytes, nLength, 4, nTotal ) ||
	    !U32( pBytes, nLength, 8, nDynamic ) || !U32( pBytes, nLength, 12, nFlags ) ||
	    !U32( pBytes, nLength, 16, nCentroid ) || !U32( pBytes, nLength, 20, nField ) )
		return Fail( "truncated VCS header", error );
	if ( nVersion >= 4 && !U32( pBytes, nLength, 24, nCRC ) )
		return Fail( "truncated VCS header", error );
	if ( nVersion != 2 && ( nVersion < 4 || nVersion > 6 ) )
		return Fail( CFmtStr( "unsupported VCS version %u", nVersion ), error );
	m_nHeaderBytes = nVersion == 2 ? 24 : 28;
	if ( !nTotal || !nDynamic || nTotal % nDynamic )
		return Fail( "invalid total/dynamic combo counts", error );
	const size_t nStaticCount = nTotal / nDynamic;
	if ( nVersion < 4 )
	{
		if ( nField > nLength - m_nHeaderBytes || nField > static_cast<uint32_t>( INT_MAX ) )
			return Fail( "legacy VCS reference exceeds file", error );
		const size_t nDictionaryStart = m_nHeaderBytes + static_cast<size_t>( nField );
		size_t nDictionarySize;
		if ( !Product( nTotal, 8, nDictionarySize ) || !Span( nLength, nDictionaryStart, nDictionarySize ) )
			return Fail( "legacy VCS dictionary exceeds file", error );
		m_nPayloadStart = nDictionaryStart + nDictionarySize;
		m_Reference.CopyArray( pBytes + m_nHeaderBytes, static_cast<int>( nField ) );
		size_t nPreviousEnd = m_nPayloadStart;
		for ( size_t i = 0; i < nTotal; ++i )
		{
			uint32_t nOffset, nSize;
			U32( pBytes, nLength, nDictionaryStart + i * 8, nOffset );
			U32( pBytes, nLength, nDictionaryStart + i * 8 + 4, nSize );
			if ( nOffset == 0xffffffffu )
			{
				if ( nSize != 0 && nSize != 0xffffffffu )
					return Fail( "invalid skipped legacy VCS entry", error );
				continue;
			}
			if ( nOffset < m_nPayloadStart || !Span( nLength, nOffset, nSize ) || nOffset < nPreviousEnd )
				return Fail( "invalid legacy VCS dictionary span", error );
			nPreviousEnd = static_cast<size_t>( nOffset ) + nSize;
		}
	}
	else if ( nVersion == 4 )
	{
		if ( nField > nLength - 28 || nField > static_cast<uint32_t>( INT_MAX ) )
			return Fail( "V4 reference exceeds file", error );
		const size_t nDictionaryStart = 28 + static_cast<size_t>( nField );
		size_t nDictionarySize;
		if ( !Product( nTotal, 8, nDictionarySize ) || !Span( nLength, nDictionaryStart, nDictionarySize ) )
			return Fail( "V4 dictionary exceeds file", error );
		m_nPayloadStart = nDictionaryStart + nDictionarySize;
		if ( nField )
			m_Reference.CopyArray( pBytes + 28, static_cast<int>( nField ) );
		size_t nPreviousEnd = m_nPayloadStart;
		for ( size_t i = 0; i < nTotal; ++i )
		{
			if ( i % nDynamic == 0 )
				nPreviousEnd = m_nPayloadStart;
			uint32_t nOffset, nSize;
			U32( pBytes, nLength, nDictionaryStart + i * 8, nOffset );
			U32( pBytes, nLength, nDictionaryStart + i * 8 + 4, nSize );
			if ( nOffset == 0xffffffffu )
			{
				if ( nSize != 0 && nSize != 0xffffffffu )
					return Fail( "invalid skipped V4 dictionary entry", error );
				continue;
			}
			if ( nOffset < m_nPayloadStart || !Span( nLength, nOffset, nSize ) || nOffset < nPreviousEnd )
				return Fail( "invalid V4 dictionary span", error );
			nPreviousEnd = static_cast<size_t>( nOffset ) + nSize;
		}
	}
	else
	{
		if ( !nField || nField > nStaticCount + 1 )
			return Fail( "invalid static record count", error );
		size_t nRecordBytes;
		if ( !Product( nField, 8, nRecordBytes ) || !Span( nLength, 28, nRecordBytes ) )
			return Fail( "static records exceed file", error );
		size_t nCursor = 28 + nRecordBytes;
		if ( nVersion == 6 )
		{
			uint32_t nAliasCount;
			if ( !U32( pBytes, nLength, nCursor, nAliasCount ) )
				return Fail( "truncated alias count", error );
			nCursor += 4;
			size_t nAliasBytes;
			if ( nAliasCount > nStaticCount || !Product( nAliasCount, 8, nAliasBytes ) ||
			    !Span( nLength, nCursor, nAliasBytes ) )
				return Fail( "aliases exceed file", error );
			for ( size_t i = 0; i < nAliasCount; ++i )
			{
				uint32_t nId, nSource;
				U32( pBytes, nLength, nCursor + i * 8, nId );
				U32( pBytes, nLength, nCursor + i * 8 + 4, nSource );
				if ( nId >= nStaticCount || nSource >= nStaticCount ||
				    ( m_Aliases.Count() && nId <= m_Aliases.Tail().m_nId ) )
					return Fail( CFmtStr( "invalid or unsorted static alias %zu", i ), error );
				m_Aliases.AddToTail( { nId, nSource } );
			}
			nCursor += nAliasBytes;
		}
		m_nPayloadStart = nCursor;
		for ( size_t i = 0; i < nField; ++i )
		{
			uint32_t nId, nOffset;
			U32( pBytes, nLength, 28 + i * 8, nId );
			U32( pBytes, nLength, 28 + i * 8 + 4, nOffset );
			if ( i + 1 == nField )
			{
				if ( nId != 0xffffffffu || nOffset != nLength )
					return Fail( "invalid static sentinel/end offset", error );
			}
			else if ( nId >= nStaticCount || nOffset < m_nPayloadStart || nOffset >= nLength ||
			    ( m_Records.Count() &&
			        ( nId <= m_Records.Tail().m_nId || nOffset <= m_Records.Tail().m_nOffset ) ) )
				return Fail( CFmtStr( "invalid or unsorted static record %zu", i ), error );
			m_Records.AddToTail( { nId, nOffset } );
		}
		if ( m_Records.Count() > 1 && m_Records[m_Records.Count() - 2].m_nOffset >= m_Records.Tail().m_nOffset )
			return Fail( "empty last static span", error );
		if ( m_Records.Head().m_nOffset != m_nPayloadStart )
			return Fail( "unreferenced bytes before static payloads", error );
		// The trailing sentinel record is not a static combo.
		const int nCanonicalCount = m_Records.Count() - 1;
		for ( int i = 0; i < m_Aliases.Count(); ++i )
		{
			const AliasRecord &alias = m_Aliases[i];
			if ( FindRecord( m_Records.Base(), nCanonicalCount, alias.m_nId ) >= 0 )
				return Fail( "alias overlaps a static record", error );
			// A source may itself be an alias, but every chain must terminate.
			uint32_t nCurrent = alias.m_nSource;
			for ( int nHops = 0; nHops <= m_Aliases.Count(); ++nHops )
			{
				if ( FindRecord( m_Records.Base(), nCanonicalCount, nCurrent ) >= 0 )
					break;
				const int nNext = FindRecord( m_Aliases.Base(), m_Aliases.Count(), nCurrent );
				if ( nNext < 0 )
					return Fail( "alias points to a missing static combo", error );
				if ( nHops == m_Aliases.Count() )
					return Fail( "static alias cycle", error );
				nCurrent = m_Aliases[nNext].m_nSource;
			}
		}
	}
	// The caller installs its owned bytes only after the directory is validated.
	m_nVersion = nVersion;
	m_nTotalCount = nTotal;
	m_nDynamicCount = nDynamic;
	m_nFlags = nFlags;
	m_nCentroidMask = nCentroid;
	m_nSourceCRC = nCRC;
	error.Clear();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Checks a payload is either a well-formed SM5 DXBC container of this
//          stage or a legacy SM1-3 token stream of this stage.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::ValidateTokens( const uint8_t *pData, size_t nSize, CUtlString &error ) const
{
	if ( nSize >= 4 && !memcmp( pData, "DXBC", 4 ) )
	{
		// DXBC header: magic, checksum[4], one, total bytes, chunk count, chunk offsets.
		uint32_t nTotal = 0;
		uint32_t nChunks = 0;
		uint32_t nOne = 0;
		if ( ( nSize & 3 ) || nSize < 36 || !U32( pData, nSize, 20, nOne ) || nOne != 1 ||
		    !U32( pData, nSize, 24, nTotal ) || nTotal != nSize ||
		    !U32( pData, nSize, 28, nChunks ) || !nChunks || nChunks > 128 ||
		    nChunks > ( nSize - 32 ) / 4 )
			return Fail( "invalid DXBC header or chunk directory", error );
		bool bProgram = false;
		for ( uint32_t i = 0; i < nChunks; ++i )
		{
			uint32_t nOffset = 0;
			uint32_t nLength = 0;
			uint32_t nToken = 0;
			U32( pData, nSize, 32 + 4 * i, nOffset );
			if ( ( nOffset & 3 ) || nOffset < 32 + nChunks * 4 || nOffset > nSize - 8 ||
			    !U32( pData, nSize, nOffset + 4, nLength ) || nLength > nSize - nOffset - 8 )
				return Fail( "truncated or misaligned DXBC chunk", error );
			if ( !memcmp( pData + nOffset, "SHDR", 4 ) || !memcmp( pData + nOffset, "SHEX", 4 ) )
			{
				// SM4+ version token: program type in bits 16-31 (0 = pixel, 1 = vertex),
				// major in bits 4-7, minor in bits 0-3; the second token is the length in DWORDs.
				uint32_t nWords = 0;
				if ( bProgram || nLength < 8 || ( nLength & 3 ) ||
				    !U32( pData, nSize, nOffset + 8, nToken ) || !U32( pData, nSize, nOffset + 12, nWords ) ||
				    nWords < 2 || nWords > nLength / 4 ||
				    ( nToken >> 16 ) != ( m_Stage == VcsStage::Vertex ? 1u : 0u ) ||
				    ( ( nToken >> 4 ) & 0xfu ) != 5u || ( nToken & 0xfu ) > 1u )
					return Fail( "invalid or wrong-stage DXBC shader program", error );
				bProgram = true;
			}
		}
		if ( !bProgram )
			return Fail( "DXBC has no SHDR/SHEX shader program", error );
		return true;
	}
	uint32_t nProfile, nEnd;
	if ( nSize < 8 || nSize % 4 || !U32( pData, nSize, 0, nProfile ) ||
	    !U32( pData, nSize, nSize - 4, nEnd ) || nEnd != 0x0000ffffu )
		return Fail( "invalid token alignment or END token", error );
	const uint32_t nMajor = ( nProfile >> 8 ) & 0xff;
	const uint32_t nMinor = nProfile & 0xff;
	const bool bSupported = m_Stage == VcsStage::Vertex ? ( ( nProfile >> 16 ) == 0xfffeu &&
	                                                          ( ( nMajor == 1 && nMinor == 1 ) || ( nMajor == 2 && nMinor == 0 ) ||
	                                                              ( nMajor == 3 && nMinor == 0 ) ) ) :
	                                                      ( ( nProfile >> 16 ) == 0xffffu &&
	                                                          ( ( nMajor == 1 && nMinor >= 1 && nMinor <= 4 ) ||
	                                                              ( nMajor == 2 && nMinor <= 1 ) || ( nMajor == 3 && nMinor == 0 ) ) );
	if ( !bSupported )
		return Fail( "unsupported stage/profile in shader tokens", error );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Decodes the dynamic combos of one static combo from a v2/v4 dictionary,
//          applying the reference diff when the file has one.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::DecodeV4( uint32_t nStaticIndex, StaticCombos &combos, CUtlString &error ) const
{
	const size_t nDictionary = m_nHeaderBytes + m_Reference.Count();
	for ( uint32_t nDynamic = 0; nDynamic < m_nDynamicCount; ++nDynamic )
	{
		const size_t nEntry = nDictionary + ( static_cast<size_t>( nStaticIndex ) + nDynamic ) * 8;
		uint32_t nOffset, nLength;
		U32( m_File.Base(), m_File.Count(), nEntry, nOffset );
		U32( m_File.Base(), m_File.Count(), nEntry + 4, nLength );
		if ( nOffset == 0xffffffffu || !nLength )
			continue;
		const uint8_t *pData = m_File.Base() + nOffset;
		size_t nOutput = nLength;
		CUtlVector<uint8> decoded;
		if ( m_Reference.Count() )
		{
			if ( nLength > static_cast<uint32_t>( INT_MAX ) ||
			    !ValidDiff( pData, nLength, m_Reference.Count(), nOutput ) )
				return Fail( CFmtStr( "invalid diff at static %u dynamic %u", nStaticIndex, nDynamic ), error );
			decoded.SetCount( m_Reference.Count() );
			int nResult = 0;
			ApplyDiffs( m_Reference.Base(), pData, m_Reference.Count(),
			    static_cast<int>( nLength ), nResult, decoded.Base(),
			    static_cast<uint32_t>( decoded.Count() ) );
			if ( nResult < 0 || static_cast<size_t>( nResult ) != nOutput )
				return Fail( "diff output length mismatch", error );
			decoded.RemoveMultipleFromTail( decoded.Count() - static_cast<int>( nOutput ) );
		}
		else
			decoded.CopyArray( pData, static_cast<int>( nLength ) );
		if ( !ValidateTokens( decoded.Base(), decoded.Count(), error ) )
		{
			error += CFmtStr( " at static %u dynamic %u", nStaticIndex, nDynamic ).Get();
			return false;
		}
		VcsPayload *pPayload = new VcsPayload;
		pPayload->tokens.Swap( decoded );
		combos.m_Dynamics.Insert( nDynamic, pPayload );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Decodes the raw/bzip2/LZMA blocks of one v5/v6 static record in
//          [nStart, nEnd) into its dynamic combos.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::DecodeBlocks( uint32_t nStaticIndex, uint32_t nRecordID, size_t nStart,
    size_t nEnd, StaticCombos &combos, CUtlString &error ) const
{
	size_t nCursor = nStart;
	while ( nCursor < nEnd )
	{
		uint32_t nTag;
		if ( !U32( m_File.Base(), nEnd, nCursor, nTag ) )
			return Fail( "truncated block tag", error );
		nCursor += 4;
		if ( nTag == 0xffffffffu )
			return nCursor == nEnd ? true : Fail( "bytes after static terminator", error );
		const uint32_t nKind = nTag & 0xc0000000u;
		const size_t nPackedSize = nTag & 0x3fffffffu;
		if ( nKind == 0xc0000000u || !nPackedSize || !Span( nEnd, nCursor, nPackedSize ) )
			return Fail( "invalid compression tag or packed span", error );
		CUtlVector<uint8> unpacked;
		if ( nKind == 0x80000000u )
		{
			if ( nPackedSize > MAX_SHADER_UNPACKED_BLOCK_SIZE )
				return Fail( "raw block exceeds unpacked limit", error );
			unpacked.CopyArray( m_File.Base() + nCursor, static_cast<int>( nPackedSize ) );
		}
		else if ( nKind == 0 )
		{
			bz_stream stream = {};
			stream.next_in = reinterpret_cast<char *>( const_cast<uint8 *>( m_File.Base() + nCursor ) );
			stream.avail_in = static_cast<unsigned int>( nPackedSize );
			unpacked.SetCount( MAX_SHADER_UNPACKED_BLOCK_SIZE );
			stream.next_out = reinterpret_cast<char *>( unpacked.Base() );
			stream.avail_out = static_cast<unsigned int>( unpacked.Count() );
			if ( BZ2_bzDecompressInit( &stream, 0, 1 ) != BZ_OK )
				return Fail( "bzip2 initialization failed", error );
			const int nStatus = BZ2_bzDecompress( &stream );
			const size_t nUsed = stream.total_in_lo32;
			const size_t nProduced = stream.total_out_lo32;
			BZ2_bzDecompressEnd( &stream );
			if ( nStatus != BZ_STREAM_END || nUsed != nPackedSize || !nProduced ||
			    nProduced > MAX_SHADER_UNPACKED_BLOCK_SIZE )
				return Fail( "bzip2 output or input length mismatch", error );
			unpacked.RemoveMultipleFromTail( unpacked.Count() - static_cast<int>( nProduced ) );
		}
		else
		{
			// CLZMA has no input or output capacities. Verify its full Source header
			// (LZMA id, actualSize, lzmaSize, properties) before calling it.
			uint32_t nId, nActual, nCompressed, nDict;
			const uint8_t *pLzma = m_File.Base() + nCursor;
			if ( nPackedSize < 17 || !U32( pLzma, nPackedSize, 0, nId ) ||
			    !U32( pLzma, nPackedSize, 4, nActual ) || !U32( pLzma, nPackedSize, 8, nCompressed ) ||
			    !U32( pLzma, nPackedSize, 13, nDict ) || nId != LZMA_ID ||
			    nCompressed != nPackedSize - 17 || !nCompressed || !nActual ||
			    nActual > MAX_SHADER_UNPACKED_BLOCK_SIZE || pLzma[12] >= 225 ||
			    nDict > 64u * 1024u * 1024u )
				return Fail( "invalid Source LZMA header or span", error );
			unpacked.SetCount( static_cast<int>( nActual ) );
			const unsigned int nProduced = CLZMA::Uncompress( const_cast<uint8_t *>( pLzma ), unpacked.Base() );
			if ( nProduced != nActual )
				return Fail( "LZMA output length mismatch", error );
		}
		nCursor += nPackedSize;
		const size_t nUnpacked = unpacked.Count();
		size_t nPos = 0;
		while ( nPos < nUnpacked )
		{
			uint32_t nId, nLength;
			if ( !U32( unpacked.Base(), nUnpacked, nPos, nId ) ||
			    !U32( unpacked.Base(), nUnpacked, nPos + 4, nLength ) )
				return Fail( "truncated dynamic payload record", error );
			nPos += 8;
			uint32_t nDynamic;
			if ( m_nVersion == 5 )
			{
				const uint64_t nBase = static_cast<uint64_t>( nRecordID ) * m_nDynamicCount;
				if ( nId < nBase || static_cast<uint64_t>( nId ) >= nBase + m_nDynamicCount )
					return Fail( "V5 full combo ID outside static range", error );
				nDynamic = nId - static_cast<uint32_t>( nBase );
			}
			else
				nDynamic = nId;
			if ( nDynamic >= m_nDynamicCount || !nLength || !Span( nUnpacked, nPos, nLength ) ||
			    combos.m_Dynamics.Find( nDynamic ) != combos.m_Dynamics.InvalidIndex() )
				return Fail( CFmtStr( "invalid or duplicate dynamic combo %u", nId ), error );
			if ( !ValidateTokens( unpacked.Base() + nPos, nLength, error ) )
			{
				error += CFmtStr( " at static %u dynamic %u", nStaticIndex, nDynamic ).Get();
				return false;
			}
			VcsPayload *pPayload = new VcsPayload;
			pPayload->tokens.CopyArray( unpacked.Base() + nPos, static_cast<int>( nLength ) );
			combos.m_Dynamics.Insert( nDynamic, pPayload );
			nPos += nLength;
		}
	}
	return Fail( "missing static-combo terminator", error );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the already-multiplied static index of the nOrdinal-th present
//          static combo (canonical records first, then aliases).
//-----------------------------------------------------------------------------
bool ShaderVcsFile::StaticComboIndex( size_t nOrdinal, uint32_t &nIndex ) const
{
	if ( !m_nVersion )
		return false;
	if ( m_nVersion <= 4 )
	{
		if ( nOrdinal >= m_nTotalCount / m_nDynamicCount )
			return false;
		nIndex = static_cast<uint32_t>( nOrdinal ) * m_nDynamicCount;
		return true;
	}
	const size_t nCanonicalCount = static_cast<size_t>( m_Records.Count() - 1 );
	if ( nOrdinal < nCanonicalCount )
		nIndex = m_Records[static_cast<int>( nOrdinal )].m_nId * m_nDynamicCount;
	else
	{
		nOrdinal -= nCanonicalCount;
		if ( nOrdinal >= static_cast<size_t>( m_Aliases.Count() ) )
			return false;
		nIndex = m_Aliases[static_cast<int>( nOrdinal )].m_nId * m_nDynamicCount;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Decodes (once) the static combo nStaticIndex, resolving v6 aliases to
//          the canonical record so aliased statics share one decode.
//-----------------------------------------------------------------------------
bool ShaderVcsFile::LoadStaticCombo( uint32_t nStaticIndex, CUtlString &error )
{
	if ( !m_nVersion )
		return Fail( "VCS file has not been opened", error );
	if ( nStaticIndex >= m_nTotalCount || nStaticIndex % m_nDynamicCount )
		return Fail( CFmtStr( "staticIndex must be an in-range, already-multiplied index: %u", nStaticIndex ), error );
	if ( m_LoadedStatics.Find( nStaticIndex ) != m_LoadedStatics.InvalidIndex() )
	{
		error.Clear();
		return true;
	}
	uint32_t nRecordID = nStaticIndex / m_nDynamicCount;
	for ( int nHops = 0; nHops < m_Aliases.Count(); ++nHops )
	{
		const int nAlias = FindRecord( m_Aliases.Base(), m_Aliases.Count(), nRecordID );
		if ( nAlias < 0 )
			break;
		nRecordID = m_Aliases[nAlias].m_nSource;
	}
	const uint32_t nCanonicalIndex = nRecordID * m_nDynamicCount;
	const uint32_t nCached = m_Cache.Find( nCanonicalIndex );
	const StaticCombos *pCombos = nullptr;
	if ( nCached == m_Cache.InvalidIndex() )
	{
		StaticCombos *pDecoded = new StaticCombos;
		bool bDecoded;
		if ( m_nVersion <= 4 )
			bDecoded = DecodeV4( nStaticIndex, *pDecoded, error );
		else
		{
			const int nRecord = FindRecord( m_Records.Base(), m_Records.Count() - 1, nRecordID );
			if ( nRecord < 0 )
			{
				delete pDecoded;
				return Fail( CFmtStr( "missing static combo %u", nStaticIndex ), error );
			}
			bDecoded = DecodeBlocks( nStaticIndex, nRecordID, m_Records[nRecord].m_nOffset,
			    m_Records[nRecord + 1].m_nOffset, *pDecoded, error );
		}
		if ( !bDecoded )
		{
			delete pDecoded;
			error += CFmtStr( " [staticIndex=%u]", nStaticIndex ).Get();
			return false;
		}
		m_Cache.Insert( nCanonicalIndex, pDecoded );
		pCombos = pDecoded;
	}
	else
		pCombos = m_Cache.Element( nCached );
	m_LoadedStatics.Insert( nStaticIndex, pCombos );
	error.Clear();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Returns a decoded payload, or nullptr for an unloaded static or a
//          skipped dynamic combo.
//-----------------------------------------------------------------------------
const VcsPayload *ShaderVcsFile::DynamicPayload( uint32_t nStaticIndex, uint32_t nDynamicIndex ) const
{
	const uint32_t nFound = m_LoadedStatics.Find( nStaticIndex );
	if ( nFound == m_LoadedStatics.InvalidIndex() || nDynamicIndex >= m_nDynamicCount )
		return nullptr;
	const StaticCombos *pCombos = m_LoadedStatics.Element( nFound );
	const uint32_t nPayload = pCombos->m_Dynamics.Find( nDynamicIndex );
	return nPayload == pCombos->m_Dynamics.InvalidIndex() ? nullptr : pCombos->m_Dynamics.Element( nPayload );
}

} // namespace shaderapidx12
