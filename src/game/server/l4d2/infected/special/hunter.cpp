//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/hunter.h"
#include "l4d2/infected/special/hunter_intention.h"
#include "l4d2/infected/special/hunter_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

extern int g_iNextZombieClass;
LINK_ENTITY_TO_CLASS( hunter, Hunter );
#ifndef L4D2
PRECACHE_REGISTER( hunter );
#endif
// Native map7C0EC8: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Hunter )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Hunter, DT_Hunter )
END_SEND_TABLE()

// S43C2E0: the leaf, not BossZombiePlayerBot, owns these four components.
Hunter::Hunter()
{
	m_intention = new HunterIntention( this );
	m_locomotion = new HunterLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
Hunter::~Hunter()
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
IIntention *Hunter::GetIntentionInterface() const { return m_intention; }
ILocomotion *Hunter::GetLocomotionInterface() const { return m_locomotion; }
IBody *Hunter::GetBodyInterface() const { return m_body; }
IVision *Hunter::GetVisionInterface() const { return m_vision; }
CBasePlayer *Hunter::AllocatePlayerEntity( edict_t *edict, const char *name )
{
	return CTerrorPlayer::CreatePlayer( "hunter", edict );
}
Hunter *CreateHunterBot( const char *name )
{
	return NextBotCreatePlayerBot< Hunter >( name );
}
void Hunter::Spawn()
{
	g_iNextZombieClass = 3;
	m_body->SetArousal( IBody::INTENSE );
	BaseClass::Spawn();
}
void Hunter::Precache()
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) )
		return;
	static const char *const sounds[] =
	{
		"HunterZombie.LungeLand", "HunterZombie.Pounce", "HunterZombie.Lunge",
		"HunterZombie.Pounce.Cancel", "HunterZombie.Pounce.FlyLoop", "HunterZombie.Pounce.Miss",
		"HunterZombie.Growl", "HunterZombie.Warn", "HunterZombie.Alert", "HunterZombie.Pain"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i )
		PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "HunterArmsVariant", "" );
		if ( arms && arms[0] )
			PrecacheModel( arms );
	}
}

// Native server 0x43c280; 0x5c5590 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace hunterPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "hunter", 0x5c5590 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "hunter", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
