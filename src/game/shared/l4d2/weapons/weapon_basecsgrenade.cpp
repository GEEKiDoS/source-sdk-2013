//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "activitylist.h"
#include "in_buttons.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "igameevents.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( weapon_basecsgrenade, CBaseCSGrenade );
IMPLEMENT_CLIENTCLASS( C_BaseCSGrenade, DT_BaseCSGrenade, CBaseCSGrenade )
#else
IMPLEMENT_SERVERCLASS( CBaseCSGrenade, DT_BaseCSGrenade )
#endif
BEGIN_NETWORK_TABLE( CBaseCSGrenade, DT_BaseCSGrenade )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bRedraw ) ),
	RecvPropBool( RECVINFO( m_bPinPulled ) ),
	RecvPropFloat( RECVINFO( m_fThrowTime ) ),
	RecvPropBool( RECVINFO( m_isGoingToRoll ) ),
#else
	SendPropBool( SENDINFO( m_bRedraw ) ),
	SendPropBool( SENDINFO( m_bPinPulled ) ),
	SendPropFloat( SENDINFO( m_fThrowTime ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_isGoingToRoll ) ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CBaseCSGrenade )
	DEFINE_PRED_FIELD( m_bRedraw, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_bPinPulled, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseCSGrenade )
	DEFINE_FIELD( m_bRedraw, FIELD_BOOLEAN ),
END_DATADESC()
#endif

static Activity GrenadeActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

CBaseCSGrenade::CBaseCSGrenade()
{
	m_bRedraw = false;
	m_bPinPulled = false;
	m_fThrowTime = 0.0f;
	m_isGoingToRoll = false;
	m_bPinPullStarted = false;
	m_bHasBeenPickedUp = false;
}

// S:0x24D7C0. Inventory calls this after its original pickup/given event.
void CBaseCSGrenade::OnPickedUpGrenade()
{
	if ( m_bHasBeenPickedUp )
		m_iClip1 = (int)MIN( (float)m_iClip1, 1.0f );
	else
	{
		m_iClip1 = 1;
		m_bHasBeenPickedUp = true;
	}
}

bool CBaseCSGrenade::AllowsAutoSwitchFrom() const
{
	return !m_bPinPulled;
}

// S:0x24D880 / C:0x230F50. Empty server deployment drops then removes the item.
bool CBaseCSGrenade::Deploy()
{
	m_bRedraw = false;
	m_bPinPulled = false;
	m_fThrowTime = 0.0f;
	m_isGoingToRoll = false;
#ifndef CLIENT_DLL
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return false;
	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		pPlayer->Weapon_Drop( this );
		UTIL_Remove( this );
		return false;
	}
#endif
	return BaseClass::Deploy();
}

// S:0x24D9B0 / C:0x230FA0. A holster cancels, it does not spawn a live grenade.
bool CBaseCSGrenade::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#ifndef CLIENT_DLL
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( m_bPinPulled && pPlayer )
		pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_GRENADE );
#endif
	m_bRedraw = false;
	m_bPinPulled = false;
	m_fThrowTime = 0.0f;
	m_isGoingToRoll = false;
	const bool bResult = BaseClass::Holster( pSwitchingTo );
#ifndef CLIENT_DLL
	if ( pPlayer && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		pPlayer->Weapon_Detach( this );
		UTIL_Remove( this );
	}
#endif
	return bResult;
}

// S:0x24DB00 / C:0x231000. Pull-pin is not a hitscan/gun primary attack.
void CBaseCSGrenade::PrimaryAttack()
{
	if ( m_bRedraw || m_bPinPulled || m_isGoingToRoll || m_fThrowTime > 0.0f )
		return;
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 || !pPlayer->CanAttack() || HandlePrimaryAttackUse() )
		return;
#ifndef CLIENT_DLL
	// S24DBAB: these are Director queries, not game-rules fields.
	if ( TheDirector->GetChapterNumber() != 0 && !TheDirector->HasAnySurvivorLeftSafeArea() )
	{
		CTerrorCheckpoint *pCheckpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
		if ( pCheckpoint && !pCheckpoint->GetOpenDoor() )
		{
			pPlayer->SwitchToNextBestWeapon( NULL );
			return;
		}
	}
#endif
	static const Activity s_PullPin = GrenadeActivity( "ACT_VM_PULLPIN" );
	SendWeaponAnim( s_PullPin );
	m_bPinPulled = true;
	m_bPinPullStarted = false;
	m_isGoingToRoll = false;
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_GRENADE_PIN_PULL );
	SetWeaponIdleTime( gpGlobals->curtime + 0.5f );
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
#ifndef CLIENT_DLL
	char modifiers[256];
	V_snprintf( modifiers, sizeof( modifiers ), "GrenadeType:%s", GetTerrorWpnData().m_szResponseRulesName );
	pPlayer->SpeakIfAllowed( "ThrewGrenade", 1, modifiers );
#endif
}

void CBaseCSGrenade::SecondaryAttack()
{
	if ( !m_bPinPulled && m_fThrowTime <= 0.0f )
		BaseClass::SecondaryAttack();
}

