//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/spitter.h"
#include "l4d2/infected/special/spitter_intention.h"
#include "l4d2/infected/special/spitter_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "particle_parse.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
extern int g_iNextZombieClass;
LINK_ENTITY_TO_CLASS( spitter, Spitter );
#ifndef L4D2
PRECACHE_REGISTER( spitter );
#endif
// Native map7C23E8: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Spitter )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Spitter, DT_Spitter )
END_SEND_TABLE()
Spitter::Spitter() // S45F330, base4253D0
{
	m_spitTarget = NULL;
	m_intention = new SpitterIntention( this );
	m_locomotion = new SpitterLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
Spitter::~Spitter() // S45F140 / scalar deleting45F400
{
	delete m_locomotion; m_locomotion = NULL;
	delete m_body; m_body = NULL;
	delete m_intention; m_intention = NULL;
	delete m_vision; m_vision = NULL;
}
IIntention *Spitter::GetIntentionInterface() const { return m_intention; }
ILocomotion *Spitter::GetLocomotionInterface() const { return m_locomotion; }
IBody *Spitter::GetBodyInterface() const { return m_body; }
IVision *Spitter::GetVisionInterface() const { return m_vision; }
CBasePlayer *Spitter::AllocatePlayerEntity( edict_t *edict, const char *name ) // S45F240 allocator callback
{
	return CTerrorPlayer::CreatePlayer( "Spitter", edict );
}
Spitter *CreateSpitterBot( const char *name ) // S2FD6D0
{
	return NextBotCreatePlayerBot< Spitter >( name );
}
void Spitter::Spawn() // S45F260; does not reset the native spit-target handle
{
	g_iNextZombieClass = 4;
	m_body->SetArousal( IBody::INTENSE );
	BaseClass::Spawn();
}
void Spitter::Precache() // S45F430
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) ) return;
	PrecacheParticleSystem( "spitter_bugged" );
	PrecacheParticleSystem( "spitter_projectile" );
	PrecacheParticleSystem( "spitter_areaofdenial" );
	static const char *const sounds[] = { "SpitterZombie.Voice", "SpitterZombie.Pain", "SpitterZombie.PainShort", "SpitterZombie.Recognize", "SpitterZombie.Alert", "SpitterZombie.Death", "SpitterZombie.Spit" };
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i ) PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "SpitterArmsVariant", "" );
		if ( arms && arms[0] ) PrecacheModel( arms );
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x45f0e0; 0x5c5694 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace spitterPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "spitter", 0x5c5694 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "spitter", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
