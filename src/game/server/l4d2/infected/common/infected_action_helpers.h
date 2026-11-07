//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFECTED_ACTION_HELPERS_H
#define L4D2_INFECTED_ACTION_HELPERS_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotBehavior.h"
#include "NextBot/NextBotBodyInterface.h"
class Infected;

// S:415670/4152F0/4153D0. The successor is owned until ChangeTo transfers it.
class InfectedBlinded : public Action< Infected >
{
public:
	InfectedBlinded( Action< Infected > *successor = NULL, const char *sound = "Zombie.Blinded" );
	virtual ~InfectedBlinded();
	virtual const char *GetName() const OVERRIDE { return "InfectedBlinded"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnBlinded( Infected *me, CBaseEntity *blinder ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE { return TryToSustain(); }
private:
	Action< Infected > *m_successor;
	const char *m_sound;
	float m_yawDrift;
	CountdownTimer m_voiceTimer;
	CountdownTimer m_blindTimer;
};

// S:418780. The timer starts at construction, not at OnStart.
class InfectedStandingActivity : public Action< Infected >
{
public:
	InfectedStandingActivity( Activity activity, float duration = -1.0f, Action< Infected > *successor = NULL );
	virtual ~InfectedStandingActivity();
	virtual const char *GetName() const OVERRIDE { return "InfectedStandingActivity"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE;
private:
	Activity m_activity;
	CountdownTimer m_timer;
	Action< Infected > *m_successor;
	bool m_complete;
};
#endif