// S:0x24DDB0 / C:0x231150. The three duration queries are original separate calls.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CBaseCSGrenade::Reload()
{
	if ( m_bRedraw )
	{
		const float now = gpGlobals->curtime;
		if ( now >= m_flNextPrimaryAttack && now >= m_flNextSecondaryAttack )
		{
			static const Activity deploy = GrenadeActivity( "ACT_VM_DEPLOY" );
			SendWeaponAnim( deploy );
			const double primaryDuration = GetViewModelSequenceDuration();
			const double primary = primaryDuration + static_cast< double >( gpGlobals->curtime );
			const float storedPrimary = static_cast< float >( primary );
			if ( static_cast< double >( static_cast< float >( m_flNextPrimaryAttack ) ) != primary )
				m_flNextPrimaryAttack.SetDirect( storedPrimary );
			const double secondaryDuration = GetViewModelSequenceDuration();
			const double secondary = secondaryDuration + static_cast< double >( gpGlobals->curtime );
			const float storedSecondary = static_cast< float >( secondary );
			if ( static_cast< double >( static_cast< float >( m_flNextSecondaryAttack ) ) != secondary )
				m_flNextSecondaryAttack.SetDirect( storedSecondary );
			const double idleDuration = GetViewModelSequenceDuration();
			const float idle = static_cast< float >( idleDuration + static_cast< double >( gpGlobals->curtime ) );
			SetWeaponIdleTime( idle );
		}
	}
	return true;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x24DED0 / C:0x231220. Release and actual emission are different phases.
void CBaseCSGrenade::ItemPostFrame()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || !pPlayer->GetViewModel( m_nViewModelIndex ) )
		return;
	if ( gpGlobals->curtime >= m_flNextPrimaryAttack && m_bPinPulled && !( pPlayer->m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) ) )
	{
		pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_GRENADE_RELEASE );
		StartGrenadeThrow();
		m_bPinPulled = false;
		SendWeaponAnim( ACT_VM_THROW );
		SetWeaponIdleTime( gpGlobals->curtime + 1.3f );
		m_flNextPrimaryAttack = gpGlobals->curtime + 1.3f;
#ifndef CLIENT_DLL
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_fire" );
		if ( pEvent )
		{
			const char *pName = GetClassname();
			if ( !V_strncmp( pName, "weapon_", 7 ) )
				pName += 7;
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetString( "weapon", pName );
			pEvent->SetInt( "weaponid", GetWeaponID() );
			gameeventmanager->FireEvent( pEvent );
		}
#endif
		return;
	}
	if ( m_bPinPulled && m_flNextPrimaryAttack > gpGlobals->curtime && !m_bPinPullStarted )
	{
		m_bPinPullStarted = true;
		return;
	}
	if ( m_fThrowTime > 0.0f && gpGlobals->curtime > m_fThrowTime )
	{
#ifndef CLIENT_DLL
		pPlayer->RemoveAmmo( 1, m_iPrimaryAmmoType );
#endif
		ThrowGrenade();
		return;
	}
	if ( m_bRedraw )
	{
#ifndef CLIENT_DLL
		if ( gpGlobals->curtime > GetWeaponIdleTime() )
		{
			if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) > 0 )
			{
				if ( !pPlayer->SwitchToNextBestWeapon( this ) )
				{
					Reload();
					m_bRedraw = false;
				}
			}
			else
			{
				pPlayer->Weapon_Drop( this );
				UTIL_Remove( this );
			}
		}
#endif
		return;
	}
	BaseClass::ItemPostFrame();
}

void CBaseCSGrenade::StartGrenadeThrow()
{
	m_fThrowTime = gpGlobals->curtime + 0.2f;
}

// S:0x24E2A0 / C:0x2313C0. The RNG draw belongs only to the non-roll server path.
void CBaseCSGrenade::ThrowGrenade()
{
#ifndef CLIENT_DLL
	CBaseCombatCharacter *pOwner = GetOwner();
	if ( !pOwner || !pOwner->IsPlayer() )
		return;
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( m_isGoingToRoll )
	{
		Vector forward;
		AngleVectors( pPlayer->EyeAngles(), &forward );
		const Vector position = pPlayer->GetAbsOrigin() + Vector( 0, 0, 36 );
		const Vector velocity = forward * 500.0f + pPlayer->GetAbsVelocity();
		EmitGrenade( position, vec3_angle, velocity, vec3_origin, pOwner );
		m_isGoingToRoll = false;
	}
	else
	{
		Vector forward;
		pPlayer->EyeVectors( &forward );
		const Vector position = pPlayer->EyePosition();
		trace_t trace;
		UTIL_TraceHull( position, position + forward * 16.0f, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ),
			MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &trace );
		const Vector velocity = pPlayer->GetAbsVelocity() + forward * 750.0f;
		const AngularImpulse angularVelocity( 600.0f, random->RandomInt( -1200, 1200 ), 0.0f );
		EmitGrenade( trace.endpos, vec3_angle, velocity, angularVelocity, pOwner );
	}
