//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_gascan.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "igameevents.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/entities/c_point_prop_use_target.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/entities/point_prop_use_target.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/effects/glowproperty.h"
#include "l4d2/weapons/inferno.h"
#include "EntityFlame.h"
#include "eventqueue.h"
#include "soundent.h"
#include "recipientfilter.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar gas_can_use_duration( "gas_can_use_duration", "2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar gascan_use_range( "gascan_use_range", "65", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar gascan_use_tolerance( "gascan_use_tolerance", "0.1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar gascan_spit_time( "gascan_spit_time", "2.9", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Gascans can survive this long in spit before they ignite." );
#ifndef CLIENT_DLL
ConVar gascan_throw_force( "gascan_throw_force", "32", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_REPLICATED
#endif
 );
ConVar scavenge_cluster_remove_on_pour( "scavenge_cluster_remove_on_pour", "1", 
#if defined( GAME_DLL )
	0x00004006
#else
	FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY
#endif
 );
ConVar scavenge_item_respawn_delay( "scavenge_item_respawn_delay", "20", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
,
	"After being destroyed, time until a scavenge item will respawn" );

float CGasCan::GetDropDistance() const
{
	return gascan_throw_force.GetFloat();
}
#endif

float CGasCan::GetActionDuration() const
{
	return gas_can_use_duration.GetFloat();
}

IMPLEMENT_NETWORKCLASS_ALIASED( GasCan, DT_GasCan )
BEGIN_NETWORK_TABLE( CGasCan, DT_GasCan )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CGasCan )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_gascan, CGasCan );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_gascan );
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CGasCan )
	DEFINE_OUTPUT( m_OnKilled, "OnKilled" ),
	DEFINE_THINKFUNC( ClearDetonateTimer ),
	DEFINE_FIELD( m_bVulnerableToSpit, FIELD_BOOLEAN ),
END_DATADESC()
#endif

CGasCan::CGasCan()
{
#ifndef CLIENT_DLL
	m_bBlockedDrop = false;
	m_bHasBeenPickedUp = false;
	m_bVulnerableToSpit = false;
	m_flSpawnTime = 0.0f;
	m_flLastNoiseTime = 0.0f;
	m_flSpitDetonateTime = 0.0f;
#endif
}

void CGasCan::Spawn()
{
	BaseClass::Spawn();
#ifndef CLIENT_DLL
	m_takedamage = DAMAGE_YES;
	SetHealth( 20 );
	m_bHasBeenPickedUp = false;
	m_flSpitDetonateTime = 0.0f;
	m_flSpawnTime = gpGlobals->curtime;
#endif
}

void CGasCan::ItemPostFrame()
{
#ifndef CLIENT_DLL
	// S:0x3CD470: world/entity time, strict greater-than1s, before base.
	if ( TerrorGameRules()->IsScavengeMode() && gpGlobals->curtime - m_flLastNoiseTime > 1.0f )
	{
		CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
		if ( pPlayer )
			pPlayer->SetNoiseLevel( 135.0f );
		m_flLastNoiseTime = gpGlobals->curtime;
	}
#endif
	BaseClass::ItemPostFrame();
}

CBaseEntity *CGasCan::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
#ifdef CLIENT_DLL
	return CPointPropUseTarget::FindUseTarget( pPlayer, gascan_use_range.GetFloat(), gascan_use_tolerance.GetFloat(), CPointPropUseTarget::USE_GASCAN, this );
#else
	return CPointPropUseTarget::FindUseTarget( pPlayer, gascan_use_range.GetFloat(), gascan_use_tolerance.GetFloat(), CPointPropUseTarget::USE_GASCAN );
#endif
}

bool CGasCan::CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3CDFD0: availability is action eligibility, NOT target selection.
	CPointPropUseTarget *pUseTarget = dynamic_cast< CPointPropUseTarget * >( pTarget );
	if ( pUseTarget && !pUseTarget->IsAvailableForUse() && pUseTarget->GetUseActionOwner() != this )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "gascan_pour_blocked" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			gameeventmanager->FireEvent( pEvent );
		}
#ifndef CLIENT_DLL
		m_bBlockedDrop = true;
