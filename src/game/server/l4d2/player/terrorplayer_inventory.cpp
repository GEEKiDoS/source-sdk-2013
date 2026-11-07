//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/weapon_chainsaw.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "ammodef.h"
#include "baseviewmodel.h"
#include "igameevents.h"
#include "in_buttons.h"
#include "physics.h"
#include "physics_saverestore.h"
#include "player_pickup.h"
#include "rumble_shared.h"
#include "vphysics/friction.h"
#include "datacache/imdlcache.h"
#include "props.h"
#include "l4d2/player/terrorplayer_inventory_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern int gEvilImpulse101;

static bool IsSurvivorTeam( const CTerrorPlayer *pPlayer )
{
	return pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}


// S2FCBC0: only the active weapon, not HasWeaponOfType's inventory scan.
bool CTerrorPlayer::IsUsingWeaponType( int weaponType ) const
{
	CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
	return weapon && weapon->GetTerrorWpnData().m_WeaponType == weaponType;
}

bool CTerrorPlayer::IsWeaponAllowedForTeam( int nWeaponID ) const
{
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( nWeaponID );
	if ( !pInfo )
		return true;
	const int nTeam = IsSurvivorTeam( this ) ? TEAM_SURVIVOR : GetTeamNumber();
	return pInfo->m_iTeam == TEAM_UNASSIGNED || pInfo->m_iTeam == nTeam;
}

bool CTerrorPlayer::Weapon_CanUse( CBaseCombatWeapon *pWeapon )
{
	CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase * >( pWeapon );
	if ( pCSWeapon && ( !IsWeaponAllowedForTeam( pCSWeapon->GetWeaponID() ) ||
		GetTerrorWeaponGlowType( this, pCSWeapon->GetWeaponID(), pCSWeapon->Clip1(), pCSWeapon->GetExtraPrimaryAmmo(), NULL, pCSWeapon->HasAnyUpgrades() ) == 0 ) )
		return false;

	CTerrorWeapon *pActive = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( pActive && pActive->GetWeaponID() == TERROR_WEAPON_CHAINSAW )
	{
		CChainsaw *pChainsaw = dynamic_cast< CChainsaw * >( pActive );
		if ( pChainsaw->m_iState == CChainsaw::ACTIVE )
			return false;
	}
	return true;
}

CTerrorWeapon *CTerrorPlayer::GetWeaponOfType( int nWeaponID ) const
{
	const unsigned char nIndex = m_weaponIndexByID[nWeaponID];
	return nIndex ? static_cast< CTerrorWeapon * >( GetWeapon( nIndex - 1 ) ) : NULL;
}

bool CTerrorPlayer::HasWeaponOfType( int nWeaponType ) const
{
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		CWeaponCSBase *pWeapon = dynamic_cast< CWeaponCSBase * >( GetWeapon( i ) );
		if ( pWeapon && pWeapon->GetTerrorWpnData().m_WeaponType == nWeaponType )
			return true;
	}
	return false;
}

void CTerrorPlayer::Weapon_Equip( CBaseCombatWeapon *pWeapon )
{
	if ( !IsAlive() )
		return;
	const int nSlot = pWeapon->GetSlot();
	CBaseCombatWeapon *pResident = Weapon_GetSlot( nSlot );
	if ( pResident )
		DropWeaponInternal( pResident, NULL, true, NULL );

	pWeapon->SetSolidFlags( FSOLID_NOT_SOLID );
	pWeapon->SetOwnerEntity( this );
	// The native inventory producer owns the handle array and ID-byte cache.
	CBaseCombatCharacter::Weapon_Equip( pWeapon );
	if ( g_pGameRules->FShouldSwitchWeapon( this, pWeapon ) )
		Weapon_Switch( pWeapon );
	if ( pWeapon->GetOwner() == this && !pWeapon->IsMarkedForDeletion() )
		OnPickedUpWeapon( pWeapon );
	if ( nSlot == 0 || nSlot == 1 )
		Weapon_Switch( pWeapon, 0 );
}

bool CTerrorPlayer::Weapon_CanSwitchTo( CBaseCombatWeapon *pWeapon )
{
	CBaseCombatWeapon *pActive = GetActiveWeapon();
	if ( pActive && !pActive->CanBeSwitchedFrom() )
		return false;
	return pWeapon->CanDeploy() && ( !pActive || pActive->CanHolster() );
}

bool CTerrorPlayer::Weapon_Switch( CBaseCombatWeapon *pWeapon, int nViewModelIndex )
{
	CBaseCombatWeapon *pPrevious = GetActiveWeapon();
	if ( !pWeapon )
		return false;
	if ( pPrevious == pWeapon )
	{
		if ( pPrevious->IsWeaponVisible() || !pPrevious->Deploy() )
			return false;
	}
	else
	{
		if ( !Weapon_CanSwitchTo( pWeapon ) || ( pPrevious && !pPrevious->Holster( pWeapon ) ) )
			return false;
		SetActiveWeapon( pWeapon );
		if ( !pWeapon->Deploy() )
			return false;
	}
	if ( pPrevious && Weapon_ShouldSetLast( pPrevious, GetActiveWeapon() ) )
		Weapon_SetLast( pPrevious->GetLastWeapon() );
	CBaseViewModel *pViewModel = GetViewModel( nViewModelIndex );
	if ( pViewModel )
		pViewModel->RemoveEffects( EF_NODRAW );
	ResetAutoaim();
	RecalculateMaxSpeed();
	return true;
}

void CTerrorPlayer::Weapon_Drop( CBaseCombatWeapon *pWeapon, const Vector *pTarget, const Vector *pVelocity )
{
	// S:0x170AC0 and the native CBasePlayer body have the same player drop
	// placement, velocity cap, active-switch fallback, and detach ordering.
	// The weapon's virtual Drop must reach Terror/CS physics, not generic Drop.
	BaseClass::Weapon_Drop( pWeapon, pTarget, pVelocity );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_drop" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		if ( pWeapon )
		{
			const char *pszItem = pWeapon->GetClassname();
			if ( Q_strstr( pszItem, "weapon_" ) )
				pszItem += 7;
			pEvent->SetString( "item", pszItem );
			pEvent->SetInt( "propid", pWeapon->entindex() );
		}
		gameeventmanager->FireEvent( pEvent );
	}
}

static void FireWeaponGivenEvent( CTerrorPlayer *pRecipient, CTerrorPlayer *pGiver, CTerrorWeapon *pWeapon )
{
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_given" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pRecipient->GetUserID() );
		pEvent->SetInt( "giver", pGiver->GetUserID() );
		pEvent->SetInt( "weapon", pWeapon->GetWeaponID() );
		pEvent->SetInt( "weaponentid", pWeapon->entindex() );
		gameeventmanager->FireEvent( pEvent );
	}
}

