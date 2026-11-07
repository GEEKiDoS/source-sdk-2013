//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/world_logic_director_audio.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( ambient_music, CAmbientMusic );

BEGIN_DATADESC( CAmbientMusic )
	DEFINE_KEYFIELD( m_iszSound, FIELD_SOUNDNAME, "message" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "PlaySound", InputPlaySound ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StopSound", InputStopSound ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( sound_mix_layer, CSoundMixLayer );

IMPLEMENT_SERVERCLASS_ST( CSoundMixLayer, DT_SoundMixLayer )
	SendPropStringT( SENDINFO( m_iszMixLayerName ) ),
	SendPropFloat( SENDINFO( m_fMixLayerLevel ), 8, 0, 0.0f, 1.0f ),
END_SEND_TABLE()

BEGIN_DATADESC( CSoundMixLayer )
	DEFINE_KEYFIELD( m_iszMixLayerName, FIELD_STRING, "MixLayerName" ),
	DEFINE_KEYFIELD( m_fMixLayerLevel, FIELD_FLOAT, "Level" ),
	DEFINE_THINKFUNC( Think ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "Level", InputSetLevel ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( logic_director_query, CLogicDirectorQuery );

BEGIN_DATADESC( CLogicDirectorQuery )
	DEFINE_KEYFIELD( m_nMinRemapVal, FIELD_INTEGER, "minAngerRange" ),
	DEFINE_KEYFIELD( m_nMaxRemapVal, FIELD_INTEGER, "maxAngerRange" ),
	DEFINE_KEYFIELD( m_nNoise, FIELD_INTEGER, "noise" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "HowAngry", InputHowAngry ),
	DEFINE_OUTPUT( m_OutAnger, "OutAnger" ),
	DEFINE_OUTPUT( m_On20SecondsToMob, "On20SecondsToMob" ),
	DEFINE_OUTPUT( m_On60SecondsToMob, "On60SecondsToMob" ),
	DEFINE_THINKFUNC( QueryThink ),
END_DATADESC()


void CAmbientMusic::Precache()
{}

void CAmbientMusic::Spawn()
{
		BaseClass::Spawn();
		SetSolid( SOLID_NONE );
		SetMoveType( MOVETYPE_NONE );
		Precache();
	}

void CAmbientMusic::InputPlaySound( inputdata_t &inputdata )
{
		DevMsg( "InputPlaySound: %s : %i\n", STRING( m_iszSound ), entindex() );
		SendMusic( false );
	}

void CAmbientMusic::InputStopSound( inputdata_t &inputdata )
{ SendMusic( true ); }

void CAmbientMusic::SendMusic( bool stop )
{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *base = UTIL_PlayerByIndex( i );
			CTerrorPlayer *player = base && base->IsPlayer() ? dynamic_cast< CTerrorPlayer * >( base ) : NULL;
			if ( !player || !player->edict() || !player->entindex() || player->State_Get() == STATE_PICKINGTEAM || !player->IsAlive() ) continue;
			if ( stop ) player->GetMusic()->Stop( STRING( m_iszSound ), -1.0f, false );
			else player->GetMusic()->Play( STRING( m_iszSound ), entindex(), -1.0f, false, true );
		}
	}

CSoundMixLayer::CSoundMixLayer()
{
		m_iszMixLayerName = NULL_STRING;
		AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
		DispatchUpdateTransmitState();
	}

void CSoundMixLayer::Precache()
{}

void CSoundMixLayer::Think()
{}

int CSoundMixLayer::UpdateTransmitState()
{ return SetTransmitState( FL_EDICT_ALWAYS ); }

void CSoundMixLayer::Spawn()
{
		BaseClass::Spawn();
		SetThink( &CSoundMixLayer::Think );
		SetNextThink( gpGlobals->curtime + 0.01f );
		Precache();
	}

void CSoundMixLayer::InputSetLevel( inputdata_t &inputdata )
{
		m_fMixLayerLevel = inputdata.value.FieldType() == FIELD_FLOAT ? inputdata.value.Float() : 0.0f;
	}

void CLogicDirectorQuery::Spawn()
{
		BaseClass::Spawn();
		m_bWithin20 = m_bWithin60 = false;
		SetThink( &CLogicDirectorQuery::QueryThink );
		SetNextThink( 0.1f ); // Native ABSOLUTE time, deliberately not curtime+.1.
	}

void CLogicDirectorQuery::QueryThink()
{
		const float remaining = static_cast< float >( TheDirector->GetMobSpawnTimeRemaining() );
		if ( m_bWithin20 ) { if ( remaining > 20.0f ) m_bWithin20 = false; }
		else if ( remaining <= 20.0f ) { m_On20SecondsToMob.FireOutput( this, this ); m_bWithin20 = true; }
		if ( m_bWithin60 ) { if ( remaining > 60.0f ) m_bWithin60 = false; }
		else if ( remaining <= 60.0f ) { m_On60SecondsToMob.FireOutput( this, this ); m_bWithin60 = true; }
		SetNextThink( 0.2f ); // Native2631EF's past-deadline per-frame quirk.
	}


static int QueryTruncate( float value ) { return _mm_cvtt_ss2si( _mm_set_ss( value ) ); }
void CLogicDirectorQuery::InputHowAngry( inputdata_t &inputdata )
{
	if ( TerrorGameRules()->IsVersusMode() || TerrorGameRules()->IsScavengeMode() )
	{
		m_OutAnger.Set( m_nMinRemapVal, inputdata.pActivator, this );
		return;
	}
	float health = 0.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		CTerrorPlayer *player = base && base->IsPlayer() ? dynamic_cast< CTerrorPlayer * >( base ) : NULL;
		if ( !player || !player->edict() || !player->entindex() || player->State_Get() == STATE_PICKINGTEAM ||
			!player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || player->IsIncapacitated() ) continue;
		health += static_cast< float >( player->GetHealth() );
	}
	float fraction = ( health * 0.25f ) * 0.01f;
	if ( fraction >= 0.0f ) { if ( fraction > 1.0f ) fraction = 1.0f; }
	else fraction = 0.0f;
	const float trueAnger = fraction * 49.0f + 1.0f;
	int anger = QueryTruncate( trueAnger );
	const int random = RandomInt( 1, 100 );
	if ( random <= m_nNoise )
	{
		float noise = ( static_cast< float >( random ) - 1.0f ) * 0.01010101f;
		if ( noise >= 0.0f ) { if ( noise > 1.0f ) noise = 1.0f; }
		else noise = 0.0f;
		anger = QueryTruncate( noise * 50.0f );
	}
	const float remapped = ( ( static_cast< float >( m_nMaxRemapVal ) - static_cast< float >( m_nMinRemapVal ) ) *
		( static_cast< float >( anger ) - 1.0f ) ) * 0.020408163f + static_cast< float >( m_nMinRemapVal );
	int output = QueryTruncate( remapped );
	if ( remapped - static_cast< float >( output ) >= 0.5f && output < m_nMaxRemapVal ) ++output;
	Msg( "TRUE Anger: %3.2f  Processed Anger: %3d  Out Anger: %3d\n", trueAnger, anger, output );
	m_OutAnger.Set( output, inputdata.pActivator, this );
}
