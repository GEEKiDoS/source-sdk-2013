//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_parse.h"
#include "filesystem.h"
#include "particles/particles.h"
#include "tier1/utlbuffer.h"
#include "tier1/fmtstr.h"
#ifdef CLIENT_DLL
#include "dmxloader/dmxloader.h"
#include "particlemgr.h"
#endif
#include <limits.h>
#include <stdio.h>

#include "tier0/memdbgon.h"

namespace
{
// All pointers into the input remain valid until the native reader returns.
// Keeping the original dictionary avoids copying or re-interning its strings.
class CTerrorPCFReader
{
public:
	CTerrorPCFReader( const CUtlBuffer &input, int nPayloadOffset, int nVersion, CUtlBuffer &output ) :
		m_pData( static_cast< const unsigned char * >( input.Base() ) ),
		m_nSize( input.TellPut() ), m_nOffset( nPayloadOffset ),
		m_nVersion( nVersion ), m_Output( output ), m_pError( NULL ), m_nElements( 0 )
	{
	}

	bool Convert();
	const char *GetError() const { return m_pError ? m_pError : "truncated payload"; }

private:
	bool Fail( const char *pError ) { m_pError = pError; return false; }
	int Remaining() const { return m_nSize - m_nOffset; }
	bool Put( const void *pData, int nBytes );
	bool Copy( int nBytes );
	bool GetInt( int &nValue );
	bool GetShort( short &nValue );
	bool GetString( const char *&pValue );
	bool GetSymbol( short &nSymbol );
	bool ConvertValue( int nType, bool bArrayEntry );