bool CTerrorPlayer::BumpWeapon( CBaseCombatWeapon *pWeapon )
{
	// The original null branch dereferences the candidate. Do not turn it into
	// a new successful no-op or an alternate GiveNamedItem implementation.
	if ( !pWeapon )
	{
		pWeapon->AddSolidFlags( FSOLID_NOT_SOLID );
		pWeapon->AddEffects( EF_NODRAW );
		Weapon_Equip( pWeapon );
		return true;
	}
	if ( pWeapon->GetOwner() || !Weapon_CanUse( pWeapon ) || !g_pGameRules->CanHavePlayerItem( this, pWeapon ) )
	{
		if ( gEvilImpulse101 )
			UTIL_Remove( pWeapon );
		return false;
	}

	CTerrorWeapon *pTerrorWeapon = dynamic_cast< CTerrorWeapon * >( pWeapon );
	CTerrorGun *pGun = pWeapon->GetTerrorGun();
	CTerrorGun *pResidentGun = NULL;
	if ( pGun && pGun->CanDualWield() )
	{
		// S:0x242B20 deliberately scans inventory order, unlike the ID cache.
		for ( int i = 0; i < MAX_WEAPONS; ++i )
		{
			CTerrorWeapon *pResident = dynamic_cast< CTerrorWeapon * >( GetWeapon( i ) );
			if ( pResident && pResident->GetWeaponID() == pGun->GetWeaponID() )
			{
				pResidentGun = pResident->GetTerrorGun();
				break;
			}
		}
	}
	if ( pResidentGun )
	{
		if ( pResidentGun->IsDualWielding() )
		{
			if ( !pGun->IsDualWielding() )
				return false;
		}
		else
		{
			if ( !pResidentGun->AcquireDualWeapons() )
				return false;
			const int nExtra = pGun->GetExtraPrimaryAmmo();
			if ( nExtra && pGun->GetPrimaryAmmoType() != -1 )
			{
				pGun->SetExtraPrimaryAmmo( 0 );
				SetAmmoCount( nExtra, pGun->GetPrimaryAmmoType() );
			}
			CTerrorPlayer *pGiver = ToTerrorPlayer( pGun->GetPreviousOwner() );
			if ( pGiver && pGiver != this && pGun->GetGiveTarget() == this )
				FireWeaponGivenEvent( this, pGiver, pGun );
			if ( pGun->IsDualWielding() )
				pGun->RemoveDualWeapons( false );
			else
				UTIL_Remove( pGun );
			return true;
		}
	}

	// Both original calls are virtual and observable. Dropping the first
	// occupant is not a replacement for the second occupancy test.
	if ( Weapon_SlotOccupied( pWeapon ) )
		DropWeaponInSlot( pWeapon->GetSlot() );
	if ( Weapon_SlotOccupied( pWeapon ) )
		return false;
	pWeapon->OnPickedUp( this );
	pWeapon->AddSolidFlags( FSOLID_NOT_SOLID );
	pWeapon->AddEffects( EF_NODRAW );
	Weapon_Equip( pWeapon );
	CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase * >( pWeapon );
	if ( pCSWeapon && pCSWeapon->GetExtraPrimaryAmmo() && pWeapon->GetSlot() != 2 && pWeapon->GetPrimaryAmmoType() != -1 )
	{
		const int nExtra = pCSWeapon->GetExtraPrimaryAmmo();
		pCSWeapon->SetExtraPrimaryAmmo( 0 );
		SetAmmoCount( nExtra, pWeapon->GetPrimaryAmmoType() );
	}
	IGameEvent *pPickup = gameeventmanager->CreateEvent( "item_pickup" );
	if ( pPickup )
	{
		const char *pszItem = pWeapon->GetClassname();
		if ( !Q_strncmp( pszItem, "weapon_", 7 ) )
			pszItem += 7;
		pPickup->SetInt( "userid", GetUserID() );
		pPickup->SetString( "item", pszItem );
		gameeventmanager->FireEvent( pPickup );
	}
	if ( pTerrorWeapon )
	{
		CTerrorPlayer *pGiver = ToTerrorPlayer( pTerrorWeapon->GetPreviousOwner() );
		if ( pGiver && pGiver != this && pTerrorWeapon->GetGiveTarget() == this )
		{
			FireWeaponGivenEvent( this, pGiver, pTerrorWeapon );
			TerrorNavArea *pArea = ToTerrorNavArea( pGiver->GetLastKnownArea() );
			if ( pArea && !( pArea->GetSpawnAttributes() & ( TERROR_NAV_CHECKPOINT | TERROR_NAV_PLAYER_START ) ) &&
				( GetHealth() < pGiver->GetHealth() || GetHealth() + GetHealthBuffer() < pGiver->GetHealth() + pGiver->GetHealthBuffer() ) )
				++pGiver->m_checkpointFirstAidShared;
		}
		if ( pTerrorWeapon->GetTerrorWpnData().m_WeaponType == TERROR_WEAPONTYPE_GRENADE )
		{
			CBaseCSGrenade *pGrenade = dynamic_cast< CBaseCSGrenade * >( pTerrorWeapon );
			if ( pGrenade )
				pGrenade->OnPickedUpGrenade();
		}
	}
	return true;
}

CBaseEntity *CTerrorPlayer::GiveNamedItem( const char *pszName, int nSubType )
{
	return GiveNamedItemInternal( pszName, nSubType, false, NULL );
}

CBaseEntity *CTerrorPlayer::GiveNamedItemInternal( const char *pszName, int nSubType, bool bForcePickup, CBaseEntity *pAppearanceSource )
{
	if ( !pszName || !pszName[0] )
		return NULL;
	if ( !Q_stricmp( pszName, "health" ) )
	{
		GiveHealth();
		return NULL;
	}
	if ( !Q_stricmp( pszName, "ammo" ) )
	{
		for ( int i = 0; i < MAX_AMMO_TYPES; ++i )
		{
			Ammo_t *pAmmo = GetAmmoDef()->GetAmmoOfIndex( i );
			if ( pAmmo && ( pAmmo->nDamageType & DMG_BULLET ) )
				GiveAmmo( GetAmmoDef()->MaxCarry( i, this ), i, false );
		}
		return NULL;
	}

	const MeleeWeaponInfoHandle_t nMelee = MeleeWeaponInfoStore()->GetMeleeWeaponInfoIndex( pszName );
	char szClassname[256];
	if ( nMelee != INVALID_MELEE_WEAPON_INFO_HANDLE )
		Q_strncpy( szClassname, "weapon_melee", sizeof( szClassname ) );
	else if ( Q_strnicmp( pszName, "weapon_", 7 ) )
		Q_snprintf( szClassname, sizeof( szClassname ), "weapon_%s", pszName );
	else
		Q_strncpy( szClassname, pszName, sizeof( szClassname ) );
	Q_strlower( szClassname );
	if ( !Q_stricmp( szClassname, "weapon_melee" ) && nMelee == INVALID_MELEE_WEAPON_INFO_HANDLE )
		return NULL;

	EHANDLE hEntity = CreateEntityByName( szClassname );
	if ( !hEntity )
	{
		Msg( "NULL Ent '%s' in GiveNamedItem!\n", szClassname );
		return NULL;
	}
	hEntity->SetLocalOrigin( EyePosition() );
	hEntity->AddSpawnFlags( SF_NORESPAWN );
	if ( pAppearanceSource )
	{
		hEntity->GlowProp()->SetType( pAppearanceSource->GlowProp()->GetType() );
		hEntity->GlowProp()->SetRange( pAppearanceSource->GlowProp()->GetRange() );
		hEntity->GlowProp()->SetRangeMin( pAppearanceSource->GlowProp()->GetRangeMin() );
		// S3111E0 copies GetColor's flashing RGB, not the packed override.
		// With an unset override the native output bytes are undefined; omit
		// only that color transfer, retaining all independent appearance work.
		if ( pAppearanceSource->GlowProp()->GetColorOverride().GetRawColor() != 0 )
		{
			unsigned char r, g, b;
			pAppearanceSource->GlowProp()->GetColor( &r, &g, &b );
			hEntity->GlowProp()->SetColor( Color( r, g, b, 255 ) );
		}
	}
	if ( nSubType )
	{
		CTerrorGun *pGun = dynamic_cast< CTerrorGun * >( hEntity.Get() );
		if ( pGun )
			pGun->m_inInitialPickup = false;
	}
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( hEntity.Get() );
	if ( pWeapon )
	{
		if ( nMelee != INVALID_MELEE_WEAPON_INFO_HANDLE )
		{
			CTerrorMeleeWeapon *pMelee = dynamic_cast< CTerrorMeleeWeapon * >( pWeapon );
			if ( pMelee )
			{
				pMelee->SetMeleeScriptName( pszName );
				pMelee->SetMeleeWeaponInfo( nMelee );
			}
		}
		DispatchSpawn( hEntity );
		const WEAPON_FILE_INFO_HANDLE hInfo = LookupWeaponInfoSlot( szClassname );
		if ( hInfo != GetInvalidWeaponInfoHandle() )
		{
			const CCSWeaponInfo *pInfo = dynamic_cast< const CCSWeaponInfo * >( GetFileWeaponInfoFromHandle( hInfo ) );
			const int nAmmoType = pInfo->iAmmoType;
			const int nMaxCarry = GetAmmoDef()->MaxCarry( nAmmoType, this );
			if ( nMaxCarry > 0 && pInfo->iMaxClip1 > 0 )
				pWeapon->SetExtraPrimaryAmmo( nMaxCarry );
		}
		pWeapon->SetForcePickup( true );
		if ( m_bRestoringFromTransition )
			pWeapon->SetPickedUpOnTransition();
	}
	m_bGivingNamedItem = true;
	if ( hEntity && !hEntity->IsMarkedForDeletion() )
	{
		CBaseCombatWeapon *pCombatWeapon = hEntity->MyCombatWeaponPointer();
		if ( pCombatWeapon )
		{
			if ( BumpWeapon( pCombatWeapon ) )
				pCombatWeapon->GiveTo( this );
		}
		else
			hEntity->Touch( this );
	}
	m_bGivingNamedItem = false;
	if ( pWeapon )
		pWeapon->SetForcePickup( false );
	if ( bForcePickup && pWeapon->GetOwner() != this )
	{
		UTIL_Remove( pWeapon );
		return NULL;
	}
	return hEntity.Get();
}

