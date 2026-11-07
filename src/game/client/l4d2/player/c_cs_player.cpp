//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_cs_player.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/c_cs_ragdoll.h"
#include "l4d2/weapons/c_addon_prop.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/player/c_transitioning_player.h"
#include "obstacle_pushaway.h"
#include "collisionproperty.h"
#include "collisionutils.h"
#include "gamerules.h"
#include "datacache/imdlcache.h"
#include "c_recipientfilter.h"
#include "cliententitylist.h"
#include "particle_property.h"
#include "c_team.h"
#include "c_playerresource.h"
#include "hud.h"
#include "history_resource.h"
#include "ivieweffects.h"
#include "view.h"
#include "util_shared.h"
#include "iinput.h"
#include "particle_parse.h"
#include "particles_new.h"
#include "iefx.h"
#include "dlight.h"
#include "toolframework_client.h"
#include "toolframework/itoolentity.h"
#include "tier1/KeyValues.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "l4d2/content/terror_precache_client.h"
#include "tier0/memdbgon.h"

#undef CCSPlayer

BEGIN_RECV_TABLE_NOBASE( C_CSPlayer, DT_CSLocalPlayerExclusive )
	RecvPropFloat( RECVINFO( m_flStamina ) ),
	RecvPropInt( RECVINFO( m_iDirection ) ),
	RecvPropInt( RECVINFO( m_iShotsFired ) ),
	RecvPropFloat( RECVINFO( m_flVelocityModifier ) ),
	RecvPropBool( RECVINFO( m_duckUntilOnGround ) ),
	RecvPropVectorXY( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ), 0, C_CSPlayer::RecvProxy_LocalOriginXY ),
	RecvPropFloat( RECVINFO_NAME( m_vecNetworkOrigin[2], m_vecOrigin[2] ), 0, C_CSPlayer::RecvProxy_LocalOriginZ ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[0] ) ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[1] ) ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[2] ) ),
END_RECV_TABLE()