	const unsigned char *m_pData;
	int m_nSize;
	int m_nOffset;
	int m_nVersion;
	CUtlBuffer &m_Output;
	const char *m_pError;
	int m_nElements;
	CUtlVector< const char * > m_Strings;
};

bool CTerrorPCFReader::Put( const void *pData, int nBytes )
{
	if ( nBytes < 0 || nBytes > INT_MAX - m_Output.TellPut() - 1 )
		return Fail( "normalized payload is too large" );
	m_Output.Put( pData, nBytes );
	return m_Output.IsValid();
}

bool CTerrorPCFReader::Copy( int nBytes )
{
	if ( nBytes < 0 || nBytes > Remaining() )
		return false;
	if ( !Put( m_pData + m_nOffset, nBytes ) )
		return false;
	m_nOffset += nBytes;
	return true;
}

bool CTerrorPCFReader::GetInt( int &nValue )
{
	if ( Remaining() < sizeof( nValue ) )
		return false;
	Q_memcpy( &nValue, m_pData + m_nOffset, sizeof( nValue ) );
	m_nOffset += sizeof( nValue );
	return true;
}

bool CTerrorPCFReader::GetShort( short &nValue )
{
	if ( Remaining() < sizeof( nValue ) )
		return false;
	Q_memcpy( &nValue, m_pData + m_nOffset, sizeof( nValue ) );
	m_nOffset += sizeof( nValue );
	return true;
}

bool CTerrorPCFReader::GetString( const char *&pValue )
{
	pValue = reinterpret_cast< const char * >( m_pData + m_nOffset );
	const unsigned char *pEnd = static_cast< const unsigned char * >( memchr( pValue, 0, Remaining() ) );
	if ( !pEnd )
		return false;
	m_nOffset = static_cast< int >( pEnd - m_pData ) + 1;
	return true;
}

bool CTerrorPCFReader::GetSymbol( short &nSymbol )
{
	if ( !GetShort( nSymbol ) )
		return false;
	if ( nSymbol < 0 || nSymbol >= m_Strings.Count() )
		return Fail( "invalid string-table index" );
	return true;
}

bool CTerrorPCFReader::ConvertValue( int nType, bool bArrayEntry )
{
	if ( nType < 1 || nType > 28 )
		return Fail( "unknown DMX attribute type" );

	// Encodings 3 and 4 reuse the old objectid slots for 1/10000-second
	// time values. Relabeling those four bytes as a sixteen-byte UUID is wrong.
	if ( nType == 7 || nType == 21 )
		return Fail( "DMX time attributes are not supported by the SDK particle reader" );

	if ( nType > 14 )
	{
		int nCount;
		if ( !GetInt( nCount ) )
			return false;
		if ( nCount < 0 || nCount > Remaining() )
			return Fail( "invalid DMX array count" );
		if ( !Put( &nCount, sizeof( nCount ) ) )
			return false;
		for ( int i = 0; i < nCount; ++i )
		{
			if ( !ConvertValue( nType - 14, true ) )
				return false;
		}
		return true;
	}

	if ( nType == 1 )
	{
		int nElement;
		if ( !GetInt( nElement ) )
			return false;
		if ( nElement < -1 || nElement >= m_nElements )
			return Fail( nElement == -2 ? "external DMX element references are unsupported" : "invalid DMX element reference" );
		return Put( &nElement, sizeof( nElement ) );
	}

	if ( nType == 5 )
	{
		const char *pValue;
		if ( m_nVersion == 4 && !bArrayEntry )
		{
			short nSymbol;
			if ( !GetSymbol( nSymbol ) )
				return false;
			pValue = m_Strings[nSymbol];
		}
		else if ( !GetString( pValue ) )
		{
			return false;
		}
		return Put( pValue, Q_strlen( pValue ) + 1 );
	}

	if ( nType == 6 )
	{
		int nBytes;
		if ( !GetInt( nBytes ) )
			return false;
		if ( nBytes < 0 || nBytes > Remaining() )
			return Fail( "invalid DMX binary length" );
		return Put( &nBytes, sizeof( nBytes ) ) && Copy( nBytes );
	}

	// int, float, bool, color, vector2/3/4, angles, quaternion, matrix.
	static const unsigned char s_nSizes[15] = { 0, 0, 4, 4, 1, 0, 0, 0, 4, 8, 12, 16, 12, 16, 64 };
	return Copy( s_nSizes[nType] );
}

bool CTerrorPCFReader::Convert()
{
	int nStrings;
	if ( m_nVersion == 4 )
	{
		if ( !GetInt( nStrings ) )
			return false;
	}
	else
	{
		short nCount;
		if ( !GetShort( nCount ) )
			return false;
		nStrings = nCount;
	}

	// The target reads both the count and indices as signed shorts.
	if ( nStrings < 0 || nStrings > SHRT_MAX || nStrings > Remaining() )
		return Fail( "string table does not fit the SDK's positive-short dictionary" );
	short nCount = static_cast< short >( nStrings );
	if ( !Put( &nCount, sizeof( nCount ) ) )
		return false;
	m_Strings.EnsureCapacity( nStrings );
	for ( int i = 0; i < nStrings; ++i )
	{
		const char *pString;
		if ( !GetString( pString ) || !Put( pString, Q_strlen( pString ) + 1 ) )
			return false;
		m_Strings.AddToTail( pString );
	}

	if ( !GetInt( m_nElements ) )
		return false;
	// Every directory record needs at least a type symbol, a name and UUID.
	if ( m_nElements <= 0 || m_nElements > Remaining() / ( 2 + 1 + 16 + 4 ) )
		return Fail( "invalid DMX element count" );
	if ( !Put( &m_nElements, sizeof( m_nElements ) ) )
		return false;
	for ( int i = 0; i < m_nElements; ++i )
	{
		short nType;
		if ( !GetSymbol( nType ) || !Put( &nType, sizeof( nType ) ) )
			return false;
		const char *pName;
		if ( m_nVersion == 4 )
		{
			short nName;
			if ( !GetSymbol( nName ) )
				return false;
			pName = m_Strings[nName];
		}
		else if ( !GetString( pName ) )
		{
			return false;
		}
		if ( !Put( pName, Q_strlen( pName ) + 1 ) || !Copy( 16 ) )
			return false;
	}

	for ( int i = 0; i < m_nElements; ++i )
	{
		int nAttributes;
		if ( !GetInt( nAttributes ) )
			return false;
		if ( nAttributes < 0 || nAttributes > Remaining() / 3 )
			return Fail( "invalid DMX attribute count" );
		if ( !Put( &nAttributes, sizeof( nAttributes ) ) )
			return false;
		for ( int j = 0; j < nAttributes; ++j )
		{
			short nName;
			if ( !GetSymbol( nName ) || !Put( &nName, sizeof( nName ) ) || Remaining() < 1 )
				return false;
			unsigned char nType = m_pData[m_nOffset++];
			if ( !Put( &nType, sizeof( nType ) ) || !ConvertValue( nType, false ) )
				return false;
		}
	}
	if ( Remaining() != 0 )
		return Fail( "unexpected bytes after the DMX element graph" );
	return true;
}

}