void CTerrorPlayer::EquipIncapacitatedWeapon()
{
	if ( !HasWeaponOfType( TERROR_WEAPONTYPE_PISTOL ) )
	{
		CBaseCombatWeapon *pSecondary = Weapon_GetSlot( 1 );
		if ( pSecondary )
		{
			Weapon_Detach( pSecondary );
			pSecondary->AddEffects( EF_NODRAW );
			m_hHiddenWeapon = pSecondary;
		}
		GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
	}
}

void CTerrorPlayer::RestoreIncapacitatedWeapon()
{
	if ( m_hHiddenWeapon )
	{
		CBaseCombatWeapon *pSecondary = Weapon_GetSlot( 1 );
		if ( pSecondary )
		{
			Weapon_Detach( pSecondary );
			UTIL_Remove( pSecondary );
		}
		CBaseCombatWeapon *pSaved = m_hHiddenWeapon.Get();
		if ( pSaved )
		{
			pSaved->RemoveEffects( EF_NODRAW );
			BumpWeapon( pSaved );
			m_hHiddenWeapon = NULL;
		}
	}
}

void CTerrorPlayer::RemoveHiddenWeapon()
{
	if ( m_hHiddenWeapon )
	{
		UTIL_Remove( m_hHiddenWeapon );
		m_hHiddenWeapon = NULL;
	}
}

static ConVar player_throwforce( "player_throwforce", "2000", 0x00006004 );

// Shared by the original hand-drop and item-pass paths. A model with bones
// but no matching player bone deliberately does not use the fallback bone.
static bool FindInventoryAttachment( CTerrorPlayer *pPlayer, CBaseCombatWeapon *pWeapon, const char *pszPlayerBone, const char *pszWeaponBone, int &nPlayerBone, int &nWeaponBone )
{
	nPlayerBone = -1;
	nWeaponBone = -1;
	if ( pszPlayerBone )
	{
		nPlayerBone = pPlayer->LookupBone( pszPlayerBone );
		nWeaponBone = pWeapon->LookupBone( pszWeaponBone ? pszWeaponBone : pszPlayerBone );
		return nPlayerBone != -1;
	}
	CStudioHdr *pStudio = pWeapon->GetModelPtr();
	if ( pStudio && pStudio->numbones() > 0 )
	{
		for ( nWeaponBone = 0; nWeaponBone < pStudio->numbones(); ++nWeaponBone )
		{
			nPlayerBone = pPlayer->LookupBone( pStudio->pBone( nWeaponBone )->pszName() );
			if ( nPlayerBone != -1 )
				return true;
		}
		return false;
	}
	nPlayerBone = pPlayer->LookupBone( "ValveBiped.Bip01_R_Hand" );
	return nPlayerBone != -1;
}

static void PlaceInventoryWeaponAtAttachment( CTerrorPlayer *pPlayer, CBaseCombatWeapon *pWeapon, int nPlayerBone, int nWeaponBone, Vector &position, QAngle &angles )
{
	matrix3x4_t playerBone, weaponBone, inverseWeaponBone, weaponToWorld;
	pPlayer->GetBoneTransform( nPlayerBone, playerBone );
	pWeapon->StopFollowingEntity();
	pWeapon->SetAbsOrigin( vec3_origin );
	pWeapon->SetAbsAngles( vec3_angle );
	pWeapon->InvalidateBoneCache();
	pWeapon->GetBoneTransform( nWeaponBone, weaponBone );
	MatrixInvert( weaponBone, inverseWeaponBone );
	ConcatTransforms( playerBone, inverseWeaponBone, weaponToWorld );
	MatrixAngles( weaponToWorld, angles, position );
	pWeapon->Teleport( &position, &angles, NULL );
}

void CTerrorPlayer::StoreWeaponReserveForDrop( CWeaponCSBase *pWeapon )
{
	const int nAmmoType = pWeapon->GetPrimaryAmmoType();
	if ( nAmmoType == -1 )
		return;
	if ( IsAlive() && GetHealth() > 0 )
	{
		for ( int i = 0; i < MAX_WEAPONS; ++i )
		{
			CBaseCombatWeapon *pOther = GetWeapon( i );
			if ( pOther && pOther != pWeapon && pOther->GetPrimaryAmmoType() == nAmmoType )
				return;
		}
	}
	pWeapon->SetExtraPrimaryAmmo( GetAmmoCount( nAmmoType ) );
	SetAmmoCount( 0, nAmmoType );
}

bool CTerrorPlayer::DropWeapon( CBaseCombatWeapon *pWeapon, bool bThrow, const Vector *pPosition )
{
	return pWeapon->GetOwner() == this && DropWeaponInternal( pWeapon, NULL, bThrow, pPosition );
}

bool CTerrorPlayer::DropWeaponInSlot( int nSlot )
{
	CBaseCombatWeapon *pWeapon = Weapon_GetSlot( nSlot );
	return pWeapon && DropWeaponInternal( pWeapon, NULL, false, NULL );
}