BEGIN_RECV_TABLE_NOBASE( C_CSPlayer, DT_CSNonLocalPlayerExclusive )
	RecvPropVectorXY( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ), 0, C_CSPlayer::RecvProxy_NonLocalOriginXY ),
	RecvPropFloat( RECVINFO_NAME( m_vecNetworkOrigin[2], m_vecOrigin[2] ), 0, C_CSPlayer::RecvProxy_NonLocalOriginZ ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[0] ) ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[1] ) ),
	RecvPropFloat( RECVINFO( m_vecViewOffset[2] ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_CSPlayer, DT_CSPlayer, CCSPlayer )
	RecvPropDataTable( "cslocaldata", 0, 0, &REFERENCE_RECV_TABLE( DT_CSLocalPlayerExclusive ) ),
	RecvPropDataTable( "csnonlocaldata", 0, 0, &REFERENCE_RECV_TABLE( DT_CSNonLocalPlayerExclusive ) ),
	RecvPropInt( RECVINFO( m_iAddonBits ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_AddonModelWeaponIDs ),
		RecvPropInt( RECVINFO( m_AddonModelWeaponIDs[0] ) ) ),
	RecvPropInt( RECVINFO( m_iThrowGrenadeCounter ) ),
	RecvPropInt( RECVINFO( m_iPlayerState ) ),
	RecvPropInt( RECVINFO( m_iAccount ) ),
	RecvPropBool( RECVINFO( m_bInBombZone ) ),
	RecvPropBool( RECVINFO( m_bInBuyZone ) ),
	RecvPropInt( RECVINFO( m_iClass ) ),
	RecvPropInt( RECVINFO( m_ArmorValue ) ),
	RecvPropFloat( RECVINFO( m_angEyeAngles[0] ) ),
	RecvPropFloat( RECVINFO( m_angEyeAngles[1] ) ),
	RecvPropFloat( RECVINFO( m_flStamina ) ),
	RecvPropBool( RECVINFO( m_bHasDefuser ), 0, C_CSPlayer::RecvProxy_HasDefuser ),
	RecvPropBool( RECVINFO( m_bNightVisionOn ), 0, C_CSPlayer::RecvProxy_NightVisionOn ),
	RecvPropBool( RECVINFO( m_bHasNightVision ) ),
	RecvPropInt( RECVINFO( m_iNightVisionScale ) ),
	RecvPropBool( RECVINFO( m_bInHostageRescueZone ) ),
	RecvPropInt( RECVINFO( m_ArmorValue ) ),
	RecvPropBool( RECVINFO( m_bIsDefusing ) ),
	RecvPropBool( RECVINFO( m_bHasHelmet ) ),
	RecvPropVector( RECVINFO( m_vecRagdollVelocity ) ),
	RecvPropFloat( RECVINFO( m_flFlashDuration ), 0, C_CSPlayer::RecvProxy_FlashDuration ),
	RecvPropFloat( RECVINFO( m_flFlashMaxAlpha ) ),
	RecvPropFloat( RECVINFO( m_flProgressBarDuration ) ),
	RecvPropFloat( RECVINFO( m_flProgressBarStartTime ) ),
	RecvPropEHandle( RECVINFO( m_hRagdoll ) ),
	RecvPropInt( RECVINFO( m_cycleLatch ), 0, C_CSPlayer::RecvProxy_CycleLatch ),
	RecvPropVector( RECVINFO( m_lastLadderNormal ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_CSPlayer )
	DEFINE_PRED_FIELD_TOL( m_flStamina, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.1f ),
	DEFINE_PRED_FIELD( m_flCycle, FIELD_FLOAT, FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_iShotsFired, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iDirection, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_duckUntilOnGround, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

C_CSPlayer::C_CSPlayer() : m_iv_angEyeAngles( "C_CSPlayer::m_iv_angEyeAngles" )
{
	// Smoke vector, ragdoll handle, last-standing/ladder intervals, shadow timer,
	// eye watcher, ID timers, TP then FP references, lists and shadow handle
	// construct ONCE by their real existing SDK types in declaration order.
	m_angEyeAngles.Init();
	DisableCachedRenderBounds( true ); // Native225197, real Root508 owner.
	AddVar( &m_angEyeAngles, &m_iv_angEyeAngles, LATCH_SIMULATION_VAR );
	if ( m_iAddonBits != 0 )
		m_iAddonBits = 0;
	if ( m_flProgressBarDuration != 0.0f )
		m_flProgressBarDuration = 0.0f;
	if ( m_flProgressBarStartTime != 0.0f )
		m_flProgressBarStartTime = 0.0f;
	m_ArmorValue = 0;
	m_bHasHelmet = false;
	m_iIDEntIndex = 0;
	m_IDTargetDelayTimer.Start( 0.0f );
	m_iOldIDEntIndex = 0;
	m_IDTargetHoldTimer.Start( 0.0f );
	m_iDirection = 0;
	// Native6248 exact private32-bit ctor store. Name is descriptive, not a
	// recovered public query or a bone/cache/prediction alias. No fake consumer.
	m_nClientPresentationState = 1;
	m_nFlashlightCleanupGate = 0;
	m_flNextClientPushawayTime = 0.0f; // Native6524; Thinkable+12 reads raw6512.
	// Native first-FP-reference clear has an impossible nonNULL arm here: all
	// four real refs just default-constructed NULL, no intervening producer.
	// Do not add another manual reference reset/link operation.
	m_flServerIntendedCycle = -1.0f;
	m_bFootstepPending[0] = false;
	m_bFootstepPending[1] = false;
	m_bUseSimpleShadow[0] = false;
	m_bUseSimpleShadow[1] = false;
	m_shadowDistanceTimer.Start( RandomFloat( 0.0f, 0.5f ) ); // EXACTLY one draw.
}


C_CSPlayer::~C_CSPlayer()
{
	MDLCACHE_CRITICAL_SECTION();
	RemoveAddonModels();
	ClearFlashlightParticles();
	m_PlayerAnimState->Release(); // Native22716D is unconditional, not a guard.
}

// C222400: publish the current max-speed policy after base PreThink.
void C_CSPlayer::PreThink()
{
	BaseClass::PreThink();
	RecalculateMaxSpeed();
}

void C_CSPlayer::RecalculateMaxSpeed()
{
	// C225400 is the genuine empty CS policy; Terror owns the speed write.
}

bool C_CSPlayer::CanAttack() const
{
	return true; // C225410; not an alias for the derived attack policy.
}

const QAngle &C_CSPlayer::EyeAngles()
{
	return IsLocalPlayer() ? BaseClass::EyeAngles() : m_angEyeAngles;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_CSPlayer::GetRagdollInitBoneArrays( matrix3x4_t *delta0, matrix3x4_t *delta1,
	matrix3x4_t *current, float boneDt )
{
	if ( IsLocalPlayer() )
	{
		BaseClass::GetRagdollInitBoneArrays( delta0, delta1, current, boneDt );
		return;
	}
	C_TerrorPlayer *player = ToTerrorPlayer( this );
	if ( ( player->GetTeamNumber() == TEAM_INFECTED && player->m_zombieClass == 8 ) ||
		( player->GetTeamNumber() == TEAM_INFECTED && player->m_zombieClass == 3 ) )
	{
		BaseClass::GetRagdollInitBoneArrays( delta0, delta1, current, boneDt );
		return;
	}
	ForceSetupBonesAtTime( delta0, gpGlobals->curtime - boneDt );
	GetRagdollCurSequenceWithDeathPose( this, delta1, gpGlobals->curtime, m_iDeathPose, m_iDeathFrame );
	SetupBones( current, 128, BONE_USED_BY_ANYTHING, gpGlobals->curtime );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool C_CSPlayer::CreateMove( float inputSampleTime, CUserCmd *cmd )
{
	// C2222B0: scope belongs only to the actual base call, not Terror's tail.
	const bool queriesValid = C_BaseEntity::IsAbsQueriesValid();
	C_BaseEntity::PushEnableAbsRecomputations( true );
	C_BaseEntity::SetAbsQueriesValid( true );
	const bool result = BaseClass::CreateMove( inputSampleTime, cmd );
	C_BaseEntity::SetAbsQueriesValid( queriesValid );
	C_BaseEntity::PopEnableAbsRecomputations();
	return result;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// C225390. Introduces the SAME float-const virtual inherited/overridden by the
// existing M3ActorSpeeds full Terror269DA0 GetWalkTopSpeed; no double alias.
float C_CSPlayer::GetWalkTopSpeed() const
{
	return 100.0f;
}

// C222EA0. Preserve both genuine rules getter calls and their native order.
void C_CSPlayer::UpdateCollisionBounds()
{
	if ( GetFlags() & FL_DUCKING )
	{
		const Vector &maxs = GameRules()->GetViewVectors()->m_vDuckHullMax;
		const Vector &mins = GameRules()->GetViewVectors()->m_vDuckHullMin;
		CollisionProp()->SetCollisionBounds( mins, maxs );
	}
	else
	{
		const Vector &maxs = GameRules()->GetViewVectors()->m_vHullMax;
		const Vector &mins = GameRules()->GetViewVectors()->m_vHullMin;
		CollisionProp()->SetCollisionBounds( mins, maxs );
	}
}

// C223F40. Native's first bool is unused. A successful/failed GetAttachment both
// write real origin/angles (2F940); no new missing-attachment fallback is added.
void C_CSPlayer::GetFootstepSoundPosition( bool running, bool left, bool force,
	Vector *origin, bool *shallowWater, bool *wading )
{
	const int waterLevel = GetWaterLevel();
	bool shallow = waterLevel == WL_Feet || waterLevel == WL_Waist || waterLevel == WL_Eyes;
	bool deep = waterLevel == WL_Waist || waterLevel == WL_Eyes;
	if ( !( GetFlags() & FL_ONGROUND ) && !force )
	{
		shallow = false;
		deep = false;
	}
	if ( shallowWater ) *shallowWater = shallow;
	if ( wading ) *wading = deep;
	if ( !shallow && !deep )
	{
		if ( origin ) *origin = GetAbsOrigin();
		return;
	}
	Vector position;
	QAngle angles;
	GetAttachment( left ? m_leftFootAttachment : m_rightFootAttachment, position, angles );
	position.z = GetAbsOrigin().z;
	if ( force )
	{
		Vector end = position;
		end.z -= 64.0f;
		trace_t trace;
		UTIL_TraceLine( position, end, PhysicsSolidMaskForEntity(), this, COLLISION_GROUP_PLAYER, &trace );
		if ( trace.DidHitWorld() )
			position.z = trace.endpos.z;
	}
	if ( shallow )
	{
		Vector end = position;
		end.z += 1024.0f;
		trace_t trace;
		UTIL_TraceLine( position, end, 0x4030, NULL, COLLISION_GROUP_NONE, &trace );
		// Native2240C4 deliberately discards this water trace's result.
	}
	if ( origin ) *origin = position;
}

// C2253B0 and Terror252790 are BOTH genuine RET leaves.
void C_CSPlayer::OnClientFootstepEvent()
{
}

// Complete C226B90, native Thinkable-this adjusted -12 by the C++ override.
void C_CSPlayer::ClientThink()
{
	if ( m_lifeState == LIFE_ALIVE )
		UpdateCollisionBounds();
	BaseClass::ClientThink(); // Genuine empty inherited Entity ClientThink.
	ProcessDelayedSounds();
	UpdateAddonModels();
	UpdateShadowWeapon();
	UpdateIDTarget();
	if ( gpGlobals->curtime >= m_flNextClientPushawayTime )
	{
		PerformObstaclePushaway( this );
		m_flNextClientPushawayTime = gpGlobals->curtime + 0.05f;
	}
	if ( m_bFootstepPending[0] || m_bFootstepPending[1] )
	{
		const int waterLevel = GetWaterLevel();
		const bool shallow = waterLevel == WL_Feet || waterLevel == WL_Waist || waterLevel == WL_Eyes;
		const bool deep = waterLevel == WL_Waist || waterLevel == WL_Eyes;
		if ( m_bFootstepPending[0] )
		{
			OnClientFootstepEvent();
			if ( !shallow )
			{
				m_bFootstepPending[0] = false;
				m_bFootstepPending[1] = false;
				return;
			}
		}
		PlayTerrorFootstepSound( GetAbsOrigin(), true, shallow, deep, true );
		if ( shallow )
		{
			GetFootstepSoundPosition( true, true, true, NULL, NULL, NULL );
			GetFootstepSoundPosition( true, false, true, NULL, NULL, NULL );
		}
		m_bFootstepPending[0] = false;
		m_bFootstepPending[1] = false;
	}
}

// Complete C223520. Ordinary model-event dispatch remains in BaseAnimating;
// native footstep events are50/51/7001/7002, NOT alternate Activity ordinals.
void C_CSPlayer::FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options )
{
	if ( event != 50 && event != 51 && event != 7001 && event != 7002 )
	{
		BaseClass::FireEvent( origin, angles, event, options );
		return;
	}
	if ( !( GetFlags() & FL_ONGROUND ) )
		return;
	bool left = event == 50;
	if ( ( event == 7001 || event == 7002 ) && options && options[0] == 'l' )
		left = true;
	const Vector &velocity = GetAbsVelocity();
	const float speed = sqrtf( ( velocity.y * velocity.y + velocity.x * velocity.x ) + 0.0f );
	const double walkSpeed = static_cast< double >( GetWalkTopSpeed() );
	const bool running = static_cast< double >( speed ) > walkSpeed + 10.0;
	Vector position;
	bool shallow, deep;
	GetFootstepSoundPosition( running, left, false, &position, &shallow, &deep );
	PlayTerrorFootstepSound( position, left, shallow, deep, false );
}

// C225450 consumes the SAME inherited queue that native ReloadEffect C226CE0
// fills. COMISS/JB skips unordered deadlines; preserve ordered >= explicitly.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_CSPlayer::ProcessDelayedSounds()
{
	for ( int index = m_DelayedSounds.Head(); index != m_DelayedSounds.InvalidIndex(); )
	{
		const int next = m_DelayedSounds.Next( index );
		const DelayedSound_t &sound = m_DelayedSounds[index];
		if ( gpGlobals->curtime >= sound.m_flPlaybackTime )
		{
			CLocalPlayerFilter filter;
			EmitSound( filter, entindex(), sound.m_soundName, NULL, 0.0f );
			m_DelayedSounds.Remove( index );
		}
		index = next;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C226130. Sole real user0 projection; the two native reference arrays remain
// actual HPARTICLEFFECT members and unlink implicitly in their native order.
void C_CSPlayer::ClearFlashlightParticles()
{
	if ( m_hThirdPersonFlashlightParticles[0] )
	{
		ParticleProp()->StopEmissionAndDestroyImmediately( m_hThirdPersonFlashlightParticles[0] );
		m_hThirdPersonFlashlightParticles[0] = NULL;
	}
	if ( m_hFirstPersonFlashlightParticles[0] )
	{
		ParticleProp()->StopEmissionAndDestroyImmediately( m_hFirstPersonFlashlightParticles[0] );
		m_hFirstPersonFlashlightParticles[0] = NULL;
	}
}

// C226AE0 is IClientNetworkable slot2, NOT a data-change callback.
void C_CSPlayer::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	if ( state == SHOULDTRANSMIT_END )
	{
		RemoveAddonModels();
		// Native226B02 reads networkable[this+6608] = object6616; unlike
		// the producer/Clear, THIS genuine callback owns the cleanup gate.
		if ( m_nFlashlightCleanupGate != 0 )
			ClearFlashlightParticles();
	}
	BaseClass::NotifyShouldTransmit( state );
}

// C2221D0 is networkable slot4 OnDataChanged. Cache rebuild40240 is inherited.
void C_CSPlayer::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	if ( updateType == DATA_UPDATE_CREATED )
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	UpdateVisibility();
}

// C223DB0 is networkable slot6 PostDataUpdate. Primary980/64890 is the
// existing observer-aware ACTIVE WEAPON getter, not a viewmodel getter.
void C_CSPlayer::PostDataUpdate( DataUpdateType_t updateType )
{
	SetNetworkAngles( GetLocalAngles() );
	BaseClass::PostDataUpdate( updateType );
	C_BaseCombatWeapon *weapon = GetActiveWeapon();
	if ( weapon )
		weapon->UpdateVisibility();
	FOR_EACH_LL( m_AddonModels, index )
	{
		C_AddonProp *model = m_AddonModels[index].m_hModel.Get();
		if ( model )
			model->UpdateVisibility();
	}
}

// Whole C2258D0. Root Remove47140 is DISTINCT from the direct100BB020 sites:
// its solid/movement changes are required for this active-shadow path only.
CStudioHdr *C_CSPlayer::OnNewModel()
{
	CStudioHdr *model = BaseClass::OnNewModel();
	if ( m_PlayerAnimState )
		m_PlayerAnimState->OnNewModel();
	if ( m_hShadowWeapon.Get() )
		m_hShadowWeapon->Remove();
	for ( int index = m_AddonModels.Head(); index != m_AddonModels.InvalidIndex(); )
	{
		const int next = m_AddonModels.Next( index );
		ClientEntityList().AddToDeleteList( m_AddonModels[index].m_hModel.Get() );
		m_AddonModels.Remove( index );
		index = next;
	}
	m_nAddonModelState = 0; // Native6760 scalar; NOT ladder normal6428.
	return model;
}

void C_CSPlayer::UpdateClientSideAnimation()
{
	if ( GetSequence() != -1 )
		FrameAdvance( 0.0f );
	// Native local virtual EyeAngles supplies ONLY yaw; pitch stays received.
	// Existing CCS EyeAngles already implements real local/remote selection.
	m_PlayerAnimState->Update( EyeAngles().y, m_angEyeAngles.x );
	if ( GetSequence() != -1 )
		OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
}

// Whole C221DC0/C221DF0, exact native last-received-position side effect.
void C_CSPlayer::RecvProxy_LocalOriginXY( const CRecvProxyData *data, void *object, void *out )
{
	float *origin = static_cast< float * >( out );
	origin[0] = data->m_Value.m_Vector[0];
	origin[1] = data->m_Value.m_Vector[1];
	C_CSPlayer *player = static_cast< C_CSPlayer * >( object );
	player->m_vecLastReceivedPosition.x = data->m_Value.m_Vector[0];
	player->m_vecLastReceivedPosition.y = data->m_Value.m_Vector[1];
}

void C_CSPlayer::RecvProxy_LocalOriginZ( const CRecvProxyData *data, void *object, void *out )
{
	*static_cast< float * >( out ) = data->m_Value.m_Float;
	static_cast< C_CSPlayer * >( object )->m_vecLastReceivedPosition.z = data->m_Value.m_Float;
}

// C221F80/C221FE0: MAIN-APPROVED paired SDK absolute-coordinate projection.
// Server CS nonlocal transport already delivers the decoded absolute value.
// Do NOT fabricate root cells/cellwidth/cell-relative-origin cache or -0x4000.
// Unmeasured cell-relative-float versus absolute-coord quantization boundary.
void C_CSPlayer::RecvProxy_NonLocalOriginXY( const CRecvProxyData *data, void *object, void *out )
{
	float *origin = static_cast< float * >( out );
	origin[0] = data->m_Value.m_Vector[0];
	origin[1] = data->m_Value.m_Vector[1];
}

void C_CSPlayer::RecvProxy_NonLocalOriginZ( const CRecvProxyData *data, void *object, void *out )
{
	*static_cast< float * >( out ) = data->m_Value.m_Float;
}


// Whole C222B90. Real NULL guard and gained-edge HUD call are original.
void C_CSPlayer::RecvProxy_HasDefuser( const CRecvProxyData *data, void *object, void *out )
{
	C_CSPlayer *player = static_cast< C_CSPlayer * >( object );
	if ( !player )
		return;
	bool gained = false;
	if ( data->m_Value.m_Int != 0 )
	{
		gained = !player->m_bHasDefuser;
		player->m_bHasDefuser = true;
	}
	else
	{
		player->m_bHasDefuser = false;
	}
	if ( player->IsLocalPlayer() && gained )
	{
		// Native active-user save/restore projects to the genuine sole user0.
		CHudHistoryResource *history = static_cast< CHudHistoryResource * >( gHUD.FindElement( "CHudHistoryResource" ) );
		if ( history )
			history->AddToHistory( HISTSLOT_ITEM, "defuser", 0 );
	}
}

// Whole C224670. Signed positive test, NOT nonzero BoolRecv semantics.
void C_CSPlayer::RecvProxy_NightVisionOn( const CRecvProxyData *data, void *object, void *out )
{
	C_CSPlayer *player = static_cast< C_CSPlayer * >( object );
	const bool on = data->m_Value.m_Int > 0;
	if ( player->m_bNightVisionOn != on )
	{
		if ( on )
			player->m_flNightVisionAlpha = 1.0f;
		if ( player->m_bNightVisionOn != on )
			player->m_bNightVisionOn = on;
	}
}

// Whole C2246B0. Remote players receive NO duration store or end-time update.
void C_CSPlayer::RecvProxy_FlashDuration( const CRecvProxyData *data, void *object, void *out )
{
	C_CSPlayer *player = static_cast< C_CSPlayer * >( object );
	if ( player->IsLocalPlayer() )
	{
		const float incoming = data->m_Value.m_Float;
		const float previous = player->m_flFlashDuration;
		if ( previous != incoming && incoming > 0.0f )
			player->m_flFlashOverlayAlpha = 1.0f;
		const float reloadedIncoming = data->m_Value.m_Float;
		if ( previous != reloadedIncoming )
			player->m_flFlashDuration = reloadedIncoming;
		player->m_flFlashEndTime = gpGlobals->curtime + player->m_flFlashDuration;
	}
}

// Whole C222C50. Never stores m_cycleLatch/pOut and never calls direct SetCycle.
void C_CSPlayer::RecvProxy_CycleLatch( const CRecvProxyData *data, void *object, void *out )
{
	C_CSPlayer *player = static_cast< C_CSPlayer * >( object );
	if ( player->IsLocalPlayer() )
		return;
	const double requested = static_cast< double >( data->m_Value.m_Int ) * 0.0625;
	const float storedRequested = static_cast< float >( requested );
	const float current = player->GetCycle();
	const double difference = fabs( static_cast< double >( current ) - requested );
	const float storedDifference = static_cast< float >( difference );
	// Upper test uses FLOAT spill; lower uses UNSTORED wide absolute delta.
	// Native JA plus ordered-less reject means NaN PROCEEDS into correction.
	if ( storedDifference > 0.85000002f || difference < static_cast< double >( 0.15000001f ) )
		return;
	if ( player->GetTeam() )
	{
		C_Team *team = player->GetTeam(); // Native getter called twice.
		const int index = player->entindex();
		const char *playerName = player->GetPlayerName();
		const char *teamName = team->Get_Name();
		DevMsg( 2, "%s %s(%d): Cycle latch wants to correct %.2f in to %.2f.\n",
			teamName, playerName, index, current, storedRequested );
	}
	player->SetServerIntendedCycle( storedRequested );
}

// Whole C2253D0 / C2253F0. Native6768 is this genuine inherited virtual family,
// NOT duck-time. Existing BaseAnimating frame advance is the actual consumer.
void C_CSPlayer::SetServerIntendedCycle( float intended )
{
	m_flServerIntendedCycle = intended;
}

float C_CSPlayer::GetServerIntendedCycle()
{
	return m_flServerIntendedCycle;
}

// Definitions belong ONCE in existing shared/l4d2/cs_gamerules.cpp (both DLLs).
extern ConVar mp_playerid;
extern ConVar mp_playerid_delay;
extern ConVar mp_playerid_hold;
extern ConVar mp_fadetoblack;

C_CSPlayer *C_CSPlayer::GetLocalCSPlayer()
{
	return ToCSPlayer( C_BasePlayer::GetLocalPlayer() );
}

// C222250; only IN_EYE substitutes its actual player target.
C_CSPlayer *C_CSPlayer::GetViewCSPlayer()
{
	C_CSPlayer *local = C_CSPlayer::GetLocalCSPlayer();
	if ( local && local->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = local->GetObserverTarget();
		if ( target && target->IsPlayer() )
			return ToCSPlayer( target );
	}
	return local;
}

// C222450. Generic client root IsGhost is genuine; no new ghost collection.
static void AddGhostPlayersToTraceIgnoreList( CTraceFilterSimpleList &filter )
{
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		C_BaseEntity *entity = cl_entitylist->GetBaseEntity( index );
		if ( entity && entity->IsPlayer() && g_PR->IsConnected( index ) && entity->IsGhost() )
			filter.AddEntityToIgnore( entity );
	}
}

int C_CSPlayer::GetIDTarget() const
{
	if ( gpGlobals->curtime < m_IDTargetDelayTimer.m_timestamp )
		return 0;
	if ( m_iIDEntIndex != 0 )
		return m_iIDEntIndex;
	if ( m_iOldIDEntIndex != 0 && gpGlobals->curtime < m_IDTargetHoldTimer.m_timestamp )
		return m_iOldIDEntIndex;
	return 0;
}

void C_CSPlayer::UpdateIDTarget()
{
	if ( !IsLocalPlayer() )
		return;
	// Main-approved projection: the genuine local/view user is0. No fake split
	// slot, resolvability flag/filter or reset scope replaces native game scopes.
	m_iIDEntIndex = 0;
	if ( mp_playerid.GetInt() == 2 || ( mp_fadetoblack.GetInt() != 0 && m_lifeState != LIFE_ALIVE ) ||
		GetObserverMode() == OBS_MODE_CHASE || GetObserverMode() == OBS_MODE_DEATHCAM )
		return;
	byte r, g, b, alpha;
	bool blend;
	vieweffects->GetFadeParams( &r, &g, &b, &alpha, &blend );
	if ( alpha > 75 && ( m_lifeState == LIFE_ALIVE || GetObserverMode() == OBS_MODE_IN_EYE ) )
		return;
	const Vector &forward = MainViewForward();
	const Vector &viewOrigin = MainViewOrigin();
	Vector end( forward.x * 2500.0f + viewOrigin.x, forward.y * 2500.0f + viewOrigin.y,
		forward.z * 2500.0f + viewOrigin.z );
	// Retain native zero multiplies: replacing these with a vector copy changes
	// nonfinite-direction behavior even though every ordinary view is finite.
	Vector start( forward.x * 0.0f + viewOrigin.x, forward.y * 0.0f + viewOrigin.y,
		forward.z * 0.0f + viewOrigin.z );
	Vector delta = end - start;
	C_Team *team = GetTeam();
	if ( !team )
		return;
	bool nearTeammate = false;
	for ( int index = team->GetNumPlayers() - 1; index >= 0; --index )
	{
		C_BasePlayer *player = team->GetPlayer( index );
		if ( player && !player->IsDormant() && player != this )
		{
			Vector mins, maxs;
			player->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			CBaseTrace boxTrace;
			if ( IntersectRayWithBox( start, delta, mins, maxs, 6.0f, &boxTrace ) )
			{
				nearTeammate = true;
				break;
			}
		}
	}
	// This rough TEAM gate is native. Do not turn it into an enemy/full-ray scan.
	if ( !nearTeammate )
		return;
	CTraceFilterSimpleList filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( GetViewCSPlayer() );
	AddGhostPlayersToTraceIgnoreList( filter );
	trace_t trace;
	UTIL_TraceLine( start, end, 33579073, &filter, &trace );
	if ( trace.startsolid )
		return;
	Vector clipEnd( forward.x * 40.0f + end.x, forward.y * 40.0f + end.y, forward.z * 40.0f + end.z );
	UTIL_ClipTraceToPlayers( start, clipEnd, 1107312651, &filter, &trace );
	clipEnd.Init( forward.x * 40.0f + end.x, forward.y * 40.0f + end.y, forward.z * 40.0f + end.z );
	TraceTransitioningPlayers( start, clipEnd, 1107312651, &filter, &trace );
	if ( trace.startsolid || !trace.DidHitNonWorldEntity() )
		return;
	C_BaseEntity *target = trace.m_pEnt;
	if ( !target || target == this )
		return;
	if ( mp_playerid.GetInt() == 1 && target->IsPlayer() )
	{
		const int myTeam = GetTeamNumber();
		if ( target->GetTeamNumber() != myTeam )
			return;
	}
	for ( int index = 0; index < m_SmokeGrenades.Count(); ++index )
	{
		C_BaseEntity *smoke = m_SmokeGrenades[index];
		if ( smoke )
		{
			const Vector smokeOrigin = smoke->GetAbsOrigin();
			const Vector &myOrigin = GetAbsOrigin();
			const Vector &targetOrigin = target->GetAbsOrigin();
			const Vector toTarget = targetOrigin - myOrigin;
			const float targetDistance = sqrtf( ( toTarget.y * toTarget.y + toTarget.z * toTarget.z ) + toTarget.x * toTarget.x );
			float first, second;
			const Vector &sphereDirection = MainViewForward();
			const Vector &sphereOrigin = MainViewOrigin();
			const bool hit = IntersectInfiniteRayWithSphere( sphereOrigin, sphereDirection, smokeOrigin, 165.5f, &first, &second );
			// Main decision22603D..226050: native reads an unwritten first output
			// BEFORE testing a failed hit. Test hit first, leave outputs uninitialized,
			// retain every defined-hit operand/unordered test/reversed difference.
			if ( hit && !( first < 0.0f ) && targetDistance > second && first - second > 33.100002f )
				return;
		}
	}
	if ( GetIDTarget() == 0 && ( m_iOldIDEntIndex == 0 || m_IDTargetHoldTimer.IsElapsed() ) )
		m_IDTargetDelayTimer.Start( mp_playerid_delay.GetFloat() );
	m_iIDEntIndex = target->entindex();
	m_iOldIDEntIndex = m_iIDEntIndex;
	m_IDTargetHoldTimer.Start( mp_playerid_hold.GetFloat() );
}

// C224DB0, C221B40, C222DF0, C224E00, C2254F0 and C225880.
// This is client-created, never a network/factory class or a second think list.
class C_ShadowWeapon : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_ShadowWeapon, C_BaseAnimating );
	C_ShadowWeapon();
	virtual bool ShouldDraw() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void OnNewParticleEffect( const char *name, CNewParticleEffect *effect ) OVERRIDE;

	int m_weaponID; // Native2392: ctor deliberately omits; allocation0.
	const char *m_modelName; // Native2396: borrowed weapon script string, not copied.

private:
	HPARTICLEFFECT m_hThrowableEffect; // Native2400..2411: the real intrusive weak reference.
};

C_ShadowWeapon::C_ShadowWeapon()
{
	// Native224DB0 initializes only the base and the weak-reference node.
	// Its implicit destructor unlinks that node before BaseAnimating, as225880.
}

bool C_ShadowWeapon::ShouldDraw()
{
	return true; // Native224E00 is genuinely MOV AL,1 / RET.
}

int C_ShadowWeapon::DrawModel( int flags )
{
	if ( !input->CAM_IsThirdPerson() )
	{
		C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
		// Native221B5B/221B60 dereferences a possibly NULL local. Main decision:
		// 4BE40 detaches children; CCS227150 removes only stowed addons, so an
		// orphan shadow may remain. NULL cannot be its local first-person viewer:
		// it does NOT take the hide guard and proceeds to ordinary DrawModel.
		// Every non-NULL operand/call order remains the original negative guard.
		if ( local && !local->IsThirdPersonCameraActive() )
			return 0;
	}
	return BaseClass::DrawModel( flags );
}

bool C_ShadowWeapon::Simulate()
{
	if ( m_weaponID == TERROR_WEAPON_MOLOTOV )
	{
		if ( !m_hThrowableEffect )
		{
			DispatchParticleEffect( GetParticleSystemNameFromIndex( g_nMolotovHeldIndex ), PATTACH_POINT_FOLLOW, this, "Wick" );
			BaseClass::Simulate();
			return true;
		}
		m_hThrowableEffect->SetSortOrigin( GetAbsOrigin() );
		m_hThrowableEffect->SetNeedsBBoxUpdate( true ); // Native effect944 |=8.
	}
	else if ( m_weaponID == TERROR_WEAPON_PIPE_BOMB )
	{
		if ( !m_hThrowableEffect )
		{
			DispatchParticleEffect( GetParticleSystemNameFromIndex( g_nPipeBombFuseIndex ), PATTACH_POINT_FOLLOW, this, "fuse" );
			BaseClass::Simulate();
			return true;
		}
		m_hThrowableEffect->SetSortOrigin( GetAbsOrigin() );
		m_hThrowableEffect->SetNeedsBBoxUpdate( true ); // SetSortOrigin alone does not set it.
	}
	BaseClass::Simulate();
	return true;
}

void C_ShadowWeapon::OnNewParticleEffect( const char *name, CNewParticleEffect *effect )
{
	if ( !V_stricmp( name, "weapon_molotov_held" ) || !V_stricmp( name, "weapon_pipebomb_fuse" ) )
		m_hThrowableEffect = effect;
}

// Native C50CDF0/C50CE20 raw input0 normalizes to DEVELOPMENTONLY before registration.
static ConVar cl_addon_fade_min( "cl_addon_fade_min", "800", 0x0000000A );
static ConVar cl_addon_fade_max( "cl_addon_fade_max", "1000", 0x0000000A );

// C225580. Active shadow model; NOT the stowed m_iAddonBits consumer below.
void C_CSPlayer::UpdateShadowWeapon()
{
	if ( !IsLocalPlayer() )
	{
		ClientEntityList().AddToDeleteList( m_hShadowWeapon.Get() );
		return;
	}
	// Native active-user flag save/restore projects to the real sole user0.
	if ( !input->CAM_IsThirdPerson() && !IsThirdPersonCameraActive() )
	{
		ClientEntityList().AddToDeleteList( m_hShadowWeapon.Get() );
		return;
	}

	CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
	if ( !weapon || !weapon->IsWeaponVisible() )
	{
		ClientEntityList().AddToDeleteList( m_hShadowWeapon.Get() );
		return;
	}
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( this );
	if ( player->GetTerrorAnimState()->IsGrenadeProjectilePresentationActive() )
	{
		ClientEntityList().AddToDeleteList( m_hShadowWeapon.Get() );
		return;
	}
	// Genuine C27E5C0 animstate query, same private actor7100 owner/friendship.
	if ( player->GetTerrorAnimState()->IsGrenadePresentationSuppressed() )
	{
		ClientEntityList().AddToDeleteList( m_hShadowWeapon.Get() );
		return;
	}

	C_ShadowWeapon *shadow = static_cast< C_ShadowWeapon * >( m_hShadowWeapon.Get() );
	if ( shadow )
	{
		const char *cachedModel = shadow->m_modelName;
		const char *currentModel = weapon->GetWorldModel();
		if ( V_stricmp( cachedModel, currentModel ) )
			ClientEntityList().AddToDeleteList( shadow );
		// Deliberately no same-call replacement, ID refresh or skin refresh.
	}
	else if ( weapon->m_iWorldModelIndex != 0 && GetTeamNumber() != TEAM_INFECTED )
	{
		shadow = new C_ShadowWeapon;
		shadow->InitializeAsClientEntity( weapon->GetWorldModel(), false );
		shadow->FollowEntity( this, true );
		const int weaponID = weapon->GetWeaponID();
		shadow->m_modelName = weapon->GetWorldModel();
		shadow->m_weaponID = weaponID;
		shadow->SetSkin( weapon->GetSkin() );
		shadow->CreateShadow();
		shadow->SetSolid( SOLID_NONE );
		shadow->RemoveEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
		m_hShadowWeapon = shadow;
	}
}

// C2266D0. The MDL lock covers pose-copy BEFORE view classification and removal.
void C_CSPlayer::UpdateAddonModels()
{
	MDLCACHE_CRITICAL_SECTION();
	if ( m_AddonModels.Count() != 0 )
	{
		const Activity activity = GetSequenceActivity( GetSequence() );
		const float cycle = GetCycle();
		FOR_EACH_LL( m_AddonModels, index )
		{
			C_AddonProp *model = m_AddonModels[index].m_hModel.Get();
			if ( model && model->ShouldCopyParentAnimation() )
				model->SetActivityCycle( activity, cycle );
		}
	}

	int addonBits = m_iAddonBits;
	// Native loops real active users; the approved target has exactly user0.
	if ( GetPlayerViewType() != PLAYER_VIEW_THIRD_PERSON )
		addonBits = 0;

	for ( int index = m_AddonModels.Head(); index != m_AddonModels.InvalidIndex(); )
	{
		const int next = m_AddonModels.Next( index );
		const AddonModel_t &addon = m_AddonModels[index];
		if ( !( ( 1 << addon.m_slot ) & addonBits ) ||
			( addon.m_slot < 5 && m_AddonModelWeaponIDs[addon.m_slot] != addon.m_weaponID ) )
		{
			ClientEntityList().AddToDeleteList( addon.m_hModel.Get() );
			m_AddonModels.Remove( index );
		}
		index = next;
	}

	int existingBits = 0;
	FOR_EACH_LL( m_AddonModels, index )
		existingBits |= 1 << m_AddonModels[index].m_slot;
	for ( int slot = 0; slot < 8; ++slot )
	{
		const int bit = 1 << slot;
		if ( ( bit & addonBits ) && !( bit & existingBits ) )
			CreateAddonModel( slot );
	}
}

// C2269B0. Native clears only the mask, calls the full update, then walks again.
void C_CSPlayer::RemoveAddonModels()
{
	if ( m_iAddonBits != 0 )
		m_iAddonBits = 0;
	UpdateAddonModels();
	for ( int index = m_AddonModels.Head(); index != m_AddonModels.InvalidIndex(); )
	{
		const int next = m_AddonModels.Next( index );
		ClientEntityList().AddToDeleteList( m_AddonModels[index].m_hModel.Get() );
		m_AddonModels.Remove( index );
		index = next;
	}
}

// C226300. Its sole226981 caller ignores EAX/AL; native has no success contract.
// Fixed-slot strings/flags are the actual eight-record table106E2770.
void C_CSPlayer::CreateAddonModel( int slot )
{
	const int team = GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
		return;
	if ( static_cast< unsigned int >( slot ) > 7 )
		return;

	struct AddonInfo_t
	{
		bool m_fixed;
		const char *m_attachment;
		const char *m_model;
		bool m_copyParentAnimation;
	};
	static const AddonInfo_t addonInfo[8] =
	{
		{ false, NULL, NULL, false },
		{ false, NULL, NULL, false },
		{ false, NULL, NULL, false },
		{ false, NULL, NULL, false },
		{ false, NULL, NULL, false },
		{ true, "spine", "models/infected/smoker_tongue_attach.mdl", true },
		{ true, "medkit", "models/w_models/weapons/w_eq_defibrillator_no_paddles.mdl", false },
		{ true, "medkit", "models/props_junk/dieselcan.mdl", false }
	};
	const AddonInfo_t &data = addonInfo[slot];
	const char *modelName = NULL;
	int attachment = 0;
	bool copyParentAnimation = false;
	int cachedWeaponID;
	Vector origin = vec3_origin;
	QAngle angles = vec3_angle;
	int skin = 0;

	if ( data.m_fixed )
	{
		modelName = data.m_model;
		attachment = LookupAttachment( data.m_attachment );
		copyParentAnimation = data.m_copyParentAnimation;
		cachedWeaponID = 1;
		if ( slot == 6 )
		{
			const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( TERROR_WEAPON_DEFIBRILLATOR );
			if ( info )
			{
				origin = info->m_vecAddonOffset;
				angles = info->m_angAddonAngles;
			}
		}
	}
	else
	{
		int weaponID = m_AddonModelWeaponIDs[slot];
		int meleeIndex = MeleeWeaponInfoStore()->GetMeleeWeaponCount();
		if ( weaponID & 0x40 )
		{
			meleeIndex = weaponID & ~0x40;
			weaponID = TERROR_WEAPON_MELEE;
		}
		// Main decision for native23157x OOB via script-written uint8 IDs:
		// use the sole bounded M4 lookup; invalid/0 enters the EXISTING NULL-info
		// arm below. No B-side guard, clamp, substitute model or alternate registry.
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
		// C22F082/22F0A5/22F0C3 prove2200 is parsed Team:0,Survivor2,Zombie3.
		if ( !info || info->m_iTeam == TEAM_INFECTED )
			return;
		if ( meleeIndex == MeleeWeaponInfoStore()->GetMeleeWeaponCount() )
		{
			modelName = info->m_szAddonModel[0] ? info->m_szAddonModel : info->szWorldModel;
			attachment = LookupAttachment( info->m_szAddonAttachment );
			cachedWeaponID = weaponID;
			origin = info->m_vecAddonOffset;
			angles = info->m_angAddonAngles;
		}
		else
		{
			const CMeleeWeaponInfo *melee = MeleeWeaponInfoStore()->GetMeleeWeaponInfo( meleeIndex );
			if ( melee )
			{
				modelName = melee->m_szPlayerModel;
				attachment = LookupAttachment( melee->m_szAddonAttachment );
				origin = melee->m_vecAddonOffset;
				angles = melee->m_angAddonAngles;
			}
			cachedWeaponID = meleeIndex | 0x40;
		}
		C_TerrorPlayer *player = ToTerrorPlayer( this );
		C_TerrorWeapon *weapon = player->GetWeaponOfType( weaponID );
		if ( weapon )
			skin = weapon->GetSkin();
	}

	if ( attachment <= 0 || !modelName )
		return;
	C_AddonProp *model = new C_AddonProp;
	model->InitializeAsClientEntity( modelName, false );
	model->SetFadeMinMax( static_cast< float >( cl_addon_fade_min.GetInt() ),
		static_cast< float >( cl_addon_fade_max.GetInt() ) );
	AddonModel_t &addon = m_AddonModels[m_AddonModels.AddToTail()];
	addon.m_weaponID = cachedWeaponID;
	addon.m_slot = slot;
	addon.m_hModel = model;
	addon.m_attachment = attachment;
	model->SetCopyParentAnimation( copyParentAnimation );
	model->SetParent( this, addon.m_attachment );
	model->SetLocalOrigin( origin );
	model->SetLocalAngles( angles );
	model->SetSkin( skin );
	if ( IsLocalPlayer() )
	{
		model->SetSolid( SOLID_NONE );
		model->RemoveEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
	}
}

// Actual plain image-initialized scalar C106E276C (1.0f), distinct from the
// script MuzzleFlashScale and muzzleflash_light cvar. One native READ xref;
// no new cvar, config getter or fake conditional/radius provider is introduced.
static float s_flCSMuzzleFlashLightScale = 1.0f;


void C_CSPlayer::ProcessMuzzleFlashEvent()
{
	const bool recording = ToolsEnabled() && clienttools->IsInRecordingMode();
	CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
	if ( !weapon )
		return;
	// Main's real-user0 projection of original rendered-user iteration. This
	// is the native1/2 classifier, NOT SDK ShouldDrawThisPlayer or visibility.
	if ( GetPlayerViewType() != PLAYER_VIEW_THIRD_PERSON )
		return;
	weapon->EjectBrass(); // Actual389 before the395 attachment/failure arm.
	const int attachment = weapon->GetTerrorMuzzleAttachment();
	if ( attachment < 0 )
		return;
	Vector origin;
	QAngle angles;
	if ( !weapon->GetAttachment( attachment, origin, angles ) )
		return;
	const int lightIndex = entindex() + 0x40000000;
	dlight_t *light = effects->CL_AllocElight( lightIndex ); // Native effects+20.
	light->origin = origin;
	light->radius = 105.0f;
	light->radius = s_flCSMuzzleFlashLightScale * 105.0f;
	light->decay = light->radius * 20.0f;
	light->die = gpGlobals->curtime + 0.075000003f;
	light->color.r = 255;
	light->color.g = 192;
	light->color.b = 64;
	light->color.exponent = 5;
	if ( recording )
	{
		KeyValues *message = new KeyValues( "TempEntity" );
		message->SetInt( "te", TE_DYNAMIC_LIGHT );
		message->SetString( "name", "TE_DynamicLight" );
		message->SetFloat( "time", gpGlobals->curtime );
		message->SetFloat( "duration", light->die ); // Native ABSOLUTE die bug.
		message->SetFloat( "originx", light->origin.x );
		message->SetFloat( "originy", light->origin.y );
		message->SetFloat( "originz", light->origin.z );
		message->SetFloat( "radius", light->radius );
		message->SetFloat( "decay", light->decay );
		message->SetColor( "color", Color( 255, 192, 64, 0 ) );
		message->SetInt( "exponent", light->color.exponent );
		message->SetInt( "lightindex", lightIndex );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, message );
		message->deleteThis();
	}
	// Native user mask1 maps to selector0; no empty-name early return. The real
	// canonical dispatcher owns precache/index/error/filter/particle lifetime.
	DispatchParticleEffect( weapon->GetCSWpnData().m_szMuzzleFlashEffect3rdPerson,
		origin, origin, angles, NULL, false, 0 );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Native22DD00: local uses the actual movement-owned surface pointer/index.
surfacedata_t *C_CSPlayer::GetTerrorFootstepSurface( int &surfaceIndex )
{
	if ( IsLocalPlayer() )
	{
		surfaceIndex = m_surfaceProps;
		return m_pSurfaceData;
	}
	return BaseClass::GetTerrorFootstepSurface( surfaceIndex );
}

// Native22DC70. Terror overrides this independently; never fold the two policies.
bool C_CSPlayer::IsTerrorFootstepSurfaceValid( float speed, bool force ) const
{
	return speed > 110.0f || force;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// c_cs_player.cpp C2253C0, inherited unchanged by Terror. Not a guessed
// true-return wrapper. L4D2 BasePlayer has NO separate AddEntity override.
bool C_CSPlayer::AddEntity()
{
	return false;
}

// c_cs_player.cpp: C2223E0. It deliberately does not propagate the base value.
bool C_CSPlayer::Simulate()
{
	BaseClass::Simulate();
	return true;
}

// C223680: the server ragdoll placeholder owns the post-creation origin.
const Vector &C_CSPlayer::GetRenderOrigin()
{
	C_CSRagdoll *ragdoll = static_cast< C_CSRagdoll * >( m_hRagdoll.Get() );
	if ( ragdoll && ragdoll->IsRagdollCreated() )
		return ragdoll->GetRenderOrigin();
	return BaseClass::GetRenderOrigin();
}

// C222150: client ragdolls have world-space bones and no entity rotation.
const QAngle &C_CSPlayer::GetRenderAngles()
{
	if ( IsRagdoll() )
		return vec3_angle;
	return m_PlayerAnimState->GetRenderAngles();
}

// C222D40: the simple player shadow uses the collision hull, not hitboxes.
void C_CSPlayer::GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType )
{
	if ( shadowType == SHADOWS_SIMPLE )
	{
		mins = CollisionProp()->OBBMins();
		maxs = CollisionProp()->OBBMaxs();
	}
	else
		GetRenderBounds( mins, maxs );
}

// C222180: only the simple player shadow has a fixed downward direction.
bool C_CSPlayer::GetShadowCastDirection( Vector *direction, ShadowType_t shadowType ) const
{
	if ( shadowType != SHADOWS_SIMPLE )
		return BaseClass::GetShadowCastDirection( direction, shadowType );
	direction->Init( 0.0f, 0.0f, -1.0f );
	return true;
}

bool C_CSPlayer::IsThirdPersonCameraActive() const
{
	return false;
}

// Whole C10223440. Native alive CS deliberately does not call BasePlayer's
// local-camera ShouldDraw gate: the actual DrawModel phase owns that decision.
bool C_CSPlayer::ShouldDraw()
{
	if ( m_lifeState == LIFE_ALIVE )
		return GetTeamNumber() != TEAM_SPECTATOR;
	return ToTerrorPlayer( this )->GetRescueModelIfDead() != NULL;
}

// Whole C10223360, mapped onto the existing SDK one-argument DrawModel ABI.
// The native extra RenderableInstance_t argument is only forwarded by this
// owner; it has no additional provider/state here. M7 retains Terror's actual
// render-instance/stencil responsibilities at its sole outer DrawModel owner.
int C_CSPlayer::DrawModel( int flags )
{
	if ( this != C_BasePlayer::GetLocalPlayer( 0 )
		|| input->CAM_IsThirdPerson()
		|| IsThirdPersonCameraActive() )
	{
		return BaseClass::DrawModel( flags );
	}
	if ( flags == 0 )
		return m_lifeState == LIFE_ALIVE;
	if ( m_lifeState != LIFE_ALIVE
		&& ToTerrorPlayer( this )->GetRescueModelIfDead() )
	{
		return BaseClass::DrawModel( flags );
	}
	// Preserve the second native ToTerrorPlayer/classification query, rather
	// than caching the result of the dead-rescue query above.
	if ( ToTerrorPlayer( this )->IsHangingFromLedge() )
		return BaseClass::DrawModel( flags );
	return 0;
}

static ConVar cl_player_shadow_dist( "cl_player_shadow_dist", "1000", 0x0000000A ); // Native raw0 ->2.

// Whole C222F10. cl_player_shadow_dist has native single GetFloat/load.
void C_CSPlayer::UpdateShadowDistanceState()
{
	const bool wasSimple = m_bUseSimpleShadow[0];
	m_bUseSimpleShadow[0] = true;
	if ( C_BasePlayer::GetLocalPlayer( 0 ) )
	{
		// Native performs the fresh second local-player lookup before origin.
		const Vector &localOrigin = C_BasePlayer::GetLocalPlayer( 0 )->GetAbsOrigin();
		const Vector &origin = GetAbsOrigin();
		const float x = origin.x - localOrigin.x;
		const float y = origin.y - localOrigin.y;
		const float z = origin.z - localOrigin.z;
		const float distanceSquared = ( x * x + y * y ) + z * z;
		const float distance = cl_player_shadow_dist.GetFloat();
		// Native COMISS(distanceSquared,limitSquared)/JA skips this clear only
		// for ordered greater. Equality AND unordered choose dynamic, not <=.
		if ( !( distanceSquared > distance * distance ) )
			m_bUseSimpleShadow[0] = false;
	}
	if ( wasSimple != m_bUseSimpleShadow[0] )
	{
		// C42BA0 is DestroyShadow, not mere reprojection dirtiness. The real
		// SDK owner invalidates its handle so CreateShadow recomputes its type.
		DestroyShadow();
	}
}

// Whole C223E50. Main's real-user0 projection uses the existing interpolation-
// produced ready bool; no second ready mask/player/flag registry is invented.
ShadowType_t C_CSPlayer::ShadowCastType()
{
	if ( GetRenderHandle() == INVALID_CLIENT_RENDER_HANDLE )
		return SHADOWS_NONE;
	if ( !m_bReadyToDraw || IsGhost() )
		return SHADOWS_NONE;
	// C222250 is the same IN_EYE-only resolver already owned by M7. Native
	// C64EE0 is actual GetViewEntity, NOT localdraw/CAM/camera-distance state.
	if ( TerrorGlow_ResolveRenderPlayer( C_BasePlayer::GetLocalPlayer( 0 ) ) == this
		&& !GetViewEntity() )
		return SHADOWS_NONE;
	if ( C_BasePlayer::GetLocalPlayer( 0 ) != this && m_shadowDistanceTimer.IsElapsed() )
	{
		m_shadowDistanceTimer.Start( 0.15f );
		UpdateShadowDistanceState();
	}
	return m_bUseSimpleShadow[0] ? SHADOWS_SIMPLE : SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
