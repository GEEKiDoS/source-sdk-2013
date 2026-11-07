//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/smoker.h"
#include "l4d2/infected/special/smoker_intention.h"
#include "l4d2/infected/special/smoker_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern int g_iNextZombieClass;
LINK_ENTITY_TO_CLASS( smoker, Smoker );
#ifndef L4D2
PRECACHE_REGISTER( smoker );
#endif
// Native map7C1C18: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Smoker )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Smoker, DT_Smoker )
END_SEND_TABLE()

// S4536B0 calls BossZombiePlayerBot4253D0 before these four allocations.
Smoker::Smoker()
{
	m_intention = new SmokerIntention( this );
	m_locomotion = new SmokerLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
// S4537E0, deleting destructor453970.
Smoker::~Smoker()
{
	delete m_locomotion; m_locomotion = NULL;
	delete m_body; m_body = NULL;
	delete m_intention; m_intention = NULL;
	delete m_vision; m_vision = NULL;
}
IIntention *Smoker::GetIntentionInterface() const { return m_intention; }
ILocomotion *Smoker::GetLocomotionInterface() const { return m_locomotion; }
IBody *Smoker::GetBodyInterface() const { return m_body; }
IVision *Smoker::GetVisionInterface() const { return m_vision; }
// S453880 and S2FD8D0. The engine allocator is the real player factory.
CBasePlayer *Smoker::AllocatePlayerEntity( edict_t *edict, const char *name )
{
	return CTerrorPlayer::CreatePlayer( "smoker", edict );
}
Smoker *CreateSmokerBot( const char *name ) { return NextBotCreatePlayerBot< Smoker >( name ); }
void Smoker::Spawn()
{
	g_iNextZombieClass = 1;
	m_body->SetArousal( IBody::INTENSE );
	BaseClass::Spawn();
}
// S453B60 is an INextBot secondary-vtable override, not a locomotion call.
bool Smoker::IsAbleToBreak( const CBaseEntity *entity ) const
{
	if ( entity->HasSpawnFlags( 0x1000 ) ) return false;
	if ( entity->HasSpawnFlags( 0x2000 ) ) return true;
	return INextBot::IsAbleToBreak( entity );
}
// S4539A0. Player precaching owns the ability, model and ordinary class assets.
void Smoker::Precache()
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) ) return;
	static const char *const sounds[] =
	{
		"SmokerZombie.Recognize", "SmokerZombie.Attack", "SmokerZombie.Breathe",
		"SmokerZombie.Death", "SmokerZombie.Explode"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i ) PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "SmokerArmsVariant", "" );
		if ( arms && arms[0] ) PrecacheModel( arms );
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x453650; 0x5c5650 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace smokerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "smoker", 0x5c5650 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "smoker", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
