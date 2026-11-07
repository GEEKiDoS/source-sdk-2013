//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/demo/server_demo_recording_file.h"
#include "l4d2/demo/server_demo_recording_state.h"
#include "tier0/memalloc.h"
#include "l4d2/demo/server_demo_recording_file_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "l4d2/demo/server_demo_system.h"
#endif
#include "tier0/memdbgon.h"

// S:502B10/502750. CServerDemoFile always requests the actual 64 MiB branch,
// not the three unrelated small-buffer pools. Preserve its read/write/used
// words, wrap order and overflow behavior; do not replace it with CUtlBuffer.
struct ServerDemoCircularBuffer_t
{
	int m_nUsed;
	int m_nRead;
	int m_nWrite;
	int m_nCapacity;
	unsigned char m_Data[1];

	static ServerDemoCircularBuffer_t *Create()
	{
		const int capacity = 0x4000000;
		ServerDemoCircularBuffer_t *buffer = static_cast< ServerDemoCircularBuffer_t * >(
			g_pMemAlloc->Alloc( capacity + sizeof( ServerDemoCircularBuffer_t ) - 1 ) );
		// The native allocator helper also initializes this header BEFORE its
		// caller's null test. No invented allocation-failure fallback here.
		buffer->m_nCapacity = capacity;
		buffer->m_nRead = buffer->m_nWrite = buffer->m_nUsed = 0;
		return buffer;
	}

	int GetFreeSpace() const { return m_nCapacity - m_nUsed; }
	void Write( const void *data, int size )
	{
		const unsigned char *source = static_cast< const unsigned char * >( data );
		int remaining = size;
		if ( size )
		{
			do
			{
				int end = m_nWrite + remaining;
				if ( end >= m_nCapacity ) end = m_nCapacity;
				int count = end - m_nWrite;
				Q_memcpy( m_Data + m_nWrite, source, count );
				source += count;
				remaining -= count;
				m_nWrite = end % m_nCapacity;
			}
			while ( remaining );
		}
		if ( size <= m_nCapacity - m_nUsed ) m_nUsed += size;
		else
		{
			m_nUsed = m_nCapacity;
			m_nRead = m_nWrite;
		}
	}
};

CServerDemoFile::CServerDemoFile() : m_pBuffer( NULL ), m_flStartTime( 0.0f )
{
	// Native1CF150 does not initialize the retained 256-byte map-name array.
}

CServerDemoFile::~CServerDemoFile()
{
	// Native502BA0 unconditionally reads the buffer capacity before freeing.
	// This file owner only creates the 64 MiB direct-allocation branch.
	const volatile int capacity = m_pBuffer->m_nCapacity;
	(void)capacity;
	g_pMemAlloc->Free( m_pBuffer );
	// The actual CUtlVector<CUtlString> then destroys its owned entries/storage.
}

bool CServerDemoFile::Init( const char *mapName, float startTime )
{
	m_pBuffer = ServerDemoCircularBuffer_t::Create();
	if ( !m_pBuffer ) return false; // Native1CDF40's original guard.
	Q_strncpy( m_szMapName, mapName, sizeof( m_szMapName ) );
	m_flStartTime = startTime;
	return true;
}

int CServerDemoFile::FindOrAddModelName( const char *modelName )
{
	if ( !modelName || !*modelName ) return -1;
	for ( int i = 0; i < m_ModelNames.Count(); ++i )
	{
		if ( !Q_strcmp( modelName, m_ModelNames[i].Get() ) ) return i;
	}
	return m_ModelNames.AddToTail( CUtlString( modelName ) );
}

void CServerDemoFile::WriteBaseEntity( KeyValues *message )
{
	ServerDemoBaseEntityState_t *state = static_cast< ServerDemoBaseEntityState_t * >( message->GetPtr( "baseentity" ) );
	if ( !state ) return; // S:1CE5B0's actual optional-payload guard.
	m_pBuffer->Write( &state->m_nFieldMask, 4 );
	unsigned int mask = state->m_nFieldMask;
	if ( mask & 1 ) m_pBuffer->Write( &state->m_nEntityIndex, 4 );
	if ( mask & 2 ) m_pBuffer->Write( &state->m_nModelName, 2 );
	if ( mask & 4 ) m_pBuffer->Write( &state->m_nOwner, 4 );
	if ( mask & 0x10 ) m_pBuffer->Write( &state->m_bVisible, 1 );
	if ( mask & 0x40 ) m_pBuffer->Write( &state->m_vecOrigin, 12 );
	if ( mask & 0x80 ) m_pBuffer->Write( &state->m_angAngles, 12 );
	if ( mask & 0x20 ) m_pBuffer->Write( &state->m_bFinalVisibleSample, 1 );
	if ( mask & 0x400 ) m_pBuffer->Write( &state->m_nFollowParentIndex, 4 );
	// Native1CDF90 has NO effects/bit8 payload write.
}

