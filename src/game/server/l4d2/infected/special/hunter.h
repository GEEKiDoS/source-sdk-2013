//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_HUNTER_H
#define L4D2_HUNTER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class HunterIntention;
class HunterLocomotion;
class PlayerBody;
class ZombieBotVision;

class Hunter : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Hunter, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Hunter();
	virtual ~Hunter();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 3; }
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
private:
	HunterIntention *m_intention;
	HunterLocomotion *m_locomotion;
	PlayerBody *m_body;
	ZombieBotVision *m_vision;
};
Hunter *CreateHunterBot( const char *name );
#endif // L4D2_HUNTER_H