// The particle library and metadata reader must own separate DMX contexts.
static bool ReadTerrorNormalizedParticleConfig( CUtlBuffer &input, bool precache,
	bool decommitTempMemory, const char *fileName )
{
#ifdef CLIENT_DLL
	// The target reader ends its context with FreeAll, even without decommit.
	// Finish that reader before allocating any graph used by the metadata pass.
	if ( !g_pParticleSystemMgr->ReadParticleConfigFile( input,
		precache, decommitTempMemory, fileName ) )
		return false;
	DECLARE_DMX_CONTEXT_DECOMMIT( decommitTempMemory );
	CUtlBuffer metadataInput( input.Base(), input.TellPut(), CUtlBuffer::READ_ONLY );
	CDmxElement *root = NULL;
	if ( !UnserializeDMX( metadataInput, &root, fileName ) || !root )
		return false;
	if ( !Q_stricmp( root->GetTypeString(), "DmeParticleSystemDefinition" ) )
	{
		ParticleMgr()->UpdateTerrorParticleDefinitionMetadata( *root );
	}
	else
	{
		const CUtlVector< CDmxElement * > &definitions = root->GetArray< CDmxElement * >( "particleSystemDefinitions" );
		for ( int i = 0; i < definitions.Count(); ++i )
		{
			CDmxElement *definition = definitions[i];
			if ( definition )
			{
				// Current file/list order wins exactly as the particle loader.
				// CUtlDict owns the name before CleanupDMX releases this graph.
				ParticleMgr()->UpdateTerrorParticleDefinitionMetadata( *definition );
			}
		}
	}
	CleanupDMX( root );
	return true;
#else
	return g_pParticleSystemMgr->ReadParticleConfigFile( input, precache,
		decommitTempMemory, fileName );
#endif
}

static bool ReadTerrorParticleConfigBuffer( CUtlBuffer &input, bool precache,
	bool decommitTempMemory, const char *fileName )
{
	const char *header = static_cast< const char * >( input.Base() );
	const char *headerEnd = static_cast< const char * >( memchr( header, 0, input.TellPut() ) );
	int encoding = 0, format = 0, headerChars = 0;
	if ( !headerEnd || sscanf( header, "<!-- dmx encoding binary %d format pcf %d -->%n",
		&encoding, &format, &headerChars ) != 2 || !headerChars || ( encoding != 3 && encoding != 4 ) )
	{
		input.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );
		return ReadTerrorNormalizedParticleConfig( input, precache, decommitTempMemory, fileName );
	}
	for ( const char *p = header + headerChars; p < headerEnd; ++p )
	{
		if ( *p != '\r' && *p != '\n' && *p != ' ' && *p != '\t' )
		{
			Warning( "Particles: Unable to normalize '%s': malformed DMX header\n", fileName );
			return false;
		}
	}
	CUtlBuffer normalized;
	normalized.PutString( CFmtStr( "<!-- dmx encoding binary 2 format pcf %d -->\n", format ) );
	CTerrorPCFReader reader( input, static_cast< int >( headerEnd - header ) + 1, encoding, normalized );
	if ( !reader.Convert() )
	{
		Warning( "Particles: Unable to normalize '%s': %s\n", fileName, reader.GetError() );
		return false;
	}
	normalized.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );
	return ReadTerrorNormalizedParticleConfig( normalized, precache, decommitTempMemory, fileName );
}

// Replace EXISTING filename overload whole; do not keep a second loader.
bool ReadTerrorParticleConfigFile( const char *fileName, bool precache, bool decommitTempMemory )
{
#ifdef CLIENT_DLL
	RegisterTerrorParticleOperators();
	RegisterTerrorParticleRenderers();
#endif
	if ( fileName[0] == '!' )
	{
		precache = true;
		++fileName;
	}
	CUtlBuffer input;
	if ( !filesystem->ReadFile( fileName, "GAME", input ) )
	{
		Warning( "Particles: Missing '%s'\n", fileName );
		return false;
	}
	return ReadTerrorParticleConfigBuffer( input, precache, decommitTempMemory, fileName );
}

// Actual live editor/tool buffer path: same normalization/table/SDK backend,
// no filename re-open, second byte copy, alternate precedence or runtime cache.
bool ReadTerrorParticleConfigFile( CUtlBuffer &input, bool precache,
	bool decommitTempMemory, const char *fileName )
{
#ifdef CLIENT_DLL
	RegisterTerrorParticleOperators();
	RegisterTerrorParticleRenderers();
#endif
	return ReadTerrorParticleConfigBuffer( input, precache, decommitTempMemory, fileName );
}

