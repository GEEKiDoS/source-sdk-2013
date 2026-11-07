//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#include "activitylist.h"
#include "in_buttons.h"
#include "l4d2/terror_physics_math.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/entities/point_prop_use_target.h"
#include "props.h"
#include "l4d2/effects/glowproperty.h"
#include "igameevents.h"
#include "vphysics_interface.h"
#include "datacache/imdlcache.h"
#endif
#include "tier0/memdbgon.h"

CCarriedProp::CCarriedProp()
{
	m_hUseTarget = NULL;
#ifndef CLIENT_DLL
	m_hWeaponSpawn = NULL;
	m_nSavedGlowType = 0;
#endif
}

CBaseEntity *CCarriedProp::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
	// S:0x3C7E40: inert carried objects have no action target. Their
	// primary attack reaches the original drop/throw fallback instead.
	return NULL;
}

bool CCarriedProp::CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	return BaseClass::CanPerformAction( mode, pPlayer, pTarget );
}

bool CCarriedProp::StartAction( int mode, int buttons )
{
	// S:0x3C7E00. Input kind2 is +use: only a failed primary attack
	// throws. A failed +use must leave the item in the inventory.
	bool bStarted = BaseClass::StartAction( mode, buttons );
	if ( !bStarted && mode == 0 && buttons != 2 )
	{
#ifndef CLIENT_DLL
		DropCarriedItem( true );
#endif
		return true;
	}
	return bStarted;
}

int CCarriedProp::GetActionButtons( int mode ) const
{
	// S:0x3C7E50: either primary attack OR use sustains a self action.
	return mode == 0 ? IN_ATTACK | IN_USE : BaseClass::GetActionButtons( mode );
}

void CCarriedProp::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	// S:0x3C9220. The target owner is the ITEM, not the survivor. Output
	// occurs before the item's target handle is assigned.
	CPointPropUseTarget *pUseTarget = dynamic_cast< CPointPropUseTarget * >( pTarget );
	if ( pUseTarget )
	{
		pUseTarget->OnUseStarted( this );
		m_hUseTarget = pUseTarget;
	}
#endif
}

bool CCarriedProp::CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons )
{
	bool bContinue = BaseClass::CanContinueAction( pPlayer, pTarget, buttons );
#ifndef CLIENT_DLL
	// S:0x3C9260: cancellation output belongs here, not StopAction.
	if ( !bContinue )
	{
		CPointPropUseTarget *pUseTarget = dynamic_cast< CPointPropUseTarget * >( m_hUseTarget.Get() );
		if ( pUseTarget )
			pUseTarget->OnUseCancelled();
	}
#endif
	return bContinue;
}

#ifndef CLIENT_DLL
void CCarriedProp::OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	CPointPropUseTarget *pUseTarget = dynamic_cast< CPointPropUseTarget * >( pTarget );
	if ( pUseTarget )
		pUseTarget->OnUseFinished();
	m_hUseTarget = NULL;
}

void CCarriedProp::OnActionStopped( CTerrorPlayer *pPlayer )
{
	// S:0x3C8F30: stop clears ownership, WITHOUT cancellation output.
	CPointPropUseTarget *pUseTarget = dynamic_cast< CPointPropUseTarget * >( m_hUseTarget.Get() );
	if ( pUseTarget )
		pUseTarget->SetUseActionOwner( NULL );
	m_hUseTarget = NULL;
}
#endif

bool CCarriedProp::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#ifndef CLIENT_DLL
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	// S:0x3C8110 tests MOVETYPE_LADDER, not lifeState or team.
	if ( !pOwner || pOwner->GetMoveType() != MOVETYPE_LADDER )
		return DropCarriedItem( false );
#endif
	return BaseClass::Holster( pSwitchingTo );
}

#ifndef CLIENT_DLL
void CCarriedProp::Equip( CBaseCombatCharacter *pOwner )
{
	// S:0x3C7F50. Outline-glow property is distinct from particle Glow.
	m_nSavedGlowType = GlowProp()->GetType();
	GlowProp()->SetType( 0 );
	BaseClass::Equip( pOwner );
}

void CCarriedProp::SetWeaponSpawn( CBaseEntity *pSpawn )
{
	// S:0x3CC730, called by the real scavenge spawner at S:0x29E580.
	m_hWeaponSpawn = pSpawn;
}

