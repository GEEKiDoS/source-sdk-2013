//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_JOCKEY_H
#define L4D2_JOCKEY_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/boss_zombie_player_bot.h"
class JockeyIntention;
class JockeyLocomotion;
class PlayerBody;
class ZombieBotVision;

class Jockey : public BossZombiePlayerBot
{
public:
	DECLARE_CLASS( Jockey, BossZombiePlayerBot );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	Jockey();
	virtual ~Jockey();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsZombieClass( int zombieClass ) const OVERRIDE { return zombieClass == 5; }
	virtual IIntention *GetIntentionInterface() const OVERRIDE;
	virtual ILocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual IBody *GetBodyInterface() const OVERRIDE;
	virtual IVision *GetVisionInterface() const OVERRIDE;
private:
	JockeyIntention *m_intention;
	JockeyLocomotion *m_locomotion;
	PlayerBody *m_body;
	ZombieBotVision *m_vision;
};
Jockey *CreateJockeyBot( const char *name );
extern ConVar z_jockey_leap_range;
#endif // L4D2_JOCKEY_H