bool CTerrorPlayer::DropWeaponInternal( CBaseCombatWeapon *pWeapon, CBaseCombatWeapon *pUnused, bool bThrow, const Vector *pPosition )
{
	if ( !pWeapon )
		return false;
	CWeaponCSBase *pCSWeapon = dynamic_cast< CWeaponCSBase * >( pWeapon );
	if ( pCSWeapon && !pCSWeapon->CanBeDropped() )
		return false;

	const bool bHadNoReserve = GetAmmoCount( pCSWeapon->GetPrimaryAmmoType() ) == 0;
	Vector forward;
	AngleVectors( EyeAngles(), &forward );
	Vector target = WorldSpaceCenter();
	if ( pPosition )
		target = *pPosition;
	else if ( bThrow )
		target += forward * pCSWeapon->GetDropDistance();
	else
	{
		Vector backward( -forward.x, -forward.y, 0.0f );
		VectorNormalize( backward );
		target += backward * 48.0f;
	}

	Weapon_Drop( pWeapon, &target, NULL );
	if ( pCSWeapon->ShouldMergeWithWeaponSpawn() )
	{
		if ( pWeapon->GetSlot() == 2 && bHadNoReserve )
		{
			UTIL_Remove( pWeapon );
			return true;
		}
		if ( MergeDroppedTerrorWeaponWithSpawn( static_cast< CTerrorWeapon * >( pCSWeapon ) ) )
		{
			const int nAmmoType = pWeapon->GetPrimaryAmmoType();
			if ( nAmmoType != -1 )
			{
				for ( int i = 0; i < MAX_WEAPONS; ++i )
				{
					CBaseCombatWeapon *pOther = GetWeapon( i );
					if ( pOther && pOther != pWeapon && pOther->GetPrimaryAmmoType() == nAmmoType )
						return true;
				}
				RemoveAmmo( GetAmmoCount( nAmmoType ), nAmmoType );
			}
			return true;
		}
	}

	pWeapon->SetSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_STANDABLE | FSOLID_USE_TRIGGER_BOUNDS );
	pWeapon->SetRenderMode( kRenderTransColor );
	pCSWeapon->SetWeaponWorldModel( pWeapon->GetWorldModel() );
	StoreWeaponReserveForDrop( pCSWeapon );
	if ( pCSWeapon->ShouldUseHandAttachmentForDrop() )
	{
		CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
		int nPlayerBone, nWeaponBone;
		if ( !FindInventoryAttachment( this, pWeapon, NULL, NULL, nPlayerBone, nWeaponBone ) )
			return true;
		Vector position;
		QAngle angles;
		PlaceInventoryWeaponAtAttachment( this, pWeapon, nPlayerBone, nWeaponBone, position, angles );
		IPhysicsObject *pPhysics = pWeapon->VPhysicsGetObject();
		if ( pPhysics )
		{
			Vector velocity;
			AngularImpulse angular;
			pPhysics->GetPosition( &position, &angles );
			pPhysics->SetPosition( position, angles, true );
			velocity = GetAbsVelocity() + forward * 64.0f;
			angular.Init();
			pPhysics->SetVelocityInstantaneous( &velocity, &angular );
		}
		return true;
	}

	pWeapon->StopFollowingEntity();
	const Vector centerOffset = pWeapon->WorldSpaceCenter() - pWeapon->GetAbsOrigin();
	Vector direction;
	EyeVectors( &direction );
	direction.z *= 0.1f;
	VectorNormalize( direction );
	Vector destination = EyePosition() + direction * 20.0f - centerOffset;
	QAngle angles = vec3_angle;
	Vector mins, maxs;
	IPhysicsObject *pPhysics = pWeapon->VPhysicsGetObject();
	if ( pPhysics )
		physcollision->CollideGetAABB( &mins, &maxs, pPhysics->GetCollide(), vec3_origin, angles );
	else
	{
		mins = pWeapon->CollisionProp()->OBBMins();
		maxs = pWeapon->CollisionProp()->OBBMaxs();
	}
	CTraceFilterSkipTwoEntities filter( pWeapon, this, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceHull( EyePosition(), destination, mins, maxs, MASK_SOLID, &filter, &trace );
	destination = trace.startsolid ? EyePosition() : trace.endpos;
	pWeapon->Teleport( &destination, &angles, NULL );
	pWeapon->SetAbsVelocity( vec3_origin );
	pWeapon->SetAbsAngles( vec3_angle );
	if ( pPhysics )
	{
		Vector position;
		pPhysics->GetPosition( &position, NULL );
		pPhysics->SetPosition( position, angles, true );
		pPhysics->SetVelocity( &vec3_origin, &vec3_origin );
		if ( bThrow )
		{
			Vector launch;
			EyeVectors( &launch );
			const float mass = clamp( pPhysics->GetMass(), 0.5f, 15.0f );
			const float factor = ( ( mass - 0.5f ) * 3.5f ) * 0.068965517f + 0.5f;
			launch *= player_throwforce.GetFloat() * factor;
			pPhysics->ApplyForceCenter( launch );
			AngularImpulse angular;
			angular.x = ( (float)rand() * 0.000030518509f * 20.0f - 10.0f ) * factor;
			angular.y = ( (float)rand() * 0.000030518509f * 20.0f - 10.0f ) * factor;
			angular.z = ( (float)rand() * 0.000030518509f * 20.0f - 10.0f ) * factor;
			pPhysics->ApplyTorqueCenter( angular );
		}
	}
	return true;
}

bool CTerrorPlayer::ThrowWeapon( CWeaponCSBase *pWeapon, CTerrorPlayer *pTarget, float flSpeed, const char *pszPlayerBone, const char *pszWeaponBone )
{
	if ( !pWeapon || !pWeapon->CanBeDropped() )
		return false;
	Vector forward;
	AngleVectors( EyeAngles(), &forward );
	Vector dropTarget = GetAbsOrigin() + forward * flSpeed;
	dropTarget.z += GetViewOffset().z - 12.0f;
	Weapon_Drop( pWeapon, &dropTarget, NULL );
	pWeapon->SetSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_STANDABLE | FSOLID_USE_TRIGGER_BOUNDS );
	pWeapon->SetRenderMode( kRenderTransColor );
	pWeapon->SetWeaponWorldModel( pWeapon->GetTerrorWpnData().szWorldModel );
	StoreWeaponReserveForDrop( pWeapon );

	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	int nPlayerBone, nWeaponBone;
	if ( !FindInventoryAttachment( this, pWeapon, pszPlayerBone, pszWeaponBone, nPlayerBone, nWeaponBone ) )
		return true;
	Vector position;
	QAngle angles;
	PlaceInventoryWeaponAtAttachment( this, pWeapon, nPlayerBone, nWeaponBone, position, angles );
	Vector velocity;
	if ( pTarget && IsSurvivorTeam( pTarget ) )
	{
		Vector target = pTarget->GetAbsOrigin() + pTarget->GetViewOffset() + pTarget->GetAbsVelocity() * 0.5f;
		if ( ( target - GetAbsOrigin() ).Length2DSqr() > 16384.0f )
			target.z += 32.0f;
		velocity = target - position;
		VectorNormalize( velocity );
		if ( DotProduct( velocity, forward ) < 0.8f )
		{
			target = pTarget->GetLocalOrigin() + pTarget->GetViewOffset();
			if ( ( target - GetLocalOrigin() ).Length2DSqr() > 16384.0f )
				target.z += 32.0f;
			velocity = target - position;
			VectorNormalize( velocity );
		}
		velocity *= flSpeed;
	}
	else
	{
		Vector target = EyePosition() + forward * flSpeed;
		target.z += 32.0f;
		velocity = target - position;
	}
	IPhysicsObject *pPhysics = pWeapon->VPhysicsGetObject();
	if ( pPhysics )
	{
		pPhysics->GetPosition( &position, &angles );
		pPhysics->SetPosition( position, angles, true );
		AngularImpulse angular = vec3_origin;
		pPhysics->GetVelocity( NULL, &angular );
		velocity += GetAbsVelocity();
		pPhysics->SetVelocity( &velocity, &angular );
	}
	return true;
}

static bool IsBeltPassScanCandidate( CTerrorPlayer *pGiver, CTerrorPlayer *pCandidate, const Vector &scanForward )
{
	if ( pCandidate == pGiver || !pCandidate->IsAlive() || pCandidate->GetTeamNumber() != TEAM_SURVIVOR || pCandidate->IsGhost() )
		return false;
	const Vector start = pGiver->EyePosition();
	Vector firstClosest;
	CalcClosestPointOnLineSegment( pCandidate->WorldSpaceCenter(), start, start + scanForward * 50.0f, firstClosest );
	Vector verticalEnd = pCandidate->GetAbsOrigin();
	verticalEnd.z += pCandidate->CollisionProp()->OBBMaxs().z;
	Vector target;
	CalcClosestPointOnLineSegment( firstClosest, pCandidate->GetAbsOrigin(), verticalEnd, target );
	Vector delta = target - start;
	VectorNormalize( delta );
	Vector giverNearest, candidateNearest;
	pGiver->CollisionProp()->CalcNearestPoint( target, &giverNearest );
	pCandidate->CollisionProp()->CalcNearestPoint( giverNearest, &candidateNearest );
	if ( ( giverNearest - candidateNearest ).Length() > 256.0f || DotProduct( scanForward, delta ) <= 1.0f - 0.3f )
		return false;
	trace_t trace;
	UTIL_TraceLine( pGiver->EyePosition(), pCandidate->EyePosition(), 81931, pGiver, COLLISION_GROUP_NONE, &trace );
	return trace.fraction >= 1.0f;
}