CBaseEntity *CCarriedProp::CreatePhysicsProp()
{
	// S:0x3C8210. Use the mounted original model and its real prop_data;
	// no carried-weapon replacement damage or synthetic hazard entity.
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( !pOwner )
		return NULL;

	MDLCACHE_CRITICAL_SECTION();
	const char *pModelName = GetCarriedPropModel();
	MDLHandle_t hModel = mdlcache->FindMDL( pModelName );
	if ( hModel == MDLHANDLE_INVALID )
		return NULL;
	studiohdr_t *pStudioHdr = mdlcache->GetStudioHdr( hModel );
	if ( !pStudioHdr || !mdlcache->GetVCollide( hModel ) )
	{
		mdlcache->Release( hModel );
		return NULL;
	}

	Vector forward;
	pOwner->EyeVectors( &forward );
	Vector position = pOwner->EyePosition() + 20.0f * forward - 0.5f * ( pStudioHdr->hull_min + pStudioHdr->hull_max );
	trace_t trace;
	UTIL_TraceLine( pOwner->EyePosition(), position, 0x0200400B, pOwner, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction < 1.0f )
	{
		Vector away = pOwner->EyePosition() - trace.endpos;
		TerrorPhysicsNormalize( away );
		// Preserve the original asymmetric x/y maximum, not a radius.
		position = trace.endpos + away * MAX( pStudioHdr->hull_max.x, pStudioHdr->hull_max.y );
	}

	bool bPrecacheAllowed = CBaseEntity::IsPrecacheAllowed();
	CBaseEntity::SetAllowPrecache( true );
	CBaseEntity *pProp = CreateEntityByName( "physics_prop" );
	if ( pProp )
	{
		pProp->KeyValue( "origin", position );
		pProp->KeyValue( "angles", "0 0 0" );
		pProp->KeyValue( "model", pModelName );
		pProp->KeyValue( "fademindist", "-1" );
		pProp->KeyValue( "fademaxdist", "0" );
		pProp->KeyValue( "fadescale", "1" );
		pProp->KeyValue( "inertiaScale", "1.0" );
		pProp->KeyValue( "physdamagescale", "0.1" );
		pProp->KeyValue( "glow", "1" );
		pProp->Precache();
		DispatchSpawn( pProp );
		pProp->Activate();
		pProp->SetCollisionGroup( COLLISION_GROUP_WEAPON );
		pProp->SetOwnerEntity( pOwner );
	}
	CBaseEntity::SetAllowPrecache( bPrecacheAllowed );
	mdlcache->Release( hModel );
	return pProp;
}

#pragma float_control( precise, on, push )
bool CCarriedProp::DropToPhysicsPropInternal( bool bThrow )
{
	// S:0x3C86F0. Even failed prop creation still detaches/removes the
	// item. Do not turn this original failure into an inventory retry.
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( !pOwner )
		return false;

	CBaseEntity *pProp = CreatePhysicsProp();
	if ( pProp && bThrow )
	{
		IPhysicsObject *pPhysics = pProp->VPhysicsGetObject();
		if ( pPhysics )
		{
			Vector forward;
			pOwner->EyeVectors( &forward );
			// S3C8760..879E re-reads mass at each ordered comparison.
			float mass;
			if ( pPhysics->GetMass() > 15.0f )
				mass = 15.0f;
			else if ( 0.5f > pPhysics->GetMass() )
				mass = 0.5f;
			else
				mass = pPhysics->GetMass();
			// S1358E0 returns the x87 chain; S3C87D1 spills it to float.
			const float scale = static_cast< float >( ( static_cast< double >( mass ) - 0.5 ) * 3.5 / 14.5 + 0.5 );
			ConVarRef player_throwforce( "player_throwforce" );
			const float force = player_throwforce.GetFloat() * scale;
			pPhysics->ApplyForceCenter( forward * force );
			AngularImpulse torque;
			torque.Random( -10.0f, 10.0f );
			pPhysics->ApplyTorqueCenter( torque * scale );
		}

		IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_drop_to_prop" );
		if ( pEvent )
		{
			const char *pItemName = GetClassname();
			if ( Q_stristr( pItemName, "weapon_" ) )
				pItemName += 7;
			pEvent->SetInt( "userid", pOwner->GetUserID() );
			pEvent->SetString( "item", pItemName );
			pEvent->SetInt( "propid", pProp->entindex() );
			gameeventmanager->FireEvent( pEvent );
		}
	}
	// S3C872D jumps here for passive drops too; only force/event are throw-only.
	if ( pProp )
		pProp->GlowProp()->SetType( m_nSavedGlowType );

	SetWeaponVisible( false );
	if ( GetOwner() )
	{
		pOwner->Weapon_Detach( this );
		UTIL_Remove( this );
	}
	return true;
}
#pragma float_control( pop )