void CServerDemoFile::WriteBaseAnimating( KeyValues *message )
{
	ServerDemoAnimatingState_t *state = static_cast< ServerDemoAnimatingState_t * >( message->GetPtr( "baseanimating" ) );
	if ( !state ) return;
	m_pBuffer->Write( &state->m_nFieldMask, 8 );
	unsigned long long mask = state->m_nFieldMask;
	if ( mask & 1 ) m_pBuffer->Write( &state->m_nSkin, 2 );
	if ( mask & 2 ) m_pBuffer->Write( &state->m_nBody, 2 );
	if ( mask & 4 ) m_pBuffer->Write( &state->m_nSequence, 2 );
	if ( mask & 8 ) m_pBuffer->Write( &state->m_bClearIKTargets, 1 );
	if ( mask & 0x10 ) m_pBuffer->Write( &state->m_bRagdoll, 1 );
	if ( mask & 0x20 ) m_pBuffer->Write( &state->m_bCreateIKContext, 1 );
	if ( mask & 0x40 ) m_pBuffer->Write( &state->m_bNoInterpolation, 1 );
	if ( mask & 0x80 ) m_pBuffer->Write( &state->m_flCycle, 4 );
	if ( mask & 0x100 ) m_pBuffer->Write( &state->m_flPlaybackRate, 4 );
	if ( mask & 0x200 ) m_pBuffer->Write( &state->m_nFrameCount, 4 );
	if ( mask & 0x100000 )
	{
		unsigned short bytes = (unsigned short)( 28 * ( state->m_Bones.m_nBoneCountAndOwnership & 0x7FFF ) + 2 );
		m_pBuffer->Write( &bytes, 2 );
		m_pBuffer->Write( &state->m_Bones, bytes );
	}
	if ( mask & 0x400 ) m_pBuffer->Write( &state->m_nPoseParameters, 1 );
	for ( int i = 0; i < state->m_nPoseParameters; ++i )
	{
		// Native shifts a 32-bit 0x800, then SIGN-extends the result to 64 bits.
		unsigned int bits = 0x800u << ( i & 31 );
		int signedBits;
		Q_memcpy( &signedBits, &bits, sizeof( signedBits ) );
		if ( mask & (unsigned long long)(long long)signedBits ) m_pBuffer->Write( &state->m_PoseParameters[i], 4 );
	}
}

void CServerDemoFile::WriteBaseAnimatingOverlay( KeyValues *message )
{
	ServerDemoAnimatingOverlayState_t *state = static_cast< ServerDemoAnimatingOverlayState_t * >( message->GetPtr( "baseanimatingoverlay" ) );
	if ( !state ) return;
	m_pBuffer->Write( &state->m_nFieldMask, 8 );
	if ( state->m_nFieldMask & 1 ) m_pBuffer->Write( &state->m_nLayers, 1 );
	for ( int i = 0; i < state->m_nLayers; ++i )
	{
		// Preserve1CEBC0's byte-truncated shifted masks and nine-bit stride.
		unsigned int bit = 2u << ( ( 9 * i ) & 31 );
		if ( state->m_nFieldMask & (unsigned char)( 2u * bit ) ) m_pBuffer->Write( &state->m_Layers[i].m_flCycle, 4 );
		if ( state->m_nFieldMask & (unsigned char)( 4u * bit ) ) m_pBuffer->Write( &state->m_Layers[i].m_nSequence, 4 );
		if ( state->m_nFieldMask & (unsigned char)( 16u * bit ) ) m_pBuffer->Write( &state->m_Layers[i].m_flWeight, 4 );
	}
}

void CServerDemoFile::WriteFixedRecord( KeyValues *message, const char *key, unsigned int size )
{
	void *state = message->GetPtr( key );
	if ( !state ) return; // Native1CE620/690/700's original optional-payload guard.
	unsigned int prefix[2] = { size, 0xFFFFFFFF };
	Q_memcpy( state, prefix, sizeof( prefix ) );
	m_pBuffer->Write( state, size );
}

void CServerDemoFile::RecordMessage( KeyValues *message, float time )
{
	if ( !m_pBuffer ) return;
	if ( m_pBuffer->GetFreeSpace() < 500 )
	{
		Warning( "Server demo buffer ran out...\n" );
		return;
	}
	struct RecordHeader
	{
		unsigned int magic;
		signed char type;
		unsigned char reserved[3];
		float deltaTime;
	};
	COMPILE_TIME_ASSERT( sizeof( RecordHeader ) == 12 );
	static RecordHeader header = { (unsigned int)-1159983106, 0, { 0, 0, 0 }, 0.0f };
	header.type = (signed char)message->GetInt( "type", 0 );
	header.deltaTime = time - m_flStartTime;
	m_pBuffer->Write( &header, sizeof( header ) );
	switch ( header.type )
	{
	case 7:
		WriteFixedRecord( message, "camera", 40 );
		// Intentional native fall-through through every base payload.
	case 6:
		WriteFixedRecord( message, "baseflex", 28 );
	case 5:
		WriteBaseAnimatingOverlay( message );
	case 4:
		WriteBaseAnimating( message );
	case 2:
		WriteBaseEntity( message );
		break;
	case 10:
		WriteFixedRecord( message, "particlesystem_created", 276 );
		break;
	default:
		break; // Native writes the header even for an unsupported type.
	}
}

#include "l4d2/demo/server_demo_recording_file_console_server_callbacks.inc"