#endif
	m_bRedraw = true;
	m_fThrowTime = 0.0f;
}

// S:0x24E750 / C:0x231390. Caller owns removal of the reserved grenade ammo.
void CBaseCSGrenade::DropGrenade()
{
#ifndef CLIENT_DLL
	CBaseCombatCharacter *pOwner = GetOwner();
	if ( !pOwner || !pOwner->IsPlayer() )
		return;
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	Vector forward;
	pPlayer->EyeVectors( &forward );
	const Vector position = pPlayer->EyePosition() + forward * 16.0f;
	const AngularImpulse angularVelocity( 600.0f, random->RandomInt( -1200, 1200 ), 0.0f );
	EmitGrenade( position, vec3_angle, pPlayer->GetAbsVelocity(), angularVelocity, pOwner );
#endif
	m_bRedraw = true;
	m_fThrowTime = 0.0f;
}

bool CBaseCSGrenade::CanDisplayHelpingHand() const
{
	return !m_bRedraw && !m_bPinPulled && !m_isGoingToRoll && m_fThrowTime <= 0.0f && BaseClass::CanDisplayHelpingHand();
}

acttable_t *CBaseCSGrenade::ActivityList( int &iActivityCount )
{
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_GREN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_GREN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_GREN" },
		{ "ACT_WALK", "ACT_WALK_GREN" },
		{ "ACT_RUN", "ACT_RUN_GREN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_GREN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_GREN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_GREN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_GREN" },
		{ "ACT_WALK_CALM", "ACT_WALK_GREN" },
		{ "ACT_RUN_CALM", "ACT_RUN_GREN" },
		{ "ACT_JUMP", "ACT_JUMP_ITEM" }
	};
	static acttable_t s_Table[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Table[i].baseAct = GrenadeActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = GrenadeActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

Activity CBaseCSGrenade::TranslatePlayerActivity( Activity activity )
{
	if ( m_bPinPulled || m_fThrowTime > 0.0f )
	{
		static const char *s_pNames[][2] =
		{
			{ "ACT_IDLE", "ACT_IDLE_GREN_PULL_BACK" },
			{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_GREN_PULL_BACK" },
			{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_GREN_PULL_BACK" },
			{ "ACT_RUN", "ACT_RUN_GREN_PULL_BACK" },
			{ "ACT_WALK", "ACT_WALK_GREN_PULL_BACK" },
			{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_GREN_PULL_BACK" },
			{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_GREN_PULL_BACK" },
			{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_GREN_PULL_BACK" },
			{ "ACT_IDLE_CALM", "ACT_IDLE_GREN_PULL_BACK" },
			{ "ACT_WALK_CALM", "ACT_WALK_GREN_PULL_BACK" },
			{ "ACT_RUN_CALM", "ACT_RUN_GREN_PULL_BACK" }
		};
		static Activity s_Source[ARRAYSIZE( s_pNames )];
		static Activity s_Target[ARRAYSIZE( s_pNames )];
		static bool s_bInitialized = false;
		if ( !s_bInitialized )
		{
			for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
			{
				s_Source[i] = GrenadeActivity( s_pNames[i][0] );
				s_Target[i] = GrenadeActivity( s_pNames[i][1] );
			}
			s_bInitialized = true;
		}
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
			if ( activity == s_Source[i] )
				return s_Target[i];
	}
	return BaseClass::TranslatePlayerActivity( activity );
}

Activity CBaseCSGrenade::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	if ( event < TERROR_ANIMEVENT_MELEE_SWEEP || event > TERROR_ANIMEVENT_MELEE_STOMP )
		return ACT_INVALID;
	static const Activity s_Activity[][2] =
	{
		{ GrenadeActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), GrenadeActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ GrenadeActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), GrenadeActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ GrenadeActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), GrenadeActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ GrenadeActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), GrenadeActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	return s_Activity[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
}

Activity CBaseCSGrenade::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = GrenadeActivity( "ACT_RELOAD_RIFLE" );
	return s_Reload;
}

Activity CBaseCSGrenade::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Deploy = GrenadeActivity( "ACT_DEPLOY_GREN" );
	return s_Deploy;
}

Activity CBaseCSGrenade::GetPlayerGrenadeActivity( Activity movement, bool bRelease ) const
{
	const bool bMoving = movement == ACT_WALK || movement == ACT_RUN || movement == ACT_RUN_CROUCH || movement == ACT_CROUCHIDLE;
	static const Activity s_Activities[2][2] =
	{
		{ GrenadeActivity( "ACT_PRIMARYATTACK_GREN1_IDLE" ), GrenadeActivity( "ACT_PRIMARYATTACK_GREN1_RUN" ) },
		{ GrenadeActivity( "ACT_PRIMARYATTACK_GREN2_IDLE" ), GrenadeActivity( "ACT_PRIMARYATTACK_GREN2_RUN" ) }
	};
	return s_Activities[bRelease ? 1 : 0][bMoving ? 1 : 0];
}