void CTerrorPlayer::GiveBeltItem( CTerrorPlayer *pTarget )
{
	if ( GetTeamNumber() != TEAM_SURVIVOR || !IsAlive() || IsIncapacitated() )
		return;
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( !pWeapon || pWeapon->GetSlot() != 4 )
		return;

	// TargetScanNotOccluded stores EyeAngles at construction, before the
	// independent EyeVectors call used by the final nearest-recipient scan.
	Vector scanForward, forward;
	AngleVectors( EyeAngles(), &scanForward );
	CUtlVector< CTerrorPlayer * > candidates;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pCandidate = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pCandidate && pCandidate->edict() && pCandidate->entindex() && pCandidate->IsConnected() &&
			IsBeltPassScanCandidate( this, pCandidate, scanForward ) )
			candidates.AddToTail( pCandidate );
	}
	EyeVectors( &forward );
	float flNearest = 16384.0f;
	FOR_EACH_VEC( candidates, i )
	{
		CTerrorPlayer *pCandidate = candidates[i];
		if ( pCandidate->Weapon_GetSlot( 4 ) )
			continue;
		const float flDistance = ( pCandidate->EyePosition() - EyePosition() ).LengthSqr();
		if ( flNearest > flDistance &&
			CalcDistanceSqrToLineSegment( pCandidate->EyePosition(), EyePosition(), EyePosition() + forward * 256.0f ) <= 1024.0f )
		{
			pTarget = pCandidate;
			flNearest = flDistance;
		}
	}
	if ( pTarget && !pTarget->Weapon_GetSlot( pWeapon->GetSlot() ) )
	{
		ThrowWeapon( pWeapon, pTarget, 512.0f, NULL, NULL );
		pWeapon->GiveToPlayer( this, pTarget );
		if ( !SpeakIfAllowed( "OfferItem", 1, NULL, NULL, 0, NULL ) )
			SpeakIfAllowed( "PlayerAlertGiveItem", 1, NULL, NULL, 0, NULL );
	}
}

void CTerrorPlayer::OnAmmoPickup()
{
	SpeakIfAllowed( "PlayerPickup", 3, "ItemPickedUp:Ammo", NULL, 0, NULL );
	EmitSound( "BaseCombatCharacter.AmmoPickup" );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "ammo_pickup" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}

void CTerrorPlayer::OnPickedUpWeapon( CBaseCombatWeapon *pCombatWeapon )
{
	if ( m_stateEnterTimer.GetStartTime() > 0.0f && m_stateEnterTimer.GetElapsedTime() < 1.0f )
		return;
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pCombatWeapon );
	CTerrorPlayer *pDonor = ToTerrorPlayer( pWeapon->GetPreviousOwner() );
	char modifiers[256] = "";
	if ( pDonor )
	{
		Q_strncat( modifiers, "ItemDonor:", sizeof( modifiers ) );
		Q_strncat( modifiers, GetSurvivorCharacterName( pDonor->GetSurvivorCharacter() ), sizeof( modifiers ) );
		Q_strncat( modifiers, ",", sizeof( modifiers ) );
	}
	Q_strncat( modifiers, "ItemPickedUp:", sizeof( modifiers ) );
	Q_strncat( modifiers, pWeapon->GetResponseRulesName(), sizeof( modifiers ) );
	const char *pszType = WeaponTypeToString( pWeapon->GetTerrorWpnData().m_WeaponType );
	if ( pszType )
	{
		Q_strncat( modifiers, ",ItemType:", sizeof( modifiers ) );
		Q_strncat( modifiers, pszType, sizeof( modifiers ) );
	}
	if ( pWeapon->GetWeaponID() != TERROR_WEAPON_PISTOL || pWeapon->IsDualWielding() )
	{
		SpeakIfAllowed( "PlayerPickup", 3, modifiers, NULL, 0, NULL );
		TerrorGameRules()->RemoveItemFromSpawnManager( pWeapon );
		RumbleEffect( 34, 0, RUMBLE_FLAG_RESTART );
	}
}

// The player_pickup controller is the pinned game's ordinary USE physics
// holder. It has no physcannon weapon, mega-cannon, sprint, or haptics policy.
struct TerrorShadowControlParams : public hlshadowcontrol_params_t
{
	DECLARE_SIMPLE_DATADESC();
};

