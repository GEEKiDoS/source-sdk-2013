//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_ENVIRONMENT_H
#define TERROR_ENVIRONMENT_H
#ifdef _WIN32
#pragma once
#endif

#include "triggers.h"
#include "playernet_vars.h"

class CBasePlayer;

// Original trigger_fog, not a player-only fog_volume or a NAV fog source.
class CFogTrigger : public CBaseTrigger
{
	DECLARE_CLASS( CFogTrigger, CBaseTrigger );
public:
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual void StartTouch( CBaseEntity *other ) OVERRIDE;
	virtual void EndTouch( CBaseEntity *other ) OVERRIDE;
	const fogparams_t &GetFog() const { return m_fog; }
private:
	fogparams_t m_fog;
};

class CPostProcessController : public CBaseEntity
{
	DECLARE_CLASS( CPostProcessController, CBaseEntity );
public:
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CPostProcessController();
	virtual void Spawn() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	void InputSetLocalContrastStrength( inputdata_t &data );
	void InputSetLocalContrastEdgeStrength( inputdata_t &data );
	void InputSetVignetteStart( inputdata_t &data );
	void InputSetVignetteEnd( inputdata_t &data );
	void InputSetVignetteBlurStrength( inputdata_t &data );
	void InputSetFadeToBlackStrength( inputdata_t &data );
	void InputSetGrainStrength( inputdata_t &data );
	void InputSetTopVignetteStrength( inputdata_t &data );
	void InputSetFadeTime( inputdata_t &data );
private:
	CNetworkVar( float, m_fLocalContrastStrength );
	CNetworkVar( float, m_fLocalContrastEdgeStrength );
	CNetworkVar( float, m_fVignetteStart );
	CNetworkVar( float, m_fVignetteEnd );
	CNetworkVar( float, m_fVignetteBlurStrength );
	CNetworkVar( float, m_fFadeToBlackStrength );
	CNetworkVar( float, m_fGrainStrength );
	CNetworkVar( float, m_fTopVignetteStrength );
	CNetworkVar( float, m_fFadeTime );
	CNetworkVar( bool, m_bMaster );
};

CPostProcessController *Terror_GetMasterPostProcessController();
CBaseEntity *Terror_GetMasterColorCorrection();
// Defined alongside the existing CColorCorrection so the original RTTI check
// remains a typed entity lookup rather than accepting an arbitrary named entity.
CBaseEntity *Terror_FindColorCorrectionByName( const char *name );
CBaseEntity *Terror_CastColorCorrection( CBaseEntity *entity );

// Original CTerrorPlayer::UpdateFogVolume, S349D20; called by the real scheduled
// expensive PostThink path, not by a new independently-timed game system.
void Terror_UpdatePlayerEnvironment( CBasePlayer *player );

#endif // TERROR_ENVIRONMENT_H