#endif
		return false;
	}
	if ( pPlayer->m_iCurrentUseAction != 0 )
	{
#ifndef CLIENT_DLL
		m_bBlockedDrop = true;
#endif
		return false;
	}
	return BaseClass::CanPerformAction( mode, pPlayer, pTarget );
}

bool CGasCan::StartAction( int mode, int buttons )
{
	return mode == 1 ? false : BaseClass::StartAction( mode, buttons );
}

void CGasCan::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	EmitSound( "Player.UsingGasCan" );
	pPlayer->SpeakIfAllowed( "PlayerPourStarted", 1, NULL, NULL, 0, NULL );
#endif
	BaseClass::OnActionStarted( mode, pPlayer, pTarget, duration );
}

bool CGasCan::CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons )
{
	bool bContinue = BaseClass::CanContinueAction( pPlayer, pTarget, buttons );
#ifndef CLIENT_DLL
	if ( !bContinue )
		pPlayer->SpeakIfAllowed( "PlayerPourCancelled", 1, NULL, NULL, 0, NULL );
#endif
	return bContinue;
}

#ifndef CLIENT_DLL
void CGasCan::OnActionStopped( CTerrorPlayer *pPlayer )
{
	BaseClass::OnActionStopped( pPlayer );
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_GAS_USE, 0 );
	StopSound( "Player.UsingGasCan" );
	EmitSound( "Player.UsingGasCanStop" );
}

void CGasCan::OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3CDCF0. Director scoring is synchronous and precedes both the
	// response-rule criteria and the completed event; an event listener
	// is not a replacement for this original producer.
	if ( scavenge_cluster_remove_on_pour.GetBool() && GetWeaponSpawn() )
		TheDirector->GetScavengeMode()->RemoveItem( GetWeaponSpawn()->GetRefEHandle() );
	BaseClass::OnActionComplete( pPlayer, pTarget );
	TheDirector->IncrementTeamScore( TEAM_SURVIVOR, pPlayer );
	const int score = TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 );
	const int remaining = GasCansRemainingToWin();
	char criteria[256];
	Q_snprintf( criteria, sizeof( criteria ), "Score:%d,ToWin:%d", score, remaining );
	pPlayer->SpeakIfAllowed( "PlayerPourFinished", 1, criteria, NULL, 0, NULL );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "gascan_pour_completed" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}
#endif

#ifndef CLIENT_DLL
int CGasCan::GasCansRemainingToWin() const
{
	const unsigned int score = static_cast< unsigned int >( TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 ) );
	const unsigned int remaining = static_cast< unsigned int >( TerrorGameRules()->GetScavengeItemsGoal() ) - score;
	if ( TerrorGameRules()->IsScavengeMode() && TerrorGameRules()->IsScavengeSecondHalf() )
		return static_cast< int >( 1u - score + static_cast< unsigned int >( TerrorGameRules()->GetScavengeTeamScore( TEAM_INFECTED, -1 ) ) );
	return static_cast< int >( remaining );
}

void CGasCan::Equip( CBaseCombatCharacter *pOwner )
{
	// S:0x3CE240: only the REAL scavenge spawner carries this handle.
	CBaseEntity *pSpawn = GetWeaponSpawn();
	if ( pSpawn )
	{
		variant_t value;
		pSpawn->FireNamedOutput( "OnItemPickedUp", value, NULL, NULL );
		if ( !scavenge_cluster_remove_on_pour.GetBool() )
			TheDirector->GetScavengeMode()->RemoveItem( pSpawn->GetRefEHandle() );
		CTerrorPlayer *pPlayer = ToTerrorPlayer( pOwner );
		if ( pPlayer )
			pPlayer->SpeakIfAllowed( "PlayerEquippedScavengeItem", 1, NULL, NULL, 0, NULL );
	}
	m_bHasBeenPickedUp = true;
	m_flLastNoiseTime = 0.0f;
	Color glowColor;
	glowColor.SetRawColor( 33023 );
	GlowProp()->SetColor( glowColor );
	m_flSpitDetonateTime = 0.0f;
	GlowProp()->SetFlashing( false );
	BaseClass::Equip( pOwner );
}

