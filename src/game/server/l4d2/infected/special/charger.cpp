//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/charger.h"
#include "l4d2/infected/special/charger_intention.h"
#include "l4d2/infected/special/charger_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "collisionutils.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier1/KeyValues.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern int g_iNextZombieClass;
ConVar z_charger_allow_shove( "z_charger_allow_shove", "0", 0x00004004, "0 = Cannot be shoved, 1 = Can be shoved" );
LINK_ENTITY_TO_CLASS( charger, Charger );
#ifndef L4D2
PRECACHE_REGISTER( charger );
#endif
// Native map7C07B0: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Charger )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Charger, DT_Charger )
END_SEND_TABLE()

// S4311A0. m_isWaitingInCover is NOT initialized here in the original.
Charger::Charger()
{
	m_intention = new ChargerIntention( this );
	m_locomotion = new ChargerLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
}
// S431300, deleting destructor4313A0.
Charger::~Charger()
{
	delete m_locomotion; m_locomotion = NULL;
	delete m_body; m_body = NULL;
	delete m_intention; m_intention = NULL;
	delete m_vision; m_vision = NULL;
}
IIntention *Charger::GetIntentionInterface() const { return m_intention; }
ILocomotion *Charger::GetLocomotionInterface() const { return m_locomotion; }
IBody *Charger::GetBodyInterface() const { return m_body; }
IVision *Charger::GetVisionInterface() const { return m_vision; }
// S430CA0 / S2FD7D0.
CBasePlayer *Charger::AllocatePlayerEntity( edict_t *edict, const char *name )
{
	return CTerrorPlayer::CreatePlayer( "charger", edict );
}
Charger *CreateChargerBot( const char *name ) { return NextBotCreatePlayerBot< Charger >( name ); }
// S4313D0. Do not clear blockers, or assign false when the random draw fails.
void Charger::Spawn()
{
	g_iNextZombieClass = 6;
	m_body->SetArousal( IBody::INTENSE );
	if ( RandomInt( 1, 100 ) < 50 ) m_isWaitingInCover = true;
	for ( CBaseEntity *blocker = gEntList.FindEntityByClassname( NULL, "func_block_charge" ); blocker;
		blocker = gEntList.FindEntityByClassname( blocker, "func_block_charge" ) )
		m_chargeBlockers.AddToTail( blocker );
	BaseClass::Spawn();
}
// S430D80 overrides general OnTakeDamage at native primary byte288, before
// player333020 dispatch. It is NOT the alive-only hook or a NextBot wrapper.
int Charger::OnTakeDamage( const CTakeDamageInfo &info )
{
	CTakeDamageInfo damage( info );
	CBaseAbility *ability = GetCustomAbility();
	if ( ability && ability->IsActive() ) damage.ScaleDamage( 0.333f );
	return CTerrorPlayer::OnTakeDamage( damage );
}
// S430FD0: ability existence, not ability-active state, suppresses the shove.
bool Charger::CanBeShoved( CBaseEntity *source ) const
{
	return !GetCustomAbility() || z_charger_allow_shove.GetBool();
}
// S430CC0 mapped through the existing SDK footstep hook. PlayStepSound has
// toggled m_nStepside before this call (baseplayer_shared.cpp710..730).
const char *Charger::GetOverrideStepSound( const char *baseSoundName )
{
	static char sound[64];
	Q_snprintf( sound, sizeof( sound ), "Charger.Default.%s", m_Local.m_nStepside ? "RunRight" : "RunLeft" );
	return sound;
}
// S431020: map charge blockers are world AABBs, no player or tongue mechanics.
bool Charger::IsChargeBlocked( const Vector &start, const Vector &end ) const
{
	Ray_t ray;
	ray.Init( start, end );
	FOR_EACH_VEC( m_chargeBlockers, i )
	{
		CBaseEntity *blocker = m_chargeBlockers[i];
		if ( !blocker ) continue;
		Vector mins, maxs;
		blocker->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		if ( IsBoxIntersectingRay( mins, maxs, ray ) ) return true;
	}
	return false;
}
// S430DD0. The real player precache owns model, ability and claw creation.
void Charger::Precache()
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) ) return;
	PrecacheParticleSystem( "charger_wall_impact" );
	static const char *const sounds[] =
	{
		"Zombie.Punch", "ChargerZombie.Stagger", "ChargerZombie.HitPerson", "ChargerZombie.Alert",
		"ChargerZombie.Warn", "ChargerZombie.Voice", "ChargerZombie.Pain", "ChargerZombie.Death",
		"ChargerZombie.Recognize", "ChargerZombie.Charge", "ChargerZombie.ChargeEnd", "ChargerZombie.Melee",
		"ChargerZombie.Pummel", "ChargerZombie.VocalizePummel", "ChargerZombie.ImpactHard"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i ) PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "ChargerArmsVariant", "" );
		if ( arms && arms[0] ) PrecacheModel( arms );
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x430c40; 0x5c5550 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace chargerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "charger", 0x5c5550 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "charger", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
