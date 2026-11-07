//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/jockey.h"
#include "l4d2/infected/special/jockey_intention.h"
#include "l4d2/infected/special/jockey_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

extern int g_iNextZombieClass;
LINK_ENTITY_TO_CLASS( jockey, Jockey );
#ifndef L4D2
PRECACHE_REGISTER( jockey );
#endif
// Native map7C15D0: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Jockey )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Jockey, DT_Jockey )
END_SEND_TABLE()

// S447390/S4474C0: same ownership and deletion order as the Hunter leaf.
Jockey::Jockey()
{
	m_intention = new JockeyIntention( this );
	m_locomotion = new JockeyLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
Jockey::~Jockey()
{
	delete m_locomotion;
	m_locomotion = NULL;
	delete m_body;
	m_body = NULL;
	delete m_intention;
	m_intention = NULL;
	delete m_vision;
	m_vision = NULL;
}
IIntention *Jockey::GetIntentionInterface() const { return m_intention; }
ILocomotion *Jockey::GetLocomotionInterface() const { return m_locomotion; }
IBody *Jockey::GetBodyInterface() const { return m_body; }
IVision *Jockey::GetVisionInterface() const { return m_vision; }
CBasePlayer *Jockey::AllocatePlayerEntity( edict_t *edict, const char *name )
{
	return CTerrorPlayer::CreatePlayer( "jockey", edict );
}
Jockey *CreateJockeyBot( const char *name )
{
	return NextBotCreatePlayerBot< Jockey >( name );
}
void Jockey::Spawn()
{
	g_iNextZombieClass = 5;
	m_body->SetArousal( IBody::INTENSE );
	BaseClass::Spawn();
}
void Jockey::Precache()
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) )
		return;
	static const char *const sounds[] =
	{
		"JockeyZombie.Growl", "JockeyZombie.Warn", "JockeyZombie.Alert", "JockeyZombie.Pain",
		"JockeyZombie.PainShort", "JockeyZombie.Voice", "JockeyZombie.Ride", "JockeyZombie.Death",
		"JockeyZombie.Recognize", "Zombie.Punch"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i )
		PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "JockeyArmsVariant", "" );
		if ( arms && arms[0] )
			PrecacheModel( arms );
	}
}

// Native server 0x447330; 0x5c55e0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace jockeyPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "jockey", 0x5c55e0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "jockey", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