void CGasCan::SetWeaponSpawn( CBaseEntity *pSpawn )
{
	BaseClass::SetWeaponSpawn( pSpawn );
	if ( pSpawn )
	{
		m_bVulnerableToSpit = true;
		m_nSkin = 1;
	}
}

bool CGasCan::DropCarriedItem( bool bThrow )
{
	// S:0x3CDE80: the failed-action latch consumes exactly one drop.
	if ( m_bBlockedDrop )
	{
		m_bBlockedDrop = false;
		return false;
	}
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer && ( pPlayer->IsDominated() || GetForcedDropAttacker() ) )
	{
		CBaseEntity *pAttacker = pPlayer->GetDominatingAttacker();
		if ( !pAttacker )
			pAttacker = ToTerrorPlayer( GetForcedDropAttacker() );
		// Original creates the event even when no valid player is found.
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "gas_can_forced_drop" );
		if ( pEvent && pAttacker )
		{
			pEvent->SetInt( "userid", engine->GetPlayerUserId( pAttacker->edict() ) );
			pEvent->SetInt( "victim", pPlayer->GetUserID() );
			gameeventmanager->FireEvent( pEvent );
		}
	}
	return BaseClass::DropCarriedItem( bThrow );
}

void CGasCan::ClearDetonateTimer()
{
	// S:0x3CE1F0: context think, one second after latest spit contact.
	m_flSpitDetonateTime = 0.0f;
	GlowProp()->SetFlashing( false );
}

#pragma float_control( precise, on, push )
int CGasCan::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	// S:0x3CE380. Preserve equality at the spit deadline and the exact
	// damage-type comparisons. These are not generic blast immunity.
	CTakeDamageInfo info = inputInfo;
	const unsigned int damageType = (unsigned int)info.GetDamageType();
	if ( damageType & 0x400 )
	{
		if ( !m_bHasBeenPickedUp )
			return 0;
		if ( m_flSpitDetonateTime == 0.0f )
		{
			m_flSpitDetonateTime = gpGlobals->curtime + gascan_spit_time.GetFloat();
			GlowProp()->SetFlashing( true );
			SetContextThink( &CGasCan::ClearDetonateTimer, gpGlobals->curtime + 1.0f, "CLEAR_DETONATE_CONTEXT" );
		}
		else if ( !( gpGlobals->curtime > m_flSpitDetonateTime ) )
		{
			SetContextThink( &CGasCan::ClearDetonateTimer, gpGlobals->curtime + 1.0f, "CLEAR_DETONATE_CONTEXT" );
			return 0;
		}
		else
		{
			info.SetDamage( GetHealth() + 1.0f );
		}
	}
	if ( damageType == 0x80 )
		info.SetDamage( 0.0f );
	else if ( damageType == 0x08000040 )
	{
		info.SetDamage( 0.0f );
		if ( !m_bHasBeenPickedUp )
			return 0;
	}
	CTerrorPlayer *pAttacker = ToTerrorPlayer( info.GetAttacker() );
	if ( pAttacker && ( pAttacker->GetFlags() & FL_FAKECLIENT ) )
		info.SetDamage( 0.0f );
	if ( !( damageType & DMG_BURN ) )
		return BaseClass::OnTakeDamage( info );

	CEntityFlame *pFlame = dynamic_cast< CEntityFlame * >( info.GetInflictor() );
	CTerrorPlayer *pBurningPlayer = pFlame ? ToTerrorPlayer( pFlame->GetParent() ) : NULL;
	if ( pBurningPlayer && pBurningPlayer->GetTeamNumber() == TEAM_INFECTED )
		return 0;
	if ( IsOnFire() && !( damageType & DMG_DIRECT ) )
		return 0;
	// Original shared integer latch is refreshed only inside the first
	// half second, not continuously resampled for each can's lifetime.
	static int s_nIgnitionGrace = 0;
	if ( gpGlobals->curtime - m_flSpawnTime < 0.5f )
	{
		s_nIgnitionGrace = scavenge_item_respawn_delay.GetInt();
		return 0;
	}
	if ( m_flSpawnTime + s_nIgnitionGrace > gpGlobals->curtime )
		return 0;
	if ( GetHealth() > info.GetDamage() )
		Ignite( random->RandomFloat( 10.0f, 15.0f ), true );
	return BaseClass::OnTakeDamage( info );
}
#pragma float_control( pop )

