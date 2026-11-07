//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/insect_swarm.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/weapon_gascan.h"
#include <emmintrin.h>
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar z_spit_spread_delay( "z_spit_spread_delay", "0.2", 0x00004004 );

LINK_ENTITY_TO_CLASS( insect_swarm, CInsectSwarm );
IMPLEMENT_SERVERCLASS_ST( CInsectSwarm, DT_InsectSwarm )
END_SEND_TABLE()

void CInsectSwarm::Spawn()
{
	BaseClass::Spawn();
	SetMaxFlames( 10 );
	m_flSpawnTime = gpGlobals->curtime;
	m_fireType = ACID;
}

CInsectSwarm *CInsectSwarm::Create( CBaseEntity *owner, const Vector &position,
	const Vector &normal, const Vector &velocity, int depth )
{
	CInsectSwarm *swarm = static_cast< CInsectSwarm * >(
		CBaseEntity::Create( "insect_swarm", position, vec3_angle, owner ) );
	if ( swarm )
		swarm->StartBurning( position, normal, velocity, depth );
	return swarm;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
double CInsectSwarm::GetDamagePerSecond() const
{
	// S382F30: SSE age/index/delta/fraction spills, then an unspilled x87 return.
	static const float damage[] = { 0.0f, 5.0f, 10.0f, 20.0f, 30.0f, 30.0f, 20.0f, 7.0f };
	const float age = gpGlobals->curtime - m_flSpawnTime;
	const int sample = _mm_cvttss_si32( _mm_set_ss( age ) );
	const float fraction = age - static_cast< float >( sample );
	// Standing out-of-array rule: each undefined read is zero, independently.
	// CVTTSS2SI also gives native INT_MIN for NaN/infinity/out-of-range.
	float base = static_cast< unsigned int >( sample ) < ARRAYSIZE( damage ) ? damage[sample] : 0.0f;
	const int next = sample + 1;
	const float nextDamage = static_cast< unsigned int >( next ) < ARRAYSIZE( damage ) ? damage[next] : 0.0f;
	const float delta = nextDamage - base;
	if ( !TerrorGameRules()->IsPlayerControlledZombies() && TerrorGameRules()->GetDifficultyLevel() == 0 )
		base *= 0.5f;
	return static_cast< double >( delta ) * static_cast< double >( fraction ) + static_cast< double >( base );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

float CInsectSwarm::GetFlameLifetime() const { return 7.0f; }
int CInsectSwarm::GetDamageType() const { return 0x00040400; }
float CInsectSwarm::GetSpreadInterval() const { return z_spit_spread_delay.GetFloat(); }
const char *CInsectSwarm::GetLoopParticleName() const { return "spitter_areaofdenial"; }
const char *CInsectSwarm::GetStartParticleName() const { return NULL; }

bool CInsectSwarm::CanHarm( CBaseEntity *entity ) const
{
	if ( entity->IsPlayer() )
		return entity->GetTeamNumber() == 2 || entity->GetTeamNumber() == 4;
	CGasCan *gasCan = dynamic_cast< CGasCan * >( entity );
	return gasCan && gasCan->IsVulnerableToSpit();
}

// Native server 0x382e70; 0x5c47ec is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace insect_swarmPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "insect_swarm", 0x5c47ec ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "insect_swarm", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