bool CCarriedProp::DropCarriedItem( bool bThrow )
{
	// S:0x3C8A20 restores the native saved last weapon AFTER conversion.
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( !pOwner )
		return false;
	CHandle< CBaseCombatWeapon > hLastWeapon = pOwner->GetLastWeapon();
	bool bDropped = DropToPhysicsPropInternal( bThrow );
	if ( hLastWeapon )
		pOwner->Weapon_Switch( hLastWeapon );
	return bDropped;
}

void CCarriedPropWeapon::Equip( CBaseCombatCharacter *pOwner )
{
	// S:0x3C92F0: the intermediate saves glow before its base saves0.
	m_nDroppedWeaponGlowType = GlowProp()->GetType();
	GlowProp()->SetType( 0 );
	BaseClass::Equip( pOwner );
}

bool CCarriedPropWeapon::DropCarriedItem( bool bThrow )
{
	// S:0x3C9330. Gas/cola are dropped WEAPONS, not physics props.
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( !pOwner )
		return false;
	GlowProp()->SetType( m_nDroppedWeaponGlowType );
	CHandle< CBaseCombatWeapon > hLastWeapon = pOwner->GetLastWeapon();
	pOwner->DropWeapon( this, bThrow, NULL );
	if ( hLastWeapon )
		pOwner->Weapon_Switch( hLastWeapon );
	return true;
}

#endif

Activity CCarriedProp::CarriedActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

Activity CCarriedProp::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? CarriedActivity( "ACT_MELEE_SWEEP_GASCAN" ) : ACT_INVALID;
}

Activity CCarriedProp::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return CarriedActivity( "ACT_DEPLOY_GREN" );
}

Activity CCarriedProp::GetDrawActivity()
{
	return CarriedActivity( "ACT_VM_DEPLOY_GASCAN" );
}

Activity CCarriedProp::TranslateViewModelActivity( Activity activity )
{
	// Identical original leaf overrides S:3CD670/3CC970/3DCF50/3DF8A0/3CE930.
	if ( activity == CarriedActivity( "ACT_VM_DEPLOY" ) ) return CarriedActivity( "ACT_VM_DEPLOY_GASCAN" );
	if ( activity == ACT_VM_IDLE ) return CarriedActivity( "ACT_VM_IDLE_GASCAN" );
	if ( activity == ACT_VM_THROW ) return CarriedActivity( "ACT_VM_THROW_GASCAN" );
	if ( activity == ACT_VM_PULLPIN ) return CarriedActivity( "ACT_VM_PULLPIN_GASCAN" );
	if ( activity == CarriedActivity( "ACT_VM_MELEE" ) ) return CarriedActivity( "ACT_VM_MELEE_GASCAN" );
	return BaseClass::TranslateViewModelActivity( activity );
}

acttable_t *CCarriedProp::ActivityList( int &iActivityCount )
{
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_GASCAN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_GASCAN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_GASCAN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_GASCAN" },
		{ "ACT_RUN", "ACT_RUN_GASCAN" },
		{ "ACT_WALK", "ACT_WALK_GASCAN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_GASCAN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_GASCAN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_GASCAN" },
		{ "ACT_WALK_CALM", "ACT_WALK_GASCAN" },
		{ "ACT_RUN_CALM", "ACT_RUN_GASCAN" },
		{ "ACT_JUMP", "ACT_JUMP_GASCAN" }
	};
	static acttable_t s_Activities[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Activities[i].baseAct = CarriedActivity( s_pNames[i][0] );
			s_Activities[i].weaponAct = CarriedActivity( s_pNames[i][1] );
			s_Activities[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Activities );
	return s_Activities;
}
