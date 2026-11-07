//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CS_RAGDOLL_H
#define L4D2_CS_RAGDOLL_H
#ifdef _WIN32
#pragma once
#endif

#include "baseanimatingoverlay.h"

class CCSPlayer;

class CCSRagdoll : public CBaseAnimatingOverlay
{
public:
	DECLARE_CLASS( CCSRagdoll, CBaseAnimatingOverlay );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CCSRagdoll();
	virtual int ObjectCaps() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	void PruneThink();
	void InitializeFromPlayer();

	CNetworkHandle( CCSPlayer, m_hPlayer );
	CNetworkVector( m_vecRagdollVelocity );
	CNetworkVector( m_vecRagdollOrigin );
	CNetworkVar( int, m_iDeathPose );
	CNetworkVar( int, m_iDeathFrame );
	CNetworkVar( bool, m_bOnFire );
	CNetworkVar( bool, m_bInterpOrigin );
	CNetworkVar( int, m_ragdollType );
	CNetworkVar( int, m_zombieClass );
	CNetworkVar( int, m_survivorCharacter );
};

#endif // L4D2_CS_RAGDOLL_H
