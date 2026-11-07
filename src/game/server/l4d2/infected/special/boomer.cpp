//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/boomer.h"
#include "l4d2/infected/special/boomer_intention.h"
#include "l4d2/infected/special/boomer_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "particle_parse.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
extern int g_iNextZombieClass;
ConVar boomer_leaker_chance( "boomer_leaker_chance", "0", 0x00004004 ); // S5A8960
LINK_ENTITY_TO_CLASS( boomer, Boomer );
#ifndef L4D2
PRECACHE_REGISTER( boomer );
#endif
// Native map7BFF48: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Boomer )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Boomer, DT_Boomer )
END_SEND_TABLE()

// S426100, base4253D0. These are the leaf's four real components.
Boomer::Boomer()
{
	m_intention = new BoomerIntention( this );
	m_locomotion = new BoomerLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
Boomer::~Boomer() // S425CA0 / scalar deleting425EB0
{
	delete m_locomotion; m_locomotion = NULL;
	delete m_body; m_body = NULL;
	delete m_intention; m_intention = NULL;
	delete m_vision; m_vision = NULL;
}
IIntention *Boomer::GetIntentionInterface() const { return m_intention; }
ILocomotion *Boomer::GetLocomotionInterface() const { return m_locomotion; }
IBody *Boomer::GetBodyInterface() const { return m_body; }
IVision *Boomer::GetVisionInterface() const { return m_vision; }
CBasePlayer *Boomer::AllocatePlayerEntity( edict_t *edict, const char *name ) // S425DA0 allocator callback
{
	return CTerrorPlayer::CreatePlayer( "boomer", edict );
}
Boomer *CreateBoomerBot( const char *name ) // S2FD9D0
{
	return NextBotCreatePlayerBot< Boomer >( name );
}
void Boomer::Spawn() // S426420
{
	g_iNextZombieClass = 2;
	m_body->SetArousal( IBody::INTENSE );
	// RNG is consumed even when the existing subtype is already1. There is
	// intentionally no else resetting it: full player Spawn owns that state.
	if ( boomer_leaker_chance.GetFloat() >= (float)RandomInt( 1, 100 ) && m_nVariantType != 1 )
		m_nVariantType = 1;
	BaseClass::Spawn();
	m_leakTimer.Start( 0.1f );
	EmitInfectedSound( "BoomerZombie.Groan", RandomFloat( 2.0f, 3.0f ), 0.0f );
}
void Boomer::Precache() // S425EE0
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) ) return;
	PrecacheParticleSystem( "blood_impact_arterial_spray" );
	static const char *const sounds[] = { "BoomerZombie.Groan", "BoomerZombie.Voice", "BoomerZombie.Gurgle", "BoomerZombie.Rage", "BoomerZombie.Attack", "BoomerZombie.Detonate", "BoomerZombie.Fall", "BoomerZombie.Warn" };
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i ) PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "BoomerArmsVariant", "" );
		if ( arms && arms[0] ) PrecacheModel( arms );
	}
}
int Boomer::OnTakeDamage( const CTakeDamageInfo &info ) // S426060, before player damage processing.
{
	if ( m_nVariantType == 1 && info.GetAttacker() != this ) return 0;
	return CTerrorPlayer::OnTakeDamage( info );
}
void Boomer::SpawnTraceBlood( float damage, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace ) // S426210, blood-admitted hits only.
{
	CTerrorPlayer::SpawnTraceBlood( damage, info, direction, trace );
	if ( !( info.GetDamageType() & DMG_BULLET ) || !m_leakTimer.IsElapsed() ) return;
	m_leakTimer.Start( RandomFloat( 0.2f, 0.7f ) );
	if ( m_nVariantType != 1 ) return;
	static const char *const belly[] = { "leak_belly_tl", "leak_belly_bl", "leak_belly_tr", "leak_belly_br" };
	static const char *const chest[] = { "leak_chest" };
	static const char *const head[] = { "leak_head_l", "leak_head_r" };
	const char *attachment;
	switch ( trace->hitgroup )
	{
	case HITGROUP_HEAD: attachment = head[RandomInt( 0, 1 )]; break;
	case HITGROUP_CHEST: attachment = chest[RandomInt( 0, 0 )]; break;
	case HITGROUP_STOMACH: attachment = belly[RandomInt( 0, 3 )]; break;
	default: return;
	}
	DispatchParticleEffect( "blood_impact_arterial_spray", PATTACH_POINT_FOLLOW, this, LookupAttachment( attachment ), false );
}
bool Boomer::CanBeShoved( CBaseEntity *pusher ) const // S426370
{
	// The native subtype/ability ownership contract is deliberate: a leaker
	// must have its real CSelfDestruct, not a fabricated readiness flag.
	if ( m_nVariantType == 1 && GetCustomAbility()->IsActive() ) return false;
	return CTerrorPlayer::CanBeShoved( pusher );
}
bool Boomer::IsAbleToBreak( const CBaseEntity *entity ) const // S426340
{
	if ( entity->GetSpawnFlags() & 0x1000 ) return false;
	if ( entity->GetSpawnFlags() & 0x2000 ) return true;
	return INextBot::IsAbleToBreak( entity );
}
bool Boomer::ShouldHide() const // S425DC0
{
	if ( ShouldRetreatToCover() ) return true;
	if ( TheDirector->IsFinale() ) return false;
	int state = TheDirector->GetIntensityState();
	return ( state == 3 || state == 2 ) && TerrorGameRules()->GetDifficultyLevel() <= 1 && !TheDirector->HasSurvivorsAdvancedBeyondTank();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x425c40; 0x5c5504 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace boomerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "boomer", 0x5c5504 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "boomer", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