datamap_t TerrorShadowControlParams::m_DataMap = { 0, 0, "game_shadowcontrol_params_t", NULL };
datamap_t *TerrorShadowControlParams::GetBaseMap() { return NULL; }
BEGIN_DATADESC_GUTS( TerrorShadowControlParams )
	DEFINE_FIELD( targetPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( targetRotation, FIELD_VECTOR ),
	DEFINE_FIELD( maxAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( dampFactor, FIELD_FLOAT ),
	DEFINE_FIELD( teleportDistance, FIELD_FLOAT ),
END_DATADESC()

class CGrabController : public IMotionEvent
{
public:
	DECLARE_SIMPLE_DATADESC();
	CGrabController();
	~CGrabController();
	void AttachEntity( CTerrorPlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhysics );
	void DetachEntity( bool bClearVelocity );
	void OnRestore();
	void SetTargetPosition( const Vector &position, const QAngle &angles );
	float ComputeError();
	bool UpdateObject( CTerrorPlayer *pPlayer, float flError );
	CBaseEntity *GetAttached() const { return m_attachedEntity.Get(); }
	bool IsHoldingEntity( CBaseEntity *pEntity ) const { return pEntity ? GetAttached() == pEntity : GetAttached() != NULL; }
	float GetSavedMass( IPhysicsObject *pPhysics ) const;
	void SetIgnorePitch( bool bIgnore ) { m_bIgnoreRelativePitch = bIgnore; }
	void SetAngleAlignment( float flAlignment ) { m_angleAlignment = flAlignment; }
	virtual simresult_e Simulate( IPhysicsMotionController *, IPhysicsObject *, float, Vector &, AngularImpulse & ) OVERRIDE;

	IPhysicsMotionController *m_controller;

private:
	QAngle TransformAnglesToPlayerSpace( const QAngle &angles, CTerrorPlayer *pPlayer ) const;
	QAngle TransformAnglesFromPlayerSpace( const QAngle &angles, CTerrorPlayer *pPlayer ) const;
	TerrorShadowControlParams m_shadow;
	float m_timeToArrive;
	float m_errorTime;
	float m_error;
	float m_contactAmount;
	float m_angleAlignment;
	bool m_bCarriedEntityBlocksLOS;
	bool m_bIgnoreRelativePitch;
	float m_flLoadWeight;
	float m_savedRotDamping[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	float m_savedMass[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	EHANDLE m_attachedEntity;
	QAngle m_vecPreferredCarryAngles;
	bool m_bHasPreferredCarryAngles;
	QAngle m_attachedAnglesPlayerSpace;
	Vector m_attachedPositionObjectSpace;
	int m_lastUpdateFrame;
};

BEGIN_SIMPLE_DATADESC( CGrabController )
	DEFINE_EMBEDDED( m_shadow ),
	DEFINE_FIELD( m_timeToArrive, FIELD_FLOAT ),
	DEFINE_FIELD( m_errorTime, FIELD_FLOAT ),
	DEFINE_FIELD( m_error, FIELD_FLOAT ),
	DEFINE_FIELD( m_contactAmount, FIELD_FLOAT ),
	DEFINE_FIELD( m_angleAlignment, FIELD_FLOAT ),
	DEFINE_FIELD( m_bCarriedEntityBlocksLOS, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bIgnoreRelativePitch, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flLoadWeight, FIELD_FLOAT ),
	DEFINE_AUTO_ARRAY( m_savedRotDamping, FIELD_FLOAT ),
	DEFINE_AUTO_ARRAY( m_savedMass, FIELD_FLOAT ),
	DEFINE_FIELD( m_attachedEntity, FIELD_EHANDLE ),
	DEFINE_FIELD( m_vecPreferredCarryAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_bHasPreferredCarryAngles, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_attachedAnglesPlayerSpace, FIELD_VECTOR ),
	DEFINE_FIELD( m_attachedPositionObjectSpace, FIELD_VECTOR ),
END_DATADESC()

CGrabController::CGrabController()
{
	m_shadow.dampFactor = 1.0f;
	m_shadow.teleportDistance = 0.0f;
	m_errorTime = 0.0f;
	m_error = 0.0f;
	m_shadow.maxSpeed = 1000.0f;
	m_shadow.maxAngular = 3600.0f;
	m_shadow.maxDampSpeed = 2000.0f;
	m_shadow.maxDampAngular = 3600.0f;
	m_attachedEntity = NULL;
	m_vecPreferredCarryAngles = vec3_angle;
	m_bHasPreferredCarryAngles = false;
	m_controller = NULL;
	m_lastUpdateFrame = 0;
}

CGrabController::~CGrabController()
{
	DetachEntity( false );
}

void CGrabController::OnRestore()
{
	if ( m_controller )
		m_controller->SetEventHandler( this );
}

void CGrabController::SetTargetPosition( const Vector &position, const QAngle &angles )
{
	m_shadow.targetPosition = position;
	m_shadow.targetRotation = angles;
	m_timeToArrive = gpGlobals->frametime;
	CBaseEntity *pAttached = GetAttached();
	if ( pAttached )
	{
		IPhysicsObject *pPhysics = pAttached->VPhysicsGetObject();
		if ( pPhysics )
			pPhysics->Wake();
		else
			DetachEntity( false );
	}
}

float CGrabController::ComputeError()
{
	if ( m_errorTime <= 0.0f )
		return 0.0f;
	CBaseEntity *pAttached = GetAttached();
	if ( pAttached )
	{
		IPhysicsObject *pPhysics = pAttached->VPhysicsGetObject();
		if ( !pPhysics )
		{
			DevMsg( "Object attached to Physcannon has no physics object\n" );
			DetachEntity( false );
			return 9999.0f;
		}
		Vector position;
		pPhysics->GetShadowPosition( &position, NULL );
		float error = ( m_shadow.targetPosition - position ).Length();
		if ( m_errorTime > 0.0f )
		{
			if ( m_errorTime > 1.0f )
				m_errorTime = 1.0f;
			if ( error / m_errorTime > m_shadow.maxSpeed )
				error *= 0.5f;
			m_error = ( 1.0f - m_errorTime ) * m_error + error * m_errorTime;
		}
	}
	if ( pAttached->IsEFlagSet( EFL_IS_BEING_LIFTED_BY_BARNACLE ) )
		m_error *= 3.0f;
	m_errorTime = 0.0f;
	return m_error;
}

static QAngle AlignInventoryCarryAngles( const QAngle &angles, float flCosine )
{
	matrix3x4_t matrix;
	AngleMatrix( angles, matrix );
	for ( int column = 2; column >= 0; --column )
	{
		Vector axis;
		MatrixGetColumn( matrix, column, axis );
		for ( int i = 0; i < 3; ++i )
		{
			if ( fabs( axis[i] ) > flCosine )
			{
				axis[i] = axis[i] < 0.0f ? -1.0f : 1.0f;
				axis[( i + 1 ) % 3] = 0.0f;
				axis[( i + 2 ) % 3] = 0.0f;
				MatrixSetColumn( axis, column, matrix );
				Vector columns[3];
				for ( int j = 0; j < 3; ++j )
					MatrixGetColumn( matrix, j, columns[j] );
				const int nNext = ( column + 1 ) % 3;
				const int nLast = ( column + 2 ) % 3;
				columns[nLast] = CrossProduct( columns[column], columns[nNext] );
				columns[nNext] = CrossProduct( columns[nLast], columns[column] );
				VectorNormalize( columns[nLast] );
				VectorNormalize( columns[nNext] );
				MatrixSetColumn( columns[nNext], nNext, matrix );
				MatrixSetColumn( columns[nLast], nLast, matrix );
				break;
			}
		}
	}
	QAngle result;
	MatrixAngles( matrix, result );
	return result;
}

QAngle CGrabController::TransformAnglesToPlayerSpace( const QAngle &angles, CTerrorPlayer *pPlayer ) const
{
	if ( m_bIgnoreRelativePitch )
	{
		QAngle playerAngles = pPlayer->EyeAngles();
		playerAngles.x = 0.0f;
		matrix3x4_t matrix;
		AngleMatrix( playerAngles, matrix );
		return TransformAnglesToLocalSpace( angles, matrix );
	}
	return TransformAnglesToLocalSpace( angles, pPlayer->EntityToWorldTransform() );
}

QAngle CGrabController::TransformAnglesFromPlayerSpace( const QAngle &angles, CTerrorPlayer *pPlayer ) const
{
	if ( m_bIgnoreRelativePitch )
	{
		QAngle playerAngles = pPlayer->EyeAngles();
		playerAngles.x = 0.0f;
		matrix3x4_t matrix;
		AngleMatrix( playerAngles, matrix );
		return TransformAnglesToWorldSpace( angles, matrix );
	}
	return TransformAnglesToWorldSpace( angles, pPlayer->EntityToWorldTransform() );
}

void CGrabController::AttachEntity( CTerrorPlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhysics )
{
	if ( pPlayer->VPhysicsGetObject() )
		PhysicsImpactSound( pPlayer, pPhysics, CHAN_STATIC, pPhysics->GetMaterialIndex(), pPlayer->VPhysicsGetObject()->GetMaterialIndex(), 1.0f, 64.0f );
	Vector position;
	QAngle angles;
	pPhysics->GetPosition( &position, &angles );
	Pickup_GetPreferredCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );
	m_bCarriedEntityBlocksLOS = pEntity->BlocksLOS();
	pEntity->SetBlocksLOS( false );
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhysics, true );
	pPhysics->Wake();
	PhysSetGameFlags( pPhysics, FVPHYSICS_PLAYER_HELD );
	SetTargetPosition( position, angles );
	m_attachedEntity = pEntity;
	IPhysicsObject *objects[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	const int count = pEntity->VPhysicsGetObjectList( objects, ARRAYSIZE( objects ) );
	m_flLoadWeight = 0.0f;
	const float damping = 10.0f;
	float factor = (float)count * 0.13333334f;
	if ( factor < 1.0f )
		factor = 1.0f;
	for ( int i = 0; i < count; ++i )
	{
		const float mass = objects[i]->GetMass();
		objects[i]->GetDamping( NULL, &m_savedRotDamping[i] );
		m_flLoadWeight += mass;
		m_savedMass[i] = mass;
		objects[i]->SetMass( 1.0f / factor );
		objects[i]->SetDamping( NULL, &damping );
	}
	pPhysics->SetMass( 1.0f );
	pPhysics->EnableDrag( false );
	m_errorTime = -1.0f;
	m_error = 0.0f;
	m_contactAmount = 0.0f;
	m_attachedAnglesPlayerSpace = TransformAnglesToPlayerSpace( angles, pPlayer );
	if ( m_angleAlignment != 0.0f )
		m_attachedAnglesPlayerSpace = AlignInventoryCarryAngles( m_attachedAnglesPlayerSpace, m_angleAlignment );
	VectorITransform( pEntity->WorldSpaceCenter(), pEntity->EntityToWorldTransform(), m_attachedPositionObjectSpace );
	CPhysicsProp *pProp = dynamic_cast< CPhysicsProp * >( pEntity );
	m_bHasPreferredCarryAngles = pProp && pProp->GetPropDataAngles( "preferred_carryangles", m_vecPreferredCarryAngles );
}

static void ClampInventoryPhysicsVelocity( IPhysicsObject *pPhysics )
{
	Vector velocity;
	AngularImpulse angular;
	pPhysics->GetVelocity( &velocity, &angular );
	float speed = VectorNormalize( velocity ) - 375.0f;
	float angularSpeed = VectorNormalize( angular ) - 720.0f;
	speed = speed < 0.0f ? 0.0f : -speed;
	angularSpeed = angularSpeed < 0.0f ? 0.0f : -angularSpeed;
	velocity *= speed;
	angular *= angularSpeed;
	pPhysics->AddVelocity( &velocity, &angular );
}

void CGrabController::DetachEntity( bool bClearVelocity )
{
	Assert( !PhysIsInCallback() );
	CBaseEntity *pEntity = GetAttached();
	if ( pEntity )
	{
		pEntity->SetBlocksLOS( m_bCarriedEntityBlocksLOS );
		IPhysicsObject *objects[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		const int count = pEntity->VPhysicsGetObjectList( objects, ARRAYSIZE( objects ) );
		for ( int i = 0; i < count; ++i )
		{
			IPhysicsObject *pPhysics = objects[i];
			if ( !pPhysics )
				continue;
			pPhysics->EnableDrag( true );
			pPhysics->Wake();
			pPhysics->SetMass( m_savedMass[i] );
			pPhysics->SetDamping( NULL, &m_savedRotDamping[i] );
			PhysClearGameFlags( pPhysics, FVPHYSICS_PLAYER_HELD );
			if ( bClearVelocity )
				PhysForceClearVelocity( pPhysics );
			else
				ClampInventoryPhysicsVelocity( pPhysics );
		}
	}
	m_attachedEntity = NULL;
	if ( physenv )
		physenv->DestroyMotionController( m_controller );
	m_controller = NULL;
}

static bool InventoryObjectHasHeavyContact( IPhysicsObject *pPhysics, float flMass )
{
	bool bContact = false;
	IPhysicsFrictionSnapshot *pSnapshot = pPhysics->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject( 1 );
		if ( !pOther->IsMoveable() || pOther->GetMass() > flMass )
		{
			bContact = true;
			break;
		}
		pSnapshot->NextFrictionData();
	}
	pPhysics->DestroyFrictionSnapshot( pSnapshot );
	return bContact;
}

IMotionEvent::simresult_e CGrabController::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pPhysics, float flDeltaTime, Vector &linear, AngularImpulse &angular )
{
	TerrorShadowControlParams params = m_shadow;
	m_contactAmount = Approach( InventoryObjectHasHeavyContact( pPhysics, m_flLoadWeight ) ? 0.1f : 1.0f, m_contactAmount, flDeltaTime * 2.0f );
	params.maxAngular = m_shadow.maxAngular * m_contactAmount * m_contactAmount * m_contactAmount;
	m_timeToArrive = pPhysics->ComputeShadowControl( params, m_timeToArrive, flDeltaTime );
	Vector velocity;
	AngularImpulse angularVelocity;
	pPhysics->GetVelocity( &velocity, &angularVelocity );
	PhysComputeSlideDirection( pPhysics, velocity, angularVelocity, &velocity, &angularVelocity, m_flLoadWeight );
	pPhysics->SetVelocityInstantaneous( &velocity, NULL );
	linear.Init();
	angular.Init();
	m_errorTime += flDeltaTime;
	return SIM_LOCAL_ACCELERATION;
}

float CGrabController::GetSavedMass( IPhysicsObject *pPhysics ) const
{
	CBaseEntity *pHeld = GetAttached();
	if ( pHeld && pPhysics->GetGameData() == pHeld )
	{
		IPhysicsObject *objects[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		const int count = pHeld->VPhysicsGetObjectList( objects, ARRAYSIZE( objects ) );
		for ( int i = 0; i < count; ++i )
			if ( objects[i] == pPhysics )
				return m_savedMass[i];
	}
	return 0.0f;
}

bool CGrabController::UpdateObject( CTerrorPlayer *pPlayer, float flError )
{
	CBaseEntity *pEntity = GetAttached();
	if ( !pEntity || ComputeError() > flError || pPlayer->GetGroundEntity() == pEntity )
		return false;
	IPhysicsObject *pPhysics = pEntity->VPhysicsGetObject();
	if ( !pPhysics || !pPhysics->IsMoveable() )
		return false;
	if ( m_lastUpdateFrame == gpGlobals->framecount )
		return true;
	m_lastUpdateFrame = gpGlobals->framecount;

	QAngle playerAngles = pPlayer->EyeAngles();
	playerAngles.x = clamp( AngleDistance( playerAngles.x, 0.0f ), -75.0f, 75.0f );
	Vector forward;
	AngleVectors( playerAngles, &forward );
	const Vector radial = physcollision->CollideGetExtent( pPhysics->GetCollide(), vec3_origin, pEntity->GetAbsAngles(), -forward );
	const float radius = pPlayer->CollisionProp()->OBBMaxs().Length2D() + fabs( DotProduct( forward, radial ) );
	const float distance = 24.0f + radius * 2.0f;
	const Vector start = pPlayer->Weapon_ShootPosition();
	Vector end = start + forward * distance;
	CTraceFilterSkipTwoEntities filter( pPlayer, pEntity, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, &filter, &trace );
	if ( trace.fraction < 0.5f )
		end = start + forward * ( radius * 0.5f );
	else if ( trace.fraction <= 1.0f )
		end = start + forward * ( distance - radius );
	Vector mins, maxs;
	pPlayer->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	Vector center = pPlayer->CollisionProp()->WorldSpaceCenter();
	Vector nearest;
	CalcClosestPointOnLine( end, center + Vector( 0, 0, mins.z ), center + Vector( 0, 0, maxs.z ), nearest, NULL );
	Vector delta = end - nearest;
	const float length = VectorNormalize( delta );
	if ( radius > length )
		end = nearest + delta * radius;

	QAngle angles = TransformAnglesFromPlayerSpace( m_attachedAnglesPlayerSpace, pPlayer );
	Pickup_GetPreferredCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );
	if ( m_bHasPreferredCarryAngles )
	{
		QAngle carryAngles = pPlayer->EyeAngles();
		carryAngles.x = 0.0f;
		Vector carryOrigin = pPlayer->EyePosition();
		carryOrigin.z -= 8.0f;
		matrix3x4_t playerMatrix;
		AngleMatrix( carryAngles, carryOrigin, playerMatrix );
		angles = TransformAnglesToWorldSpace( m_vecPreferredCarryAngles, playerMatrix );
	}
	matrix3x4_t attachedToWorld;
	AngleMatrix( angles, attachedToWorld );
	Vector offset;
	VectorRotate( m_attachedPositionObjectSpace, attachedToWorld, offset );
	SetTargetPosition( end - offset, angles );
	return true;
}

class CPlayerPickupController : public CBaseEntity
{
public:
	DECLARE_CLASS( CPlayerPickupController, CBaseEntity );
	DECLARE_DATADESC();
	void Init( CTerrorPlayer *pPlayer, CBaseEntity *pObject );
	void Shutdown( bool bThrown = false );
	virtual bool OnControls( CBaseEntity *pControls ) OVERRIDE { return true; }
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE type, float value ) OVERRIDE;
	virtual void OnRestore() OVERRIDE { m_grabController.OnRestore(); }
	virtual void VPhysicsUpdate( IPhysicsObject *pPhysics ) OVERRIDE {}
	virtual void VPhysicsShadowUpdate( IPhysicsObject *pPhysics ) OVERRIDE {}
	bool IsHoldingEntity( CBaseEntity *pEntity ) const { return m_grabController.IsHoldingEntity( pEntity ); }
	float GetSavedMass( IPhysicsObject *pPhysics ) const { return m_grabController.GetSavedMass( pPhysics ); }
	CBaseEntity *GetHeldEntity() const { return m_grabController.GetAttached(); }

private:
	CGrabController m_grabController;
	CTerrorPlayer *m_pPlayer;
};

LINK_ENTITY_TO_CLASS( player_pickup, CPlayerPickupController );

BEGIN_DATADESC( CPlayerPickupController )
	DEFINE_EMBEDDED( m_grabController ),
	DEFINE_PHYSPTR( m_grabController.m_controller ),
	DEFINE_FIELD( m_pPlayer, FIELD_CLASSPTR ),
END_DATADESC()

void CPlayerPickupController::Init( CTerrorPlayer *pPlayer, CBaseEntity *pObject )
{
	if ( pPlayer->GetActiveWeapon() && !pPlayer->GetActiveWeapon()->Holster( NULL ) )
	{
		Shutdown();
		return;
	}
	if ( pObject->GetCollisionGroup() == COLLISION_GROUP_DEBRIS )
		pObject->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
	SetParent( pPlayer );
	m_grabController.SetIgnorePitch( true );
	m_grabController.SetAngleAlignment( DOT_30DEGREE );
	m_pPlayer = pPlayer;
	// The original player holder passes numeric reason zero, even though the
	// native SDK names zero PICKED_UP_BY_CANNON. Preserve the callback value.
	Pickup_OnPhysGunPickup( pObject, pPlayer, static_cast< PhysGunPickup_t >( 0 ) );
	m_grabController.AttachEntity( pPlayer, pObject, pObject->VPhysicsGetObject() );
	m_pPlayer->m_Local.m_iHideHUD |= HIDEHUD_WEAPONSELECTION;
	m_pPlayer->SetUseEntity( this );
	m_pPlayer->OnUseEntityChanged();
}

void CPlayerPickupController::Shutdown( bool bThrown )
{
	CBaseEntity *pObject = m_grabController.GetAttached();
	bool bClearVelocity = false;
	if ( !bThrown && pObject && pObject->VPhysicsGetObject() )
		bClearVelocity = pObject->VPhysicsGetObject()->GetContactPoint( NULL, NULL );
	m_grabController.DetachEntity( bClearVelocity );
	if ( pObject )
		Pickup_OnPhysGunDrop( pObject, m_pPlayer, bThrown ? THROWN_BY_PLAYER : DROPPED_BY_PLAYER );
	if ( m_pPlayer )
	{
		m_pPlayer->SetUseEntity( NULL );
		m_pPlayer->OnUseEntityChanged();
		CBaseCombatWeapon *pActive = m_pPlayer->GetActiveWeapon();
		if ( pActive && !pActive->Deploy() )
			m_pPlayer->SwitchToNextBestWeapon( NULL );
		m_pPlayer->m_Local.m_iHideHUD &= ~HIDEHUD_WEAPONSELECTION;
	}
	Remove();
}

void CPlayerPickupController::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE type, float value )
{
	if ( ToTerrorPlayer( pActivator ) != m_pPlayer )
		return;
	CBaseEntity *pAttached = m_grabController.GetAttached();
	if ( !pAttached || type == USE_OFF || m_grabController.ComputeError() > 12.0f )
	{
		Shutdown();
		return;
	}
	IPhysicsObject *pPhysics = pAttached->VPhysicsGetObject();
	if ( pPhysics && !pPhysics->IsMoveable() )
	{
		Shutdown();
		return;
	}
	if ( m_pPlayer->m_nButtons & IN_ATTACK )
	{
		Shutdown( true );
		Vector launch;
		m_pPlayer->EyeVectors( &launch );
		const float mass = clamp( pPhysics->GetMass(), 0.5f, 15.0f );
		const float factor = RemapVal( mass, 0.5f, 15.0f, 0.5f, 4.0f );
		launch *= player_throwforce.GetFloat() * factor;
		pPhysics->ApplyForceCenter( launch );
		AngularImpulse angular = RandomAngularImpulse( -10, 10 ) * factor;
		pPhysics->ApplyTorqueCenter( angular );
	}
	else if ( m_pPlayer->m_nButtons & IN_ATTACK2 )
		Shutdown( true );
	else if ( type == USE_SET )
		m_grabController.UpdateObject( m_pPlayer, 12.0f );
}