void CGasCan::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	// S:0x3CD850. The ordinary native entity flame owns burn damage.
	if ( IsOnFire() )
		return;
	BaseClass::Ignite( flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner );
	// Original rules v124 is S:0x0FD320 (alwaysfalse); its optional
	// effect-entity flag mutation is unreachable for CTerrorGameRules.
	CSoundEnt::InsertSound( SOUND_DANGER, GetAbsOrigin(), 128, 1.0f, this, SOUNDENT_CHANNEL_REPEATED_DANGER, NULL );
}

void CGasCan::Event_Killed( const CTakeDamageInfo &info )
{
	// S:0x3CD910: trace the actual ground; sky suppresses creation.
	trace_t trace;
	UTIL_TraceLine( GetAbsOrigin() + Vector( 0, 0, 10 ), GetAbsOrigin() - Vector( 0, 0, 999999.88f ), 0x0001400B, this, COLLISION_GROUP_NONE, &trace );
	if ( !( trace.surface.flags & SURF_SKY ) )
		CInferno::Create( NULL, trace.endpos, trace.plane.normal, vec3_origin, 0 );
	CTerrorPlayer *pAttacker = ToTerrorPlayer( info.GetAttacker() );
	if ( pAttacker && !( pAttacker->GetFlags() & FL_FAKECLIENT ) && ( (unsigned int)info.GetDamageType() & ( DMG_BULLET | 0x20000000 ) ) )
	{
		char criteria[64];
		Q_snprintf( criteria, sizeof( criteria ), "ToWin:%d", GasCansRemainingToWin() );
		pAttacker->SpeakIfAllowed( "PlayerShotGasCan", 1, criteria, NULL, 0, NULL );
	}
	m_OnKilled.FireOutput( this, this );
	if ( GetWeaponSpawn() )
	{
		g_EventQueue.AddEvent( GetWeaponSpawn(), "SpawnItem", scavenge_item_respawn_delay.GetFloat(), this, this );
	}
	// Native re-resolves the damage-info attacker after OnKilled/map I/O.
	pAttacker = ToTerrorPlayer( info.GetAttacker() );
	if ( pAttacker )
	{
		if ( TerrorGameRules()->IsScavengeMode() )
			pAttacker->OnScavengeGasCanDestroyed();
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "scavenge_gas_can_destroyed" );
		if ( pEvent )
		{
			pAttacker = ToTerrorPlayer( info.GetAttacker() );
			pEvent->SetInt( "userid", pAttacker ? pAttacker->GetUserID() : 0 );
			gameeventmanager->FireEvent( pEvent );
		}
	}
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( pOwner && pOwner->m_iCurrentUseAction == 8 && pOwner->m_useActionOwner.Get() == pOwner )
		pOwner->StopUseAction();
	BaseClass::Event_Killed( info );
}

void CTerrorPlayer::OnScavengeGasCanDestroyed()
{
	// S:0x306DE0 -> 0x304610 -> 0x301300. The original transports this
	// to each real human HUD as PZDmgMsg, not an achievement gameevent.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pRecipient = UTIL_PlayerByIndex( i );
		if ( !pRecipient || !pRecipient->edict() || pRecipient->entindex() == 0 ||
			!pRecipient->IsConnected() || ( pRecipient->GetFlags() & FL_FAKECLIENT ) || pRecipient->IsSplitScreenPlayer() )
			continue;
		CSingleUserRecipientFilter filter( pRecipient );
		filter.MakeReliable();
		UserMessageBegin( filter, "PZDmgMsg" );
		WRITE_BYTE( 6 );
		WRITE_SHORT( GetUserID() );
		WRITE_SHORT( GetUserID() );
		WRITE_SHORT( 0 );
		WRITE_SHORT( 0 );
		MessageEnd();
	}
}
#endif

// Native server 0x3cd400; 0x5c4ca0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_gascanPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_gascan", 0x5c4ca0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_gascan", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
