//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/entities/survivor_position.h"
#include "l4d2/player/terrorplayer.h"
#include "entitylist.h"
#include "l4d2/player/terror_playeranimstate.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_survivor_position, CSurvivorPosition );

IMPLEMENT_SERVERCLASS_ST( CSurvivorPosition, DT_SurvivorPosition )
	SendPropInt( SENDINFO( m_order ), 4, SPROP_UNSIGNED ),
END_SEND_TABLE()

BEGIN_DATADESC( CSurvivorPosition )
	DEFINE_INPUTFUNC( FIELD_STRING, "SetViewControl", InputSetViewControl ),
	DEFINE_KEYFIELD( m_order, FIELD_INTEGER, "Order" ),
	DEFINE_KEYFIELD( m_iszSurvivorName, FIELD_STRING, "SurvivorName" ),
	DEFINE_KEYFIELD( m_iszSurvivorIntroSequence, FIELD_STRING, "SurvivorIntroSequence" ),
	DEFINE_KEYFIELD( m_iszGameMode, FIELD_STRING, "GameMode" ),
	DEFINE_KEYFIELD( m_iszSurvivorConcept, FIELD_STRING, "SurvivorConcept" ),
	DEFINE_KEYFIELD( m_bHideWeapons, FIELD_BOOLEAN, "HideWeapons" ),
END_DATADESC()

CSurvivorPosition::CSurvivorPosition()
{
	m_order = 0;
	m_iszSurvivorName = NULL_STRING;
	m_iszSurvivorIntroSequence = NULL_STRING;
	m_iszGameMode = NULL_STRING;
	m_iszSurvivorConcept = NULL_STRING;
	m_bHideWeapons = false;
}

int CSurvivorPosition::GetOrder() const
{
	return m_order;
}

const char *CSurvivorPosition::GetSurvivorName() const
{
	return m_iszSurvivorName != NULL_STRING ? STRING( m_iszSurvivorName ) : NULL;
}

const char *CSurvivorPosition::GetGameMode() const
{
	return m_iszGameMode != NULL_STRING ? STRING( m_iszGameMode ) : NULL;
}

const char *CSurvivorPosition::GetSurvivorIntroSequence() const
{
	// S2CC3C0 preserves NULL separately from a non-NULL empty string.
	return m_iszSurvivorIntroSequence != NULL_STRING ? STRING( m_iszSurvivorIntroSequence ) : NULL;
}

const char *CSurvivorPosition::GetSurvivorConcept() const
{
	// S2CC3D0 is the response concept, not a scene-file name.
	return m_iszSurvivorConcept != NULL_STRING ? STRING( m_iszSurvivorConcept ) : NULL;
}

void CSurvivorPosition::InputSetViewControl( inputdata_t &inputdata )
{
	// S2CC530 re-resolves the attached player after actual name lookup.
	if ( !m_attachedSurvivor.Get() )
		return;
	CBaseEntity *pViewControl = gEntList.FindEntityByName( NULL, inputdata.value.String() );
	if ( pViewControl )
	{
		variant_t value;
		pViewControl->AcceptInput( "Enable", m_attachedSurvivor.Get(), NULL, value, 0 );
	}
}

CBaseEntity *CTerrorPlayer::GetSurvivorPosition() const
{
	// S2C7C60: resolve the sole handle, without type/alive/self filtering.
	return m_positionEntity.Get();
}

void CTerrorPlayer::SetSurvivorPosition( CSurvivorPosition *position )
{
	// S3170F0. No new alive/current-owner/camera safeguards are admitted.
	CBaseEntity *old = m_positionEntity.Get();
	CSurvivorPosition *oldPosition = dynamic_cast< CSurvivorPosition * >( old );
	if ( oldPosition )
		oldPosition->SetAttachedSurvivor( NULL );
	SetParent( NULL );
	if ( position )
	{
		Teleport( &position->GetAbsOrigin(), &vec3_angle, &vec3_origin );
		EnableControl( false );
		SetParent( position );
		position->SetAttachedSurvivor( this );
		m_Local.m_iHideHUD |= 0x77B;
		m_bSurvivorPositionHidingWeapons = position->ShouldHideWeapons();
	}
	else
	{
		EnableControl( true );
		if ( old )
			Teleport( &old->GetAbsOrigin(), &old->GetAbsAngles(), &vec3_origin );
		m_Local.m_iHideHUD &= ~0x77B;
		m_bSurvivorPositionHidingWeapons = false;
	}
	m_positionEntity = position;
}

void CSurvivorPosition::SetAttachedSurvivor( CTerrorPlayer *player )
{
	m_attachedSurvivor = player;
}

void CTerrorPlayer::InputTeleportToSurvivorPosition( inputdata_t &inputdata )
{
	// S3188A0: regular entity-name lookup, then actual position RTTI.
	const char *name = inputdata.value.FieldType() == FIELD_STRING ? inputdata.value.String() : "";
	if ( !name )
		name = "";
	CSurvivorPosition *position = dynamic_cast< CSurvivorPosition * >( gEntList.FindEntityByName( NULL, name ) );
	if ( !position )
		return;
	SetSurvivorPosition( position );
	if ( position->GetSurvivorIntroSequence() )
	{
		const int sequence = LookupSequence( position->GetSurvivorIntroSequence() );
		if ( sequence != -1 )
			GetTerrorAnimState()->ForceSequenceOverride( sequence );
	}
	else if ( position->GetSurvivorConcept() )
	{
		SpeakIfAllowed( position->GetSurvivorConcept(), 3, NULL, NULL, 0, NULL );
	}
}

void CTerrorPlayer::InputReleaseFromSurvivorPosition( inputdata_t &inputdata )
{
	// S318980: detach first, then only the genuine sequence-lock byte.
	SetSurvivorPosition( NULL );
	GetTerrorAnimState()->ClearSequenceOverride();
}