void PlayerPickupObject( CBasePlayer *pPlayer, CBaseEntity *pObject )
{
	if ( !pObject->VPhysicsGetObject() )
		return;
	CPlayerPickupController *pController = static_cast< CPlayerPickupController * >( CBaseEntity::Create( "player_pickup", pObject->GetAbsOrigin(), vec3_angle, pPlayer ) );
	if ( pController )
		pController->Init( static_cast< CTerrorPlayer * >( pPlayer ), pObject );
}

bool CTerrorPlayer::IsHoldingEntity( CBaseEntity *pEntity )
{
	CPlayerPickupController *pController = dynamic_cast< CPlayerPickupController * >( GetUseEntity() );
	return pController && pController->IsHoldingEntity( pEntity );
}

CBaseEntity *CTerrorPlayer::GetHeldEntity()
{
	// S2FBE50 ->2B4930: real use-entity handle and existing TU-private grab controller.
	CPlayerPickupController *controller = dynamic_cast< CPlayerPickupController * >( GetUseEntity() );
	return controller ? controller->GetHeldEntity() : NULL;
}

void CTerrorPlayer::UpdateHoldingObjectState()
{
	m_holdingObject = IsHoldingEntity( NULL );
}

void CTerrorPlayer::ForceDropOfCarriedPhysObjects( CBaseEntity *pOnlyIfHoldingThis )
{
	// S:0x3144F0 ignores its argument, releases ANY use entity, then push.
	ClearUseEntity();
	SetPushEntity( NULL );
}

