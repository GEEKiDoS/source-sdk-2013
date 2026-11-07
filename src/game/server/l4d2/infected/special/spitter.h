//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SPITTER_H
#define L4D2_SPITTER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class SpitterIntention;
class SpitterLocomotion;
class PlayerBody;
class ZombieBotVision;
class Spitter : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Spitter, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Spitter();
	virtual ~Spitter();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 4; }
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
	CTerrorPlayer *GetSpitTarget() const { return m_spitTarget.Get(); } // S3662E0; native leaf field17216
	void SetSpitTarget( CTerrorPlayer *target ) { m_spitTarget = target; }
private:
	PlayerBody *m_body;
	SpitterIntention *m_intention;
	SpitterLocomotion *m_locomotion;
	ZombieBotVision *m_vision;
	CHandle< CTerrorPlayer > m_spitTarget;
};
Spitter *CreateSpitterBot( const char *name );
#endif // L4D2_SPITTER_H
