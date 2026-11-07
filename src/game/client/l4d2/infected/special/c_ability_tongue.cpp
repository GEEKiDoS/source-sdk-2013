//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "particle_parse.h"
#include "tier0/memdbgon.h"

CTongue::CTongue()
{
	m_potentialTarget = NULL;
	m_currentTipTarget = NULL;
	m_lastTongueState = 0;
	AddToEntityList( ENTITY_LIST_SIMULATE ); // C28F118 -> native04E3B0(this,3).
}
CTongue::~CTongue() { StopTongueEffects(); }

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CTongue::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	return owner && m_tongueState == 0 && !owner->IsGhost() &&
		( owner->GetFlags() & FL_ONGROUND ) && !( owner->GetPlayerMaxSpeed() < 10.0f ) &&
		owner->m_lifeState < LIFE_RESPAWNABLE && !owner->IsStaggering() && BaseClass::CanActivate();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Native client28EDA0 is empty: tongue acquisition/travel/grab is entirely
// authoritative. It has no leaf prediction fields (unlike C_Charge).
bool CTongue::ActivateAbility() { return true; }
void CTongue::Update() {}
void CTongue::OnRemove() { BaseClass::OnRemove(); }
void CTongue::SetOwner( CTerrorPlayer *owner ) { BaseClass::SetOwner( owner ); }
// C28EFD0: miss state1 displays recharge, but unsigned states>=2 hide it.
float CTongue::GetRechargeProgress() const
{
	if ( !GetOwner() || static_cast< unsigned int >( m_tongueState.Get() ) >= 2u ) return 0.0f;
	return BaseClass::GetRechargeProgress();
}
const char *CTongue::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CTongue::GetCooldownForeground() const { return "HUD/PZ_charge_smoker"; }

// C28F300 creates a separately referenced particle for EACH segment, including
// the mouth-to-tip segment before attachment. No beam entity or point sprite.
void CTongue::UpdateTongueEffects()
{
	for ( int segment = 0; segment <= m_bendPointCount; ++segment )
	{
		if ( !m_tongueEffects[segment] )
		{
			m_tongueEffects[segment] = new CNewParticleEffect( NULL, "smoker_tongue" );
			m_tongueEffects[segment]->SetDynamicallyAllocated( true );
		}
		m_tongueEffects[segment]->SetControlPoint( 0, GetSegmentStart( segment ) );
		m_tongueEffects[segment]->SetControlPoint( 1, GetSegmentEnd( segment ) );
	}
	for ( int segment = m_bendPointCount + 1; segment < 11; ++segment )
	{
		if ( m_tongueEffects[segment] )
		{
			m_tongueEffects[segment]->StopEmission( false, true, true );
			m_tongueEffects[segment] = NULL;
		}
	}
}

void CTongue::StopTongueEffects()
{
	for ( int segment = 0; segment < 11; ++segment )
	{
		if ( m_tongueEffects[segment] )
		{
			m_tongueEffects[segment]->StopEmission( false, true, true );
			m_tongueEffects[segment] = NULL;
		}
		m_effectStarts[segment].Init();
		m_effectEnds[segment].Init();
	}
}

// C28F580 uses the LAST rendered endpoints, not fresh attachment locations.
// That distinction is visible when an owner or victim disappears first.
void CTongue::DropTongueEffects()
{
	for ( int segment = 0; segment < 11; ++segment )
	{
		if ( !m_tongueEffects[segment] ) continue;
		DispatchParticleEffect( "smoker_tongue_fall", m_effectStarts[segment], m_effectEnds[segment], GetAbsAngles(), this );
		m_tongueEffects[segment]->StopEmission( false, true, true );
		m_tongueEffects[segment] = NULL;
	}
}

bool CTongue::Simulate()
{
	if ( m_tongueState != m_lastTongueState )
	{
		switch ( m_tongueState )
		{
		case 0:
		case 1: StopTongueEffects(); break;
		case 2:
		case 3: UpdateTongueEffects(); break;
		case 4: DropTongueEffects(); break;
		}
	}
	if ( m_tongueState == 2 || m_tongueState == 3 ) UpdateTongueEffects();
	m_lastTongueState = m_tongueState;
	// C28F6F1 sets AL=1 on the common return path, after the state store.
	return true;
}

void CTongue::OnParticleEffectDeleted( CNewParticleEffect *effect )
{
	for ( int segment = 0; segment < 11; ++segment )
	{
		if ( m_tongueEffects[segment]() == effect )
		{
			m_tongueEffects[segment] = NULL;
			break;
		}
	}
}
void CTongue::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	if ( state == SHOULDTRANSMIT_END ) StopTongueEffects();
}
void CTongue::UpdateOnRemove()
{
	StopTongueEffects();
	BaseClass::UpdateOnRemove();
}