float CTerrorPlayer::GetHeldObjectMass( IPhysicsObject *pHeldObject )
{
	CPlayerPickupController *pController = dynamic_cast< CPlayerPickupController * >( GetUseEntity() );
	return pController ? pController->GetSavedMass( pHeldObject ) : 0.0f;
}

void CTerrorPlayer::PickupObject( CBaseEntity *pObject, bool bLimitMassAndSize )
{
	if ( !IsSurvivorTeam( this ) )
		return;
	m_hLastPickupObject = pObject;
	if ( pObject->ClassMatches( "prop_physics" ) )
	{
		const char *pszModel = STRING( pObject->GetModelName() );
		for ( int i = 0; i < ARRAYSIZE( m_weaponIndexByID ); ++i )
		{
			const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( i );
			if ( pInfo && !Q_stricmp( pInfo->szWorldModel, pszModel ) )
			{
				if ( GiveNamedItemInternal( pInfo->szClassName, 0, true, pObject ) )
					UTIL_Remove( pObject );
				return;
			}
		}
	}
	if ( !TerrorGameRules()->CanPickupObject( this, pObject ) )
		return;
	// The extra ground-nonnull check is present in the pinned original.
	if ( GetGroundEntity() == pObject || !GetGroundEntity() )
		return;
	if ( bLimitMassAndSize && !CBasePlayer::CanPickupObject( pObject, 35.0f, 128.0f ) )
		return;
	if ( pObject->HasNPCsOnIt() || dynamic_cast< CWeaponCSBase * >( pObject ) )
		return;
	PlayerPickupObject( this, pObject );
}

void CTerrorPlayer::OnGivenWeapon( CBaseCombatWeapon *pWeapon )
{
	Weapon_Switch( pWeapon, 0 );
	SpeakIfAllowed( "PlayerGivenItem", 1, NULL, NULL, 0, NULL );
}

void CTerrorPlayer::GiveDefaultItems()
{
	// S:0x2FAED0. A present GetDefaultItem callback handles the whole
	// loadout, even when its first result is not a string.
	if ( !IsSurvivorTeam( this ) || TerrorGameRules()->GiveDefaultItemsFromScript( this ) )
		return;

	GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
	if ( ( TerrorGameRules()->IsSurvivalMode() || TerrorGameRules()->IsScavengeMode() ) &&
		!m_bRespawningFromDefibrillator )
	{
		GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
		GiveNamedItemInternal( "first_aid_kit", 0, true, NULL );
		GiveNamedItemInternal( "pain_pills", 0, true, NULL );
	}

	// This is after all real acquisitions. It intentionally clears ammo
	// supplied by those pickups before restoring only these two types.
	RemoveAllAmmo();
	CAmmoDef *pAmmoDef = GetAmmoDef();
	GiveAmmo( pAmmoDef->MaxCarry( pAmmoDef->Index( "AMMO_TYPE_PISTOL" ), this ), "AMMO_TYPE_PISTOL", false );
	GiveAmmo( pAmmoDef->MaxCarry( pAmmoDef->Index( "AMMO_TYPE_PISTOL_MAGNUM" ), this ), "AMMO_TYPE_PISTOL_MAGNUM", false );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::RemoveAmmo( int count, int ammoIndex )
{
	// S303750 returns only for a negative index. Substitute zero only at an
	// undefined array read; base removal and the real MaxCarry query still run.
	if ( ammoIndex < 0 ) return;
	const int previous = ammoIndex < MAX_AMMO_SLOTS ? m_iAmmo[ammoIndex] : 0;
	BaseClass::RemoveAmmo( count, ammoIndex );
	const int maximum = GetAmmoDef()->MaxCarry( ammoIndex, this );
	// Native excludes -2 and POSITIVE 1, not -1.
	if ( maximum == -2 || maximum == 1 ) return;
	if ( static_cast< float >( previous ) / static_cast< float >( maximum ) >= 0.4f &&
		static_cast< float >( ammoIndex < MAX_AMMO_SLOTS ? m_iAmmo[ammoIndex] : 0 ) / static_cast< float >( maximum ) < 0.4f )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "total_ammo_below_40" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			gameeventmanager->FireEvent( event );
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#include "l4d2/player/terrorplayer_inventory_console_server_callbacks.inc"
