//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/weapons/terror_weapon.h"
#include "rumble_shared.h"
#include "l4d2/sound/terror_music.h"
#include "ai_criteria.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "tier0/memdbgon.h"

ConVar survivor_stun_immunity_duration( "survivor_stun_immunity_duration", "0", 0x00004004 );

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S30D720, genuine CBaseEntity ground-change virtual157. The entity setter
// calls this after handle/list/FL_ONGROUND changes, not from movement OnTry.
void CTerrorPlayer::OnGroundChanged( CBaseEntity *oldGround, CBaseEntity *newGround )
{
	if ( !oldGround )
	{
		if ( !newGround )
			return;
		StopTrackedSound( "Player.Fall" );
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 && m_Local.m_bDucked )
		{
			if ( CBaseAbility *ability = GetCustomAbility() )
				ability->OnOwnerLanded();
		}
	}
	if ( newGround )
	{
		CBaseEntity *view = GetViewEntity();
		if ( IsAlive() && 720.0f > m_Local.m_flFallVelocity && view != this && view &&
			FClassnameIs( view, "point_deathfall_camera" ) )
		{
			ClearZoomOwner();
			SetFOV( this, 0, 0.0f, 0 );
			SetViewEntity( NULL );
		}
		const int team = GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			FClassnameIs( newGround, "prop_car_alarm" ) )
		{
			variant_t value;
			newGround->AcceptInput( "SurvivorStandingOnCar", this, this, value, 0 );
		}
	}
}

// S3146F0. The immunity interval affects only starting a new stun; all pin
// releases, immunity refresh and rumble still happen on an immune bash.
void CTerrorPlayer::Bash( float duration )
{
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
		m_Local.m_flFallVelocity = 0.0f;
	if ( gpGlobals->curtime >= m_bashImmunityTimer.m_timestamp )
	{
		const double expiry = static_cast< double >( gpGlobals->curtime ) + duration;
		if ( m_stunTimer.m_timestamp.Get() != expiry )
			m_stunTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
		m_stunTimer.m_duration = duration;
		if ( CBaseAbility *ability = m_customAbility.Get() )
			ability->StopActivation( duration );
		if ( CTerrorWeapon *weapon = dynamic_cast<CTerrorWeapon *>( GetActiveWeapon() ) )
			weapon->OnOwnerStunned( duration );
	}
	const float immunity = survivor_stun_immunity_duration.GetFloat();
	const double immunityExpiry = static_cast< double >( gpGlobals->curtime ) + immunity;
	if ( m_bashImmunityTimer.m_timestamp.Get() != immunityExpiry )
		m_bashImmunityTimer.m_timestamp.GetForModify() = static_cast< float >( immunityExpiry );
	m_bashImmunityTimer.m_duration = immunity;
	ReleasePounce();
	if ( CTerrorPlayer *victim = ToTerrorPlayer( m_tongueVictim.Get() ) )
		DebugAbilityVictim( victim, victim, "Tongue letting go on getting bashed." );
	// The native inlined block at S31490E is the non-forced release path:
	// no victim skips the entire tongue event/owner cleanup block.
	ReleaseTongue( false );
	ReleaseJockey( NULL );
	RumbleEffect( 32, 0, RUMBLE_FLAG_RESTART );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// S329F10's injury phase invokes the actual concrete ability virtual.
void CTerrorPlayer::OnAbilityOwnerDamaged( const CTakeDamageInfo &info )
{
	if ( CBaseAbility *ability = m_customAbility.Get() )
		ability->OnOwnerDamaged( info );
}

// S2C9ED0. This is the real player weapon frame, not an extra entity think.
void CTerrorPlayer::ItemPostFrame()
{
	CBaseAbility *ability = m_customAbility.Get();
	if ( ability )
	{
		if ( ability->GetActivationButton() & m_afButtonPressed )
			ability->TryActivate();
		if ( ability->GetActivationButton() & m_afButtonReleased )
			ability->OnButtonReleased();
		ability->Update();
	}
	UpdateStillness();
	BeginTerrorWeaponMessageScope();
	if ( ( entindex() + gpGlobals->tickcount ) % (int)( 0.25f / gpGlobals->interval_per_tick + 0.5f ) == 0 )
		UpdateBuildableButtonUseEntity();
	BaseClass::ItemPostFrame();
	EndTerrorWeaponMessageScope();
}

// S32AFA0, called only by the ordinary shove presentation callback, after
// its rumble/sound. A claw's OnSwing is deliberately a different override.
void CTerrorPlayer::OnShove()
{
	m_terrorShoveInterval.Start();
	CCSPlayer::NoteTerrorWeaponFired();
	GetMusic()->OnWeaponFired();
	if ( CTerrorWeapon *weapon = dynamic_cast<CTerrorWeapon *>( GetActiveWeapon() ) )
	{
		const TerrorWeaponType_t type = weapon->GetTerrorWpnData().m_WeaponType;
		if ( type == TERROR_WEAPONTYPE_HEALING || type == TERROR_WEAPONTYPE_REVIVING )
		{
			char modifiers[256];
			Q_snprintf( modifiers, sizeof( modifiers ), "Item:%s", weapon->GetName() );
			SpeakIfAllowed( "BashWithItem", 1, modifiers );
		}
	}
	StopDragging();
}

// S3226B0: these are the original character hit counters, not damage points
// or a second statistics event. The original first-character lookup and its
// Tank/Jockey side effect happen even when the subsequent hit is throttled.
void CTerrorPlayer::OnClawHitSurvivorCharacter( int character )
{
	if ( (unsigned int)character > 3 )
		return;
	++m_survivorClawHits[character];
	++m_totalSurvivorClawHits;
	CTerrorPlayer *victim = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) );
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		++m_checkpointPZTankPunches;
		// Native character-first selection is preserved, including no alive
		// filter. A disconnected victim can have no match: Main's minimal-
		// skip rule omits only the undefined victim-dependent dereference.
		if ( victim )
		{
			if ( CTerrorPlayer *jockey = ToTerrorPlayer( victim->m_jockeyAttacker.Get() ) )
				jockey->ReleaseJockey( NULL );
		}
	}
}

