//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/effects/terror_flashlight.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/infected/infected_activities.h"
#include "collisionutils.h"
#include "debugoverlay_shared.h"
#include "in_buttons.h"
#include "gamerules.h"
#include "l4d2/weapons/c_base_mounted_weapon.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "c_playerresource.h"
#include "c_team.h"
#include "usercmd.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "cliententitylist.h"
#include "l4d2/weapons/terror_viewmodel.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/gamerules/terror_mode.h"
#include "bone_setup.h"
#include "usermessages.h"
#include "c_user_message_register.h"
#include "view.h"
#include "model_types.h"
#include "c_sceneentity.h"
#include "clientleafsystem.h"
#include "particle_property.h"
#include "particle_parse.h"
#include "particles_new.h"
#include "soundenvelope.h"
#include "iefx.h"
#include "dlight.h"
#include "steam/steam_api.h"
#include "steam/isteamtimeline.h"
#include "choreoevent.h"
#include "util_shared.h"
#include "l4d2/effects/terror_glow_player.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "l4d2/effects/terror_glow_visibility.h"
#include "l4d2/effects/glow_cvars.h"
#include "l4d2/effects/terror_glow_entities.h"
#include "l4d2/effects/terror_glow_scene.h"
#include "l4d2/effects/terror_glow_render.h"
#include "glow_outline_effect.h"
#include "l4d2/entities/c_point_prop_use_target.h"
#include "l4d2/weapons/weapon_carried_item.h"
#include "voice_status.h"
#include "datacache/imdlcache.h"
#include <xmmintrin.h>
#include "iinput.h"
#include "input.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "c_vguiscreen.h"
#include "engine/IEngineTrace.h"
#include "mathlib/vmatrix.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "vgui_controls/Panel.h"
#include "tier2/renderutils.h"
#include "prediction.h"
#include "l4d2/entities/c_func_elevator.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "c_baseviewmodel.h"
#include "cdll_bounded_cvars.h"
#include "l4d2/infected/base_ability.h"
#include "igameresources.h"
#include "c_recipientfilter.h"
#include "fx_impact.h"
#include "physics_shared.h"
#include "winlite.h"
#undef CreateEvent
#include <emmintrin.h>
#include "tier0/memdbgon.h"

#undef CTerrorPlayer

// C2862B0: native survivor-team predicate, shared by animation and observer view.
static bool IsSurvivorTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}

static void RecvProxy_TerrorZombieState( const CRecvProxyData *, void *, void * );

ConVar cl_viewtarget_player( "cl_viewtarget_player", "0", 0x00004008, "Lock 3rd person spec looking into target's eyes" );
ConVar cl_viewtarget_offset( "cl_viewtarget_offset", "0", 0x00004008 );
extern ConVar NextBotStop;
EXTERN_RECV_TABLE( DT_CountdownTimer );
EXTERN_RECV_TABLE( DT_IntervalTimer );
EXTERN_RECV_TABLE( DT_Music );


static void RecvProxy_TerrorGestureActivity( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	*(int *)pOut = TerrorActivityFromOriginal( pData->m_Value.m_Int );
}

BEGIN_RECV_TABLE_NOBASE( C_TerrorPlayer, DT_TerrorLocalPlayerExclusive )
	RecvPropVector( RECVINFO( m_shoveForce ) ),
	RecvPropBool( RECVINFO( m_airMovementRestricted ) ),
	RecvPropDataTable( RECVINFO_DT( m_stunTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropBool( RECVINFO( m_isInMissionStartArea ) ),
	RecvPropBool( RECVINFO( m_isCulling ) ),
	RecvPropBool( RECVINFO( m_isRelocating ) ),
	RecvPropEHandle( RECVINFO( m_pushEntity ) ),
	RecvPropFloat( RECVINFO( m_pushDistance ) ),
	RecvPropInt( RECVINFO( m_ghostSpawnState ) ),
	RecvPropInt( RECVINFO( m_ghostSpawnClockMaxDelay ) ),
	RecvPropInt( RECVINFO( m_ghostSpawnClockCurrentDelay ) ),
	RecvPropFloat( RECVINFO( m_flNextShoveTime ) ),
	RecvPropInt( RECVINFO( m_iShovePenalty ) ),
	RecvPropEHandle( RECVINFO( m_dragTarget ) ),
	RecvPropVector( RECVINFO( m_lineOfScrimmagePos ) ),
	RecvPropVector( RECVINFO( m_lineOfScrimmageAngle ) ),
	RecvPropVector( RECVINFO( m_rearLineOfScrimmagePos ) ),
	RecvPropVector( RECVINFO( m_rearLineOfScrimmageAngle ) ),
	RecvPropVector( RECVINFO( m_scrimmageSphereCenter ) ),
	RecvPropFloat( RECVINFO( m_scrimmageSphereInitialRadius ) ),
	RecvPropFloat( RECVINFO( m_scrimmageStartTime ) ),
	RecvPropFloat( RECVINFO( m_survivorsLineOfScrimmageDistance ) ),
	RecvPropBool( RECVINFO( m_isAutoCrouchEnabled ) ),
	RecvPropDataTable( RECVINFO_DT( m_autoCrouchTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropArray( RecvPropVector( RECVINFO( m_yardLineAngles[0] ) ), m_yardLineAngles ),
	RecvPropArray( RecvPropVector( RECVINFO( m_yardLinePos[0] ) ), m_yardLinePos ),
	RecvPropDataTable( RECVINFO_DT( m_noAvoidanceTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropFloat( RECVINFO( m_TimeForceExternalView ) ),
	RecvPropInt( RECVINFO( m_nVariantType ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_TerrorPlayer, DT_TerrorPlayer, CTerrorPlayer )
	RecvPropDataTable( "terrorlocaldata", 0, 0, &REFERENCE_RECV_TABLE( DT_TerrorLocalPlayerExclusive ) ),
	RecvPropFloat( RECVINFO( m_mainSequenceStartTime ) ),
	RecvPropInt( RECVINFO( m_nSequence ), 0, C_TerrorPlayer::RecvProxy_TerrorSequence ),
	RecvPropInt( RECVINFO( m_grenadeLayerSequence ) ),
	RecvPropInt( RECVINFO( m_fireLayerSequence ) ),
	RecvPropInt( RECVINFO( m_flinchLayerSequence ) ),
	RecvPropInt( RECVINFO( m_fidgetLayerSequence ) ),
	RecvPropInt( RECVINFO( m_reloadLayerSequence ) ),
	RecvPropInt( RECVINFO( m_reloadAltLayerSequence ) ),
	RecvPropInt( RECVINFO( m_reloadLayerNumShells ) ),
	RecvPropFloat( RECVINFO( m_grenadeLayerStartTime ) ),
	RecvPropFloat( RECVINFO( m_fireLayerStartTime ) ),
	RecvPropFloat( RECVINFO( m_flinchLayerStartTime ) ),
	RecvPropFloat( RECVINFO( m_fidgetLayerStartTime ) ),
	RecvPropFloat( RECVINFO( m_reloadLayerStartTime ) ),
	RecvPropFloat( RECVINFO( m_reloadAltLayerStartTime ) ),
	RecvPropEHandle( RECVINFO( m_hBuildableButtonUseEnt ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_NetGestureSequence ), RecvPropInt( RECVINFO( m_NetGestureSequence[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_NetGestureActivity ), RecvPropInt( RECVINFO( m_NetGestureActivity[0] ), 0, RecvProxy_TerrorGestureActivity ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_NetGestureStartTime ), RecvPropFloat( RECVINFO( m_NetGestureStartTime[0] ) ) ),
	RecvPropFloat( RECVINFO( m_fServerAnimStartTime ) ),
	RecvPropBool( RECVINFO( m_hasVisibleThreats ) ),
	RecvPropFloat( RECVINFO( m_burnPercent ) ),
	RecvPropFloat( RECVINFO( m_bbqPercent ) ),
	RecvPropBool( RECVINFO( m_holdingObject ) ),
	RecvPropFloat( RECVINFO( m_healthBuffer ) ),
	RecvPropFloat( RECVINFO( m_healthBufferTime ) ),
	RecvPropBool( RECVINFO( m_isGoingToDie ) ),
	RecvPropFloat( RECVINFO( m_noiseLevel ) ),
	RecvPropInt( RECVINFO( m_noiseLevelLatch ) ),
	RecvPropInt( RECVINFO( m_iBloodyHandsLevel ) ),
	RecvPropDataTable( RECVINFO_DT( m_itTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropBool( RECVINFO( m_bAdrenalineActive ) ),
	RecvPropInt( RECVINFO( m_survivorCharacter ) ),
	RecvPropInt( RECVINFO( m_zombieClass ) ),
	RecvPropInt( RECVINFO( m_zombieState ), 0, RecvProxy_TerrorZombieState ),
	RecvPropBool( RECVINFO( m_isIncapacitated ) ),
	RecvPropBool( RECVINFO( m_isCalm ) ),
	RecvPropEHandle( RECVINFO( m_useActionOwner ) ),
	RecvPropEHandle( RECVINFO( m_useActionTarget ) ),
	RecvPropInt( RECVINFO( m_iCurrentUseAction ) ),
	RecvPropEHandle( RECVINFO( m_reviveOwner ) ),
	RecvPropEHandle( RECVINFO( m_reviveTarget ) ),
	RecvPropEHandle( RECVINFO( m_tongueVictim ) ),
	RecvPropEHandle( RECVINFO( m_tongueOwner ) ),
	RecvPropDataTable( RECVINFO_DT( m_tongueVictimTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropVector( RECVINFO( m_initialTonguePullDir ) ),
	RecvPropBool( RECVINFO( m_isHangingFromTongue ) ),
	RecvPropBool( RECVINFO( m_reachedTongueOwner ) ),
	RecvPropBool( RECVINFO( m_isProneTongueDrag ) ),
	RecvPropEHandle( RECVINFO( m_positionEntity ) ),
	RecvPropEHandle( RECVINFO( m_customAbility ) ),
	RecvPropArray( RecvPropInt( RECVINFO( m_checkpointAwardCounts[0] ) ), m_checkpointAwardCounts ),
	RecvPropArray( RecvPropInt( RECVINFO( m_missionAwardCounts[0] ) ), m_missionAwardCounts ),
	RecvPropArray( RecvPropInt( RECVINFO( m_checkpointZombieKills[0] ) ), m_checkpointZombieKills ),
	RecvPropArray( RecvPropInt( RECVINFO( m_missionZombieKills[0] ) ), m_missionZombieKills ),
	RecvPropInt( RECVINFO( m_checkpointSurvivorDamage ) ),
	RecvPropInt( RECVINFO( m_missionSurvivorDamage ) ),
	RecvPropArray( RecvPropInt( RECVINFO( m_classSpawnCount[0] ) ), m_classSpawnCount ),
	RecvPropInt( RECVINFO( m_iMaxHealth ) ),
	RecvPropFloat( RECVINFO( m_maxDeadDuration ) ),
	RecvPropFloat( RECVINFO( m_totalDeadDuration ) ),
	RecvPropFloat( RECVINFO( m_jumpSupressedUntil ) ),
	RecvPropInt( RECVINFO( m_checkpointMedkitsUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointPillsUsed ) ),
	RecvPropInt( RECVINFO( m_missionMedkitsUsed ) ),
	RecvPropInt( RECVINFO( m_missionPillsUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointMolotovsUsed ) ),
	RecvPropInt( RECVINFO( m_missionMolotovsUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointPipebombsUsed ) ),
	RecvPropInt( RECVINFO( m_missionPipebombsUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointBoomerBilesUsed ) ),
	RecvPropInt( RECVINFO( m_missionBoomerBilesUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointAdrenalinesUsed ) ),
	RecvPropInt( RECVINFO( m_missionAdrenalinesUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointDefibrillatorsUsed ) ),
	RecvPropInt( RECVINFO( m_missionDefibrillatorsUsed ) ),
	RecvPropInt( RECVINFO( m_checkpointDamageTaken ) ),
	RecvPropInt( RECVINFO( m_missionDamageTaken ) ),
	RecvPropInt( RECVINFO( m_checkpointReviveOtherCount ) ),
	RecvPropInt( RECVINFO( m_missionReviveOtherCount ) ),
	RecvPropInt( RECVINFO( m_checkpointFirstAidShared ) ),
	RecvPropInt( RECVINFO( m_missionFirstAidShared ) ),
	RecvPropInt( RECVINFO( m_checkpointIncaps ) ),
	RecvPropInt( RECVINFO( m_missionIncaps ) ),
	RecvPropInt( RECVINFO( m_checkpointDamageToTank ) ),
	RecvPropInt( RECVINFO( m_checkpointDamageToWitch ) ),
	RecvPropInt( RECVINFO( m_missionAccuracy ) ),
	RecvPropInt( RECVINFO( m_checkpointHeadshots ) ),
	RecvPropInt( RECVINFO( m_checkpointHeadshotAccuracy ) ),
	RecvPropInt( RECVINFO( m_missionHeadshotAccuracy ) ),
	RecvPropInt( RECVINFO( m_checkpointDeaths ) ),
	RecvPropInt( RECVINFO( m_missionDeaths ) ),
	RecvPropInt( RECVINFO( m_checkpointMeleeKills ) ),
	RecvPropInt( RECVINFO( m_missionMeleeKills ) ),
	RecvPropInt( RECVINFO( m_checkpointPZIncaps ) ),
	RecvPropInt( RECVINFO( m_checkpointPZTankDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZHunterDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZSmokerDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZBoomerDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZJockeyDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZSpitterDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZChargerDamage ) ),
	RecvPropInt( RECVINFO( m_checkpointPZKills ) ),
	RecvPropInt( RECVINFO( m_checkpointPZPounces ) ),
	RecvPropInt( RECVINFO( m_checkpointPZPushes ) ),
	RecvPropInt( RECVINFO( m_checkpointPZTankPunches ) ),
	RecvPropInt( RECVINFO( m_checkpointPZTankThrows ) ),
	RecvPropInt( RECVINFO( m_checkpointPZHung ) ),
	RecvPropInt( RECVINFO( m_checkpointPZPulled ) ),
	RecvPropInt( RECVINFO( m_checkpointPZBombed ) ),
	RecvPropInt( RECVINFO( m_checkpointPZVomited ) ),
	RecvPropInt( RECVINFO( m_checkpointPZHighestDmgPounce ) ),
	RecvPropInt( RECVINFO( m_checkpointPZLongestSmokerGrab ) ),
	RecvPropInt( RECVINFO( m_checkpointPZLongestJockeyRide ) ),
	RecvPropInt( RECVINFO( m_checkpointPZNumChargeVictims ) ),
	RecvPropBool( RECVINFO( m_isHangingFromLedge ) ),
	RecvPropBool( RECVINFO( m_isFallingFromLedge ) ),
	RecvPropDataTable( RECVINFO_DT( m_hangTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropVector( RECVINFO( m_hangAirPos ) ),
	RecvPropVector( RECVINFO( m_hangPos ) ),
	RecvPropVector( RECVINFO( m_hangStandPos ) ),
	RecvPropVector( RECVINFO( m_hangNormal ) ),
	RecvPropInt( RECVINFO( m_frustration ) ),
	RecvPropInt( RECVINFO( m_clientIntensity ) ),
	RecvPropEHandle( RECVINFO( m_pummelVictim ) ),
	RecvPropEHandle( RECVINFO( m_pummelAttacker ) ),
	RecvPropEHandle( RECVINFO_NAME( m_pummelTarget, m_queuedPummelVictim ) ),
	RecvPropEHandle( RECVINFO( m_carryAttacker ) ),
	RecvPropEHandle( RECVINFO( m_carryVictim ) ),
	RecvPropEHandle( RECVINFO( m_pounceVictim ) ),
	RecvPropEHandle( RECVINFO( m_pounceAttacker ) ),
	RecvPropEHandle( RECVINFO( m_jockeyVictim ) ),
	RecvPropEHandle( RECVINFO( m_jockeyAttacker ) ),
	RecvPropVector( RECVINFO( m_jockeyDesiredMove ) ),
	RecvPropBool( RECVINFO( m_jockeyFirmAttach ) ),
	RecvPropBool( RECVINFO( m_jockeyRotatedDesire ) ),
	RecvPropInt( RECVINFO( m_knockdownReason ) ),
	RecvPropDataTable( RECVINFO_DT( m_knockdownTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_staggerTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropVector( RECVINFO( m_staggerStart ) ),
	RecvPropVector( RECVINFO( m_staggerDir ) ),
	RecvPropFloat( RECVINFO( m_staggerDist ) ),
	RecvPropDataTable( RECVINFO_DT( m_tugTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropVector( RECVINFO( m_tugStart ) ),
	RecvPropVector( RECVINFO( m_tugDir ) ),
	RecvPropFloat( RECVINFO( m_tugDist ) ),
	RecvPropInt( RECVINFO( m_currentReviveCount ) ),
	RecvPropBool( RECVINFO( m_bIsOnThirdStrike ) ),
	RecvPropBool( RECVINFO( m_bIsFirstManOut ) ),
	RecvPropDataTable( RECVINFO_DT( m_music ), 0, &REFERENCE_RECV_TABLE( DT_Music ) ),
	RecvPropBool( RECVINFO( m_usingMountedGun ) ),
	RecvPropBool( RECVINFO( m_usingMountedWeapon ) ),
	RecvPropBool( RECVINFO( m_isGhost ) ),
	RecvPropBool( RECVINFO( m_bWasPresentAtSurvivalStart ) ),
	RecvPropEHandle( RECVINFO( m_vocalizationSubject ) ),
	RecvPropDataTable( RECVINFO_DT( m_vocalizationSubjectTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropVector( RECVINFO( m_pounceStartPosition ) ),
	RecvPropBool( RECVINFO( m_isAttemptingToPounce ) ),
	RecvPropFloat( RECVINFO( m_vomitStart ) ),
	RecvPropFloat( RECVINFO( m_vomitFadeStart ) ),
	RecvPropFloat( RECVINFO( m_bashedStart ) ),
	RecvPropFloat( RECVINFO( m_salivaStart ) ),
	RecvPropInt( RECVINFO( m_iVersusTeam ) ),
	RecvPropInt( RECVINFO( m_scrimmageType ) ),
	RecvPropEHandle( RECVINFO( m_lookatPlayer ) ),
	RecvPropBool( RECVINFO( m_bSurvivorGlowEnabled ) ),
	RecvPropBool( RECVINFO( m_bSurvivorPositionHidingWeapons ) ),
END_RECV_TABLE()

// Whole native five own prediction rows, in native descriptor order.
BEGIN_PREDICTION_DATA( C_TerrorPlayer )
	DEFINE_PRED_FIELD( m_customAbility, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_autoCrouchTimer.m_timestamp, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flNextShoveTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flLastShoveTime, FIELD_FLOAT, 0 ),
	DEFINE_PRED_FIELD( m_iShovePenalty, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

// C2540D0: one virtual speed query, rounded into the existing player speed.
void C_TerrorPlayer::RecalculateMaxSpeed()
{
	SetMaxSpeed( GetPlayerMaxSpeed() );
}

C_TerrorPlayer::C_TerrorPlayer()
{
	// Music default-constructs once at its native member phase.
	m_music.SetOwner( this );
	// Actual handle/timer/vector/panel/map/reference constructors are supplied
	// by the one ordered union. D586 removes only vectors7160/7180.
	AddVar( &m_Local.m_vecPunchAngle, &m_Local.m_iv_vecPunchAngle, LATCH_SIMULATION_VAR, false );
	AddVar( &m_Local.m_vecPunchAngleVel, &m_Local.m_iv_vecPunchAngleVel, LATCH_SIMULATION_VAR, false );
	{
		MDLCACHE_CRITICAL_SECTION();
		m_PlayerAnimState = new CTerrorPlayerAnimState( this );
	}
	m_bGrenadePresentationSuppressed = false;
	m_lowViolenceSequenceMap.SetLessFunc( DefLessFunc( int ) );
	m_nTeammateFocusCharacter = SurvivorCharacter_Unknown;
	m_bTeammateFocusActive = false;
	m_flClientInteractionState = 0.0f;
	m_tongueVictimTimer.Invalidate();
	m_tongueOwner = NULL;
	m_tongueVictim = NULL;
	m_vecEyeAngularVelocity.Init();
	m_vecEyeAngularAcceleration.Init();
	m_angPreviousMotionEyeAngles.Init();
	m_flLastEyeMotionUpdateTime = 0.0f;
	m_flViewBobTime = 0.0f;
	m_nViewBobFrame = 0;
	m_flCurrentThirdPersonCameraDistance = 0.0f;
	m_vecThirdPersonCameraOffset.Init();
	m_flUseActionViewYawOffset = 0.0f;
	m_angLatchedUseActionViewAngles.Init();
	m_bUseActionViewAnglesLatched = false;
	m_bRestoreUseActionViewYaw = false;
	m_pszClientExpressionScene = NULL;
	m_hClientExpressionScene = NULL;
	m_flNextExpressionUpdateTime = 0.0f;
	m_flPreviousVomitStart = 0.0f;
	m_angryFlex[0] = LocalFlexController_t( -1 );
	m_fearFlex[0] = LocalFlexController_t( -1 );
	m_angryFlex[1] = LocalFlexController_t( -1 );
	m_fearFlex[1] = LocalFlexController_t( -1 );
	m_angryFlex[2] = LocalFlexController_t( -1 );
	m_fearFlex[2] = LocalFlexController_t( -1 );
	m_deadFlex = LocalFlexController_t( -1 );
	m_eyesUpDownFlex = LocalFlexController_t( -1 );
	m_eyesRightLeftFlex = LocalFlexController_t( -1 );
	m_modelFlexTimer.Invalidate();
	m_flModelFlexState = 0.0f;
	m_flModelFlexBlend = 0.0f;
	m_presserFlex = LocalFlexController_t( -1 );
	m_GlowView[0].m_flAlpha = 0.0f;
	m_GlowView[0].m_bAbilityHighlight = false;
	m_GlowView[1].m_flAlpha = 0.0f;
	m_GlowView[1].m_bAbilityHighlight = false;
	m_previousLineOfScrimmagePos = m_lineOfScrimmagePos;
	m_previousRearLineOfScrimmagePos = m_rearLineOfScrimmagePos;
	m_previousYardLinePos[0] = m_yardLinePos[0];
	m_previousYardLinePos[1] = m_yardLinePos[1];
	m_previousYardLinePos[2] = m_yardLinePos[2];
	m_previousYardLinePos[3] = m_yardLinePos[3];
	m_previousYardLinePos[4] = m_yardLinePos[4];
	m_pHunterPounceSound = NULL;
	m_pSimulationDLight = NULL;
	m_nDismountPoseParameter = 0;
	m_flNextGlowSelectionUpdate = 0.0f;
	m_flClientMotionState = 0.0f;
	m_flSmartLookStart = 0.0f;
	m_bWasAliveLastClientThink = true;
	m_flPerformanceStatsStartTime = 0.0f;
	if ( m_jockeyFirmAttach )
		m_jockeyFirmAttach = false;
	if ( m_jockeyRotatedDesire )
		m_jockeyRotatedDesire = false;
	m_bWasPresentAtSurvivalStart = false;
	m_bBandageEffectActive = false;
	m_noiseLevelLatch = 0;
	m_flGlowNoiseTimestamp = gpGlobals->curtime;
	m_nGlowNoiseLatch = 0;
	m_flClientLookAtScale = 1.0f;
	m_nClientLookAtState = -1;
	m_jockeyAttachmentViewTime = 0.0f;
	m_bJockeyPredictionSuppressed = false;
	C_BaseEntity *world = ClientEntityList().GetBaseEntityFromHandle( CBaseHandle( 0, 0 ) );
	if ( m_hBuildableButtonUseEnt.Get() != world )
		m_hBuildableButtonUseEnt.Init( 0, 0 );
}


#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_TerrorPlayer::UpdateClientSideAnimation()
{
	if ( GetTeamNumber() < TEAM_SURVIVOR )
		return;
	if ( NextBotStop.GetBool() || ( static_cast< unsigned int >( GetFlags() ) & 0x80000000u ) != 0 )
		return;
	UpdateAnimationCulling();
	if ( GetSequence() != -1 )
		FrameAdvance( 0.0f );
	C_BasePlayer *forcedTarget = NULL;
	const int targetIndex = cl_viewtarget_player.GetInt();
	if ( targetIndex > 0 )
		forcedTarget = ToBasePlayer( ClientEntityList().GetBaseEntity( targetIndex ) );
	Vector forward;
	GetVectors( &forward, NULL, NULL );
	if ( forcedTarget )
	{
		Vector direction = forcedTarget->EyePosition() - EyePosition();
		VectorNormalize( direction );
		// Native computes this even though the following auto-target reject is
		// unreachable for the forced target. No angle interpolation is applied.
		const float dot = ( forward.y * direction.y + forward.x * direction.x ) + forward.z * direction.z;
		(void)dot;
	}
	QAngle eyeAngles = IsPlayer() && IsLocalPlayer() ? EyeAngles() : m_angEyeAngles;
	if ( forcedTarget )
	{
		m_viewtarget = forcedTarget->EyePosition();
		m_viewtarget.z = m_viewtarget.z + cl_viewtarget_offset.GetFloat();
	}
	else if ( m_lookatPlayer.Get() )
	{
		m_viewtarget = m_lookatPlayer->EyePosition();
		C_BasePlayer *localTarget = ToBasePlayer( m_lookatPlayer.Get() );
		if ( localTarget && localTarget->IsLocalPlayer() )
		{
			C_BasePlayer *target = ToBasePlayer( m_lookatPlayer.Get() );
			if ( target && target->GetObserverMode() == OBS_MODE_NONE )
				m_viewtarget = MainViewOrigin();
		}
	}
	else if ( !m_hSceneLookAtTarget.Get() || m_sceneLookAtTimer.IsElapsed() )
	{
		Vector offset;
		offset.x = forward.x * 1000.0f;
		offset.y = forward.y * 1000.0f;
		offset.z = forward.z * 1000.0f;
		m_viewtarget = EyePosition() + offset;
	}
	else
	{
		m_viewtarget = m_hSceneLookAtTarget->EyePosition();
		C_BasePlayer *localTarget = ToBasePlayer( m_hSceneLookAtTarget.Get() );
		if ( localTarget && localTarget->IsLocalPlayer() )
		{
			C_BasePlayer *target = ToBasePlayer( m_hSceneLookAtTarget.Get() );
			if ( target && target->GetObserverMode() == OBS_MODE_NONE )
				m_viewtarget = MainViewOrigin();
		}
	}
	m_PlayerAnimState->Update( eyeAngles.y, eyeAngles.x );
	if ( GetSequence() != -1 )
		OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
	if ( IsSurvivorTeam( GetTeamNumber() ) )
		UpdateSurvivorExpression();
	else if ( GetTeamNumber() == TEAM_INFECTED )
		UpdateInfectedExpression();
	C_BaseCombatWeapon *weapon = GetActiveWeapon();
	if ( weapon )
	{
		C_BaseViewModel *viewmodel = GetViewModel( weapon->m_nViewModelIndex );
		if ( viewmodel )
			static_cast< CTerrorViewModel * >( viewmodel )->UpdateTerrorArms();
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

const QAngle &C_TerrorPlayer::GetRenderAngles()
{
	if ( m_lifeState != LIFE_ALIVE && GetTeamNumber() == TEAM_SURVIVOR )
	{
		C_BaseEntity *rescue = TerrorGlow_FindRescueModel( this );
		if ( rescue ) return rescue->GetRenderAngles();
	}
	return BaseClass::GetRenderAngles(); // C258BE0 chains C222150.
}

bool C_TerrorPlayer::IsAnimationBusy() const
{
	return GetTerrorAnimState()->IsAnimationBusy();
}

Activity C_TerrorPlayer::GetTerrorActivity() const
{
	return GetTerrorAnimState()->GetTerrorActivity();
}

void C_TerrorPlayer::DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData )
{
	GetTerrorAnimState()->DoTerrorAnimationEvent( event, nData );
}

bool C_TerrorPlayer::ShouldHideGrenadeProjectile() const
{
	return m_bGrenadePresentationSuppressed || !GetTerrorAnimState()->IsGrenadeProjectilePresentationActive();
}

void C_TerrorPlayer::FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options )
{
	// C252390: ghost suppresses BOTH receivers; model handler precedes CS.
	if ( IsGhost() )
		return;
	GetTerrorAnimState()->FireEvent( origin, angles, event, options );
	BaseClass::FireEvent( origin, angles, event, options );
}

void C_TerrorPlayer::OnPreDataChanged( DataUpdateType_t updateType )
{
	m_PreviousServerGestureStartTime[0] = m_fireLayerStartTime;
	m_PreviousServerGestureSequence[0] = m_fireLayerSequence;
	m_PreviousServerGestureStartTime[1] = m_reloadLayerStartTime;
	m_PreviousServerGestureSequence[1] = m_reloadLayerSequence;
	m_PreviousServerGestureStartTime[2] = m_reloadAltLayerStartTime;
	m_PreviousServerGestureSequence[2] = m_reloadAltLayerSequence;
	m_PreviousServerGestureStartTime[3] = m_grenadeLayerStartTime;
	m_PreviousServerGestureSequence[3] = m_grenadeLayerSequence;
	m_PreviousServerGestureStartTime[4] = m_flinchLayerStartTime;
	m_PreviousServerGestureSequence[4] = m_flinchLayerSequence;
	m_PreviousServerGestureStartTime[5] = m_fidgetLayerStartTime;
	m_PreviousServerGestureSequence[5] = m_fidgetLayerSequence;
	for ( int slot = 0; slot < GESTURE_SLOT_COUNT; ++slot )
	{
		const int activity = m_NetGestureActivity[slot];
		m_PreviousNetGestureStartTime[slot] = m_NetGestureStartTime[slot];
		m_PreviousNetGestureActivity[slot] = activity;
	}
	// Native explicitly calls C_BasePlayer658F0, bypassing CCS for this callback.
	C_BasePlayer::OnPreDataChanged( updateType );
}

void C_TerrorPlayer::OnDataChanged( DataUpdateType_t updateType )
{
	for ( int slot = 0; slot < GESTURE_SLOT_COUNT; ++slot )
	{
		if ( m_NetGestureActivity[slot] != m_PreviousNetGestureActivity[slot] ||
			m_NetGestureStartTime[slot] != m_PreviousNetGestureStartTime[slot] )
			GetTerrorAnimState()->OnReceivedGesture( slot, static_cast< Activity >( m_NetGestureActivity[slot] ) );
	}
	if ( m_noiseLevelLatch != m_nGlowNoiseLatch )
	{
		m_nGlowNoiseLatch = m_noiseLevelLatch;
		m_flGlowNoiseTimestamp = gpGlobals->curtime;
	}
	BaseClass::OnDataChanged( updateType );
}

void C_TerrorPlayer::Spawn()
{
	BaseClass::Spawn();
}

extern ConVar player_use_radius;
extern ConVar player_incap_use_radius;
extern ConVar z_use_tolerance;
ConVar cl_debug_player_use( "cl_debug_player_use", "0", 0x0000000A,
    "Visualizes +use logic for the given range. Green cross=trace success, Red cross=trace too far, Green box=radius success" );

// C:6E87D0 is loaded byte1. One actual definition in CLIENT DLL; the
// independent SERVER DLL definition remains in U's server source.
bool g_bDebugPlayerUse = true;

C_BaseMountedWeapon *C_TerrorPlayer::GetMountedWeapon()
{
	// C26B330: mounted-weapon byte, same received use handle and real RTTI.
	return m_usingMountedWeapon ? dynamic_cast< C_BaseMountedWeapon * >( GetUseEntity() ) : NULL;
}

bool C_TerrorPlayer::IsPerformingOtherUseAction( int nAction ) const
{
	// C269B00 has the same self-target exclusion as S2C7F30.
	if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
		return false;
	if ( m_useActionOwner.Get() != this )
		return false;
	return ToBasePlayer( m_useActionTarget.Get() ) != this;
}

void C_TerrorPlayer::OnMountedWeaponFired()
{
	// C254670. Native gun/minigun firing timers; also runs for remotes.
	if ( m_continuousFireInterval.GetStartTime() <= 0.0f )
		m_continuousFireInterval.Start();
	m_terrorFireInterval.Start();
}

bool C_TerrorPlayer::IsPerformingUseAction( int nAction ) const
{
    // C:269A00. Zero asks for any NONZERO current action.
    if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
        return false;
    return m_useActionOwner.Get() == this;
}

bool C_TerrorPlayer::IsUseActionTarget( int nAction ) const
{
    // C:269970. An absent owner is also "not this" in the original.
    if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
        return false;
    if ( m_useActionOwner.Get() == this )
        return false;
    return ToTerrorPlayer( m_useActionTarget.Get() ) == this;
}

bool C_TerrorPlayer::IsPerformingSelfUseAction( int nAction ) const
{
    // C:269A70 requires BOTH owner and player-valued target to be this.
    if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
        return false;
    return m_useActionOwner.Get() == this && ToTerrorPlayer( m_useActionTarget.Get() ) == this;
}

bool C_TerrorPlayer::IsDominated() const
{
    // C:26C7B0. Actual five serial-checked received handles.
    return IsPummelVictim() || IsBeingPounced() || IsTongueVictim() ||
        IsJockeyVictim() || IsBeingCarried();
}

bool C_TerrorPlayer::IsUseActionAnimationBlocked() const
{
    // C:27E330 -> C:146020 reads actual native MAIN activity. Client policy
    // differs from the server's independent animation-owner byte349.
    switch ( LookupTerrorOriginalActivity( GetTerrorActivity() ) )
    {
    case TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_NORTH:
    case TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_UP:
    case TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_INCAP:
    case TERROR_ORIGINAL_ACT_TERROR_CHARGER_POUNDED_DOWN:
        return true;
    default:
        return false;
    }
}

int C_TerrorPlayer::GetUsePriority( C_BaseEntity *pUser )
{
    // C:282C50. The life-state test is direct, not a copied server vfunc.
    if ( m_lifeState != LIFE_ALIVE )
        return 1;
    if ( CanBeRevived() )
        return 2;
    return GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ? 0 : 1;
}

int C_TerrorPlayer::ObjectCaps()
{
    // C:282CD0. Native client base caps are0; retain the actual base call.
    const int caps = C_BaseEntity::ObjectCaps();
    const Activity activity = GetTerrorActivity();
    if ( activity == ACT_TERROR_DIE_FROM_STAND || activity == ACT_TERROR_DIE_WHILE_RUNNING ||
        m_lifeState != LIFE_ALIVE ||
        ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
        return caps;
    return caps | ( CanBeRevived() ? FCAP_CONTINUOUS_USE : FCAP_IMPULSE_USE );
}

bool C_TerrorPlayer::IsUseableEntity( C_BaseEntity *pEntity, unsigned int requiredCaps )
{
    // C:254F20. This is NOT the server script/ghost/base delegation body.
    if ( !pEntity || pEntity->IsDormant() )
        return false;
    C_TerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
    if ( pPlayer && pPlayer->CanBeRevived() &&
        ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
        ( m_lifeState != LIFE_ALIVE ||
          ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ) )
        return false;
    if ( pPlayer && pPlayer->GetTeamNumber() != GetTeamNumber() )
        return false;
    const unsigned int caps = pEntity->ObjectCaps();
    if ( requiredCaps & FCAP_USE_BY_INFECTED )
        return ( caps & FCAP_USE_BY_INFECTED ) != 0;
    if ( !( caps & ( FCAP_IMPULSE_USE | FCAP_CONTINUOUS_USE | FCAP_ONOFF_USE | FCAP_DIRECTIONAL_USE ) ) ||
        ( requiredCaps & caps ) != requiredCaps )
        return false;
    // Original slot152 is MyCombatWeaponPointer; C016090 resolves actual
    // weapon m_hOwner then owner's MyCombatCharacterPointer, i.e.GetOwner.
    C_BaseCombatWeapon *pWeapon = pEntity->MyCombatWeaponPointer();
    return !pWeapon || !pWeapon->GetOwner();
}

typedef CUtlVectorFixedGrowable< C_BaseEntity *, 256 > UseCandidateVector;
typedef CUtlVectorFixedGrowable< IHandleEntity *, 256 > UseIgnoredVector;

class CUseTraceFilter : public CTraceFilterSimpleList
{
public:
    DECLARE_CLASS( CUseTraceFilter, CTraceFilterSimpleList );

    CUseTraceFilter( C_TerrorPlayer *pUser )
        : BaseClass( COLLISION_GROUP_INTERACTIVE ), m_pUser( pUser ),
          m_pWeapon( static_cast< CTerrorWeapon * >( pUser->GetActiveWeapon() ) )
    {
    }

    void AddEntitiesToIgnore( const UseIgnoredVector &entities )
    {
        // C:1AD930's native bulk append; copy native interface pointers once.
        m_PassEntities.AddMultipleToTail( entities.Count(), entities.Base() );
    }

    virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask ) OVERRIDE
    {
        // C:2830F0 first calls C:287250, the real list/no-ghosts base gate.
        if ( !BaseClass::ShouldHitEntity( pHandleEntity, contentsMask ) )
            return false;
        C_BaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
        if ( pEntity && pEntity->IsPlayer() && ToTerrorPlayer( pEntity )->IsGhost() )
            return false;

        // The original then resolves the candidate again in its own body.
        pEntity = EntityFromEntityHandle( pHandleEntity );
        if ( !pEntity || !pEntity->IsPlayer() )
            return true;
        C_TerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
        if ( pPlayer->IsGhost() || pPlayer->m_lifeState != LIFE_ALIVE || !m_pUser ||
            ( m_pUser->GetTeamNumber() != TEAM_SURVIVOR && m_pUser->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
            ( pPlayer->GetTeamNumber() != TEAM_SURVIVOR && pPlayer->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
            return false;
        if ( !pPlayer->CanBeRevived() )
            return m_pWeapon && ( m_pWeapon->GetWeaponID() == TERROR_WEAPON_PAIN_PILLS ||
                m_pWeapon->GetWeaponID() == TERROR_WEAPON_FIRST_AID_KIT ||
                m_pWeapon->GetWeaponID() == TERROR_WEAPON_ADRENALINE );
        return !pPlayer->m_reviveOwner.Get() || pPlayer->m_reviveOwner.Get() == m_pUser;
    }

private:
    C_TerrorPlayer *m_pUser;
    CTerrorWeapon *m_pWeapon;
};

static int UseCandidateSort( C_BaseEntity *const *pLeft, C_BaseEntity *const *pRight )
{
    // C:282AC0 is a BOOL comparator. Preserve its malformed0/1 result.
    return ( *pLeft )->entindex() < ( *pRight )->entindex();
}

static int UseIgnoredSort( IHandleEntity *const *pLeft, IHandleEntity *const *pRight )
{
    // C:282B40 resolves IHandleEntity through GetBaseEntity before entindex.
    return EntityFromEntityHandle( *pLeft )->entindex() < EntityFromEntityHandle( *pRight )->entindex();
}

static float UseCollisionDistanceSqr( C_BaseEntity *pEntity, const Vector &position )
{
    Vector local, nearest;
    CCollisionProperty *pCollision = pEntity->CollisionProp();
    pCollision->WorldToCollisionSpace( position, &local );
    CalcClosestPointOnAABB( pCollision->OBBMins(), pCollision->OBBMaxs(), local, nearest );
    return ( local - nearest ).LengthSqr();
}

C_BaseEntity *C_TerrorPlayer::FindUseEntity()
{
    // Source SDK noarg entry; genuine policy lives in the expanded selector.
    return FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
}

C_BaseEntity *C_TerrorPlayer::FindUseEntity( float flRange, float flDirectRange, float flMinimumDot,
    bool *pOutInRange, bool bPrioritizePlayers )
{
    // C:283200..2844F2. Do not replace with the different server body.
    if ( flDirectRange <= 0.0f || flDirectRange > flRange )
        flDirectRange = flRange;
    if ( flMinimumDot <= 0.0f )
    {
        CTerrorWeapon *pWeapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
        // Native client weapon v412, not backpack/melee/weapon-ID inference.
        flMinimumDot = pWeapon && pWeapon->IsCarriedProp() ? 0.98f : z_use_tolerance.GetFloat();
    }
    if ( pOutInRange )
        *pOutInRange = false;
    if ( IsPerformingUseAction() || GetMoveType() == MOVETYPE_LADDER || IsDominated() ||
        IsKnockedDown() || IsUseActionAnimationBlocked() )
        return NULL;
    if ( m_reviveTarget.Get() || m_reviveOwner.Get() )
    {
        if ( pOutInRange )
            *pOutInRange = m_reviveTarget.Get() != NULL;
        return m_reviveTarget.Get();
    }
    if ( m_dragTarget.Get() )
    {
        if ( pOutInRange )
            *pOutInRange = m_dragTarget.Get() != NULL;
        return m_dragTarget.Get();
    }

    Vector forward;
    EyeVectors( &forward );
    const Vector eye = EyePosition();
    // C:1091F0 replaces ONLY XY. It returns AbsOrigin Z in EAX without
    // storing it; the caller ignores EAX, so the vector retains EyePosition Z.
    Vector useOrigin = eye;
    useOrigin.x = GetAbsOrigin().x;
    useOrigin.y = GetAbsOrigin().y;
    const Vector directDelta = forward * flDirectRange;
    const Vector rangeDelta = forward * flRange;
    const float flRangeSqr = flRange * flRange;
    const float flDirectRangeSqr = flDirectRange * flDirectRange;
    const bool bDebug = g_bDebugPlayerUse && cl_debug_player_use.GetInt() == entindex();

    UseCandidateVector candidates;
    UseIgnoredVector ignored;
    CUseTraceFilter filter( this );
    filter.AddEntityToIgnore( this );
    trace_t directTrace;
    UTIL_TraceLine( eye, eye + rangeDelta, 0x0200608B, &filter, &directTrace );
    if ( bDebug )
    {
        NDebugOverlay::Sphere( useOrigin, vec3_angle, flRange, 255, 255, 0, 32, false, 0.5f );
        Msg( "\n" );
    }

    C_BaseEntity *pBest = NULL;
    C_BaseEntity *pDirect = directTrace.m_pEnt;
    float flBestDot = 0.0f;
    int nBestPriority = 0;
    bool bBestInRange = false;
    if ( pDirect && IsUseableEntity( pDirect, 0 ) )
    {
        const float flDistanceSqr = UseCollisionDistanceSqr( pDirect, useOrigin );
        if ( flRangeSqr > flDistanceSqr )
        {
            C_TerrorPlayer *pPlayer = ToTerrorPlayer( pDirect );
            bool bAccept = true;
            float flNearSqr = flDirectRangeSqr;
            if ( pPlayer && !pPlayer->CanBeRevived() )
            {
                if ( flRange == player_use_radius.GetFloat() && flDistanceSqr > 9216.0f )
                    bAccept = false;
                flNearSqr = 9216.0f;
            }
            if ( bAccept )
            {
                pBest = pDirect;
                flBestDot = 1.0f;
                nBestPriority = pPlayer && bPrioritizePlayers ? 2 : pDirect->GetUsePriority( this );
                bBestInRange = flDirectRange == flRange || flNearSqr >= flDistanceSqr;
            }
        }
    }

    const float flCollectRangeSqr = Square( flRange + 32.0f );
    const CUtlVector< C_BaseEntity * > &entities = C_BaseEntity::GetUseableEntityList();
    for ( int i = 0; i < entities.Count(); ++i )
    {
        C_BaseEntity *pEntity = entities[i];
        if ( !pEntity || pEntity == this || pEntity == pBest )
            continue;
        if ( bDebug )
        {
            Vector mins, maxs;
            pEntity->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
            NDebugOverlay::Box( vec3_origin, mins, maxs, 0, 0, 255, 20, 0.01023f );
        }
        if ( !( UseCollisionDistanceSqr( pEntity, useOrigin ) <= flCollectRangeSqr ) ||
            !IsUseableEntity( pEntity, 0 ) )
            continue;
        candidates.AddToTail( pEntity );
        if ( pEntity->GetUsePriority( this ) != 2 )
            ignored.AddToTail( pEntity );
    }
    candidates.Sort( UseCandidateSort );
    ignored.Sort( UseIgnoredSort );
    if ( ignored.Count() > 0 )
        filter.AddEntitiesToIgnore( ignored );

    for ( int i = 0; i < candidates.Count(); ++i )
    {
        C_BaseEntity *pCandidate = candidates[i];
        if ( bDebug )
            Msg( "entity %d(%s) at %f %f %f\n", pCandidate->entindex(), pCandidate->GetDebugName(),
                pCandidate->GetAbsOrigin().x, pCandidate->GetAbsOrigin().y, pCandidate->GetAbsOrigin().z );
        const int nPriority = pCandidate->IsPlayer() && bPrioritizePlayers ? 2 : pCandidate->GetUsePriority( this );
        if ( nPriority < nBestPriority )
            continue;
        const float flDistanceSqr = UseCollisionDistanceSqr( pCandidate, useOrigin );
        if ( bDebug )
        {
            Vector nearest;
            pCandidate->CollisionProp()->CalcNearestPoint( useOrigin, &nearest );
            char label[264];
            Q_snprintf( label, sizeof( label ), "%d: %f", pCandidate->entindex(), sqrt( (double)flDistanceSqr ) );
            NDebugOverlay::Text( nearest, label, true, 0.5f );
        }
        if ( flDistanceSqr > flRangeSqr )
            continue;
        const bool bInRange = flDirectRange == flRange || flDirectRangeSqr >= flDistanceSqr;
        if ( bBestInRange && !bInRange )
            continue;
        if ( pCandidate->IsPlayer() && flRange == player_use_radius.GetFloat() && flDistanceSqr > 9216.0f )
            continue;

        CCollisionProperty *pCollision = pCandidate->CollisionProp();
        Vector worldMins, worldMaxs, triggerMins, triggerMaxs;
        pCollision->WorldSpaceAABB( &worldMins, &worldMaxs );
        pCollision->WorldSpaceTriggerBounds( &triggerMins, &triggerMaxs );
        float flBloat = triggerMaxs.x - worldMaxs.x;
        const Vector &delta = bInRange ? directDelta : rangeDelta;
        CBaseTrace boxTrace;
        Vector candidatePoint;
        if ( pCandidate->GetSolid() == SOLID_OBB )
        {
            if ( !IntersectRayWithOBB( eye, delta, pCollision->CollisionToWorldTransform(),
                pCollision->OBBMins(), pCollision->OBBMaxs(), 0.0f, &boxTrace ) )
                continue;
            // Original applies the world transform AGAIN to the OBB endpoint.
            VectorTransform( boxTrace.endpos, pCollision->CollisionToWorldTransform(), candidatePoint );
        }
        else if ( IntersectRayWithBox( eye, delta, worldMins, worldMaxs, 1.0f, &boxTrace ) )
        {
            candidatePoint = boxTrace.endpos;
        }
        else
        {
            CalcClosestPointOnLineSegment( pCandidate->WorldSpaceCenter(), eye, eye + delta * 0.5f, candidatePoint );
        }
        if ( flBloat > 0.0f )
        {
            Vector towardEye = eye - candidatePoint;
            flBloat = MIN( VectorNormalize( towardEye ) - 1.0f, flBloat );
            if ( flBloat > 0.0f )
            {
                towardEye.z = MAX( towardEye.z, 0.0f );
                candidatePoint += towardEye * flBloat;
            }
        }
        Vector nearestPoint;
        pCollision->CalcNearestPoint( candidatePoint, &nearestPoint );
        if ( flBloat > 0.0f )
        {
            Vector towardEye = eye - nearestPoint;
            const float flShift = MIN( VectorNormalize( towardEye ) - 1.0f, flBloat );
            if ( flShift > 0.0f )
            {
                towardEye.z = MAX( towardEye.z, 0.0f );
                nearestPoint += towardEye * flShift;
            }
        }
        if ( bDebug )
        {
            NDebugOverlay::Cross3D( candidatePoint, 5.0f, 255, 255, 0, true, 0.5f );
            NDebugOverlay::Cross3D( nearestPoint, 5.0f, 255, 0, 255, true, 0.5f );
        }
        Vector direction = nearestPoint - eye;
        VectorNormalize( direction );
        const float flDot = DotProduct( direction, forward );
        if ( !( flMinimumDot <= flDot ) || !( ( !bBestInRange && bInRange ) ||
            flBestDot < flDot || nPriority > nBestPriority ) )
            continue;
        trace_t trace;
        UTIL_TraceLine( eye, nearestPoint, 0x0200608B, &filter, &trace );
        if ( trace.fraction != 1.0f && trace.m_pEnt != pCandidate )
        {
            UTIL_TraceLine( eye, pCandidate->WorldSpaceCenter(), 0x0200608B, &filter, &trace );
            if ( trace.fraction != 1.0f && trace.m_pEnt != pCandidate )
                continue;
        }
        nBestPriority = nPriority;
        bBestInRange = bInRange;
        flBestDot = flDot;
        pBest = pCandidate;
    }

    if ( bDebug )
    {
        if ( !pBest || pBest == pDirect )
        {
            const int red = pBest ? 0 : 255;
            const int green = pBest ? 255 : 0;
            NDebugOverlay::Line( useOrigin, directTrace.endpos, red, green, 0, true, 0.5f );
            NDebugOverlay::Cross3D( directTrace.endpos, 16.0f, red, green, 0, true, 0.5f );
        }
        else
        {
            NDebugOverlay::Box( pBest->WorldSpaceCenter(), Vector( -8, -8, -8 ), Vector( 8, 8, 8 ),
                0, 255, 0, 1, 0.5f );
        }
        if ( pBest )
            Msg( "Radial using: %d(%s) at %f %f %f\n", pBest->entindex(), pBest->GetDebugName(),
                pBest->GetAbsOrigin().x, pBest->GetAbsOrigin().y, pBest->GetAbsOrigin().z );
        else
            Msg( "Radial using: no usable entity found\n" );
    }

    if ( !pBest )
    {
        // Client has literal.85. No server engine slot126 query is involved.
        float flBestFallbackDot = 0.85f;
        for ( int i = 1; i <= gpGlobals->maxClients; ++i )
        {
            C_TerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
            if ( !pPlayer || pPlayer == this || !pPlayer->CanBeRevived() ||
                ( pPlayer->GetTeamNumber() != TEAM_SURVIVOR && pPlayer->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
                continue;
            Vector position = pPlayer->GetAbsOrigin();
            if ( pPlayer->IsHangingFromLedge() && GetAbsOrigin().z > position.z )
                position.z += g_pGameRules->GetViewVectors()->m_vView.z;
            Vector direction = position - EyePosition();
            if ( !( direction.LengthSqr() < Square( player_incap_use_radius.GetFloat() ) ) )
                continue;
            VectorNormalize( direction );
            const float flDot = DotProduct( direction, forward );
            if ( !( flBestFallbackDot < flDot ) )
                continue;
            CTraceFilterNoNPCsOrPlayer fallbackFilter( NULL, COLLISION_GROUP_NONE );
            trace_t trace;
            UTIL_TraceLine( position, EyePosition(), 0x0200608B, &fallbackFilter, &trace );
            if ( trace.fraction != 1.0f )
                continue;
            flBestFallbackDot = flDot;
            pBest = pPlayer;
            bBestInRange = true;
        }
    }
    if ( pOutInRange )
        *pOutInRange = bBestInRange;
    return pBest;
}

extern ConVar z_round_start_attack_prevention;

bool C_TerrorPlayer::IsStaggeringOrPending() const
{
    switch ( LookupTerrorOriginalActivity( GetTerrorActivity() ) )
    {
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_CHAINSAW:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_CHAINSAW:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_CHAINSAW:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_CHAINSAW:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_BAT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_BAT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_BAT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_BAT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_MELEE:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_MELEE:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_MELEE:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_MELEE:
    case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_L:
    case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_R:
    case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_BACKWARD:
    case TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_FORWARD:
        return true;
    default:
        return false;
    }
}

const Vector *C_TerrorPlayer::IsStaggering() const
{
    if ( !IsStaggeringOrPending() )
        return NULL;
    if ( gpGlobals->curtime >= m_staggerTimer.m_timestamp )
        return NULL;
    if ( ( GetAbsOrigin() - m_staggerStart ).Length() > m_staggerDist )
        return NULL;
    return &m_staggerDir;
}

bool C_TerrorPlayer::IsKnockedDown() const
{
    if ( m_knockdownReason == 0 )
        return false;
    float duration = 2.0f;
    if ( m_knockdownReason == 2 )
        duration = 1.5f;
    if ( m_knockdownReason == 3 )
        duration = 2.5f;
    return !IsIncapacitated() &&
        ( ( !m_pounceAttacker.Get() && !m_pounceVictim.Get() &&
            m_knockdownTimer.GetStartTime() > 0.0f && m_knockdownTimer.GetElapsedTime() < duration ) ||
          GetTerrorActivity() == ACT_TERROR_POUNCED_TO_STAND );
}

bool C_TerrorPlayer::CanAttack() const
{
    if ( ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
         m_roundStartTime[0] + z_round_start_attack_prevention.GetFloat() > gpGlobals->curtime )
        return false;
    if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 && !( GetFlags() & FL_ONGROUND ) )
        return false;
    if ( m_isHangingFromTongue || ( IsKnockedDown() && m_knockdownReason == 2 ) )
        return false;
    if ( IsStaggering() || m_pounceAttacker.Get() || m_jockeyAttacker.Get() || m_pummelAttacker.Get() )
        return false;
    const Activity activity = GetTerrorActivity();
    if ( IsAttackBlockingActivity( activity ) || IsPerformingUseAction( 10 ) || IsPerformingUseAction( 11 ) )
        return false;
    switch ( LookupTerrorOriginalActivity( activity ) )
    {
    case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH:
    case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TANKPUNCH:
    case TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND:
    case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_CHARGER:
    case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_CHARGERHIT:
    case TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW:
    case TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND:
        return false;
    default:
        return true;
    }
}

void C_TerrorPlayer::ResetForSpawn()
{
	m_roundStartTime[ GetTeamNumber() % 2 ] = gpGlobals->curtime;
	GetTerrorAnimState()->ClearAnimationState();
	m_jumpSupressedUntil = -1.0f;
	m_TimeForceExternalView = -1.0f;
	SetPunchAngle( vec3_angle );
	m_Local.m_vecPunchAngleVel = vec3_angle;
	QAngle angles;
	engine->GetViewAngles( angles );
	angles.z = 0.0f;
	engine->SetViewAngles( angles );
	m_flLastShoveTime = gpGlobals->curtime;
	m_iShovePenalty = 0;
	m_flNextShoveTime = 0.0f;
	UnforceButtons( IN_SPEED );
	UnforceButtons( IN_DUCK );
	EnableButtons( IN_JUMP );
}

// C26A7D0 preserves the death-animation exception before all use/pin gates.
bool C_TerrorPlayer::CanPerformBackpackAction() const
{
	if ( IsIncapacitated() )
		return false;
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( GetTerrorActivity() );
	if ( activity == TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND ||
		activity == TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING )
		return true;
	if ( IsBeingPounced() || m_pounceVictim.Get() || IsPummelVictim() ||
		m_pummelVictim.Get() || m_jockeyVictim.Get() || IsBeingCarried() || IsTongueVictim() )
		return false;
	const CTerrorWeapon *weapon = static_cast< const CTerrorWeapon * >( GetActiveWeapon() );
	return ( !weapon || weapon->CanPerformUseAction() ) && m_iCurrentUseAction == 0;
}

ConVar first_aid_kit_view_duration( "first_aid_kit_view_duration", "0.5", 0x0000000A,
	"time it takes to rotate the view when healing others", true, 0.1f, true, 2.0f );
ConVar cl_player_max_separation_force( "cl_player_max_separation_force", "500", 0x0000000A );
extern ConVar survivor_teammatefocus_speed;
extern ConVar survivor_teammatefocus_stickyness;
extern ConVar sb_pushscale;
extern ConVar z_sidespeed;
extern ConVar z_forwardspeed;
extern ConVar z_backspeed;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// C253DB0: one actual remapped connected/non-dormant character lookup.
C_TerrorPlayer *GetTerrorPlayerBySurvivorCharacter( int character )
{
	if ( !g_PR )
		return NULL;
	const int mapped = MapTerrorSurvivorCharacter( character );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && g_PR->IsConnected( i ) && !player->IsDormant() &&
			( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			player->GetSurvivorCharacter() == mapped )
			return player;
	}
	return NULL;
}

static float TerrorCommandAngleDiff( float desired, float current )
{
	const float delta = desired - current; // Native363500 wraps once.
	if ( delta < -180.0f )
		return delta + 360.0f;
	if ( delta > 180.0f )
		return delta - 360.0f;
	return delta;
}

// Whole C2550F0. Native D2ED constants are F32 loads promoted by the
// surrounding double expression, not decompiler-rounded double literals.
bool C_TerrorPlayer::CreateMove( float inputSampleTime, CUserCmd *cmd )
{
	const bool result = BaseClass::CreateMove( inputSampleTime, cmd );
	if ( !IsBeingPounced() && !m_pounceVictim.Get() && !IsBeingCarried() && !IsPummelVictim() )
	{
		if ( GetUseEntity() && m_flCurrentThirdPersonCameraDistance == 0.0f )
		{
			C_BaseMountedWeapon *mounted = GetMountedWeapon();
			if ( mounted )
			{
				QAngle angles;
				engine->GetViewAngles( angles );
				mounted->ClampViewAngles( angles );
				engine->SetViewAngles( angles );
				cmd->viewangles = angles;
			}
		}
		else if ( m_bTeammateFocusActive )
		{
			C_TerrorPlayer *target = GetTerrorPlayerBySurvivorCharacter( m_nTeammateFocusCharacter );
			if ( target )
			{
				const float speed = survivor_teammatefocus_speed.GetFloat() * inputSampleTime;
				const Vector origin = GetAbsOrigin();
				Vector targetPosition = target->WorldSpaceCenter();
				if ( target->IsIncapacitated() && !target->m_isHangingFromLedge && !target->m_isHangingFromTongue )
					targetPosition.z -= 42.0f;
				Vector delta = origin - targetPosition;
				const float dz = delta.z;
				const float length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
				const float divisor = length <= 1.0f ? 1.0f : length;
				const float pitch = (float)( atan2( (double)( dz / divisor ), 1.0 ) * 57.295780181884765625f );
				float old = cmd->viewangles.x;
				if ( old != pitch )
					cmd->viewangles.x = (float)( ( 1.0 - fabs( (double)old - pitch ) * 0.00277777784503996372222900390625f ) *
						speed * ( pitch - cmd->viewangles.x ) + cmd->viewangles.x );
				delta.z = 0.0f;
				VectorNormalize( delta );
				const float perpendicular = ( delta.y * 0.0f - delta.x * 1.0f ) + delta.z * 0.0f;
				if ( perpendicular != 0.0f )
				{
					const float current = cmd->viewangles.y;
					float yaw = (float)( acos( (double)perpendicular ) * 57.295780181884765625f );
					if ( current != yaw )
					{
						if ( delta.y > 0.0f )
							yaw = -yaw;
						old = cmd->viewangles.y;
						const double desired = (double)TerrorCommandAngleDiff( yaw, old ) + (double)old;
						cmd->viewangles.y = (float)( ( 1.0 - fabs( (double)old - desired ) * 0.00277777784503996372222900390625f ) *
							speed * ( desired - cmd->viewangles.y ) + cmd->viewangles.y );
					}
				}
				if ( survivor_teammatefocus_stickyness.GetInt() < abs( (int)cmd->mousedx ) + abs( (int)cmd->mousedy ) )
				{
					m_bTeammateFocusActive = false;
					m_nTeammateFocusCharacter = 8;
				}
			}
			else
			{
				m_bTeammateFocusActive = false;
				m_nTeammateFocusCharacter = 8;
			}
		}
		else
		{
			C_BaseEntity *useTarget = m_useActionTarget.Get();
			if ( useTarget || !( fabsf( m_flUseActionViewYawOffset ) <= 0.5f ) )
			{
				const bool usingTarget = useTarget != NULL;
				const float desired = ( !useTarget || useTarget == this || m_iCurrentUseAction == 10 ) ? 0.0f : -90.0f;
				float delta = desired - m_flUseActionViewYawOffset;
				if ( fabsf( delta ) > 0.5f )
				{
					float step = ( ( usingTarget ? 90.0f : 180.0f ) / first_aid_kit_view_duration.GetFloat() ) * gpGlobals->frametime;
					if ( delta > step || ( step = -step ) > delta )
						delta = step;
				}
				m_flUseActionViewYawOffset = m_flUseActionViewYawOffset + delta;
				if ( usingTarget || m_bRestoreUseActionViewYaw )
				{
					cmd->viewangles.y = AngleNormalize( cmd->viewangles.y + delta );
					engine->SetViewAngles( cmd->viewangles );
				}
				if ( usingTarget && fabsf( m_flUseActionViewYawOffset - desired ) < 0.5f )
				{
					if ( m_bUseActionViewAnglesLatched )
					{
						const float pitchDelta = AngleNormalize( cmd->viewangles.x - m_angLatchedUseActionViewAngles.x );
						const float yawDelta = AngleNormalize( cmd->viewangles.y - m_angLatchedUseActionViewAngles.y );
						if ( m_bRestoreUseActionViewYaw && ( fabsf( pitchDelta ) > 10.0f || fabsf( yawDelta ) > 10.0f ) )
							m_bRestoreUseActionViewYaw = false;
					}
					else
					{
						m_angLatchedUseActionViewAngles = cmd->viewangles;
						m_bUseActionViewAnglesLatched = true;
						m_bRestoreUseActionViewYaw = true;
					}
				}
			}
			else
			{
				m_bUseActionViewAnglesLatched = false;
			}

			Vector tongueTarget = vec3_origin;
			if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 1 )
			{
				CTongue *tongue = dynamic_cast< CTongue * >( GetCustomAbility() );
				if ( tongue && tongue->IsTongueExtended() )
				{
					if ( m_tongueVictim.Get() )
						tongueTarget = m_tongueVictim.Get()->EyePosition();
					else
						tongueTarget = tongue->GetTipPosition();
					if ( !tongueTarget.IsZero( 0.01f ) && m_lifeState == LIFE_ALIVE &&
						tongueTarget != vec3_origin && tongueTarget != EyePosition() )
					{
						QAngle viewAngles;
						engine->GetViewAngles( viewAngles );
						Vector forward;
						AngleVectors( viewAngles, &forward );
						Vector direction = tongueTarget - EyePosition();
						VectorNormalize( direction );
						QAngle targetAngles;
						VectorAngles( direction, targetAngles );
						const float dot = ( forward.y * direction.y + direction.x * forward.x ) + forward.z * direction.z;
						if ( cos( 0.5235987755982988 ) > (double)dot )
						{
							float pitch = AngleNormalize( viewAngles.x - targetAngles.x );
							const float yaw = viewAngles.y - targetAngles.y;
							if ( pitch > 30.0f )
								pitch = 30.0f;
							else if ( pitch < -30.0f )
								pitch = -30.0f;
							cmd->viewangles.x = pitch + targetAngles.x;
							cmd->viewangles.y = yaw + targetAngles.y;
							cmd->viewangles.z = targetAngles.z;
						}
					}
				}
			}
			AvoidPlayers( cmd );
		}
	}
	else
	{
		if ( cmd->viewangles.x < 20.0f )
			cmd->viewangles.x = 20.0f;
	}

	if ( m_jockeyVictim.Get() )
	{
		QAngle angles;
		engine->GetViewAngles( angles );
		const double yaw = (double)angles.y * 0.01745329238474369049072265625f;
		const float s = (float)sin( yaw ), c = (float)cos( yaw );
		const float forward = cmd->forwardmove;
		const float side = cmd->sidemove;
		const float rotatedSide = forward * s - side * c;
		cmd->forwardmove = side * s + forward * c;
		cmd->sidemove = rotatedSide;
	}
	if ( TerrorGameRules()->IsInIntro() &&
		( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		cmd->viewangles = vec3_angle;
	if ( GetMoveType() == MOVETYPE_NOCLIP && !engine->IsPlayingDemo() && in_joystick.GetBool() )
	{
		cmd->buttons &= ~( IN_MOVELEFT | IN_MOVERIGHT );
		cmd->sidemove = 0.0f;
	}
	return result;
}

// Whole C26BC80. Preserve the original capped-direction/uncapped-amount bug.
void C_TerrorPlayer::AvoidPlayers( CUserCmd *cmd )
{
	if ( m_lifeState != LIFE_ALIVE ||
		!( m_noAvoidanceTimer.m_timestamp <= 0.0f || gpGlobals->curtime >= m_noAvoidanceTimer.m_timestamp ) ||
		(unsigned int)( LookupTerrorOriginalActivity( GetTerrorActivity() ) - TERROR_ORIGINAL_ACT_TERROR_HEAL_SELF ) <= 7u ||
		IsBeingPounced() || IsPummelVictim() || m_jockeyVictim.Get() || IsJockeyVictim() || CanBeRevived() || IsGhost() )
		return;
	C_Team *team = GetTeam();
	if ( !team )
		return;

	Vector center = GetAbsOrigin();
	Vector mins = GetPlayerMins();
	Vector maxs = GetPlayerMaxs();
	mins.x = center.x + mins.x;
	mins.y = center.y + mins.y;
	maxs.x = maxs.x + center.x;
	center.z = ( maxs.z - mins.z ) * 0.5f + center.z;
	mins.z = center.z + mins.z;
	maxs.y = maxs.y + center.y;
	maxs.z = center.z + maxs.z;
	C_TerrorPlayer *overlap = NULL;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( team->GetPlayer( i ) );
		if ( !player || player == this || player->CanBeRevived() || player->IsGhost() ||
			player->IsDormant() || player->IsSolidFlagSet( FSOLID_NOT_SOLID ) )
			continue;
		const Vector otherOrigin = player->GetAbsOrigin();
		Vector otherMins = player->GetPlayerMins();
		Vector otherMaxs = player->GetPlayerMaxs();
		const float middleZ = ( otherMaxs.z - otherMins.z ) * 0.5f + otherOrigin.z;
		otherMins.y = otherMins.y + otherOrigin.y;
		otherMins.x = otherMins.x + otherOrigin.x;
		otherMins.z = middleZ + otherMins.z;
		otherMaxs.x = otherMaxs.x + otherOrigin.x;
		otherMaxs.y = otherMaxs.y + otherOrigin.y;
		otherMaxs.z = middleZ + otherMaxs.z;
		if ( IsBoxIntersectingBox( mins, maxs, otherMins, otherMaxs ) )
		{
			overlap = player;
			break;
		}
	}
	if ( !overlap )
		return;
	if ( !IsBot() && overlap->IsBot() )
		return;
	const bool botYieldingToHuman = IsBot() && !overlap->IsBot();
	const Vector toPlayer = overlap->WorldSpaceCenter() - center;
	const Vector collisionMins = overlap->WorldAlignMins();
	const Vector collisionMaxs = overlap->WorldAlignMaxs();
	const float extentY = collisionMaxs.y - collisionMins.y;
	const float extentX = collisionMaxs.x - collisionMins.x;
	const float width = sqrtf( ( extentY * extentY + 0.0f * 0.0f ) + extentX * extentX );
	const float range = sqrtf( ( toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z ) + toPlayer.x * toPlayer.x );
	const float maximumForce = (float)cl_player_max_separation_force.GetInt();
	float amount;
	if ( width == 0.0f )
	{
		amount = range < 0.0f ? 0.0f : maximumForce;
	}
	else
	{
		float fraction = ( range - width ) / ( 0.0f - width );
		if ( fraction < 0.0f )
			fraction = 0.0f;
		else if ( fraction > 1.0f )
			fraction = 1.0f;
		amount = 0.0f + ( maximumForce - 0.0f ) * fraction;
	}
	if ( botYieldingToHuman )
		amount = sb_pushscale.GetFloat() * amount;
	if ( !( amount >= 0.01f ) )
		return;

	Vector side;
	const Vector velocity = GetAbsVelocity();
	if ( velocity.x * velocity.x + velocity.y * velocity.y <= 0.1f )
	{
		const QAngle flatAngles( 0.0f, cmd->viewangles.y, cmd->viewangles.z );
		AngleVectors( flatAngles, NULL, &side, NULL );
	}
	else
	{
		const Vector freshVelocity = GetAbsVelocity();
		side.x = 0.0f * 0.0f - freshVelocity.y * 1.0f;
		side.y = freshVelocity.x * 1.0f - 0.0f * 0.0f;
		side.z = freshVelocity.y * 0.0f - freshVelocity.x * 0.0f;
		VectorNormalize( side );
	}
	float signedAmount = amount;
	if ( ( side.y * toPlayer.y + side.x * toPlayer.x ) + side.z * toPlayer.z >= 0.0f )
		signedAmount = -amount;
	Vector force;
	force.x = side.x * signedAmount;
	force.y = side.y * signedAmount;
	force.z = side.z * signedAmount;
	float maximum = MaxSpeed();
	if ( ( GetFlags() & FL_DUCKING ) && GetGroundEntity() )
		maximum = maximum * 1.3333334f;
	if ( ( force.y * force.y + force.x * force.x ) + force.z * force.z > maximum * maximum )
	{
		VectorNormalize( force );
		force.x = force.x * maximum;
		force.y = force.y * maximum;
		force.z = force.z * maximum;
	}

	const QAngle flatAngles( 0.0f, cmd->viewangles.y, cmd->viewangles.z );
	Vector forward, right;
	AngleVectors( flatAngles, &forward, &right, NULL );
	Vector direction = force;
	VectorNormalize( direction );
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT );
	const float down = -m_Local.m_flStepSize;
	Vector end = GetAbsOrigin();
	end.x = end.x + direction.x * 20.0f;
	end.y = end.y + direction.y * 20.0f;
	end.z = ( end.z + down ) + direction.z * 20.0f;
	Vector start = GetAbsOrigin();
	start.x = start.x + direction.x * 20.0f;
	start.y = start.y + direction.y * 20.0f;
	start.z = start.z + direction.z * 20.0f;
	trace_t trace;
	const unsigned int mask = PlayerSolidMask( false );
	const Vector hullMaxs = GetPlayerMaxs();
	const Vector hullMins = GetPlayerMins();
	UTIL_TraceHull( start, end, hullMins, hullMaxs, mask, &filter, &trace );
	if ( !trace.DidHit() )
		return;
	const float forwardMove = ( ( forward.y * direction.y + forward.x * direction.x ) + forward.z * direction.z ) *
		amount + cmd->forwardmove;
	const float sideMove = ( ( right.y * direction.y + right.x * direction.x ) + right.z * direction.z ) *
		amount + cmd->sidemove;
	cmd->forwardmove = forwardMove;
	cmd->sidemove = sideMove;
	float forwardScale = 1.0f;
	const float forwardLimit = fabsf( z_forwardspeed.GetFloat() );
	if ( forwardMove <= forwardLimit )
	{
		const float backwardLimit = fabsf( z_backspeed.GetFloat() );
		if ( -backwardLimit > forwardMove )
			forwardScale = backwardLimit / fabsf( forwardMove );
	}
	else
	{
		forwardScale = forwardLimit / forwardMove;
	}
	const float absoluteSide = fabsf( sideMove );
	float sideScale = 1.0f;
	const float sideLimit = fabsf( z_sidespeed.GetFloat() );
	if ( absoluteSide > sideLimit )
		sideScale = sideLimit / absoluteSide;
	if ( sideScale <= forwardScale )
		forwardScale = sideScale;
	cmd->forwardmove = forwardMove * forwardScale;
	cmd->sidemove = sideMove * forwardScale;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Whole C253BD0, actual primary190: only the OVERLAY accumulation is culled;
// this is NOT primary175 BuildTransformations. No bone-transform suppression.
void C_TerrorPlayer::AccumulateLayers( IBoneSetup &setup, Vector pos[], Quaternion q[], float currentTime )
{
	if ( m_nAnimationCullFrame != gpGlobals->framecount || !m_bAnimationCulled )
		BaseClass::AccumulateLayers( setup, pos, q, currentTime );
}

// Whole C259740. CUtlVector RemoveAll retains allocation, matching native count0.
void C_TerrorPlayer::MaintainSequenceTransitions( IBoneSetup &setup, float cycle, Vector pos[], Quaternion q[] )
{
	if ( m_nAnimationCullFrame == gpGlobals->framecount && m_bAnimationCulled )
		m_SequenceTransitioner.m_animationQueue.RemoveAll();
	BaseClass::MaintainSequenceTransitions( setup, cycle, pos, q );
}

// Whole C252680 / primary175 is literally JMP C_BaseAnimating39850. Preserve
// the real inherited implementation; no culling, transforms or bone repair.
void C_TerrorPlayer::BuildTransformations( CStudioHdr *studio, Vector *pos, Quaternion q[],
	const matrix3x4_t &cameraTransform, int boneMask, CBoneBitList &computed )
{
	BaseClass::BuildTransformations( studio, pos, q, cameraTransform, boneMask, computed );
}

// Whole C256FF0. One genuine base nullsub then lazy current model / exact
// case-insensitive pose lookup. Native NULL LookupPoseParameter returns-1;
// constructor's initial0 bug remains until the real model callback executes.
void C_TerrorPlayer::OnModelPoseParametersChanged()
{
	BaseClass::OnModelPoseParametersChanged();
	m_nDismountPoseParameter = LookupPoseParameter( "dismount" );
}

// C26ABA0, existing Terror class, no alternate generic hull provider.
void C_TerrorPlayer::UpdateCollisionBounds()
{
	if ( !m_isHangingFromLedge && ( GetFlags() & FL_DUCKING ) )
	{
		const Vector &maxs = GameRules()->GetViewVectors()->m_vDuckHullMax;
		const Vector &mins = GameRules()->GetViewVectors()->m_vDuckHullMin;
		CollisionProp()->SetCollisionBounds( mins, maxs );
	}
	else if ( !m_isHangingFromLedge && m_jockeyVictim.Get() )
	{
		Vector maxs = GameRules()->GetViewVectors()->m_vHullMax;
		maxs.z += 32.0f;
		const Vector &mins = GameRules()->GetViewVectors()->m_vHullMin;
		CollisionProp()->SetCollisionBounds( mins, maxs );
	}
	else
	{
		const Vector &maxs = GameRules()->GetViewVectors()->m_vHullMax;
		const Vector &mins = GameRules()->GetViewVectors()->m_vHullMin;
		CollisionProp()->SetCollisionBounds( mins, maxs );
	}
}

// C252710, existing Terror class. Ghost suppresses both water outputs.
void C_TerrorPlayer::GetFootstepSoundPosition( bool running, bool left, bool force,
	Vector *origin, bool *shallowWater, bool *wading )
{
	if ( !IsGhost() )
	{
		BaseClass::GetFootstepSoundPosition( running, left, force, origin, shallowWater, wading );
		return;
	}
	if ( origin ) *origin = GetAbsOrigin();
	if ( shallowWater ) *shallowWater = false;
	if ( wading ) *wading = false;
}

// Whole252DF0 through its actual unavailable-interface boundary. Main approved
// omitting ONLY the MatchFramework1088B824+44 -> GetLocalPlayer -> stats/property
// chain: no IMatchFramework exists on target (recorded MatchSession25BED0 gap).
// The dtor's literal(0,0) performs no writes in that omitted chain. Do NOT return
// fake matchmaking/player/stat objects or turn this into a per-frame producer.
void ResetClientTerrorPlayerPerformanceStats( int reason, int result )
{
	if ( engine->IsPlayingDemo() )
		return;
	// One actual local player/user0; the native iterator's NULL arm remains.
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );
	if ( player )
	{
		player->m_flPerformanceStatsStartTime = static_cast< float >( Plat_FloatTime() );
		const bool eligible = g_TerrorModes.IsBaseMode( "versus" ) && TerrorGameRules() &&
			TerrorGameRules()->IsInSecondHalfOfRound() && reason == 1 && result == 5;
		bool won = false;
		if ( eligible )
		{
			const int team = player->m_iVersusTeam;
			int score = TerrorGameRules()->GetVersusChapterScore( team );
			if ( score == 0 )
				score = TerrorGameRules()->GetVersusScore( team, false );
			const int other = 2 - ( team != 1 );
			int otherScore = TerrorGameRules()->GetVersusChapterScore( other );
			if ( otherScore == 0 )
				otherScore = TerrorGameRules()->GetVersusScore( other, false );
			won = score >= otherScore;
		}
		(void)won; // Only its unavailable framework stats writer consumes this.
	}
}

// Whole25DCE0. Native callers use bounded real slots0/1; no added clamp/guard.
// StopEmission can invoke deletion callbacks before the explicit reference clear.
void C_TerrorPlayer::ClearTerrorLaserSightEffects( int user )
{
	if ( m_hThirdPersonLaserSightParticles[user] )
	{
		ParticleProp()->StopEmission( m_hThirdPersonLaserSightParticles[user] );
		m_hThirdPersonLaserSightParticles[user] = NULL;
	}
	if ( m_hFirstPersonLaserSightParticles[user] )
	{
		ParticleProp()->StopEmission( m_hFirstPersonLaserSightParticles[user] );
		m_hFirstPersonLaserSightParticles[user] = NULL;
	}
}

C_TerrorPlayer::~C_TerrorPlayer()
{
	RemoveTerrorTeammateFlashlight( entindex() );
	if ( C_BasePlayer::GetLocalPlayer() == this )
	{
		if ( SteamTimeline() )
			SteamTimeline()->ClearTimelineStateDescription( 0.0f );
		ResetClientTerrorPlayerPerformanceStats( 0, 0 );
	}
	for ( int user = 0; user < 2; ++user )
		ClearTerrorLaserSightEffects( user );
	// Requires canonical native151180 int onlyDrawForLocalUser filter cutover;
	// SDK's unrelated third bool inverse flag is NOT an equivalent call.
	ParticleProp()->StopParticlesNamed( "blood_bleedout", true, -1 );
	ParticleProp()->StopParticlesNamed( "blood_incapacitated", true, -1 );
	for ( int i = 0; i < 5; ++i )
		m_yardLinePanels[i].Deactivate();
	if ( m_pHunterPounceSound )
	{
		CSoundEnvelopeController::GetController().SoundDestroy( m_pHunterPounceSound );
		m_pHunterPounceSound = NULL;
	}
	if ( m_pSimulationDLight )
		m_pSimulationDLight->die = gpGlobals->curtime;
	m_pSimulationDLight = NULL;
	StopSpeechVoiceParticle();
	// No additional clears/stops/frees. In particular7160/7180 are D586-elided;
	// the map and borrowed expression name are not replacement-owned objects.
}

ConVar survivor_expressions( "survivor_expressions", "1", 0x0000000A );
ConVar survivor_expressions_anger( "survivor_expressions_anger", "-1", 0x0000000A );
ConVar survivor_expressions_fear( "survivor_expressions_fear", "-1", 0x0000000A );
ConVar cl_viewtarget_debug( "cl_viewtarget_debug", "0", 0x00004008 );
// Exact client105137E0 definition; the existing server owner is independent.
ConVar survivor_ledge_grab_health( "survivor_ledge_grab_health", "300", 0x0000200A );
extern ConVar survivor_limp_health;

// Whole C2522A0. Reuse sole health-buffer decay owner; native CVTTSS2SI has its
// defined INT_MIN indefinite for unordered/out-of-range, not C++ cast UB.
int C_TerrorPlayer::GetRealHealth() const
{
	const float buffer = GetHealthBuffer();
	const float health = static_cast< float >( GetHealth() );
	const float total = health + buffer;
	return _mm_cvttss_si32( _mm_set_ss( total ) );
}

C_BaseCombatWeapon *C_TerrorPlayer::Weapon_GetSlot( int slot ) const
{
	if ( !IsPlayer() || !IsLocalPlayer() )
		return NULL;
	return BaseClass::Weapon_GetSlot( slot );
}

// C253C20: only the currently selected expression scene ramps flexes in.
bool C_TerrorPlayer::IsSceneActive( CChoreoScene *scene )
{
	C_SceneEntity *expression = m_hClientExpressionScene.Get();
	return expression && expression->GetChoreoScene() == scene;
}

// Whole C257040. Native leaves stale/unresolved nonINVALID handle bits alone;
// playing scenes use the direct delete QUEUE, not scheduled Root::Remove.
void C_TerrorPlayer::ClearClientExpressionScene()
{
	C_SceneEntity *scene = m_hClientExpressionScene.Get();
	if ( scene )
	{
		if ( scene->IsPlayingBack() )
		{
			scene->StopClientOnlyScene();
			ClientEntityList().AddToDeleteList( scene );
		}
		m_hClientExpressionScene = NULL;
	}
	m_pszClientExpressionScene = NULL;
}

// Whole C258D50. Real Source scene entity, model-less client init false (native
//0 is renderWithViewModels, NOT SDK RenderGroup_t0), then genuine leaf enable.
void C_TerrorPlayer::SetClientExpressionScene( const char *expression )
{
	if ( !m_pszClientExpressionScene || !expression || V_stricmp( m_pszClientExpressionScene, expression ) != 0 )
	{
		ClearClientExpressionScene();
		if ( expression && survivor_expressions.GetBool() && m_survivorCharacter != 8 )
		{
			const char *filename = VarArgs( "scenes/%s/%s", GetSurvivorCharacterName( m_survivorCharacter ), expression );
			C_SceneEntity *scene = new C_SceneEntity;
			if ( scene->InitializeAsClientEntity( "", false ) )
			{
				ClientLeafSystem_EnableRendering( scene->GetRenderHandle(), false );
				m_hClientExpressionScene = scene;
				m_pszClientExpressionScene = expression;
				scene->SetupClientOnlyScene( filename, this, true );
			}
			else
			{
				scene->Remove();
			}
		}
	}
}

// Whole C257110. survivor_expressions==0 clears angry/fear pairs ascending;
// dead weight then state0. Do not invert the cvar guard to the live-body guard.
void C_TerrorPlayer::UpdateDeadExpression()
{
	ClearClientExpressionScene();
	if ( !survivor_expressions.GetBool() )
	{
		for ( int i = 0; i < 3; ++i )
		{
			SetFlexWeight( m_angryFlex[i], 0.0f );
			SetFlexWeight( m_fearFlex[i], 0.0f );
		}
	}
	SetFlexWeight( m_deadFlex, 1.0f );
	m_nClientExpressionState = 0;
}

// Whole C257180. Every arithmetic temporary below is native SSE f32. Ordered
// clamps preserve NaN; JBE selection complements below explicitly retain native
// unordered choices. Hanging tongue (8068) is NOT going-to-die (8116).
void C_TerrorPlayer::GetExpressionIntensity( float *fear, float *anger )
{
	const float health = static_cast< float >( GetRealHealth() );
	if ( m_isHangingFromTongue )
	{
		*anger = 1.0f;
		*fear = 0.0f;
	}
	else if ( m_isHangingFromLedge )
	{
		*anger = 0.0f;
		*fear = ( 1.0f - static_cast< float >( GetHealth() ) / survivor_ledge_grab_health.GetFloat() ) + 0.33000001f;
		if ( *fear > 1.0f )
			*fear = 1.0f;
	}
	else if ( IsTongueVictim() || IsBeingPounced() || IsJockeyVictim() || m_isFallingFromLedge || IsPummelVictim() )
	{
		*anger = 0.0f;
		*fear = 1.0f;
	}
	else if ( !m_itTimer.IsElapsed() || IsIncapacitated() )
	{
		*anger = 0.0f;
		*fear = 0.66000003f;
	}
	else
	{
		*anger = static_cast< float >( m_clientIntensity ) * 0.0099999998f;
		*fear = 0.0f;
		const float limp = survivor_limp_health.GetFloat() * 1.25f;
		if ( limp > health )
		{
			float intensity = static_cast< float >( m_clientIntensity ) * 0.0099999998f;
			if ( intensity > 1.0f )
				intensity = 1.0f;
			else if ( intensity < 0.33000001f )
				intensity = 0.33000001f;
			float distress = ( 1.0f - health / limp ) * 2.0f;
			if ( distress > 1.0f ) distress = 1.0f;
			else if ( distress < 0.0f ) distress = 0.0f;
			*fear = ( intensity * 0.66000003f ) * distress;
			*anger = ( 1.0f - distress ) * intensity;
		}
	}
	if ( m_combatExpressionTimer.HasStarted() &&
		!( gpGlobals->curtime >= static_cast< float >( m_combatExpressionTimer.m_timestamp ) ) )
	{
		float ratio = 0.0f;
		if ( m_combatExpressionTimer.HasStarted() )
		{
			const float remaining = static_cast< float >( m_combatExpressionTimer.m_timestamp ) - gpGlobals->curtime;
			ratio = remaining / static_cast< float >( m_combatExpressionTimer.m_duration );
			if ( ratio < 0.0f ) ratio = 0.0f;
			else if ( ratio > 1.0f ) ratio = 1.0f;
		}
		if ( !( *anger > ratio ) )
			*anger = m_combatExpressionTimer.GetRemainingRatio();
		*fear = ( 1.0f - *anger ) * *fear;
	}
	if ( m_continuousFireInterval.HasStarted() )
	{
		const float elapsed = m_continuousFireInterval.GetElapsedTime();
		*anger = elapsed * 0.2f;
		if ( *anger > 1.0f ) *anger = 1.0f;
		else if ( *anger < 0.0f ) *anger = 0.0f;
		const float sinceFire = m_terrorFireInterval.GetElapsedTime();
		if ( sinceFire > 3.0f )
		{
			const float fade = 1.0f - ( sinceFire - 3.0f ) * 0.5f;
			if ( !( fade > *anger ) )
				*anger = fade;
		}
		if ( *anger > 1.0f ) *anger = 1.0f;
		else if ( *anger < 0.0f ) *anger = 0.0f;
		*fear = ( 1.0f - *anger ) * *fear;
	}
	const float angerOverride = survivor_expressions_anger.GetFloat();
	if ( angerOverride >= 0.0f )
		*anger = angerOverride;
	const float fearOverride = survivor_expressions_fear.GetFloat();
	if ( fearOverride >= 0.0f )
		*fear = fearOverride;
}

// Whole C2575A0. Original activity695 is ACT_SECONDARYATTACK, NOT a custom
// boomer-vomit enum. Canonical base animstate owns the slot/activity/layer.
void C_TerrorPlayer::UpdateInfectedExpression()
{
	if ( GetTeamNumber() != TEAM_INFECTED || m_zombieClass != 2 )
	{
		SetFlexWeight( m_presserFlex, 0.0f );
		return;
	}
	if ( m_PlayerAnimState->IsGestureSlotPlaying( 0, TerrorActivityFromOriginal( 695 ) ) )
	{
		SetFlexWeight( m_presserFlex, m_PlayerAnimState->GetGestureSlotLayer( 0 )->m_flWeight );
		m_presserTimer.Invalidate();
	}
	else
	{
		if ( !m_presserTimer.HasStarted() || m_presserTimer.IsElapsed() )
		{
			m_presserTimer.Start( RandomFloat( 0.5f, 1.5f ) );
			m_flPresserTargetStrength = RandomFloat( 0.0f, 0.60000002f );
		}
		const float target = m_flPresserTargetStrength * 0.2f;
		const float previous = GetFlexWeight( m_presserFlex ) * 0.80000001f;
		SetFlexWeight( m_presserFlex, target + previous );
	}
}

// Whole C258E60. The original live guard is opposite UpdateDeadExpression.
void C_TerrorPlayer::UpdateSurvivorExpression()
{
	if ( m_lifeState != LIFE_ALIVE )
	{
		UpdateDeadExpression();
		return;
	}
	if ( survivor_expressions.GetBool() )
	{
		for ( int i = 0; i < 3; ++i )
		{
			SetFlexWeight( m_angryFlex[i], 0.0f );
			SetFlexWeight( m_fearFlex[i], 0.0f );
		}
	}
	SetFlexWeight( m_deadFlex, 0.0f );
	float anger = 0.0f;
	float fear = 0.0f;
	GetExpressionIntensity( &fear, &anger );
	if ( !survivor_expressions.GetBool() )
	{
		// The following two ramps are deliberately separate native owner writes.
		if ( m_angryFlex[0] >= 0 )
		{
			if ( !( anger <= 0.33333334f ) )
			{
				if ( !( anger <= 0.66666669f ) )
				{
					anger = ( anger - 0.66666669f ) * 3.0f;
					SetFlexWeight( m_angryFlex[0], 0.0f );
					SetFlexWeight( m_angryFlex[1], 1.0f - anger );
					SetFlexWeight( m_angryFlex[2], anger );
				}
				else
				{
					anger = ( anger - 0.33333334f ) * 3.0f;
					SetFlexWeight( m_angryFlex[0], 1.0f - anger );
					SetFlexWeight( m_angryFlex[1], anger );
					SetFlexWeight( m_angryFlex[2], 0.0f );
				}
			}
			else
			{
				SetFlexWeight( m_angryFlex[0], anger * 3.0f );
				SetFlexWeight( m_angryFlex[1], 0.0f );
				SetFlexWeight( m_angryFlex[2], 0.0f );
			}
		}
		if ( m_fearFlex[0] >= 0 )
		{
			if ( !( fear <= 0.33333334f ) )
			{
				if ( !( fear <= 0.66666669f ) )
				{
					fear = ( fear - 0.66666669f ) * 3.0f;
					SetFlexWeight( m_fearFlex[0], 0.0f );
					SetFlexWeight( m_fearFlex[1], 1.0f - fear );
					SetFlexWeight( m_fearFlex[2], fear );
				}
				else
				{
					fear = ( fear - 0.33333334f ) * 3.0f;
					SetFlexWeight( m_fearFlex[0], 1.0f - fear );
					SetFlexWeight( m_fearFlex[1], fear );
					SetFlexWeight( m_fearFlex[2], 0.0f );
				}
			}
			else
			{
				SetFlexWeight( m_fearFlex[0], fear * 3.0f );
				SetFlexWeight( m_fearFlex[1], 0.0f );
				SetFlexWeight( m_fearFlex[2], 0.0f );
			}
		}
	}
	else
	{
		if ( !( anger >= fear ) )
		{
			if ( fear > 0.667f ) SetClientExpressionScene( "exp_fear_level_3_01.vcd" );
			else if ( fear > 0.333f ) SetClientExpressionScene( "exp_fear_level_2_01.vcd" );
			else SetClientExpressionScene( "exp_fear_level_1_01.vcd" );
			m_flNextExpressionUpdateTime = 0.0f;
		}
		else if ( anger > 0.667f )
		{
			SetClientExpressionScene( "exp_angry_level_3_01.vcd" );
			m_flNextExpressionUpdateTime = 0.0f;
		}
		else if ( anger > 0.333f )
		{
			SetClientExpressionScene( "exp_angry_level_2_01.vcd" );
			m_flNextExpressionUpdateTime = 0.0f;
		}
		else if ( anger > 0.0f )
		{
			SetClientExpressionScene( "exp_angry_level_1_01.vcd" );
			m_flNextExpressionUpdateTime = 0.0f;
		}
		else if ( gpGlobals->curtime > m_flNextExpressionUpdateTime )
		{
			const int idle = RandomInt( 0, 2 );
			SetClientExpressionScene( idle == 0 ? "exp_idle_01.vcd" : idle == 1 ? "exp_idle_02.vcd" : "exp_idle_03.vcd" );
			m_flNextExpressionUpdateTime = gpGlobals->curtime + 30.0f;
		}
	}
	if ( cl_viewtarget_debug.GetInt() == entindex() )
		NDebugOverlay::HorzArrow( EyePosition(), m_viewtarget, 0.5f, 0, 255, 0, 128, true, 0.1f );
	if ( gpGlobals->curtime >= static_cast< float >( m_blinkTimer.m_timestamp ) )
	{
		m_blinktoggle = m_blinktoggle == 0;
		m_blinkTimer.Start( RandomFloat( 1.5f, 4.0f ) );
	}
}

// Whole C258A30, retaining precise helper call/handle/weapon order.
bool C_TerrorPlayer::CanDoSceneGesture()
{
	if ( m_isHangingFromLedge || GetMoveType() == MOVETYPE_LADDER || IsPerformingUseAction() ||
		m_reviveTarget.Get() || m_reviveOwner.Get() || m_isHangingFromTongue ||
		GetUseEntity() || m_pushEntity.Get() || IsBeingPounced() || IsJockeyVictim() ||
		IsPummelVictim() || IsKnockedDown() )
		return false;
	C_BaseCombatWeapon *weapon = GetActiveWeapon();
	return !weapon || !weapon->m_bInReload;
}

// Whole C259310. SDK GESTURE_SLOT_VCD is5, but this original transport uses3;
// preserve the actual native slot rather than relabeling/reordering seven slots.
bool C_TerrorPlayer::StartGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene,
	CChoreoEvent *event, CChoreoActor *actor, C_BaseEntity *target )
{
	info->m_nSequence = LookupSequence( event->GetParameters() );
	if ( info->m_nSequence < 0 || !CanDoSceneGesture() )
		return false;
	const Activity activity = static_cast< Activity >( GetSequenceActivity( info->m_nSequence ) );
	if ( activity == ACT_INVALID )
		Warning( "StartGestureSceneEvent tried to start sequence '%s' (%d) without Activity!\n",
			GetSequenceName( info->m_nSequence ), info->m_nSequence );
	else
		m_PlayerAnimState->RestartGesture( 3, activity, true, false );
	return true;
}

// Whole C259CE0, actual primary222 StartSceneEvent (five args), NOT ProcessEvent.
bool C_TerrorPlayer::StartSceneEvent( CSceneEventInfo *info, CChoreoScene *scene,
	CChoreoEvent *event, CChoreoActor *actor, C_BaseEntity *target )
{
	const int eventType = event->GetType();
	if ( eventType == CChoreoEvent::LOOKAT )
	{
		const float duration = event->GetDuration();
		m_sceneLookAtTimer.Start( duration );
		m_hSceneLookAtTarget = target;
		const double elapsed = ( static_cast< double >( gpGlobals->curtime ) -
			static_cast< double >( static_cast< float >( m_blinkTimer.m_timestamp ) ) ) +
			static_cast< double >( static_cast< float >( m_blinkTimer.m_duration ) );
		if ( elapsed > 1.0 )
		{
			m_blinktoggle = m_blinktoggle == 0;
			m_blinkTimer.Start( RandomFloat( 1.5f, 4.0f ) );
		}
		return false;
	}
	if ( eventType == CChoreoEvent::SEQUENCE || eventType == CChoreoEvent::GESTURE )
		return StartGestureSceneEvent( info, scene, event, actor, target );
	return BaseClass::StartSceneEvent( info, scene, event, actor, target );
}

void C_TerrorPlayer::OnNewParticleEffect( const char *name, CNewParticleEffect *effect )
{
	BaseClass::OnNewParticleEffect( name, effect );
	if ( !Q_strcmp( name, "cigarette_smoke" ) )
		m_hCigaretteSmoke = effect;
	else if ( !Q_strcmp( name, "molotov_groundfire" ) )
		m_hMolotovGroundFire = effect;
	else if ( !Q_strcmp( name, "screen_adrenaline" ) )
		m_hAdrenalineScreen = effect;
	else if ( !Q_strcmp( name, "burning_character_screen" ) )
		m_hBurningCharacterScreen = effect;
	else if ( !Q_strcmp( name, "speech_voice" ) )
		m_hSpeechVoice = effect;
	else if ( !Q_strcmp( name, "bandage_01" ) )
		m_bBandageEffectActive = true;
}

void C_TerrorPlayer::OnParticleEffectDeleted( CNewParticleEffect *effect )
{
	BaseClass::OnParticleEffectDeleted( effect );
	if ( effect == m_hCigaretteSmoke )
		m_hCigaretteSmoke = NULL;
	else if ( effect == m_hMolotovGroundFire )
		m_hMolotovGroundFire = NULL;
	else if ( effect == m_hSmokerSporeTrail )
		m_hSmokerSporeTrail = NULL;
	else if ( effect == m_hITBodyEffect )
		m_hITBodyEffect = NULL;
	else if ( effect == m_hVomitScreenEffect )
		m_hVomitScreenEffect = NULL;
	else if ( effect == m_hSmokerSporeAttack )
		m_hSmokerSporeAttack = NULL;
	else if ( effect == m_hBurningCharacterScreen )
		m_hBurningCharacterScreen = NULL;
	else if ( effect == m_hAdrenalineScreen )
		m_hAdrenalineScreen = NULL;
	else if ( effect == m_hSpeechVoice )
		m_hSpeechVoice = NULL;
}

// C25A730. Actual existing UTIL_IsLowViolence is native1AD240, not a new gate.
int C_TerrorPlayer::RemapLowViolenceSequence( int sequence )
{
	if ( !UTIL_IsLowViolence() )
		return sequence;
	const unsigned short index = m_lowViolenceSequenceMap.Find( sequence );
	if ( index == m_lowViolenceSequenceMap.InvalidIndex() )
		return sequence;
	return m_lowViolenceSequenceMap[index];
}

// C25AAC0, the TERROR custom proxy. Root generic proxy remains unchanged for
// all other BaseAnimating families; objectNULL deliberately leaves output alone.
void C_TerrorPlayer::RecvProxy_TerrorSequence( const CRecvProxyData *data,
	void *object, void *out )
{
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( object );
	if ( player )
	{
		*static_cast< int * >( out ) = player->RemapLowViolenceSequence( data->m_Value.m_Int );
		player->SetReceivedSequence();
		player->UpdateVisibility();
	}
}

// C261410. Whole body, including pre-base sequence snapshot and weighted
// selection order. The map is built even when low violence currently is FALSE.
CStudioHdr *C_TerrorPlayer::OnNewModel()
{
	const int oldSequence = GetSequence();
	CStudioHdr *model = BaseClass::OnNewModel();
	m_lowViolenceSequenceMap.RemoveAll(); // Real1E5600 retains backing allocation.
	if ( model )
	{
		for ( int sequence = 0; sequence < model->GetNumSeq(); ++sequence )
		{
			Activity replacement;
			switch ( LookupTerrorOriginalActivity( static_cast< Activity >(
				GetSequenceActivity( sequence ) ) ) )
			{
			case TERROR_ORIGINAL_ACT_TERROR_INCAP_FROM_TONGUE:
				replacement = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_INCAP_FROM_TONGUE_GERMANY );
				break;
			case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TONGUE:
				replacement = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TONGUE_GERMANY );
				break;
			case TERROR_ORIGINAL_ACT_TERROR_HANGING_FROM_TONGUE:
				replacement = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_HANGING_FROM_TONGUE_GERMANY );
				break;
			case TERROR_ORIGINAL_ACT_TERROR_STANDING_CHOKE_FROM_TONGUE:
				replacement = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_STANDING_CHOKE_FROM_TONGUE_GERMANY );
				break;
			default:
				continue;
			}
			const int selected = SelectWeightedSequence( replacement );
			if ( selected >= 0 )
				m_lowViolenceSequenceMap.Insert( sequence, selected );
		}
	}
	m_angryFlex[0] = FindFlexController( "angry1" );
	m_angryFlex[1] = FindFlexController( "angry2" );
	m_angryFlex[2] = FindFlexController( "angry3" );
	m_fearFlex[0] = FindFlexController( "fear1" );
	m_fearFlex[1] = FindFlexController( "fear2" );
	m_fearFlex[2] = FindFlexController( "fear3" );
	m_deadFlex = FindFlexController( "dead" );
	m_eyesUpDownFlex = FindFlexController( "eyes_updown" );
	m_eyesRightLeftFlex = FindFlexController( "eyes_rightleft" );
	m_modelFlexTimer.Invalidate();
	m_flModelFlexState = 0.0f;
	m_flModelFlexBlend = 0.0f;
	m_presserFlex = FindFlexController( "presser" );
	m_iv_flexWeight.Reset();
	for ( int index = 0; index < GetNumFlexControllers(); ++index )
		SetFlexWeight( static_cast< LocalFlexController_t >( index ), 0.0f );
	if ( model && oldSequence < model->GetNumSeq() )
	{
		SetSequence( oldSequence );
		ResetSequenceInfo();
	}
	RemoveFromLeafSystem();
	AddToLeafSystem(); // Whole46920, unconditional507 propagation, not guard.
	return model;
}

// Native26ACA0: same existing GetWeaponOfType contract as server2C98F0.
C_TerrorWeapon *C_TerrorPlayer::GetWeaponOfType( int weaponID ) const
{
	const unsigned char index = m_weaponIndexByID[weaponID];
	return index ? static_cast< C_TerrorWeapon * >( GetWeapon( index - 1 ) ) : NULL;
}

// Whole C259F00. Native deliberately ignores pOut and remote entities; the
// original diagnostic split-user scope is omitted ONLY under Main's user0 rule.
static void RecvProxy_TerrorZombieState( const CRecvProxyData *data, void *object, void *out )
{
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( object );
	if ( player && player->IsPlayer() && player->IsLocalPlayer() )
	{
		const int state = data->m_Value.m_Int;
		if ( player->m_zombieState != state )
			player->m_zombieState = state;
	}
}

static ConVar cl_glow_los_delay( "cl_glow_los_delay", "0.0", 0x00006008, "Time out of sight before a survivor friend shows up through a wall to a survivor." );
static ConVar cl_glow_los_fade_in_time( "cl_glow_los_fade_in_time", "0.5", 0x00006008, "Time after cl_glow_los_delay before a survivor friend shows up fully through wall." );
static ConVar cl_glow_los_fade_out_time( "cl_glow_los_fade_out_time", "0.5", 0x00006008, "Time after cl_glow_los_delay before a survivor friend goes away" );
static ConVar cl_glow_force( "cl_glow_force", "255", 0x00004008, "Forces glows on" );
extern ConVar z_noise_level_hold_time;
extern ConVar z_noise_level_fade_rate;

static ConVar cl_glow_ability_r( "cl_glow_ability_r", "1.0", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_ability_g( "cl_glow_ability_g", "0.0", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_ability_b( "cl_glow_ability_b", "0.0", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_ability_colorblind_r( "cl_glow_ability_colorblind_r", "0.3", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_ability_colorblind_g( "cl_glow_ability_colorblind_g", "1.0", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_ability_colorblind_b( "cl_glow_ability_colorblind_b", "1.0", 0x00080008, "Color of ability glow" );
static ConVar cl_glow_infected_r( "cl_glow_infected_r", "0.3", 0x00080008, "Color of infected glow" );
static ConVar cl_glow_infected_g( "cl_glow_infected_g", "0.4", 0x00080008, "Color of infected glow" );
static ConVar cl_glow_infected_b( "cl_glow_infected_b", "1.0", 0x00080008, "Color of infected glow" );
static ConVar cl_glow_ghost_infected_r( "cl_glow_ghost_infected_r", "0.3", 0x00080008, "Color of infected ghost glow" );
static ConVar cl_glow_ghost_infected_g( "cl_glow_ghost_infected_g", "0.4", 0x00080008, "Color of infected ghost glow" );
static ConVar cl_glow_ghost_infected_b( "cl_glow_ghost_infected_b", "1.0", 0x00080008, "Color of infected ghost glow" );
static ConVar cl_glow_infected_vomit_r( "cl_glow_infected_vomit_r", "0.79", 0x00080008, "Color the PZs see the IT victim glow" );
static ConVar cl_glow_infected_vomit_g( "cl_glow_infected_vomit_g", "0.07", 0x00080008, "Color the PZs see the IT victim glow" );
static ConVar cl_glow_infected_vomit_b( "cl_glow_infected_vomit_b", "0.72", 0x00080008, "Color the PZs see the IT victim glow" );
static ConVar cl_glow_survivor_r( "cl_glow_survivor_r", "0.3", 0x00080008, "Color of survivor team mate glow" );
static ConVar cl_glow_survivor_g( "cl_glow_survivor_g", "0.4", 0x00080008, "Color of survivor team mate glow" );
static ConVar cl_glow_survivor_b( "cl_glow_survivor_b", "1.0", 0x00080008, "Color of survivor team mate glow" );
static ConVar cl_glow_survivor_hurt_r( "cl_glow_survivor_hurt_r", "1.0", 0x00080008, "Color of survivor team mate glow when incapacitated" );
static ConVar cl_glow_survivor_hurt_g( "cl_glow_survivor_hurt_g", "0.4", 0x00080008, "Color of survivor team mate glow when incapacitated" );
static ConVar cl_glow_survivor_hurt_b( "cl_glow_survivor_hurt_b", "0.0", 0x00080008, "Color of survivor team mate glow when incapacitated" );
static ConVar cl_glow_survivor_vomit_r( "cl_glow_survivor_vomit_r", "1.0", 0x00080008, "Color the Survivors see the IT victim glow" );
static ConVar cl_glow_survivor_vomit_g( "cl_glow_survivor_vomit_g", "0.4", 0x00080008, "Color the Survivors see the IT victim glow" );
static ConVar cl_glow_survivor_vomit_b( "cl_glow_survivor_vomit_b", "0.0", 0x00080008, "Color the Survivors see the IT victim glow" );

bool TerrorGlow_IsIT( const C_TerrorPlayer &player )
{
	return gpGlobals->curtime < player.GetITTimerTimestamp();
}

// C:10255020. Noise refresh is produced by the actual received latch in
// OnDataChanged (native networkable-this+8 adjustment, C:10252D20).
float C_TerrorPlayer::GetGlowNoiseAlpha() const
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() && cl_glow_force.GetInt() != 0 )
		return cl_glow_force.GetFloat();
	if ( TerrorGlow_IsIT( *this ) )
		return 255.0f;
	float flNoise = m_noiseLevel;
	if ( flNoise > 0.0f )
	{
		const float flElapsed = gpGlobals->curtime - m_flGlowNoiseTimestamp;
		if ( flElapsed > z_noise_level_hold_time.GetFloat() )
		{
			flNoise -= ( flElapsed - z_noise_level_hold_time.GetFloat() ) * z_noise_level_fade_rate.GetFloat();
			if ( flNoise < 0.0f )
				return 0.0f;
		}
		if ( flNoise > 0.0f )
			return flNoise * 255.0f / m_noiseLevel;
	}
	return flNoise;
}

// C:10257CE0, with explicit per-slot view inputs replacing ONLY the original
// engine's active-slot TLS/view-array transport. Slot zero is produced by the
// real SDK2013 view hook; no additional local-player or camera is invented.
void C_TerrorPlayer::UpdateGlow( int slot, const Vector &viewOrigin, const Vector &viewForward )
{
	TerrorGlowPlayerState_t &state = m_GlowView[slot];
	const float flOldAlpha = state.m_flAlpha;
	C_TerrorPlayer *pRawLocal = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer( slot ) );
	C_TerrorPlayer *pViewer = ToTerrorPlayer( TerrorGlow_ResolveTeamColorPlayer( pRawLocal,
		TerrorGameRules()->IsPlayerControlledZombies() ) );
	if ( !pViewer || pViewer == this )
	{
		state.m_flAlpha = 0.0f;
		state.m_bAbilityHighlight = false;
		return;
	}
	if ( IsEffectActive( EF_NOINTERP ) || Teleported() )
	{
		state.m_flAlpha = 0.0f;
		state.m_bAbilityHighlight = false;
		state.m_NotVisibleTimer.Start();
		return;
	}
	if ( pViewer->m_lifeState != LIFE_ALIVE &&
		( !spec_keep_team_glows.GetBool() || pViewer->GetObserverMode() == OBS_MODE_DEATHCAM ) )
	{
		if ( !pRawLocal || pRawLocal->GetTeamNumber() != TEAM_INFECTED || pRawLocal->m_zombieClass != 2 || !TerrorGlow_IsIT( *this ) )
		{
			state.m_flAlpha = 0.0f;
			state.m_bAbilityHighlight = false;
			return;
		}
	}
	if ( IsDormant() )
	{
		state.m_flAlpha = 0.0f;
		state.m_bAbilityHighlight = false;
		return;
	}
	C_BaseEntity *pGlowTarget = m_lifeState != LIFE_ALIVE && GetTeamNumber() == TEAM_SURVIVOR ?
		TerrorGlow_FindRescueModel( this ) : NULL;
	if ( !pGlowTarget )
		pGlowTarget = this;
	if ( m_lifeState != LIFE_ALIVE && pGlowTarget->IsPlayer() )
	{
		state.m_flAlpha = 0.0f;
		state.m_bAbilityHighlight = false;
		return;
	}
	if ( pRawLocal && pRawLocal->GetTeamNumber() == TEAM_INFECTED && TerrorGlow_IsIT( *this ) )
	{
		state.m_flAlpha = 1.0f;
		state.m_bAbilityHighlight = false;
		return;
	}
	Vector direction = pGlowTarget->EyePosition() - viewOrigin;
	VectorNormalize( direction );
	if ( DotProduct( viewForward, direction ) < 0.44999999f )
	{
		state.m_flAlpha = 0.0f;
		state.m_bAbilityHighlight = false;
		if ( !state.m_NotVisibleTimer.HasStarted() )
			state.m_NotVisibleTimer.Start();
		return;
	}
	if ( m_lifeState != LIFE_ALIVE && !pGlowTarget->IsPlayer() )
	{
		state.m_flAlpha = 1.0f;
		state.m_bAbilityHighlight = false;
		return;
	}
	if ( pViewer->GetTeamNumber() == TEAM_INFECTED )
	{
		if ( TerrorGlow_IsSurvivorTeam( GetTeamNumber() ) )
		{
			state.m_flAlpha = GetGlowNoiseAlpha() * 0.0039215689f;
			state.m_bAbilityHighlight = false;
			return;
		}
		if ( GetTeamNumber() != TEAM_INFECTED )
			return;
		if ( IsGhost() || ( m_zombieClass == 8 && IsEffectActive( EF_NODRAW ) ) )
		{
			state.m_flAlpha = 1.0f;
			state.m_bAbilityHighlight = false;
			return;
		}
	}
	state.m_flAlpha = 0.0f;
	state.m_bAbilityHighlight = false;
	if ( pViewer->GetTeamNumber() == TEAM_SURVIVOR && GetTeamNumber() == TEAM_INFECTED )
	{
		if ( ( m_zombieClass == 3 && m_pounceVictim.Get() && m_pounceVictim.Get() != pViewer ) ||
			( m_zombieClass == 6 && m_pummelVictim.Get() && m_pummelVictim.Get() != pViewer ) ||
			( m_zombieClass == 5 && m_jockeyVictim.Get() && m_jockeyVictim.Get() != pViewer ) )
		{
			state.m_flAlpha = 1.0f;
			state.m_bAbilityHighlight = true;
		}
		return;
	}
	if ( pViewer->IsIncapacitated() )
	{
		state.m_flAlpha = 1.0f;
		return;
	}
	if ( TerrorGlow_IsAbleToSee( pViewer, this ) )
	{
		if ( !state.m_VisibleTimer.HasStarted() )
			state.m_VisibleTimer.m_timestamp = gpGlobals->curtime -
				cl_glow_los_fade_out_time.GetFloat() * clamp( 1.0f - flOldAlpha, 0.0f, 1.0f );
		state.m_NotVisibleTimer.Invalidate();
		const float flElapsed = state.m_VisibleTimer.GetElapsedTime();
		if ( cl_glow_los_fade_out_time.GetFloat() > flElapsed )
			state.m_flAlpha = 1.0f - clamp( flElapsed / cl_glow_los_fade_out_time.GetFloat(), 0.0f, 1.0f );
	}
	else
	{
		if ( !state.m_NotVisibleTimer.HasStarted() )
			state.m_NotVisibleTimer.Start();
		state.m_flAlpha = 1.0f;
		const float flElapsed = state.m_NotVisibleTimer.GetElapsedTime();
		const float flDelay = cl_glow_los_delay.GetFloat();
		float flFade = 1.0f;
		if ( flElapsed < flDelay )
			flFade = 0.0f;
		else if ( flElapsed < flDelay + cl_glow_los_fade_in_time.GetFloat() )
			flFade = ( flElapsed - flDelay ) / cl_glow_los_fade_in_time.GetFloat();
		state.m_flAlpha *= flFade;
		state.m_VisibleTimer.Invalidate();
	}
	if ( IsTongueVictim() || IsBeingPounced() || IsIncapacitated() || IsPummelVictim() || m_jockeyAttacker.Get() )
		state.m_flAlpha = 1.0f;
	const float flDefaultFOV = (float)pViewer->GetDefaultFOV();
	const float flCurrentFOV = pViewer->GetFOV();
	const double flFOVRatio = flCurrentFOV == flDefaultFOV || flDefaultFOV < 0.001f ? 1.0 : (double)flCurrentFOV / (double)flDefaultFOV;
	const Vector delta = pViewer->EyePosition() - EyePosition();
	const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
	state.m_flAlpha = TerrorGlow_GetDistanceAlpha( distance, flFOVRatio, state.m_flAlpha );
}

// C:10257830; Tank and every playable special inherit this exact body.
void C_TerrorPlayer::GetGlowColor( float *r, float *g, float *b, float *a )
{
	const int slot = 0; // Native active-view lookup, target's sole real local slot.
	const TerrorGlowPlayerState_t &state = m_GlowView[slot];
	*r = *g = *b = 0.0f;
	*a = state.m_flAlpha;
	C_TerrorPlayer *pViewer = ToTerrorPlayer( TerrorGlow_ResolveTeamColorPlayer( C_BasePlayer::GetLocalPlayer( slot ),
		TerrorGameRules()->IsPlayerControlledZombies() ) );
	if ( !pViewer || pViewer == this || ( TerrorGlow_IsSurvivorTeam( pViewer->GetTeamNumber() ) &&
		( !IsSurvivorGlowEnabled() || sv_disable_glow_survivors.GetBool() ) ) )
	{
		*a = 0.0f;
		return;
	}
	Vector color( 0.0f, 0.0f, 0.0f );
	if ( state.m_bAbilityHighlight )
	{
		if ( cl_colorblind.GetInt() == 2 )
			color.Init( cl_glow_ability_colorblind_r.GetFloat(), cl_glow_ability_colorblind_g.GetFloat(), cl_glow_ability_colorblind_b.GetFloat() );
		else
			color.Init( cl_glow_ability_r.GetFloat(), cl_glow_ability_g.GetFloat(), cl_glow_ability_b.GetFloat() );
	}
	else if ( pViewer->GetTeamNumber() == TEAM_SURVIVOR )
	{
		if ( TerrorGlow_IsIT( *this ) )
		{
			if ( TerrorGlow_IsSurvivorTeam( GetTeamNumber() ) )
				color.Init( cl_glow_survivor_vomit_r.GetFloat(), cl_glow_survivor_vomit_g.GetFloat(), cl_glow_survivor_vomit_b.GetFloat() );
			else
				color.Init( cl_glow_infected_vomit_r.GetFloat(), cl_glow_infected_vomit_g.GetFloat(), cl_glow_infected_vomit_b.GetFloat() );
			*a = 1.0f;
		}
		else if ( TerrorGlow_IsSurvivorTeam( GetTeamNumber() ) )
		{
			if ( IsIncapacitated() || IsBeingPounced() || IsPummelVictim() || IsTongueVictim() ||
				m_jockeyAttacker.Get() || m_isHangingFromLedge || m_lifeState != LIFE_ALIVE )
			{
				color.Init( cl_glow_survivor_hurt_r.GetFloat(), cl_glow_survivor_hurt_g.GetFloat(), cl_glow_survivor_hurt_b.GetFloat() );
				if ( m_lifeState == LIFE_ALIVE )
					*a = 1.0f;
			}
			else
				color.Init( cl_glow_survivor_r.GetFloat(), cl_glow_survivor_g.GetFloat(), cl_glow_survivor_b.GetFloat() );
		}
	}
	else if ( TerrorGlow_IsIT( *this ) )
	{
		color.Init( cl_glow_infected_vomit_r.GetFloat(), cl_glow_infected_vomit_g.GetFloat(), cl_glow_infected_vomit_b.GetFloat() );
		*a = 1.0f;
	}
	else if ( TerrorGlow_IsSurvivorTeam( GetTeamNumber() ) )
	{
		TerrorGlow_GetSurvivorHealthColor( *this, color );
		*r = color.x;
		*g = color.y;
		*b = color.z;
		return;
	}
	else if ( !IsGhost() && !( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 && IsEffectActive( EF_NODRAW ) ) )
		color.Init( cl_glow_infected_r.GetFloat(), cl_glow_infected_g.GetFloat(), cl_glow_infected_b.GetFloat() );
	else
		color.Init( cl_glow_ghost_infected_r.GetFloat(), cl_glow_ghost_infected_g.GetFloat(), cl_glow_ghost_infected_b.GetFloat() );
	color *= cl_glow_brightness.GetFloat();
	*r = color.x;
	*g = color.y;
	*b = color.z;
}

// C2525C0, primary217. Keep this genuine virtual separate from C258570
// DrawModel and from the addon/weapon shared C2FFF0 parent policy.
bool C_TerrorPlayer::GetStencilState( ShaderStencilState_t &state )
{
	float r, g, b, alpha;
	GetGlowColor( &r, &g, &b, &alpha );
	if ( !( alpha > 0.0f ) ) // C252604 JBE also rejects unordered.
		return false;
	int reference = 8;
	if ( IsGlowHighlightedByAbility( 0 ) || !TerrorGlow_IsSurvivorTeam( GetTeamNumber() ) )
		reference = TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer() ) ? 8 : 2;
	state.m_bEnable = true;
	state.m_nReferenceValue = reference;
	state.m_nWriteMask = reference;
	state.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
	state.m_PassOp = STENCILOPERATION_REPLACE;
	state.m_FailOp = STENCILOPERATION_KEEP;
	state.m_ZFailOp = STENCILOPERATION_REPLACE;
	return true;
}

void TerrorGlow_GetAbilityColor( Vector &color )
{
	if ( cl_colorblind.GetInt() == 2 )
		color.Init( cl_glow_ability_colorblind_r.GetFloat(), cl_glow_ability_colorblind_g.GetFloat(), cl_glow_ability_colorblind_b.GetFloat() );
	else
		color.Init( cl_glow_ability_r.GetFloat(), cl_glow_ability_g.GetFloat(), cl_glow_ability_b.GetFloat() );
}

const Vector &C_TerrorPlayer::GetRenderOrigin()
{
	if ( m_lifeState != LIFE_ALIVE && GetTeamNumber() == TEAM_SURVIVOR )
	{
		C_BaseEntity *rescue = TerrorGlow_FindRescueModel( this );
		if ( rescue ) return rescue->GetRenderOrigin();
	}
	return BaseClass::GetRenderOrigin();
}

int C_TerrorPlayer::DrawModel( int flags )
{
	const int slot = 0; // The real target view producer; no active-slot ABI.
	const bool ordinaryDraw = !TerrorGlow_IsModelPass() && ( flags & STUDIO_RENDER ) && !( flags & 0x40000000 );
	if ( ordinaryDraw ) MarkGlowDrawFrame( slot );
	const bool stencil = ordinaryDraw && !( flags & 0x800 ) && !TerrorModel_SkipEntityStencil() && TerrorGlow_ApplyPlayerStencil( this );
	const int result = BaseClass::DrawModel( flags );
	if ( stencil ) TerrorGlow_EndStencilMask();
	return result;
}

extern "C" double L4D2_GlowNormalize( Vector *direction );

extern ConVar player_use_radius;
static ConVar survivor_autolook_voc_delay( "survivor_autolook_voc_delay", "1", 0x00004008,
	"After staring an object for this many seconds, a survivor will say a smartlook concept for it if available." );
static ConVar cl_use_update_interval( "cl_use_update_interval", ".2", 0x00004008,
	"Time between use target updates" );

// C:253260. Geometry and registration order, not nearest entity/LOS, decide
// the Tank's optional use/glow candidate.
C_BaseEntity *C_TerrorPlayer::FindTankGlowEntity()
{
	Vector start, forward;
	EyePositionAndVectors( &start, &forward, NULL, NULL );
	C_BaseEntity *best = NULL;
	float bestDot = 0.89999998f;
	CEntitySphereQuery sphere( start, 500.0f );
	for ( C_BaseEntity *entity = sphere.GetCurrentEntity(); entity; sphere.NextEntity(), entity = sphere.GetCurrentEntity() )
	{
		if ( !IsUseableEntity( entity, 0x1000 ) )
			continue;
		Vector mins, maxs, closest;
		entity->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		CBaseTrace trace;
		if ( IntersectRayWithBox( start, forward * 500.0f, mins, maxs, 1.0f, &trace ) )
			closest = trace.endpos;
		else
		{
			// C:363170: nearest point to the entity center on the half ray.
			const Vector delta = forward * 250.0f;
			const float denominator = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
			float t = 0.0f;
			if ( denominator >= 0.0000099999997f )
			{
				const Vector &center = entity->WorldSpaceCenter();
				t = ( ( center.y * delta.y + center.x * delta.x ) + center.z * delta.z -
					( ( start.y * delta.y + start.x * delta.x ) + start.z * delta.z ) ) / denominator;
				if ( t > 1.0f ) t = 1.0f;
				else if ( t < 0.0f ) t = 0.0f;
			}
			closest = start + delta * t;
		}
		Vector nearest;
		entity->CollisionProp()->CalcNearestPoint( closest, &nearest );
		Vector direction = nearest - start;
		L4D2_GlowNormalize( &direction );
		const float dot = ( direction.x * forward.x + direction.y * forward.y ) + direction.z * forward.z;
		if ( dot > bestDot )
		{
			best = entity;
			bestDot = dot;
		}
	}
	return best;
}

// C:25A150. All writes are on the real local player's native selection 
void C_TerrorPlayer::UpdateGlowSelection()
{
	const int oldUseEntity = m_nUseEntity;
	m_nUseEntity = 0;
	m_nGlowEntity = 0;
	m_hUseGlowTarget = NULL;
	if ( !IsLocalPlayer() || m_lifeState != LIFE_ALIVE )
		return;
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
		if ( weapon && weapon->GetWeaponID() == 16 )
		{
			C_BaseEntity *target = m_useActionTarget.Get();
			if ( !target )
			{
				CCarriedProp *carried = dynamic_cast< CCarriedProp * >( weapon );
				if ( carried ) target = carried->FindActionTarget( this, 0 );
			}
			CPointPropUseTarget *useTarget = dynamic_cast< CPointPropUseTarget * >( target );
			if ( useTarget ) m_hUseGlowTarget = useTarget->GetGasNozzle();
		}
	}
	bool inRange = false;
	C_BaseEntity *use = FindUseEntity( 500.0f, player_use_radius.GetFloat(), 0.0f, &inRange );
	if ( use )
	{
		// C:25A150 queries native v348, not the radial-use priority at v236.
		const int useType = use->GetUseType( this );
		if ( useType >= 0 && useType <= 1 ) inRange = false;
		else if ( useType == 4 ) return;
	}
	if ( inRange && use ) m_nUseEntity = use->entindex();
	if ( m_nUseEntity != oldUseEntity )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "use_target" );
		if ( event )
		{
			const char *name = m_nUseEntity ? use->GetClassname() : "";
			const char *space = strchr( name, ' ' );
			event->SetString( "classname", space ? space + 1 : name );
			event->SetInt( "targetid", m_nUseEntity );
			if ( m_nUseEntity ) event->SetBool( "isprop", ( use->ObjectCaps() & 0x2000 ) != 0 );
			gameeventmanager->FireEventClientSide( event );
		}
	}
	C_BaseEntity *candidate = use && !use->IsPlayer() ? use : NULL;
	if ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR )
		candidate = GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 ? FindTankGlowEntity() : NULL;
	if ( candidate )
	{
		candidate = candidate->GetUseRedirectEntity();
		if ( candidate && !candidate->IsGlowUseTarget() )
		{
			candidate = NULL;
		}
	}
	FOR_EACH_VEC_BACK( m_Candidates, i )
		if ( !m_Candidates[i].Get() || m_Candidates[i].Get() != candidate ) m_Candidates.FastRemove( i );
	if ( candidate )
	{
		m_nGlowEntity = candidate->entindex();
		const EHANDLE handle( candidate );
		if ( m_Candidates.Find( handle ) == m_Candidates.InvalidIndex() ) m_Candidates.AddToTail( handle );
	}
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		if ( candidate && use && m_hSmartLookTarget.Get() == use )
		{
			const Vector eye = GetAbsOrigin() + GetViewOffset();
			const Vector delta = use->GetAbsOrigin() - eye;
			const float range = player_use_radius.GetFloat() * 2.0f;
			const float distanceSqr = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
			if ( use != m_hLastSmartLookTarget.Get() && range * range >= distanceSqr &&
				gpGlobals->curtime > m_flSmartLookStart + survivor_autolook_voc_delay.GetFloat() )
			{
				engine->ClientCmd_Unrestricted( "vocalize smartlook auto" );
				m_hLastSmartLookTarget = use;
			}
			m_hSmartLookTarget = use;
		}
		else
		{
			m_hSmartLookTarget = use;
			m_flSmartLookStart = gpGlobals->curtime;
		}
	}
}

void C_TerrorPlayer::ClientThink()
{
	BaseClass::ClientThink();
	if ( !m_bWasAliveLastClientThink && m_lifeState == LIFE_ALIVE )
		ClearRagdoll();
	m_bWasAliveLastClientThink = m_lifeState == LIFE_ALIVE;
	// Native268: eight Terror-family primaries inherit genuine698A0 RET0; the
	// concrete C_SurvivorBot inherits actual2D5870 RET1. Existing class dispatch
	// stays canonical, not the old M7 substitute player-resource fake-client bit.
	if ( !IsBot() && gpGlobals->curtime > m_flNextGlowSelectionUpdate )
	{
		UpdateGlowSelection();
		m_flNextGlowSelectionUpdate = cl_use_update_interval.GetFloat() + gpGlobals->curtime;
	}
	// Exactly one real user0, no fake slots/TLS. Existing M7 owner stays sole.
	UpdateGlow( 0, MainViewOrigin(), MainViewForward() );
	if ( m_lifeState == LIFE_ALIVE && !( IsPlayer() && IsLocalPlayer() ) )
	{
		C_BaseCombatWeapon *weapon = GetActiveWeapon();
		if ( weapon )
		{
			CTerrorGun *gun = weapon->GetTerrorGun();
			if ( gun )
				gun->UpdateSpread(); // Real original30CAD0 owner, not muzzle/idle thunk.
		}
	}
	if ( m_pounceAttacker.Get() )
	{
		const float now = gpGlobals->curtime;
		if ( static_cast< float >( m_knockdownTimer.m_timestamp ) != now )
			m_knockdownTimer.m_timestamp.SetDirect( now );
	}
	if ( ( !m_terrorFireInterval.HasStarted() || m_terrorFireInterval.GetElapsedTime() > 5.0f ) &&
		static_cast< float >( m_continuousFireInterval.m_timestamp ) != -1.0f )
		m_continuousFireInterval.Invalidate();
}

// Port name for native primary virtual362 (+0x5A8), C:10254750.
// This controls the attached speech_voice particle, NOT the entity outline.
bool C_TerrorPlayer::ShouldDrawSpeechVoice()
{
	if ( sv_disable_glow_survivors.GetInt() != 0 )
		return false;
	return GetClientVoiceMgr()->IsPlayerSpeaking( entindex() ) &&
		!IsLocalPlayer() && GetTeamNumber() != TEAM_INFECTED;
}

void C_TerrorPlayer::StopSpeechVoiceParticle()
{
	if ( !m_hSpeechVoice )
		return;
	m_hSpeechVoice->StopEmission( false, false, false );
	// Canonical particle owner uses native all-users selection.
	ParticleProp()->StopParticlesNamed( "speech_voice", true, -1 );
	m_hSpeechVoice = NULL;
}

// C:1025F4D0. Ownership is acquired by OnNewParticleEffect, not by retaining
// DispatchParticleEffect's temporary creation result or a raw effect pointer.
void C_TerrorPlayer::SetSpeechVoiceParticleEnabled( bool enabled )
{
	if ( enabled )
	{
		if ( !m_hSpeechVoice )
		{
			MDLCACHE_CRITICAL_SECTION();
			DispatchParticleEffect( "speech_voice", PATTACH_POINT_FOLLOW, this, "forward", false );
		}
	}
	else
		StopSpeechVoiceParticle();
}

// Native C:10261360 ends in shared suffix1025FE00. Query/create precedes the
// life/team/dormancy cleanup; do not move those tests ahead of creation.
void C_TerrorPlayer::UpdateSpeechVoicePresentation()
{
	SetSpeechVoiceParticleEnabled( ShouldDrawSpeechVoice() );
	if ( m_lifeState != LIFE_ALIVE || GetTeamNumber() == TEAM_SPECTATOR || IsDormant() )
		StopSpeechVoiceParticle();
}

// BLOCK C. Two actual native constructor records, not extra shadow ConVars.
static void TerrorClientFOVChanged( IConVar *variable, const char *oldString, float oldValue )
{
	// Whole251610; actual IConVar->ConVar adjustment is the supported typed cast.
	DevMsg( "debugfov: Changing cl_fov from %f to %f\n", oldValue,
		static_cast< ConVar * >( variable )->GetFloat() );
}
static ConVar cl_fov( "cl_fov", "90", 0x0000000A,
	"Client-side fov control that is global for all splitscreen players on this machine.  This gets overriden via splitscreen_config.txt for splitscreen.",
	TerrorClientFOVChanged ); // Native1050F750 input2, real registration/callback retained.
ConVar fov_desired( "fov_desired", "90", 0x00000288,
	"Sets the base field-of-view for non-splitscreen.", true, 75.0f, true, 120.0f ); // Native10512720 input640, NO extraDEV flag.

int C_TerrorPlayer::GetDefaultFOV() // Whole2527E0 under Main-approved valid-userCOUNT=1 projection.
{
	// SPLIT-SCREEN ADAPTATION: native101B2AD0 is count106DEB30>=2, whose sole
	// writer101B3710 enumerates unsupported ENGINE532/536. Main authorized
	// native SINGLE-user branch here. No invented count/IsSplitScreenActive,
	// SDK m_iDefaultFOV fallback, dynamic local-entity-count surrogate or
	// dual-user support claim. Real cl_fov declaration/callback remains above.
	const float desired = fov_desired.GetFloat();
	if ( desired > 120.0f ) return 120;
	if ( desired < 75.0f ) return 75;
	return _mm_cvttss_si32( _mm_set_ss( desired ) ); // Native NaN ->INT_MIN, no UB int cast.
}

void C_TerrorPlayer::ProcessMuzzleFlashEvent()
{
	C_BaseCombatWeapon *weapon = GetActiveWeapon();
	if ( !weapon )
		return;
	CTerrorGun *gun = weapon->GetTerrorGun();
	if ( !gun )
		return;
	if ( !( IsPlayer() && IsLocalPlayer() ) )
		gun->IncreaseSpread();
	gun->NotifyTerrorMuzzleFlash();
	BaseClass::ProcessMuzzleFlashEvent();
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Native269C10: repeated actual ghost query and the original flag ordering.
bool C_TerrorPlayer::ShouldPlayTerrorFootstepSound() const
{
	return !IsGhost() && !( GetFlags() & FL_FROZEN ) &&
		( !( GetFlags() & FL_ATCONTROLS ) || ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 6 ) ) && !IsGhost();
}

// Native2693B0 ignores speed and force, unlike the CS base.
bool C_TerrorPlayer::IsTerrorFootstepSurfaceValid( float speed, bool force ) const
{
	return !IsGhost() && !( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 );
}

// Native2692E0. One native static64 buffer; Charger consumes the RIGHT-foot bit.
const char *C_TerrorPlayer::GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force )
{
	if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 ) )
		return "";
	static char name[64];
	if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 ) )
		Q_snprintf( name, sizeof( name ), "Tank.%s", sound );
	else if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 2 ) )
		Q_snprintf( name, sizeof( name ), "Boomer.%s", sound );
	else if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 5 ) )
		Q_snprintf( name, sizeof( name ), "Jockey.%s", sound );
	else if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 6 ) )
		Q_snprintf( name, sizeof( name ), "Charger.Default.%s", right ? "RunRight" : "RunLeft" );
	else if ( GetTeamNumber() == TEAM_INFECTED )
		Q_snprintf( name, sizeof( name ), "Infected.%s", sound );
	else
		Q_snprintf( name, sizeof( name ), "Survivor.%s", sound );
	return name;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static ConVar cl_viewbob_cyclerate_run( "cl_viewbob_cyclerate_run", "0.4", 0x0000000A, "Speed of view bob while running" );
static ConVar cl_viewbob_cyclerate_walk( "cl_viewbob_cyclerate_walk", "0.8", 0x0000000A, "Speed of view bob while walking" );
static ConVar cl_viewbob_cyclerate_duck( "cl_viewbob_cyclerate_duck", "0.6", 0x0000000A, "Speed of view bob while ducked" );
static ConVar cl_viewbob_up( "cl_viewbob_up", "0.5", 0x0000000A, "Percent of view bob that is above the eye pos" );
static ConVar cl_viewbob_scale( "cl_viewbob_scale", "0.01", 0x0000000A, "Size of view bob" );
static ConVar cl_viewbob_scale_duck( "cl_viewbob_scale_duck", "0.02", 0x0000000A, "Size of view bob while ducked" );
static ConVar cl_viewbob( "cl_viewbob", "1", 0x0000000A, "enables view bob" );

// Native253B42..253B81 retains the unspilled x87 amplitude across FSIN.
extern "C" float L4D2_ComputePlayerViewBob( double angle, float velocityX,
	float velocityY, float scale, float *amplitude );
extern "C" double L4D2_GlowNormalize( Vector *direction );

// Whole C253990. The observer owns only time/frame/velocity/ground/mounted
// inputs; amount and last-curtime deliberately remain on THIS actor.
void C_TerrorPlayer::AddViewBob( Vector &eyeOrigin, float &fov )
{
	if ( !cl_viewbob.GetBool() )
		return;
	C_TerrorPlayer *viewPlayer = this;
	if ( GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = GetObserverTarget();
		if ( target && target->IsPlayer() )
			viewPlayer = static_cast< C_TerrorPlayer * >( target );
	}
	if ( viewPlayer->m_nViewBobFrame != gpGlobals->framecount )
	{
		viewPlayer->m_flViewBobTime = gpGlobals->frametime + viewPlayer->m_flViewBobTime;
		viewPlayer->m_nViewBobFrame = gpGlobals->framecount;
	}
	if ( viewPlayer->GetGroundEntity() && gpGlobals->curtime != m_flLastViewBobTime &&
		!( gpGlobals->frametime <= 0.0f ) && !viewPlayer->m_usingMountedWeapon )
	{
		m_flLastViewBobTime = gpGlobals->curtime;
		Vector velocity;
		viewPlayer->EstimateAbsVelocity( velocity );
		velocity.z = 0.0f;
		float scale = cl_viewbob_scale.GetFloat();
		float cycle;
		if ( IsDucked() )
		{
			scale = cl_viewbob_scale_duck.GetFloat();
			cycle = cl_viewbob_cyclerate_duck.GetFloat();
		}
		else if ( m_nButtons & IN_SPEED )
			cycle = cl_viewbob_cyclerate_walk.GetFloat();
		else
			cycle = cl_viewbob_cyclerate_run.GetFloat();
		const float reciprocal = 1.0f / cycle;
		const float cycleCount = reciprocal * viewPlayer->m_flViewBobTime;
		const int wholeCycle = _mm_cvttss_si32( _mm_set_ss( cycleCount ) );
		const float wrappedCycle = static_cast< float >( wholeCycle ) * cycle;
		const float phase = ( viewPlayer->m_flViewBobTime - wrappedCycle ) * reciprocal;
		const float up = cl_viewbob_up.GetFloat();
		const double pi = 3.141592653589793;
		double angle;
		if ( up > phase )
			angle = static_cast< double >( phase ) * pi / static_cast< double >( up );
		else
		{
			const float numeratorUp = cl_viewbob_up.GetFloat();
			const float denominatorUp = cl_viewbob_up.GetFloat();
			angle = ( static_cast< double >( phase ) - static_cast< double >( numeratorUp ) ) * pi /
				( 1.0 - static_cast< double >( denominatorUp ) ) + pi;
		}
		m_flViewBobAmount = L4D2_ComputePlayerViewBob( angle, velocity.x, velocity.y, scale, &m_flViewBobAmount );
		if ( !( 4.0f > m_flViewBobAmount ) )
			m_flViewBobAmount = 4.0f;
		if ( !( m_flViewBobAmount > -7.0f ) )
			m_flViewBobAmount = -7.0f;
	}
	eyeOrigin.z = m_flViewBobAmount + eyeOrigin.z;
}

// Whole C256640. The qualified base call consumes ALL five original args;
// bob and the Terror-introduced camera virtual run only after that owner.
void C_TerrorPlayer::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
	if ( m_lifeState != LIFE_ALIVE && GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = GetObserverTarget();
		if ( target && target->IsPlayer() )
		{
			C_TerrorPlayer *viewPlayer = static_cast< C_TerrorPlayer * >( target );
			const Vector offset = viewPlayer->ComputeMovementViewOffset( viewPlayer->GetAbsOrigin(), eyeAngles );
			C_BaseEntity::SetViewOffset( offset );
		}
	}
	else
	{
		Vector offset = ComputeMovementViewOffset( GetAbsOrigin(), eyeAngles );
		offset.z = GetViewOffset().z;
		C_BaseEntity::SetViewOffset( offset );
	}
	C_BasePlayer::CalcView( eyeOrigin, eyeAngles, zNear, zFar, fov );
	if ( m_lifeState == LIFE_ALIVE || GetObserverMode() == OBS_MODE_IN_EYE )
		AddViewBob( eyeOrigin, fov );
	if ( m_lifeState == LIFE_ALIVE )
	{
		m_hLastObserverCameraTarget = NULL;
		if ( !input->CAM_IsThirdPerson() )
			CalcThirdPersonView( eyeOrigin, eyeAngles );
	}
	Vector forward;
	AngleVectors( eyeAngles, &forward );
	L4D2_GlowNormalize( &forward );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

extern ConVar survivor_incapacitated_roll;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Whole C255AA0: actual native primary1312, selected by observer mode4.
// Keep the existing SDK virtual's spelling rather than relabeling its slot.
void C_TerrorPlayer::CalcDeathCamView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov )
{
	C_BasePlayer::CalcDeathCamView( eyeOrigin, eyeAngles, fov );
	C_BaseEntity *target = GetObserverTarget();
	if ( !target || !target->IsPlayer() )
		return;
	C_TerrorPlayer *viewPlayer = static_cast< C_TerrorPlayer * >( target );
	if ( viewPlayer->m_lifeState == LIFE_ALIVE && viewPlayer->m_isIncapacitated )
	{
		engine->GetViewAngles( eyeAngles );
		eyeAngles.z = survivor_incapacitated_roll.GetFloat();
	}
	else
	{
		engine->GetViewAngles( eyeAngles );
		eyeAngles.z = 0.0f;
	}
	engine->SetViewAngles( eyeAngles );
	if ( viewPlayer->m_lifeState != LIFE_ALIVE )
		return;
	if ( m_hLastObserverCameraTarget.Get() != viewPlayer )
	{
		m_hLastObserverCameraTarget = viewPlayer;
		m_flObserverEyeHeight = C_BaseEntity::GetViewOffset().z;
	}
	const float previous = m_flObserverEyeHeight;
	const float desired = C_BaseEntity::GetViewOffset().z;
	if ( desired > previous )
	{
		float height = gpGlobals->frametime * 96.0f + previous;
		m_flObserverEyeHeight = height;
		if ( height > desired ) height = desired;
		m_flObserverEyeHeight = height;
	}
	else if ( previous > desired )
	{
		float height = previous - gpGlobals->frametime * 48.0f;
		m_flObserverEyeHeight = height;
		// Native's preceding ordered height>height branch is never taken;
		// preserve the following ordered clamp, including unordered omission.
		if ( desired > height ) height = desired;
		m_flObserverEyeHeight = height;
	}
	if ( !viewPlayer->m_usingMountedWeapon )
	{
		const bool pounced = viewPlayer->IsBeingPounced();
		const float height = m_flObserverEyeHeight;
		const Vector &origin = viewPlayer->GetAbsOrigin();
		const Vector offset = TraceMovementViewOffset( origin, eyeAngles, height, pounced );
		const Vector &freshOrigin = viewPlayer->GetAbsOrigin();
		eyeOrigin.x = freshOrigin.x + offset.x;
		eyeOrigin.y = freshOrigin.y + offset.y;
		eyeOrigin.z = freshOrigin.z + offset.z;
	}
	// Whole10063860's other-registered-local-player query under Main's one
	// actual user0 projection, not an invented second local entity or count.
	if ( viewPlayer != this && C_BasePlayer::GetLocalPlayer( 0 ) == viewPlayer )
		eyeOrigin = viewPlayer->EyePosition();
	viewPlayer->CalcThirdPersonView( eyeOrigin, eyeAngles );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Place once in sole c_terrorplayer.cpp before yardline helpers.
// Exact contracts/client_cvars_commands.json metadata, no callbacks/bounds/help.
// RAW0 native client controls normalize to DEVELOPMENTONLY, not CHEAT/ARCHIVE.
static ConVar z_scrimmage_height( "z_scrimmage_height", "50", 0x0000000A );
static ConVar z_scrimmage_r( "z_scrimmage_r", "0", 0x0000000A );
static ConVar z_scrimmage_g( "z_scrimmage_g", "255", 0x0000000A );
static ConVar z_scrimmage_b( "z_scrimmage_b", "0", 0x0000000A );
static ConVar z_scrimmage_a_near( "z_scrimmage_a_near", "10", 0x0000000A );
static ConVar z_scrimmage_a_far( "z_scrimmage_a_far", "255", 0x0000000A );
static ConVar z_scrimmage_far( "z_scrimmage_far", "3000", 0x0000000A );
static ConVar z_scrimmage_yardline_height( "z_scrimmage_yardline_height", "6", 0x0000000A );
static ConVar z_scrimmage_yardline_width( "z_scrimmage_yardline_width", "12", 0x0000000A );
static ConVar z_scrimmage_yardline_r( "z_scrimmage_yardline_r", "0", 0x0000000A );
static ConVar z_scrimmage_yardline_g( "z_scrimmage_yardline_g", "255", 0x0000000A );
static ConVar z_scrimmage_yardline_b( "z_scrimmage_yardline_b", "0", 0x0000000A );
static ConVar z_scrimmage_yardline_a_near( "z_scrimmage_yardline_a_near", "15", 0x0000000A );
static ConVar z_scrimmage_yardline_a_far( "z_scrimmage_yardline_a_far", "25", 0x0000000A );
static ConVar z_scrimmage_yardline_far( "z_scrimmage_yardline_far", "500", 0x0000000A );
static ConVar z_scrimmage_debug( "z_scrimmage_debug", "0", 0x0000000A );
static ConVar z_scrimmage_reset( "z_scrimmage_reset", "0", 0x0000000A );
// These paired client registrations are native RAW8192/effective8194, separate
// modules from server's existing same-name replicated controls, not a shadow
// local cache. If a parent shared owner already supplies them, keep that owner
// and use extern declarations here instead of duplicate client registrations.
ConVar z_scrimmage_creep_delay( "z_scrimmage_creep_delay", "30", 0x0000200A );
ConVar z_scrimmage_creep_rate( "z_scrimmage_creep_rate", "50", 0x0000200A );

// Include before yardline_geometry.inc in the sole actor CPP.
// Native103601C0/10360340/10365ED0; full retained evidence147F/5EF4.
// ONE shared helper appended to the existing glow_property_math_x64.asm,
// agreed with Move; no CRT SinCos or second assembly translation unit.
extern "C" void L4D2_SinCosFloat( float radians, float *sine, float *cosine );
extern "C" void L4D2_ScrimmageVectorAngles( const Vector *forward, QAngle *angles );

static void ScrimmageAngleVectors( const QAngle &angles, Vector *forward, Vector *right, Vector *up )
{
	float sy, cy, sp, cp, sr, cr;
	L4D2_SinCosFloat( angles.y * 0.017453292f, &sy, &cy );
	L4D2_SinCosFloat( angles.x * 0.017453292f, &sp, &cp );
	L4D2_SinCosFloat( angles.z * 0.017453292f, &sr, &cr );
	if ( forward )
		forward->Init( cp * cy, cp * sy, -sp );
	if ( right )
		right->Init( cr * sy - ( sr * sp ) * cy,
			( cr * -1.0f ) * cy - ( sr * sp ) * sy, ( sr * -1.0f ) * cp );
	if ( up )
		up->Init( ( cr * sp ) * cy + sr * sy,
			( cr * sp ) * sy - sr * cy, cr * cp );
}

static void SetupScrimmagePanelMatrix( VMatrix &matrix, const Vector &origin, const QAngle &angles )
{
	float sy, cy, sp, cp, sr, cr;
	L4D2_SinCosFloat( angles.y * 0.017453292f, &sy, &cy );
	L4D2_SinCosFloat( angles.x * 0.017453292f, &sp, &cp );
	L4D2_SinCosFloat( angles.z * 0.017453292f, &sr, &cr );
	matrix[0][0] = cp * cy;
	matrix[1][0] = cp * sy;
	matrix[2][0] = -sp;
	matrix[0][1] = ( sr * sp ) * cy - cr * sy;
	matrix[1][1] = ( sr * sp ) * sy + cr * cy;
	matrix[2][1] = sr * cp;
	matrix[1][2] = ( cr * sp ) * sy - sr * cy;
	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
	matrix[0][2] = ( cr * sp ) * cy + sr * sy;
	matrix[2][2] = cr * cp;
	matrix[0][3] = origin.x;
	matrix[1][3] = origin.y;
	matrix[3][0] = 0.0f;
	matrix[2][3] = origin.z;
	matrix[3][1] = 0.0f;
	matrix[3][2] = 0.0f;
	matrix[3][3] = 1.0f;
}

// Source-ready inclusion in the sole c_terrorplayer.cpp owner, before the actor methods.
// Provenance: C7D6 captures.actorSmallProviders.yard_polyline,
// captures.yardLineStepAndControls.step/step_asm, captures.yardTraceFilter.
// Required existing includes: cbase.h, c_terrorplayer.h, util_shared.h,
// engine/IEngineTrace.h. No second CPanelWrapper, trace service or control owner.

class CTraceFilterScrimmage : public CTraceFilterSimple
{
public:
	CTraceFilterScrimmage() : CTraceFilterSimple( NULL, COLLISION_GROUP_NONE, NULL ) {}

	virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( handle, contentsMask ) )
			return false;
		C_BaseEntity *entity = EntityFromEntityHandle( handle );
		if ( !entity )
			return false;
		const char *name = entity->GetClassname();
		return Q_stristr( name, "door" ) == NULL && Q_stristr( name, "break" ) == NULL;
	}
};

// 2658D0 constructs line rays explicitly. Do not replace the second ray's
// x-x/y-y with literal zero: infinities and NaNs retain their native behavior.
static void InitScrimmageRay( Ray_t &ray, const Vector &start, const Vector &end )
{
	ray.m_Delta.x = end.x - start.x;
	ray.m_Delta.y = end.y - start.y;
	ray.m_Delta.z = end.z - start.z;
	const float xy = ray.m_Delta.y * ray.m_Delta.y + ray.m_Delta.x * ray.m_Delta.x;
	ray.m_IsSwept = ( xy + ray.m_Delta.z * ray.m_Delta.z ) != 0.0f;
	ray.m_Extents.Init( 0.0f, 0.0f, 0.0f );
	ray.m_IsRay = true;
	ray.m_StartOffset.Init( 0.0f, 0.0f, 0.0f );
	ray.m_Start = start;
}

// Native2658D0 returns its result through the first Vector argument.
static void TraceScrimmageStep( Vector &result, const Vector &position, const Vector &direction, float step )
{
	Vector start( position.x, position.y, position.z + 18.0f );
	Vector end( direction.x * step + position.x,
		direction.y * step + position.y, direction.z * step + start.z );
	CTraceFilterScrimmage filter;
	trace_t trace;
	trace.endpos.Init( 0.0f, 0.0f, 0.0f );

	for ( ;; )
	{
		const float dx = trace.endpos.x - position.x;
		const float dy = trace.endpos.y - position.y;
		if ( !( dy * dy + dx * dx > 1.0f ) )
			break;

		Ray_t ray;
		InitScrimmageRay( ray, start, end );
		enginetrace->TraceRay( ray, 0x0202420B, &filter, &trace );
		if ( r_visualizetraces.GetBool() )
			DebugDrawLine( trace.startpos, trace.endpos, 255, 0, 0, true, -1.0f );
		// COMISS/JB takes the rejection arm on unordered plane.normal.z.
		if ( trace.fraction == 1.0f || !( trace.plane.normal.z >= 0.7f ) )
			break;

		const Vector raisedEnd( trace.endpos.x, trace.endpos.y, trace.endpos.z + 18.0f );
		InitScrimmageRay( ray, trace.endpos, raisedEnd );
		enginetrace->TraceRay( ray, 0x0202420B, &filter, &trace );
		if ( r_visualizetraces.GetBool() )
			DebugDrawLine( trace.startpos, trace.endpos, 255, 0, 0, true, -1.0f );
		// COMISS(0,fraction)/JNB does not reject unordered fraction.
		if ( trace.fraction <= 0.0f )
			break;
		start = trace.endpos;
		end.z = trace.endpos.z;
	}

	const float dx = trace.endpos.x - position.x;
	const float dy = trace.endpos.y - position.y;
	const float halfStep = step * 0.5f;
	// Native COMISS/JBE also rejects an unordered distance/threshold.
	if ( !( dx * dx + dy * dy > halfStep * halfStep ) )
	{
		result = vec3_origin;
		return;
	}
	end.Init( trace.endpos.x, trace.endpos.y, trace.endpos.z - step * 2.0f );
	// The SDK trace wrapper is the real100127C0 owner, including its one
	// r_visualizetraces debug draw. Do not add a second explicit debug line.
	UTIL_TraceLine( trace.endpos, end, 0x0202420B, &filter, &trace );
	result = trace.fraction == 1.0f ? vec3_origin : trace.endpos;
}

static float ScrimmageDistanceSquared( const Vector &point, const Vector &origin )
{
	const float x = point.x - origin.x;
	const float y = point.y - origin.y;
	const float z = point.z - origin.z;
	return ( y * y + x * x ) + z * z;
}

static bool IsScrimmageStepZero( const Vector &point )
{
	return point.x > -0.01f && point.x < 0.01f &&
		point.y > -0.01f && point.y < 0.01f &&
		point.z > -0.01f && point.z < 0.01f;
}

// Native266F70. RemoveAll/InsertBefore/AddToTail use actual CUtlVector
// capacity growth and element lifetimes; no per-frame temporary vector.
static void BuildScrimmagePolyline( const Vector &position, const QAngle &angles, CUtlVector< Vector > &polyline )
{
	if ( IsScrimmageStepZero( position ) )
		return;

	polyline.RemoveAll();
	polyline.InsertBefore( 0, position );
	Vector forward, right;
	ScrimmageAngleVectors( angles, &forward, &right, NULL );
	for ( int i = 1; i < 60; ++i )
	{
		const Vector previous = polyline.Tail();
		Vector next;
		TraceScrimmageStep( next, previous, right, 25.0f );
		if ( IsScrimmageStepZero( next ) )
			break;
		if ( polyline.Count() != 0 &&
			ScrimmageDistanceSquared( polyline.Tail(), position ) > ScrimmageDistanceSquared( next, position ) )
			break;
		polyline.InsertBefore( polyline.Count(), next );
	}
	for ( int i = -1; i > -60; --i )
	{
		const Vector first = polyline[0];
		const Vector reversed( right.x * -1.0f, right.y * -1.0f, right.z * -1.0f );
		Vector next;
		TraceScrimmageStep( next, first, reversed, 25.0f );
		if ( IsScrimmageStepZero( next ) )
			break;
		if ( polyline.Count() != 0 &&
			ScrimmageDistanceSquared( polyline[0], position ) > ScrimmageDistanceSquared( next, position ) )
			break;
		polyline.InsertBefore( 0, next );
	}
	if ( polyline.Count() < 2 )
	{
		const Vector ahead( position.x + right.x * 10.0f,
			position.y + right.y * 10.0f, position.z + right.z * 10.0f );
		polyline.InsertBefore( polyline.Count(), ahead );
		const Vector behind( position.x + right.x * -10.0f,
			position.y + right.y * -10.0f, position.z + right.z * -10.0f );
		polyline.InsertBefore( 0, behind );
	}
}

// Existing-owner bodies: append to the sole c_terrorplayer.cpp, not a new actor TU.
// Include c_terror_player_resource.h, c_vguiscreen.h and vgui_controls/Panel.h.
// Geometry and exact math helpers precede these bodies.
// Native252830/262280. g_PR is the sole C_PlayerResource-owned lifetime.
double C_TerrorPlayer::GetBecomeGhostAt() const
{
	if ( !g_PR )
		return 0.0;
	return static_cast< C_TerrorPlayerResource * >( g_PR )->GetBecomeGhostAt( entindex() );
}

// Native2673B0, the SECOND ACTOR LEAF of261360, following cigarette.
void C_TerrorPlayer::UpdateLineOfScrimmagePresentation()
{
	bool changed = !( m_lineOfScrimmagePos.x == m_previousLineOfScrimmagePos.x &&
		m_lineOfScrimmagePos.y == m_previousLineOfScrimmagePos.y &&
		m_lineOfScrimmagePos.z == m_previousLineOfScrimmagePos.z &&
		m_rearLineOfScrimmagePos.x == m_previousRearLineOfScrimmagePos.x &&
		m_rearLineOfScrimmagePos.y == m_previousRearLineOfScrimmagePos.y &&
		m_rearLineOfScrimmagePos.z == m_previousRearLineOfScrimmagePos.z );
	if ( !changed )
	{
		for ( int i = 0; i < 5; ++i )
		{
			if ( !( m_yardLinePos[i].x == m_previousYardLinePos[i].x &&
				m_yardLinePos[i].y == m_previousYardLinePos[i].y &&
				m_yardLinePos[i].z == m_previousYardLinePos[i].z ) )
			{
				changed = true;
				break;
			}
		}
	}
	if ( z_scrimmage_reset.GetBool() )
	{
		changed = true;
		Msg( "Rebuilding line of scrimmage at (%2.2f %2.2f %2.2f)\n",
			static_cast< double >( m_lineOfScrimmagePos.x ),
			static_cast< double >( m_lineOfScrimmagePos.y ),
			static_cast< double >( m_lineOfScrimmagePos.z ) );
	}
	// Native x87 FCOMIP(now,deadline)/JA rejects only ordered now>deadline;
	// an unordered clock/deadline therefore does NOT force cleanup.
	if ( IsPlayer() && IsLocalPlayer() && GetRenderTeamNumber() == TEAM_INFECTED &&
		m_lifeState != LIFE_ALIVE && m_scrimmageType == 1 &&
		!( static_cast< double >( gpGlobals->curtime ) > GetBecomeGhostAt() ) && !changed )
	{
		if ( m_lineOfScrimmagePolyline.Count() == 0 )
		{
			BuildScrimmagePolyline( m_lineOfScrimmagePos, m_lineOfScrimmageAngle, m_lineOfScrimmagePolyline );
			//1001A3D0 is Vector::operator!=, not Vector::IsZero(epsilon).
			if ( m_rearLineOfScrimmagePos != vec3_origin )
				BuildScrimmagePolyline( m_rearLineOfScrimmagePos, m_rearLineOfScrimmageAngle, m_rearLineOfScrimmagePolyline );
			for ( int i = 0; i < 5; ++i )
			{
				BuildScrimmagePolyline( m_yardLinePos[i], m_yardLineAngles[i], m_yardLinePolylines[i] );
				VGuiScreenInitData_t initData( this );
				m_yardLinePanels[i].Activate( "yard_line_panel", NULL, 0, &initData );
				vgui::Panel *panel = m_yardLinePanels[i].GetPanel();
				if ( panel )
				{
					int x, y;
					panel->GetBounds( x, y, m_yardLinePanelWidth[i], m_yardLinePanelHeight[i] );
				}
				else
				{
					m_yardLinePanelHeight[i] = 0;
					m_yardLinePanelWidth[i] = 0;
				}
			}
		}
		return;
	}

	m_lineOfScrimmagePolyline.RemoveAll();
	m_previousLineOfScrimmagePos.x = m_lineOfScrimmagePos.x;
	m_previousLineOfScrimmagePos.y = m_lineOfScrimmagePos.y;
	m_previousLineOfScrimmagePos.z = m_lineOfScrimmagePos.z;
	m_rearLineOfScrimmagePolyline.RemoveAll();
	m_previousRearLineOfScrimmagePos.x = m_rearLineOfScrimmagePos.x;
	m_previousRearLineOfScrimmagePos.y = m_rearLineOfScrimmagePos.y;
	m_previousRearLineOfScrimmagePos.z = m_rearLineOfScrimmagePos.z;
	for ( int i = 0; i < 5; ++i )
	{
		m_yardLinePolylines[i].RemoveAll();
		m_yardLinePanels[i].Deactivate();
		m_previousYardLinePos[i].x = m_yardLinePos[i].x;
		m_previousYardLinePos[i].y = m_yardLinePos[i].y;
		m_previousYardLinePos[i].z = m_yardLinePos[i].z;
	}
	CNewParticleEffect *effect = m_hMolotovGroundFire;
	if ( effect )
		effect->StopEmission( false, false, false );
	z_scrimmage_reset.SetValue( 0.0f );
}

// Native2657E0/265820: resolve each wrapper anew in ascending order.
int C_TerrorPlayer::GetYardLineNumber( vgui::Panel *panel )
{
	for ( int i = 0; i < 5; ++i )
	{
		if ( panel == m_yardLinePanels[i].GetPanel() )
			return i;
	}
	return -1;
}

int C_TerrorPlayer::GetYardLineAlpha( vgui::Panel *panel )
{
	for ( int i = 0; i < 5; ++i )
	{
		if ( panel == m_yardLinePanels[i].GetPanel() )
			return m_yardLineAlpha[i];
	}
	return 0;
}

// Native26A410 and26A480, client-owned direct queries, no server provider alias.
double C_TerrorPlayer::GetScrimmageSphereRadius() const
{
	float radius = m_scrimmageSphereInitialRadius;
	if ( !( m_scrimmageStartTime <= 0.0f ) )
	{
		const float elapsed = ( gpGlobals->curtime - m_scrimmageStartTime ) - z_scrimmage_creep_delay.GetFloat();
		if ( !( elapsed <= 0.0f ) )
		{
			radius = m_scrimmageSphereInitialRadius - z_scrimmage_creep_rate.GetFloat() * elapsed;
			if ( radius < 10.0f )
				return 10.0;
		}
	}
	return radius;
}

const Vector &C_TerrorPlayer::GetScrimmageSphereCenter() const
{
	return m_scrimmageSphereCenter;
}

// Native265DC0 and265D40. Sole c_terrorplayer.cpp bodies, NOT DrawModel.
// Required includes: xmmintrin.h, mathlib/vmatrix.h, VGuiMatSurface/IMatSystemSurface.h,
// vgui_controls/Panel.h, and tier2/renderutils.h; native game overloads below.
void RenderSphere( const Vector &center, float radius, int nTheta, int nPhi,
	Color color, IMaterial *material, bool insideOut );
void RenderScrimmageTriangleNative( const Vector &a, const Vector &b,
	const Vector &c, Color color, bool depthTest );

static int ScrimmageTruncate32( float value )
{
	return _mm_cvttss_si32( _mm_set_ss( value ) );
}

static float ScrimmageSquareRoot( float value )
{
	return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( value ) ) );
}

static float ScrimmagePresentationAlpha( float distance, ConVar &farDistance, ConVar &nearAlpha, ConVar &farAlpha )
{
	const float farRange = farDistance.GetFloat();
	if ( distance > farRange || farRange == 0.0f )
		return farAlpha.GetFloat();
	const float ratio = distance / farRange;
	const float farA = farAlpha.GetFloat();
	return ( 1.0f - ratio ) * nearAlpha.GetFloat() + farA * ratio;
}

// The native min loop updates on unordered candidate<=current as well.
static float ScrimmagePolylineDistance( C_TerrorPlayer *player, const CUtlVector< Vector > &polyline, float distance )
{
	for ( int i = 0; i < polyline.Count() - 1; ++i )
	{
		const Vector &point = polyline[i];
		const Vector &origin = player->GetAbsOrigin(); // Native repeats this virtual every iteration.
		const float x = origin.x - point.x;
		const float y = origin.y - point.y;
		const float z = origin.z - point.z;
		const float candidate = ScrimmageSquareRoot( ( x * x + y * y ) + z * z );
		if ( !( candidate > distance ) )
			distance = candidate;
	}
	return distance;
}

static void DrawScrimmageTriangle( const Vector &a, const Vector &b, const Vector &c, Color color )
{
	if ( z_scrimmage_debug.GetBool() )
	{
		color[3] = static_cast< unsigned char >( ScrimmageTruncate32( static_cast< float >( color.a() ) * 0.5f ) );
		RenderScrimmageTriangleNative( a, b, c, color, false );
		RenderScrimmageTriangleNative( a, b, c, color, true );
	}
	else
	{
		RenderScrimmageTriangleNative( a, b, c, color, true );
	}
}

void C_TerrorPlayer::DrawLineOfScrimmagePresentation()
{
	if ( !z_scrimmage_debug.GetBool() || m_lifeState == LIFE_ALIVE ||
		GetRenderTeamNumber() != TEAM_INFECTED || !( IsPlayer() && IsLocalPlayer() ) ||
		m_ghostSpawnState != 0 || State_Get() != static_cast< CSPlayerState >( 6 ) )
		return;
	const float red = z_scrimmage_r.GetFloat();
	const float green = z_scrimmage_g.GetFloat();
	const float blue = z_scrimmage_b.GetFloat();
	const float sphereAlpha = z_scrimmage_a_far.GetFloat();
	if ( m_scrimmageType == 2 )
	{
		const float radius = static_cast< float >( GetScrimmageSphereRadius() );
		const Vector center = GetScrimmageSphereCenter();
		IMaterial *material = materials->FindMaterial( "effects/spawn_sphere.vmt", NULL, false, NULL );
		const int r = ScrimmageTruncate32( red );
		const int g = ScrimmageTruncate32( green );
		const int b = ScrimmageTruncate32( blue );
		const int a = ScrimmageTruncate32( sphereAlpha );
		const Color color( r, g, b, a );
		RenderSphere( center, radius, 100, 100, color, material, false );
		RenderSphere( center, radius, 100, 100, color, material, true );
		return;
	}
	if ( m_lineOfScrimmagePolyline.Count() <= 1 )
		return;

	const Vector &frontOrigin = GetAbsOrigin();
	const float frontX = frontOrigin.x - m_lineOfScrimmagePos.x;
	const float frontY = frontOrigin.y - m_lineOfScrimmagePos.y;
	const float frontZ = frontOrigin.z - m_lineOfScrimmagePos.z;
	float distance = ScrimmageSquareRoot( ( frontY * frontY + frontZ * frontZ ) + frontX * frontX );
	distance = ScrimmagePolylineDistance( this, m_lineOfScrimmagePolyline, distance );
	const float frontAlpha = ScrimmagePresentationAlpha( distance, z_scrimmage_far, z_scrimmage_a_near, z_scrimmage_a_far );
	const int frontG = ScrimmageTruncate32( green );
	const int frontR = ScrimmageTruncate32( red );
	const int frontB = ScrimmageTruncate32( blue );
	const int frontA = ScrimmageTruncate32( frontAlpha );
	const Color frontColor( frontR, frontG, frontB, frontA );
	for ( int i = 0; i < m_lineOfScrimmagePolyline.Count() - 1; ++i )
	{
		const Vector a = m_lineOfScrimmagePolyline[i];
		const Vector b = m_lineOfScrimmagePolyline[i + 1];
		const Vector topA( a.x, a.y, a.z + z_scrimmage_height.GetFloat() );
		const Vector topB( b.x, b.y, b.z + z_scrimmage_height.GetFloat() );
		DrawScrimmageTriangle( a, topA, topB, frontColor );
		DrawScrimmageTriangle( topB, b, a, frontColor );
		DrawScrimmageTriangle( a, topB, topA, frontColor );
		DrawScrimmageTriangle( topB, a, b, frontColor );
	}
	if ( m_rearLineOfScrimmagePolyline.Count() != 0 )
	{
		const Vector &rearOrigin = GetAbsOrigin();
		const float x = rearOrigin.x - m_rearLineOfScrimmagePos.x;
		const float y = rearOrigin.y - m_rearLineOfScrimmagePos.y;
		const float z = rearOrigin.z - m_rearLineOfScrimmagePos.z;
		distance = ScrimmageSquareRoot( ( y * y + z * z ) + x * x );
		distance = ScrimmagePolylineDistance( this, m_rearLineOfScrimmagePolyline, distance );
		const float alpha = ScrimmagePresentationAlpha( distance, z_scrimmage_far, z_scrimmage_a_near, z_scrimmage_a_far );
		const int g = ScrimmageTruncate32( green );
		const int b = ScrimmageTruncate32( blue );
		const int r = ScrimmageTruncate32( red );
		const int a = ScrimmageTruncate32( alpha );
		const Color color( r, g, b, a );
		for ( int i = 0; i < m_rearLineOfScrimmagePolyline.Count() - 1; ++i )
		{
			const Vector p = m_rearLineOfScrimmagePolyline[i];
			const Vector q = m_rearLineOfScrimmagePolyline[i + 1];
			const Vector topP( p.x, p.y, p.z + z_scrimmage_height.GetFloat() );
			const Vector topQ( q.x, q.y, q.z + z_scrimmage_height.GetFloat() );
			DrawScrimmageTriangle( p, topP, topQ, color );
			DrawScrimmageTriangle( topQ, q, p, color );
			DrawScrimmageTriangle( p, topQ, topP, color );
			DrawScrimmageTriangle( topQ, p, q, color );
		}
	}

	const float yardRed = z_scrimmage_yardline_r.GetFloat();
	const float yardGreen = z_scrimmage_yardline_g.GetFloat();
	const float yardBlue = z_scrimmage_yardline_b.GetFloat();
	for ( int i = 0; i < 5; ++i )
	{
		CUtlVector< Vector > &polyline = m_yardLinePolylines[i];
		if ( polyline.Count() <= 1 )
			continue;
		const Vector &origin = GetAbsOrigin();
		const float x = origin.x - m_yardLinePos[i].x;
		const float y = origin.y - m_yardLinePos[i].y;
		const float z = origin.z - m_yardLinePos[i].z;
		distance = ScrimmageSquareRoot( ( x * x + y * y ) + z * z );
		distance = ScrimmagePolylineDistance( this, polyline, distance );
		const float alpha = ScrimmagePresentationAlpha( distance, z_scrimmage_yardline_far,
			z_scrimmage_yardline_a_near, z_scrimmage_yardline_a_far );
		m_yardLineAlpha[i] = ScrimmageTruncate32( alpha );
		Vector forward, right;
		ScrimmageAngleVectors( m_yardLineAngles[i], &forward, &right, NULL );
		const float width = z_scrimmage_yardline_width.GetFloat();
		const float height = z_scrimmage_yardline_height.GetFloat();
		const Vector halfWidth( ( forward.x * width ) * 0.5f,
			( forward.y * width ) * 0.5f, ( forward.z * width ) * 0.5f );
		const int r = ScrimmageTruncate32( yardRed );
		const int b = ScrimmageTruncate32( yardBlue );
		const int a = ScrimmageTruncate32( alpha );
		const int g = ScrimmageTruncate32( yardGreen );
		const Color color( r, g, b, a );
		for ( int j = 0; j < polyline.Count() - 1; ++j )
		{
			const Vector &p = polyline[j];
			const Vector &q = polyline[j + 1];
			const Vector a0( p.x - halfWidth.x, p.y - halfWidth.y, ( p.z + height ) - halfWidth.z );
			const Vector b0( q.x - halfWidth.x, q.y - halfWidth.y, ( q.z + height ) - halfWidth.z );
			// Native adds ONE halfWidth after subtraction; do not repair this
			// to a full-width symmetric quad or simplify away its roundoff.
			const Vector a1( a0.x + halfWidth.x, a0.y + halfWidth.y, a0.z + halfWidth.z );
			const Vector b1( b0.x + halfWidth.x, b0.y + halfWidth.y, b0.z + halfWidth.z );
			DrawScrimmageTriangle( a0, a1, b1, color );
			DrawScrimmageTriangle( b1, b0, a0, color );
			DrawScrimmageTriangle( a0, b1, a1, color );
			DrawScrimmageTriangle( b1, a0, b0, color );
		}
		const Vector halfRight( ( right.x * 25.0f ) * 0.5f,
			( right.y * 25.0f ) * 0.5f, ( right.z * 25.0f ) * 0.5f );
		const Vector panelOrigin(
			( ( m_yardLinePos[i].x + halfWidth.x * 3.0f ) - ( forward.x * 50.0f ) * 0.5f ) - halfRight.x,
			( ( m_yardLinePos[i].y + halfWidth.y * 3.0f ) - ( forward.y * 50.0f ) * 0.5f ) - halfRight.y,
			( ( ( height + m_yardLinePos[i].z ) + halfWidth.z * 3.0f ) - ( forward.z * 50.0f ) * 0.5f ) - halfRight.z );
		QAngle angles;
		L4D2_ScrimmageVectorAngles( &forward, &angles );
		VMatrix panelToWorld;
		SetupScrimmagePanelMatrix( panelToWorld, panelOrigin, angles );
		vgui::Panel *panel = m_yardLinePanels[i].GetPanel();
		// Main-approved Skip266EF2: skip ONLY this panel's GetVPanel/draw
		// when NULL. All native geometry/alpha/state above remain executed.
		if ( !panel )
			continue;
		g_pMatSystemSurface->DrawPanelIn3DSpace( panel->GetVPanel(), panelToWorld,
			m_yardLinePanelWidth[i], m_yardLinePanelHeight[i], 50.0f, 25.0f );
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Whole C25CB80, genuine Terror-introduced primary1452; all nine actors inherit.
bool C_TerrorPlayer::CanEnableFlashlight()
{
	const int team = GetRenderTeamNumber();
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}

// Whole C253180. One-way enable; the Boolean controls only prediction-gated sound.
bool C_TerrorPlayer::EnableFlashlight( bool playSound )
{
	if ( m_lifeState != LIFE_ALIVE || !CanEnableFlashlight() || IsEffectActive( EF_DIMLIGHT ) )
		return false;
	AddEffects( EF_DIMLIGHT );
	if ( playSound && prediction->IsFirstTimePredicted() )
		EmitSound( "Player.FlashlightOn", 0.0f );
	return true;
}

// Whole C2531F0, NONVIRTUAL. Actual impulse5032 is latched by real RunCommand.
void C_TerrorPlayer::ProcessFlashlightImpulse()
{
	if ( GetImpulse() != 100 || GetRenderTeamNumber() != TEAM_SURVIVOR )
		return;
	const int effects = GetEffects();
	if ( effects & EF_DIMLIGHT )
	{
		const bool alive = m_lifeState == LIFE_ALIVE;
		RemoveEffects( EF_DIMLIGHT ); // Inlined raw clear BEFORE sound gate; no visibility/list change.
		if ( alive && prediction->IsFirstTimePredicted() )
			EmitSound( "Player.FlashlightOff", 0.0f );
	}
	else
		EnableFlashlight( true );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

ConVar z_view_distance( "z_view_distance", "0", 0x00004008,
	"Default camera distance when in-eye (non-zero is pulled back like staggering, hanging, etc)" );

namespace
{
// C269500 is NOT the broader IsClimbingActivity predicate.
bool IsCameraRecoveryActivity( Activity activity )
{
	const TerrorOriginalActivity original = LookupTerrorOriginalActivity( activity );
	return original == TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW ||
		original == TERROR_ORIGINAL_ACT_TANK_OVERHEAD_THROW;
}

bool IsInsideNativeCameraTolerance( const Vector &v )
{
	return v.x > -0.0099999998f && v.x < 0.0099999998f &&
		v.y > -0.0099999998f && v.y < 0.0099999998f &&
		v.z > -0.0099999998f && v.z < 0.0099999998f;
}
}

// C25CBA0, primary byte1004. This is not SDK ShouldDrawLocalPlayer().
bool C_TerrorPlayer::IsThirdPersonCameraActive() const
{
	return m_flCurrentThirdPersonCameraDistance != 0.0f || GetViewEntity() != NULL;
}

// C254370. The original raw timer subtraction does not use HasStarted().
bool C_TerrorPlayer::IsBeingDraggedByTongue() const
{
	return m_tongueOwner.Get() != NULL && !m_isHangingFromTongue &&
		(double)gpGlobals->curtime - (double)m_tongueVictimTimer.m_timestamp.Get() > 1.0;
}

// C26B3F0..26B70E. Every original branch and repeated ability/activity query.
double C_TerrorPlayer::GetDesiredThirdPersonCameraDistance() const
{
	const float ordinaryDistance = z_view_distance.GetFloat();
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( GetTerrorActivity() );
	if ( m_isHangingFromLedge )
		return -64.0;
	if ( m_isHangingFromTongue || IsBeingDraggedByTongue() )
		return -100.0;
	if ( IsPouncing() || IsBeingPounced() || IsPummelVictim() )
		return -86.0;
	if ( IsJockeyVictim() || IsJockeyRiding() )
	{
		if ( !( gpGlobals->curtime > m_jockeyAttachmentViewTime ) )
			return ordinaryDistance;
		return -86.0;
	}
	if ( IsPummelVictim() || m_pummelVictim.Get() )
		return -86.0;
	if ( IsBeingCarried() || activity == TERROR_ORIGINAL_ACT_TERROR_SLAMMED_WALL ||
		activity == TERROR_ORIGINAL_ACT_TERROR_SLAMMED_GROUND )
		return -240.0;
	if ( activity == TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND )
		return -86.0;
	if ( IsIncapacitated() && GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 )
		return -128.0;
	if ( IsStaggering() && GetTeamNumber() == TEAM_INFECTED )
		return -64.0;
	if ( IsPerformingOtherUseAction() )
		return m_iCurrentUseAction != 10 ? -128.0 : -86.0;
	if ( IsPerformingSelfUseAction() || m_reviveTarget.Get() )
		return -64.0;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 1 )
	{
		CTongue *tongue = dynamic_cast< CTongue * >( GetCustomAbility() );
		return tongue && tongue->IsPastTongueLaunchPhase() ? -64.0 : ordinaryDistance;
	}
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 2 )
	{
		// Native9720 is M5's sole received m_nVariantType, not camera state.
		if ( m_nVariantType != 1 || !GetCustomAbility() )
			return ordinaryDistance;
		return GetCustomAbility()->IsActive() ? -128.0 : ordinaryDistance;
	}
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 )
		return ordinaryDistance;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 6 )
	{
		if ( !GetCustomAbility() || !GetCustomAbility()->IsActive() )
			return m_pummelVictim.Get() ? -128.0 : ordinaryDistance;
		return -240.0;
	}
	if ( IsAttackBlockingActivity( GetTerrorActivity() ) || IsCameraRecoveryActivity( GetTerrorActivity() ) )
		return GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 ? -96.0 : -64.0;
	if ( m_TimeForceExternalView > gpGlobals->curtime )
		return -128.0;
	return ordinaryDistance;
}

// C256780..256FE4, genuine Terror-introduced primary virtual byte1456.
void C_TerrorPlayer::CalcThirdPersonView( Vector &eyeOrigin, QAngle &eyeAngles )
{
	Vector backwards;
	AngleVectors( eyeAngles, &backwards );
	VectorNormalize( backwards );
	const float desiredDistance = (float)GetDesiredThirdPersonCameraDistance();
	float growSpeed = 256.0f;
	float shrinkSpeed = 58.0f;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 )
	{
		if ( IsIncapacitated() || IsAttackBlockingActivity( GetTerrorActivity() ) ||
			IsCameraRecoveryActivity( GetTerrorActivity() ) )
			growSpeed = 512.0f;
	}
	else if ( IsPerformingOtherUseAction() ||
		m_vecThirdPersonCameraOffset.x <= -0.0099999998f || m_vecThirdPersonCameraOffset.x >= 0.0099999998f ||
		m_vecThirdPersonCameraOffset.y <= -0.0099999998f || m_vecThirdPersonCameraOffset.y >= 0.0099999998f ||
		m_vecThirdPersonCameraOffset.z <= -0.0099999998f || m_vecThirdPersonCameraOffset.z >= 0.0099999998f ||
		IsBeingDraggedByTongue() )
	{
		growSpeed = 512.0f;
		shrinkSpeed = 512.0f;
	}
	else if ( IsJockeyRiding() || IsJockeyVictim() )
		growSpeed = 512.0f;
	else if ( IsBeingCarried() )
		growSpeed = 1024.0f;

	if ( desiredDistance > m_flCurrentThirdPersonCameraDistance )
	{
		m_flCurrentThirdPersonCameraDistance = gpGlobals->frametime * growSpeed + m_flCurrentThirdPersonCameraDistance;
		if ( m_flCurrentThirdPersonCameraDistance > desiredDistance )
			m_flCurrentThirdPersonCameraDistance = desiredDistance;
	}
	else if ( m_flCurrentThirdPersonCameraDistance > desiredDistance )
	{
		m_flCurrentThirdPersonCameraDistance = m_flCurrentThirdPersonCameraDistance - gpGlobals->frametime * shrinkSpeed;
		if ( desiredDistance > m_flCurrentThirdPersonCameraDistance )
			m_flCurrentThirdPersonCameraDistance = desiredDistance;
	}
	backwards *= m_flCurrentThirdPersonCameraDistance;
	Vector desiredOffset( vec3_origin );
	if ( IsPerformingOtherUseAction() )
	{
		C_BaseEntity *target = m_useActionTarget.Get();
		if ( target )
		{
			const Vector targetCenter = target->IsPlayer() ? target->WorldSpaceCenter() : target->GetAbsOrigin();
			const Vector &center = WorldSpaceCenter();
			desiredOffset.x = ( targetCenter.x - center.x ) * 0.5f;
			desiredOffset.y = ( targetCenter.y - center.y ) * 0.5f;
			desiredOffset.z = ( targetCenter.z - center.z ) * 0.5f - 10.0f;
		}
	}
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 && m_flCurrentThirdPersonCameraDistance != 0.0f )
		desiredOffset.z += 24.0f;
	Vector delta = desiredOffset - m_vecThirdPersonCameraOffset;
	if ( IsInsideNativeCameraTolerance( delta ) )
		m_vecThirdPersonCameraOffset = desiredOffset;
	else
	{
		const float distance = VectorNormalize( delta );
		float speed = 50.0f / first_aid_kit_view_duration.GetFloat();
		if ( IsInsideNativeCameraTolerance( desiredOffset ) )
			speed *= 2.0f;
		const float step = gpGlobals->frametime * speed;
		float amount = distance;
		if ( distance > step ) amount = step;
		else if ( distance < 0.0f ) amount = 0.0f;
		m_vecThirdPersonCameraOffset.x = m_vecThirdPersonCameraOffset.x + delta.x * amount;
		m_vecThirdPersonCameraOffset.y = m_vecThirdPersonCameraOffset.y + delta.y * amount;
		m_vecThirdPersonCameraOffset.z = delta.z * amount + m_vecThirdPersonCameraOffset.z;
	}
	eyeOrigin.x = m_vecThirdPersonCameraOffset.x + eyeOrigin.x;
	eyeOrigin.y = m_vecThirdPersonCameraOffset.y + eyeOrigin.y;
	eyeOrigin.z = m_vecThirdPersonCameraOffset.z + eyeOrigin.z;

	// Main-approved single-resolution treatment, retaining Get()'s native
	// index/serial/non-NULL validity checks. Native fault paths are C256CC8
	// (NULL vtable load) and C256D85 (NULL velocity load after C256D7D).
	// Earlier genuine policy queries are deliberately not hoisted or cached.
	C_BaseEntity *jockeyVictim = m_jockeyVictim.Get();
	if ( jockeyVictim )
		eyeOrigin = jockeyVictim->WorldSpaceCenter();
	else if ( m_jockeyAttacker.Get() && gpGlobals->curtime > m_jockeyAttachmentViewTime )
		eyeOrigin = WorldSpaceCenter();
	C_BaseEntity *carrier = m_carryAttacker.Get();
	if ( carrier )
	{
		const Vector &velocity = carrier->GetAbsVelocity();
		eyeOrigin.x = velocity.x * 0.1f + eyeOrigin.x;
		eyeOrigin.y = velocity.y * 0.1f + eyeOrigin.y;
		eyeOrigin.z = velocity.z * 0.1f + eyeOrigin.z;
	}
	if ( m_flCurrentThirdPersonCameraDistance != 0.0f )
	{
		C_BaseEntity::PushEnableAbsRecomputations( false );
		CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
		const Vector destination( eyeOrigin.x + backwards.x, backwards.y + eyeOrigin.y, backwards.z + eyeOrigin.z );
		trace_t trace;
		UTIL_TraceHull( eyeOrigin, destination, Vector( -7.0f, -7.0f, -7.0f ), Vector( 7.0f, 7.0f, 7.0f ),
			0x0200400Bu, &filter, &trace );
		C_BaseEntity::PopEnableAbsRecomputations();
		if ( trace.fraction < 1.0f )
		{
			const Vector difference = trace.endpos - eyeOrigin;
			const float length = sqrtf( ( difference.y * difference.y + difference.z * difference.z ) + difference.x * difference.x );
			double clippedDistance = -0.1;
			if ( !( -(double)length > -0.1 ) )
				clippedDistance = -(double)sqrtf( ( difference.y * difference.y + difference.z * difference.z ) + difference.x * difference.x );
			m_flCurrentThirdPersonCameraDistance = (float)clippedDistance;
		}
		eyeOrigin = trace.endpos;
	}
}

// Whole C102540F0. This is a real rescue-model entity, NOT the defibrillator
// death-model registry. Reuse M7's ordered, dormant/serial-checked C251300
// provider (TerrorGlow_FindRescueModel); do not add a second registry.
C_BaseEntity *C_TerrorPlayer::GetRescueModelIfDead() const
{
	if ( m_lifeState != LIFE_ALIVE && GetTeamNumber() == TEAM_SURVIVOR )
		return TerrorGlow_FindRescueModel( this );
	return NULL;
}

// Whole C10253140. Remote ghosts and EF_NODRAW are suppressed in the ordinary
// renderable path too; absence of native IClientModelRenderable is not a fake
// NULL getter or a reason to skip these real concrete visibility decisions.
bool C_TerrorPlayer::ShouldDraw()
{
	if ( IsEffectActive( EF_NODRAW ) )
		return false;
	if ( IsGhost() && this != C_BasePlayer::GetLocalPlayer( 0 ) )
		return false;
	return BaseClass::ShouldDraw();
}

// C10252860, primary byte516. This is ShouldPredict, NOT a draw-hide query.
// Native ctor25CAE8 explicitly clears the sole bool (BL was zero); the whole
// Simulate2603EC..26049B owns suppression/restart in native order. Its else is
// BasePlayerC63610 (actual IsPlayer && local flag), not an animation cache.
bool C_TerrorPlayer::ShouldPredict( void )
{
	if ( m_bJockeyPredictionSuppressed )
		return false;
	return BaseClass::ShouldPredict();
}

// Whole C10254120; reused evidence native_scene_10254120.txt#0E03.
void C_TerrorPlayer::UpdateRescueAnimation()
{
	if ( m_lifeState == LIFE_ALIVE )
		return;
	const int team = GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
		return;

	// Preserve native repeated state/team queries and its TEAM2-only rescue
	// lookup. The earlier broader team2/4 predicate is not interchangeable.
	if ( m_lifeState == LIFE_ALIVE
		|| GetTeamNumber() != TEAM_SURVIVOR
		|| !TerrorGlow_FindRescueModel( this ) )
	{
		FrameAdvance();
		return;
	}

	const int sequence = SelectWeightedSequence( TerrorActivityFromOriginal( 703 ) );
	if ( GetSequence() != sequence || GetCycle() > 0.99000001f )
	{
		ResetSequence( sequence );
		FrameAdvance();
	}
	// Native matching-sequence cycle<=.99 deliberately does NOT FrameAdvance.
}

// Whole C10254700; preserve the second team query and raw received class read.
// This is NOT IsVisible/IsDormant/IsAlive and must not be substituted by them.
bool C_TerrorPlayer::ShouldDrawEntityFlameLight() const
{
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		return false;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 ) // Native Tank class domain.
		return true;
	return !UTIL_IsLowViolence();
}

static ConVar sb_perf_collect( "sb_perf_collect", "1", 0x0000000A, "Collect perf for Steam stats" );
static ConVar sb_perf_dump_low( "sb_perf_dump_low", "40", 0x0000000A, "Low perf threshold" );
static ConVar sb_perf_dump_low_dist( "sb_perf_dump_low_dist", "64", 0x0000000A, "Dump low perf points at least this distance apart" );
static ConVar sb_perf_dump( "sb_perf_dump", "0", 0x00006008, "If true, low perf places will be dumped" );
static ConVar sb_perf_dump_bots( "sb_perf_dump_bots", "", 0x00006008, "Which bots to use for dumping: namvet, girl, biker, manager" );

struct TerrorLowPerfPoint_t
{
	float time;
	Vector origin;
	QAngle angles;
	int framesPerSecond;
	bool coalesced;
};
static CUtlVector< TerrorLowPerfPoint_t > s_TerrorLowPerfPoints;
static float s_TerrorPerfFPS[10];
static float s_TerrorPerfLastRealTime;
static Vector s_TerrorPerfLastPosition( 0.0f, 0.0f, 0.0f );
static int s_TerrorPerfLowestFPS = 100; // Native initialized data106E6B98, not INT_MAX.

// Whole C3D9720. Failure is NOT cached: the native probe caches only success.
// This is the actual native Win32 mapping protocol, not a false platform or a
// substituted generic debugger query. The outer C2520E0 decision is cached once.
static bool TerrorPerf_IsValveModuleDebugged()
{
	static bool detected;
	if ( !detected )
	{
		char name[260];
		sprintf( name, "VALVE-MODULE-DEBUG-%08X", GetCurrentProcessId() );
		HANDLE mapping = OpenFileMappingA( FILE_MAP_READ, FALSE, name );
		if ( mapping && mapping != INVALID_HANDLE_VALUE )
		{
			CloseHandle( mapping );
			detected = true;
		}
	}
	return detected;
}

// Whole C2520E0. Native CSteamAPIContext+12 is SteamUtils; its virtual8 is
// GetConnectedUniverse. Real Utils absence cachesFALSE, exactly as native.
static bool TerrorPerf_ShouldReport()
{
	static bool checked;
	static bool report;
	if ( !checked )
	{
		checked = true;
		ISteamUtils *utils = steamapicontext->SteamUtils();
		const bool betaUniverse = utils && utils->GetConnectedUniverse() == k_EUniverseBeta;
		if ( betaUniverse && !TerrorPerf_IsValveModuleDebugged() )
			report = true;
	}
	return report;
}

// C251EF0: ONLY history clears here, no position/minimum/vector/time reset.
static void TerrorPerf_ResetFPSHistory()
{
	for ( int i = 0; i < ARRAYSIZE( s_TerrorPerfFPS ); ++i )
		s_TerrorPerfFPS[i] = 0.0f;
}

// C251E80 is the distinct invalid-frame reset. MinimumFPS and points survive.
static void TerrorPerf_ResetInvalidFrame()
{
	TerrorPerf_ResetFPSHistory();
	s_TerrorPerfLastPosition.Init();
	s_TerrorPerfLastRealTime = gpGlobals->realtime;
}

// Whole C251F30. COMISS/JBE rejects unordered/nonpositive samples, unlike a
// guessed <=0 source predicate. Preserve actual CVTTSS2SI indefinite conversion.
static int TerrorPerf_AverageFPS()
{
	const float delta = gpGlobals->realtime - s_TerrorPerfLastRealTime;
	s_TerrorPerfLastRealTime = gpGlobals->realtime;
	if ( !( delta > 0.0f ) || delta >= 1.0f )
	{
		if ( delta != 0.0f )
			TerrorPerf_ResetInvalidFrame();
		return 0;
	}
	const float currentFPS = 1.0f / delta;
	float sum = currentFPS;
	int count = 1;
	bool incomplete = false;
	for ( int i = 0; i < 9; ++i )
	{
		const float sample = s_TerrorPerfFPS[i + 1];
		s_TerrorPerfFPS[i] = sample;
		if ( !( sample > 0.0f ) )
			incomplete = true;
		else
		{
			sum = sample + sum;
			++count;
		}
	}
	s_TerrorPerfFPS[9] = currentFPS;
	const float average = incomplete ? 0.0f : sum / static_cast< float >( count );
	return _mm_cvtt_ss2si( _mm_set_ss( average ) );
}

static float TerrorPerf_DistanceSquared( const Vector &a, const Vector &b )
{
	const float x = a.x - b.x;
	const float y = a.y - b.y;
	const float z = a.z - b.z;
	return ( y * y + x * x ) + z * z;
}

// Whole C259F80 plus actual vector insertion/copy providers C2596D0/C251E40.
static void TerrorPerf_RememberPoint( const TerrorLowPerfPoint_t &point )
{
	if ( s_TerrorLowPerfPoints.Count() >= 1024 )
	{
		DevWarning( "Low Perf Report: %d fps: map %s; setpos %f %f %f; setang %f %f %f; (not remembered)\n",
			point.framesPerSecond, engine->GetLevelName(), point.origin.x, point.origin.y, point.origin.z,
			point.angles.x, point.angles.y, point.angles.z );
		return;
	}
	if ( TerrorPerf_DistanceSquared( point.origin, s_TerrorPerfLastPosition )
		> sb_perf_dump_low_dist.GetFloat() * sb_perf_dump_low_dist.GetFloat()
		|| point.framesPerSecond < s_TerrorPerfLowestFPS )
	{
		s_TerrorPerfLastPosition = point.origin;
		s_TerrorPerfLowestFPS = point.framesPerSecond;
		if ( !s_TerrorLowPerfPoints.Count()
			|| s_TerrorLowPerfPoints.Tail().coalesced
			|| sb_perf_dump_low_dist.GetFloat() * sb_perf_dump_low_dist.GetFloat()
				<= TerrorPerf_DistanceSquared( point.origin, s_TerrorLowPerfPoints.Tail().origin ) )
		{
			s_TerrorLowPerfPoints.AddToTail( point );
		}
		else
		{
			s_TerrorLowPerfPoints.Tail() = point;
			s_TerrorLowPerfPoints.Tail().coalesced = true;
		}
		DevWarning( "Low Perf Report: %d fps: map %s; setpos %f %f %f; setang %f %f %f; (#%d)\n",
			point.framesPerSecond, engine->GetLevelName(), point.origin.x, point.origin.y, point.origin.z,
			point.angles.x, point.angles.y, point.angles.z, s_TerrorLowPerfPoints.Count() );
	}
}

// C25FE90 head, before the real Jockey prediction/deadline block. Native per-
// split loops project to the sole actual user0 (#3ABD/#6D37), with M7's exact
// IN_EYE/CHASE resolver, not the distinct IN_EYE-only render-player policy.
void C_TerrorPlayer::UpdateLowPerformanceReports()
{
	if ( !TerrorPerf_ShouldReport() )
		return;
	if ( sb_perf_collect.GetBool() )
	{
		const int fps = TerrorPerf_AverageFPS();
		if ( fps > 0 && fps < sb_perf_dump_low.GetInt() )
		{
			C_BasePlayer *viewer = TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer( 0 ) );
			if ( viewer )
			{
				TerrorLowPerfPoint_t point;
				point.origin = viewer->GetAbsOrigin();
				point.angles = viewer->GetAbsAngles();
				point.time = gpGlobals->curtime;
				point.framesPerSecond = fps;
				point.coalesced = false;
				TerrorPerf_RememberPoint( point );
				TerrorPerf_ResetFPSHistory();
			}
		}
		return;
	}
	if ( !sb_perf_dump.GetBool() || m_lifeState != LIFE_ALIVE )
		return;
	const int team = GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
		return;
	const char *filter = sb_perf_dump_bots.GetString();
	const unsigned int character = static_cast< unsigned int >( m_survivorCharacter );
	if ( filter && *filter
		&& !( character < 8 && static_cast< unsigned int >( Q_strlen( filter ) ) > character && strchr( "+1Xx", filter[character] ) ) )
		return;
	const int fps = TerrorPerf_AverageFPS();
	if ( fps <= 0 || fps >= sb_perf_dump_low.GetInt() )
		return;
	const Vector origin = GetAbsOrigin();
	(void)GetAbsAngles(); // Preserve the native pre-distance absolute-angle query.
	if ( TerrorPerf_DistanceSquared( origin, s_TerrorPerfLastPosition )
		> sb_perf_dump_low_dist.GetFloat() * sb_perf_dump_low_dist.GetFloat()
		|| fps < s_TerrorPerfLowestFPS )
	{
		s_TerrorPerfLastPosition = origin;
		s_TerrorPerfLowestFPS = fps;
		char commands[1024] = { 0 };
		C_BasePlayer *viewer = TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer( 0 ) );
		if ( viewer )
		{
			const Vector viewOrigin = viewer->GetAbsOrigin();
			const QAngle viewAngles = viewer->GetAbsAngles();
			// Native slot0 command prefix1052F3AB is the actual empty string;
			// slot1's "cmd2 " is not invented for this single-user target.
			sprintf( commands + strlen( commands ), " %ssetpos %f %f %f; %ssetang %f %f %f;", "",
				viewOrigin.x, viewOrigin.y, viewOrigin.z, "", viewAngles.x, viewAngles.y, viewAngles.z );
		}
		DevWarning( "Low Perf: %d fps: map %s; %s\n", fps, engine->GetLevelName(), commands );
		TerrorPerf_ResetFPSHistory();
	}
}

// Main rule: native C_PerfCrawlListener Init2530D0/258D20 has NO construction/
// registration path (performance_listener_reachability.json#E8D5). Deliberately
// omit that unreachable listener, NOT add a fake game_newmap count reset.

static ConVar cl_survivor_light_enable( "cl_survivor_light_enable", "1", 0x0000400A );
static ConVar cl_survivor_light_offset_x( "cl_survivor_light_offset_x", "5", 0x0000400A );
static ConVar cl_survivor_light_offset_y( "cl_survivor_light_offset_y", "2", 0x0000400A );
static ConVar cl_survivor_light_offset_z( "cl_survivor_light_offset_z", "5", 0x0000400A );
static ConVar cl_survivor_light_radius( "cl_survivor_light_radius", "12", 0x0000400A );
static ConVar cl_survivor_light_rot( "cl_survivor_light_rot", "0", 0x0000400A );
static ConVar cl_survivor_light_cone( "cl_survivor_light_cone", "0", 0x0000400A );
static ConVar cl_survivor_light_debug( "cl_survivor_light_debug", "0", 0x0000400A );
static ConVar cl_survivor_light_brightness( "cl_survivor_light_brightness", "4", 0x0000400A );

// Whole C10254810, with Main's explicit owner-index renderer-capability omission.
void C_TerrorPlayer::UpdateSimulationDLight()
{
	if ( m_lifeState == LIFE_ALIVE
		&& GetTeamNumber() == TEAM_INFECTED
		&& m_zombieClass == 4 // Native Spitter class domain.
		&& !IsGhost() )
	{
		if ( !m_pSimulationDLight )
			m_pSimulationDLight = effects->CL_AllocDlight( entindex() + 0x08000000 );
		// Main-approved omission ONLY: C254810 native dlight+64 = entindex().
		// Evidence client_simulation_native.json#74AC; target public/dlight.h:43-82
		// is64 bytes and has no owner field. No trailing field/header/engine hook.
		const int attachment = LookupAttachment( "mouth" );
		Vector origin, forward, right, up;
		QAngle angles;
		if ( GetAttachment( attachment, origin, angles ) )
		{
			AngleVectors( angles, &forward, &right, &up );
			Vector lightOrigin;
			lightOrigin.x = ( right.x + ( forward.x * 16.0f + origin.x ) ) + up.x * -10.0f;
			lightOrigin.y = ( right.y + ( origin.y + forward.y * 16.0f ) ) + up.y * -10.0f;
			lightOrigin.z = ( right.z + ( origin.z + forward.z * 16.0f ) ) + up.z * -10.0f;
			m_pSimulationDLight->origin = lightOrigin;
			m_pSimulationDLight->radius = 32.0f;
			m_pSimulationDLight->decay = 0.0f;
			Vector direction( -forward.x, -forward.y, -forward.z );
			VectorNormalize( direction );
			m_pSimulationDLight->m_Direction = direction;
			m_pSimulationDLight->m_OuterAngle = 0.0f;
			m_pSimulationDLight->m_InnerAngle = 0.0f;
			if ( cl_survivor_light_debug.GetBool() )
			{
				NDebugOverlay::Axis( lightOrigin, angles, 10.0f, true, 0.0f );
				NDebugOverlay::Line( lightOrigin, lightOrigin + direction * 10.0f, 255, 255, 0, true, 0.0f );
			}
			m_pSimulationDLight->die = gpGlobals->curtime + 1000000.0f;
			m_pSimulationDLight->color.r = 122;
			m_pSimulationDLight->color.g = 207;
			m_pSimulationDLight->color.b = 57;
			m_pSimulationDLight->color.exponent = 5;
		}
		// Native failed attachment leaves the existing pooled light untouched.
		return;
	}

	const int team = GetTeamNumber();
	if ( ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
		|| IsLocalPlayer()
		|| m_lifeState != LIFE_ALIVE
		|| !IsEffectActive( EF_DIMLIGHT )
		|| m_pounceAttacker.Get()
		|| m_jockeyAttacker.Get()
		|| IsHangingFromLedge()
		|| m_pummelAttacker.Get()
		|| !cl_survivor_light_enable.GetBool() )
	{
		if ( m_pSimulationDLight )
		{
			m_pSimulationDLight->color.r = 0;
			m_pSimulationDLight->color.g = 0;
			m_pSimulationDLight->color.b = 0;
			m_pSimulationDLight->color.exponent = 1;
			m_pSimulationDLight->die = -1.0f;
			m_pSimulationDLight->radius = 0.0f;
		}
		return;
	}

	if ( !m_pSimulationDLight )
		m_pSimulationDLight = effects->CL_AllocDlight( entindex() + 0x08000000 );
	// Main-approved omission ONLY: C254810 native dlight+64 = entindex().
	// Evidence client_simulation_native.json#74AC; target public/dlight.h:43-82
	// is64 bytes and has no owner field. No trailing field/header/engine hook.
	const int attachment = LookupAttachment( "survivor_light" );
	Vector origin, forward, right, up;
	QAngle angles;
	if ( GetAttachment( attachment, origin, angles ) )
	{
		AngleVectors( angles, &forward, &right, &up );
		const float upOffset = cl_survivor_light_offset_z.GetFloat();
		const float rightOffset = cl_survivor_light_offset_y.GetFloat();
		const float forwardOffset = cl_survivor_light_offset_x.GetFloat();
		Vector lightOrigin;
		lightOrigin.x = ( ( forward.x * forwardOffset + origin.x ) + right.x * rightOffset ) + up.x * upOffset;
		lightOrigin.y = ( ( origin.y + forward.y * forwardOffset ) + right.y * rightOffset ) + up.y * upOffset;
		lightOrigin.z = ( ( origin.z + forward.z * forwardOffset ) + right.z * rightOffset ) + up.z * upOffset;
		m_pSimulationDLight->origin = lightOrigin;
		m_pSimulationDLight->radius = cl_survivor_light_radius.GetFloat();
		m_pSimulationDLight->decay = 0.0f;
		const float rotation = cl_survivor_light_rot.GetFloat();
		Vector direction( forward.x * rotation + right.x,
			forward.y * rotation + right.y, forward.z * rotation + right.z );
		VectorNormalize( direction );
		m_pSimulationDLight->m_Direction = direction;
		m_pSimulationDLight->m_OuterAngle = cl_survivor_light_cone.GetFloat();
		m_pSimulationDLight->m_InnerAngle = m_pSimulationDLight->m_OuterAngle;
		if ( cl_survivor_light_debug.GetBool() )
		{
			NDebugOverlay::Axis( lightOrigin, angles, 10.0f, true, 0.0f );
			NDebugOverlay::Line( lightOrigin, lightOrigin + direction * 10.0f, 255, 255, 0, true, 0.0f );
		}
		m_pSimulationDLight->die = gpGlobals->curtime + 1000000.0f;
		m_pSimulationDLight->color.r = 255;
		m_pSimulationDLight->color.g = 192;
		m_pSimulationDLight->color.b = 64;
		m_pSimulationDLight->color.exponent = static_cast< signed char >( cl_survivor_light_brightness.GetInt() );
	}
}

extern ConVar still_vel_limit;
extern ConVar still_view_accel_limit;
extern "C" float L4D2_EyeAngularVelocityComponent( float current, float previous, float reciprocalTime );
extern "C" float L4D2_ScaleViewAcceleration( int defaultFOV, float magnitude );
extern "C" bool L4D2_IsViewAccelerationUnsteady( float numerator, float fov, float limit );

// Whole native26C9E0. Actual time for angular derivatives is REALTIME, while
// native interval timer starts use CURTIME. Do not merge these clocks/state.
void C_TerrorPlayer::UpdateClientMotionTimers()
{
	if ( m_lifeState != LIFE_ALIVE ) return;
	const float elapsed = gpGlobals->realtime - m_flLastEyeMotionUpdateTime;
	if ( elapsed > 0.0f )
	{
		const QAngle &eyes = EyeAngles();
		const float reciprocal = 1.0f / elapsed;
		const float pitchSpeed = L4D2_EyeAngularVelocityComponent( eyes.x, m_angPreviousMotionEyeAngles.x, reciprocal );
		const float yawSpeed = L4D2_EyeAngularVelocityComponent( eyes.y, m_angPreviousMotionEyeAngles.y, reciprocal );
		const float negativeRollSpeed = -m_vecEyeAngularVelocity.z;
		m_vecEyeAngularAcceleration.x = reciprocal * ( pitchSpeed - m_vecEyeAngularVelocity.x );
		m_vecEyeAngularAcceleration.z = reciprocal * negativeRollSpeed;
		m_vecEyeAngularAcceleration.y = reciprocal * ( yawSpeed - m_vecEyeAngularVelocity.y );
		m_vecEyeAngularVelocity.x = pitchSpeed;
		m_vecEyeAngularVelocity.y = yawSpeed;
		m_vecEyeAngularVelocity.z = 0.0f;
		m_angPreviousMotionEyeAngles.x = eyes.x;
		m_angPreviousMotionEyeAngles.y = eyes.y;
		m_angPreviousMotionEyeAngles.z = eyes.z;
		m_flLastEyeMotionUpdateTime = gpGlobals->realtime;
	}
	bool stationary = false;
	if ( GetFlags() & FL_ONGROUND )
	{
		const float limit = still_vel_limit.GetFloat(); // Native snapshot BEFORE lazy abs-velocity update.
		const Vector &velocity = GetAbsVelocity();
		const float squared = ( velocity.x * velocity.x + velocity.y * velocity.y ) + velocity.z * velocity.z;
		stationary = limit * limit > squared; // Ordered JA; unordered does NOT establish stillness.
	}
	if ( stationary || ( m_lifeState == LIFE_ALIVE && m_isIncapacitated ) ||
		m_isHangingFromTongue || m_isHangingFromLedge )
	{
		if ( !( m_stationaryTimer.m_timestamp.Get() > 0.0f ) ) m_stationaryTimer.Start();
	}
	else if ( m_stationaryTimer.m_timestamp.Get() != -1.0f ) m_stationaryTimer.Invalidate();

	const float z = m_vecEyeAngularAcceleration.z;
	const float y = m_vecEyeAngularAcceleration.y;
	const float x = m_vecEyeAngularAcceleration.x;
	const float limit = still_view_accel_limit.GetFloat();
	const float magnitude = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( x * x + y * y ) + z * z ) ) );
	const int defaultFOV = GetDefaultFOV(); // Native NONCONST virtual1208 FIRST.
	const float numerator = L4D2_ScaleViewAcceleration( defaultFOV, magnitude );
	const float fov = GetFOV(); // Native FLOAT virtual1204 AFTER numerator f32 spill.
	if ( L4D2_IsViewAccelerationUnsteady( numerator, fov, limit ) )
	{
		if ( m_viewAccelerationRestTimer.m_timestamp.Get() != -1.0f ) m_viewAccelerationRestTimer.Invalidate();
	}
	else if ( !( m_viewAccelerationRestTimer.m_timestamp.Get() > 0.0f ) ) m_viewAccelerationRestTimer.Start();
}

// Whole reached native100A9670 iterator +268C70 real collector specialization.
// Keep all four ACTUAL runtime filters and native callback/read order, including
// two ghost queries and two possible incapacitation queries. No nearest-sort,
// actor-only universe, health substitute, NULL recovery or hidden living rule.
class TerrorClientPlayerCollector
{
public:
	TerrorClientPlayerCollector( int team, int lifeMask, int ghostMask, int zombieClass ) :
		m_Team( team ), m_LifeMask( lifeMask ), m_GhostMask( ghostMask ), m_ZombieClass( zombieClass ) {}
	void Collect()
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			C_BasePlayer *player = UTIL_PlayerByIndex( i );
			if ( player && player->IsPlayer() && !player->IsDormant() && GameResources()->IsConnected( i ) )
			{
				if ( !Visit( player ) ) return;
			}
		}
	}
	CUtlVector< C_TerrorPlayer * > m_Players;
private:
	bool Visit( C_BasePlayer *base )
	{
		C_TerrorPlayer *player = base && base->IsPlayer() ? static_cast< C_TerrorPlayer * >( base ) : NULL;
		const int team = m_Team;
		// Native caller guarantees the player-valued argument. Do not add a
		// second NULL/RTTI safety policy after its resource callback.
		if ( ( team == -1 || player->GetRenderTeamNumber() == team ) &&
			( team != TEAM_INFECTED || m_ZombieClass == 9 || player->GetZombieClass() == m_ZombieClass ) &&
			( player->IsGhost() || ( m_GhostMask & 1 ) ) &&
			( !player->IsGhost() || ( m_GhostMask & 2 ) ) )
		{
			if ( player->m_lifeState != LIFE_ALIVE && ( m_LifeMask & 1 ) ) m_Players.AddToTail( player );
			if ( player->m_lifeState == LIFE_ALIVE )
			{
				if ( !player->IsIncapacitated() && ( m_LifeMask & 2 ) ) m_Players.AddToTail( player );
				if ( player->m_lifeState == LIFE_ALIVE && player->IsIncapacitated() && ( m_LifeMask & 4 ) )
					m_Players.AddToTail( player );
			}
		}
		return true;
	}
	int m_Team, m_LifeMask, m_GhostMask, m_ZombieClass;
};

// Whole native25A790. Actual10048 patch/10052 SOURCE handle are already frozen
// in198A/parent header union. Ordinary stop branches leave the source handle
// unchanged. m_isAttemptingToPounce10032 and start10036 are real received fields.
void C_TerrorPlayer::UpdateHunterPounceSound()
{
	if ( !IsLocalPlayer() ) return;
	TerrorClientPlayerCollector collector( TEAM_INFECTED, 2, 3, 3 ); // Original four ints3,2,3,3.
	collector.Collect();
	(void)GetAbsOrigin(); // Native virtual44 AFTER complete collection, even if empty.
	C_TerrorPlayer *source = NULL;
	for ( int i = 0; i < collector.m_Players.Count(); ++i )
	{
		if ( collector.m_Players[i]->m_isAttemptingToPounce )
		{
			source = collector.m_Players[i];
			break; // FIRST matching collected index, not nearest/visible Hunter.
		}
	}
	if ( !source )
	{
		if ( m_pHunterPounceSound )
		{
			CSoundEnvelopeController::GetController().SoundDestroy( m_pHunterPounceSound );
			m_pHunterPounceSound = NULL;
		}
		return;
	}
	if ( m_hHunterPounceSoundSource.Get() != source && m_pHunterPounceSound )
	{
		CSoundEnvelopeController::GetController().SoundDestroy( m_pHunterPounceSound );
		m_pHunterPounceSound = NULL;
	}
	const Vector &origin = source->GetAbsOrigin();
	const float dy = origin.y - source->m_pounceStartPosition.y;
	const float dx = origin.x - source->m_pounceStartPosition.x;
	const float distance = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( dy * dy + dx * dx ) ) );
	float volume = 0.0f;
	if ( distance > 300.0f )
		volume = distance >= 1000.0f ? 1.0f : ( distance - 300.0f ) * 0.0014285714f;
	if ( volume > 0.0f )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
		if ( !m_pHunterPounceSound )
		{
			CSingleUserRecipientFilter filter( this );
			filter.MakeReliable(); // Native101FA580 writes actual reliable byte4, NOT prediction rules.
			m_pHunterPounceSound = controller.SoundCreate( filter, source->entindex(), "HunterZombie.Pounce.FlyLoop" );
			controller.Play( m_pHunterPounceSound, volume, 100.0f, 0.0f ); // Preserve native no alloc-failure guard.
			m_hHunterPounceSoundSource = source; // Native fresh real reference handle after Play.
		}
		else controller.SoundChangeVolume( m_pHunterPounceSound, volume, 0.1f );
	}
	else if ( m_pHunterPounceSound )
	{
		CSoundEnvelopeController::GetController().SoundDestroy( m_pHunterPounceSound );
		m_pHunterPounceSound = NULL;
	}
}

extern "C" double L4D2_GlowNormalize( Vector *direction );

// Whole native258770. No invented DidHit/fraction/entity-index filter: native
// compares the trace entity POINTER with freshly resolved world entity0.
bool C_TerrorPlayer::EmitTankAttachmentImpact( int attachment )
{
	const Vector eye = EyePosition();
	Vector position;
	if ( !GetAttachment( attachment, position ) ) return false;
	trace_t trace;
	UTIL_TraceLine( eye, position, 0x400B, this, COLLISION_GROUP_NONE, &trace );
	if ( trace.startsolid || trace.m_pEnt != ClientEntityList().GetBaseEntity( 0 ) ) return false;
	Vector direction = trace.endpos - eye;
	L4D2_GlowNormalize( &direction );
	const surfacedata_t *surface = physprops->GetSurfaceData( trace.surface.surfaceProps );
	PerformCustomEffects( trace.endpos, trace, direction,
		static_cast< unsigned short >( surface->game.material ), 1, 0 );
	return true;
}

// Whole native258880. Timer7060 is already real198A-constructed state. The
// original samples velocity only AFTER restarting its0.1s timer, including
// motionless actors. Ordered strict range means NaN velocity TAKES effects.
void C_TerrorPlayer::UpdateTankAttachmentImpacts()
{
	if ( m_lifeState != LIFE_ALIVE || GetRenderTeamNumber() != TEAM_INFECTED || m_zombieClass != 8 ) return;
	if ( !( static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_tankAttachmentImpactTimer.m_timestamp.Get() ) ) ) return;
	m_tankAttachmentImpactTimer.Start( 0.1f );
	const Vector &velocity = GetAbsVelocity();
	if ( velocity.x > -0.01f && velocity.x < 0.01f &&
		velocity.y > -0.01f && velocity.y < 0.01f &&
		velocity.z > -0.01f && velocity.z < 0.01f ) return;
	int attachment = LookupAttachment( "lshoulder" );
	if ( attachment > 0 && !EmitTankAttachmentImpact( attachment ) )
	{
		attachment = LookupAttachment( "lelbow" );
		if ( attachment > 0 ) EmitTankAttachmentImpact( attachment );
	}
	attachment = LookupAttachment( "rshoulder" );
	if ( attachment > 0 && !EmitTankAttachmentImpact( attachment ) )
	{
		attachment = LookupAttachment( "relbow" );
		if ( attachment > 0 ) EmitTankAttachmentImpact( attachment );
	}
}

extern void FormatViewModelAttachment( Vector &origin, bool inverse );
extern "C" float L4D2_LaserSpreadFloat( float spread );
extern "C" bool L4D2_LaserJitterShouldDecay( float compareTime, float lastRandomTime,
	float timerNow, float fireTimestamp );
extern "C" void L4D2_ApplyLaserSightWobble( Vector *end, float ownerSpeed,
	float spread, float time, float distance );

static ConVar r_draw_lasersight_1st_person( "r_draw_lasersight_1st_person", "1", 0x0000000A, "Draw laser sight for the local player" ); // C105105E0 raw0 ->2.
static ConVar r_draw_lasersight_3rd_person( "r_draw_lasersight_3rd_person", "1", 0x0000000A, "Draw laser sights for other players" ); // C10510610 raw0 ->2.

static FORCEINLINE float TerrorLaserSqrt( float squared )
{
	return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( squared ) ) );
}

// Whole native252A50: retain both virtual calls and load their referenced
// components AFTER the second call, then write genuine7972 scratch state.
const QAngle &C_TerrorPlayer::GetAimEyeAnglesWithPunch()
{
	const QAngle &punch = GetPunchAngle();
	const QAngle &eyes = EyeAngles();
	m_angAimEyeAnglesWithPunch.x = punch.x + eyes.x;
	m_angAimEyeAnglesWithPunch.y = punch.y + eyes.y;
	m_angAimEyeAnglesWithPunch.z = punch.z + eyes.z;
	return m_angAimEyeAnglesWithPunch;
}

void C_TerrorPlayer::UpdateLaserSightParticles()
{
	const int user = 0;
	if ( m_lifeState != LIFE_ALIVE ) goto cleanup; // Native327 is lifeState; waterLevel is326.
	{
		const int team = GetRenderTeamNumber();
		if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) goto cleanup;
	}
	if ( C_BasePlayer::GetLocalPlayer() == this )
	{
		if ( m_hThirdPersonLaserSightParticles[user] )
		{
			ParticleProp()->StopEmission( m_hThirdPersonLaserSightParticles[user] );
			m_hThirdPersonLaserSightParticles[user] = NULL;
		}
		if ( IsThirdPersonCameraActive() || input->CAM_IsThirdPerson() ||
			!r_draw_lasersight_1st_person.GetBool() ) goto cleanup;
		C_BaseViewModel *viewModel = GetViewModel( 0 );
		C_BaseCombatWeapon *active = GetActiveWeapon();
		CTerrorGun *gun = active ? active->GetTerrorGun() : NULL; // Actual paired root1228, NOT RTTI.
		if ( !gun || !gun->IsWeaponVisible() || !viewModel || !viewModel->ShouldDraw() ||
			!gun->HasUpgrade( 2 ) || gun->GetSlot() != 0 ) goto cleanup;
		const int attachment = viewModel->LookupAttachment( "flashlight" );
		if ( attachment <= 0 ) goto cleanup;
		if ( !m_hFirstPersonLaserSightParticles[user] )
		{
			m_hFirstPersonLaserSightParticles[user] = ParticleProp()->Create(
				"weapon_laser_sight_FP", PATTACH_POINT_FOLLOW, attachment );
			ParticleProp()->AddControlPoint( m_hFirstPersonLaserSightParticles[user],
				1, this, PATTACH_CUSTOMORIGIN );
			ParticleProp()->AddControlPoint( m_hFirstPersonLaserSightParticles[user],
				2, this, PATTACH_CUSTOMORIGIN );
			ParticleProp()->AddControlPoint( m_hFirstPersonLaserSightParticles[user],
				3, this, PATTACH_CUSTOMORIGIN );
			m_hFirstPersonLaserSightParticles[user]->SetDrawOnlyForLocalUser( user );
		}
		Vector attachmentOrigin;
		QAngle attachmentAngles;
		viewModel->GetAttachment( attachment, attachmentOrigin, attachmentAngles ); // Return ignored natively.
		Vector attachmentForward, attachmentRight, attachmentUp;
		AngleVectors( attachmentAngles, &attachmentForward, &attachmentRight, &attachmentUp );
		Vector formattedStart = attachmentOrigin;
		Vector formattedFar;
		formattedFar.x = attachmentOrigin.x + attachmentForward.x * 200000.0f;
		formattedFar.y = attachmentOrigin.y + attachmentForward.y * 200000.0f;
		formattedFar.z = attachmentOrigin.z + attachmentForward.z * 200000.0f;
		::FormatViewModelAttachment( formattedStart, true );
		::FormatViewModelAttachment( formattedFar, true ); // Native observable call even though no later read.
		m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 1, formattedStart );

		Vector eyeForward;
		AngleVectors( GetAimEyeAnglesWithPunch(), &eyeForward );
		const float dot = ( eyeForward.x * attachmentForward.x + eyeForward.y * attachmentForward.y ) +
			eyeForward.z * attachmentForward.z;
		float weight = 1.0f;
		bool misaligned = false;
		if ( dot + 0.029999999f >= 1.0f )
		{
			const float bend = ( 1.0f - dot ) * 33.333336f * 0.5f;
			const float candidate = ( 1.0f - bend ) * 1.15f;
			weight = candidate <= 1.0f ? candidate : 1.0f;
		}
		else misaligned = true;
		const float inverseWeight = 1.0f - weight;
		const Vector &renderOrigin = GetRenderOrigin();
		const Vector &viewOffset = C_BaseEntity::GetViewOffset();
		Vector start;
		start.x = ( viewOffset.x + renderOrigin.x ) * weight + inverseWeight * attachmentOrigin.x;
		start.y = ( viewOffset.y + renderOrigin.y ) * weight + inverseWeight * attachmentOrigin.y;
		start.z = ( viewOffset.z + renderOrigin.z ) * weight + inverseWeight * attachmentOrigin.z;
		Vector end;
		end.x = ( eyeForward.x * weight + inverseWeight * attachmentForward.x ) * 200000.0f + start.x;
		end.y = ( eyeForward.y * weight + inverseWeight * attachmentForward.y ) * 200000.0f + start.y;
		end.z = ( eyeForward.z * weight + inverseWeight * attachmentForward.z ) * 200000.0f + start.z;

		// MODULE-global native clock/weight; no per-actor cache or once/frame gate.
		static float lastRandomTime = gpGlobals->curtime;
		static float jitterWeight = 0.0f;
		const float compareTime = gpGlobals->curtime;
		if ( L4D2_LaserJitterShouldDecay( compareTime, lastRandomTime,
			gpGlobals->curtime, m_terrorFireInterval.GetStartTime() ) )
			jitterWeight = ( 1.0f - gpGlobals->frametime ) * jitterWeight;
		else
		{
			lastRandomTime = compareTime;
			jitterWeight = RandomFloat( 0.0f, 2.0f );
		}
		end.x = ( eyeForward.x * 200000.0f + start.x - end.x ) * jitterWeight + end.x;
		end.y = ( eyeForward.y * 200000.0f + start.y - end.y ) * jitterWeight + end.y;
		end.z = jitterWeight * 0.0f + end.z; // Preserve native NaN/signed-zero arithmetic.
		const float spread = L4D2_LaserSpreadFloat( gun->GetCurrentSpread() );
		const Vector &ownerVelocity = gun->GetTerrorPlayerOwner()->GetLocalVelocity(); // Native no NULL guard.
		const float ownerSpeed = TerrorLaserSqrt( ( ownerVelocity.x * ownerVelocity.x +
			ownerVelocity.y * ownerVelocity.y ) + ownerVelocity.z * ownerVelocity.z );
		const float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
		const float distance = TerrorLaserSqrt( ( dx * dx + dy * dy ) + dz * dz );
		L4D2_ApplyLaserSightWobble( &end, ownerSpeed, spread, gpGlobals->curtime, distance );

		Vector nearPoint;
		nearPoint.x = attachmentForward.x * 32.0f + formattedStart.x;
		nearPoint.y = attachmentForward.y * 32.0f + formattedStart.y;
		nearPoint.z = attachmentForward.z * 32.0f + formattedStart.z;
		m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 3, nearPoint );
		trace_t trace;
		UTIL_TraceLine( start, end, 0x46004003, this, COLLISION_GROUP_NONE, &trace );
		Vector belowEye = EyePosition();
		belowEye.z -= 32.0f;
		Vector aboveEye = EyePosition(); // Genuine SECOND fresh virtual query.
		aboveEye.z += 32.0f;
		trace_t vertical;
		UTIL_TraceLine( belowEye, aboveEye, 0x0000400B, this, COLLISION_GROUP_NONE, &vertical );
		const Vector &velocity = GetCachedAbsVelocity(); // Native raw256, NO CalcAbsoluteVelocity side effect.
		const float horizontalSpeed = TerrorLaserSqrt( velocity.y * velocity.y + velocity.x * velocity.x );
		if ( vertical.fraction != 1.0f && vertical.plane.normal.z < 0.85000002f &&
			horizontalSpeed > 157.5f && trace.fraction * 200000.0f < 196.0f )
		{
			m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 2, formattedStart );
			return;
		}
		if ( trace.startsolid )
		{
			misaligned = true;
			m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 3, formattedStart );
		}
		if ( trace.DidHit() && !( trace.surface.flags & SURF_SKY ) && !misaligned )
			m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 2, trace.endpos );
		else
			m_hFirstPersonLaserSightParticles[user]->SetControlPoint( 2, formattedStart );
		return;
	}
	{
		if ( m_hFirstPersonLaserSightParticles[user] )
		{
			ParticleProp()->StopEmission( m_hFirstPersonLaserSightParticles[user] );
			m_hFirstPersonLaserSightParticles[user] = NULL;
		}
		if ( !r_draw_lasersight_3rd_person.GetBool() ) goto cleanup;
		C_BaseCombatWeapon *active = GetActiveWeapon();
		CTerrorGun *gun = active ? active->GetTerrorGun() : NULL; // Same actual1228 provider as FP.
		if ( !gun || !gun->ShouldDrawWeapon() || gun->m_bInReload || gun->m_bSwingInProgress ||
			gun->GetSlot() != 0 || !gun->HasUpgrade( 2 ) ) goto cleanup;
		gun->EnsureCorrectRenderingModel();
		const int attachment = gun->m_iLaserSightAttachment; // AFTER model callback here, unlike25ED60.
		if ( attachment <= 0 ) goto cleanup;
		Vector origin;
		QAngle angles;
		gun->GetAttachment( attachment, origin, angles ); // Return ignored natively.
		Vector forward, right, up;
		AngleVectors( angles, &forward, &right, &up );
		Vector end;
		end.x = origin.x + up.x * 10000.0f;
		end.y = origin.y + up.y * 10000.0f;
		end.z = origin.z + up.z * 10000.0f;
		trace_t trace;
		UTIL_TraceLine( origin, end, 0x46004003, this, COLLISION_GROUP_NONE, &trace );
		if ( !m_hThirdPersonLaserSightParticles[user] )
		{
			m_hThirdPersonLaserSightParticles[user] = ParticleProp()->Create(
				"weapon_laser_sight", PATTACH_POINT_FOLLOW, attachment );
			ParticleProp()->AddControlPoint( m_hThirdPersonLaserSightParticles[user],
				1, this, PATTACH_CUSTOMORIGIN );
			ParticleProp()->AddControlPoint( m_hThirdPersonLaserSightParticles[user],
				2, this, PATTACH_CUSTOMORIGIN );
			m_hThirdPersonLaserSightParticles[user]->SetControlPoint( 1, trace.startpos );
			m_hThirdPersonLaserSightParticles[user]->SetControlPoint( 2, trace.endpos );
			m_hThirdPersonLaserSightParticles[user]->SetControlPointForwardVector( 1, up );
			m_hThirdPersonLaserSightParticles[user]->SetDrawOnlyForLocalUser( user );
		}
		if ( m_hThirdPersonLaserSightParticles[user] )
		{
			// Native repeats these updates on the newly-created path too.
			m_hThirdPersonLaserSightParticles[user]->SetControlPoint( 1, trace.startpos );
			m_hThirdPersonLaserSightParticles[user]->SetControlPoint( 2, trace.endpos );
			m_hThirdPersonLaserSightParticles[user]->SetControlPointForwardVector( 1, up );
			Vector surfaceRight, surfaceUp;
			VectorVectors( trace.plane.normal, surfaceRight, surfaceUp );
			surfaceRight.Negate();
			m_hThirdPersonLaserSightParticles[user]->SetControlPointOrientation(
				2, surfaceRight, surfaceUp, trace.plane.normal );
		}
		return;
	}
cleanup:
	ClearTerrorLaserSightEffects( user ); // B sole25DCE0 NORMAL StopEmission, not flashlight immediate cleanup.
}

static ConVar r_draw_flashlight_3rd_person( "r_draw_flashlight_3rd_person", "1", 0x0000000A, "Draw flashlight beams for other players" ); // C10510580 raw0 ->2.
static ConVar r_flashlight_3rd_person_range( "r_flashlight_3rd_person_range", "60", 0x0000000A, "Distance to draw flashlight beams for other players" ); // C105105B0 raw0 ->2.

void C_TerrorPlayer::UpdateFlashlightParticles()
{
	const int user = 0;
	if ( C_BasePlayer::GetLocalPlayer() == this )
	{
		if ( IsEffectActive( EF_DIMLIGHT ) && GetRenderTeamNumber() == TEAM_SURVIVOR )
		{
			C_BaseViewModel *viewModel = GetViewModel( 0 );
			CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
			if ( !weapon || !viewModel || !viewModel->ShouldDraw() || !weapon->IsWeaponVisible() )
			{
				ClearFlashlightParticles();
				return;
			}
			const int helpingHandState = weapon->m_helpingHandState;
			if ( helpingHandState == 2 || helpingHandState == 3 )
			{
				ClearFlashlightParticles();
				return;
			}
			const int attachment = viewModel->LookupAttachment( "flashlight" );
			if ( attachment <= 0 )
			{
				ClearFlashlightParticles();
				return;
			}
			if ( !m_hFirstPersonFlashlightParticles[user] )
			{
				// ESI is the ACTOR throughout the native create; this is not the
				// weapon/viewmodel property despite the viewmodel attachment index.
				m_hFirstPersonFlashlightParticles[user] = ParticleProp()->Create(
					"flashlight_firstperson", PATTACH_POINT_FOLLOW, attachment );
				m_hFirstPersonFlashlightParticles[user]->SetDrawOnlyForLocalUser( user );
				ParticleProp()->AddControlPoint( m_hFirstPersonFlashlightParticles[user],
					1, this, PATTACH_CUSTOMORIGIN );
				ParticleProp()->AddControlPoint( m_hFirstPersonFlashlightParticles[user],
					2, this, PATTACH_CUSTOMORIGIN );
				ParticleProp()->AddControlPoint( m_hFirstPersonFlashlightParticles[user],
					3, ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ), PATTACH_ABSORIGIN_FOLLOW );
			}
			Vector start = m_vecFlashlightOrigin;
			Vector end;
			end.x = m_vecFlashlightForward.x * 32.0f + start.x;
			end.y = m_vecFlashlightForward.y * 32.0f + start.y;
			end.z = m_vecFlashlightForward.z * 32.0f + start.z;
			::FormatViewModelAttachment( start, true );
			::FormatViewModelAttachment( end, true );
			trace_t trace;
			UTIL_TraceLine( start, end, 0x06004003, this, COLLISION_GROUP_NONE, &trace );
			m_hFirstPersonFlashlightParticles[user]->SetControlPoint( 1, trace.startpos );
			m_hFirstPersonFlashlightParticles[user]->SetControlPoint( 2, trace.endpos );
			return;
		}
		// Native local inactive path stops ONLY first-person; the third-person
		// reference is not routed through the all-array cleanup helper here.
		if ( m_hFirstPersonFlashlightParticles[user] )
		{
			ParticleProp()->StopEmissionAndDestroyImmediately( m_hFirstPersonFlashlightParticles[user] );
			m_hFirstPersonFlashlightParticles[user] = NULL;
		}
		return;
	}

	C_BasePlayer *observer = C_BasePlayer::GetLocalPlayer();
	if ( observer && observer->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = observer->GetObserverTarget();
		if ( target && target->IsPlayer() ) observer = ToBasePlayer( target );
	}
	if ( observer == this )
	{
		RemoveTerrorTeammateFlashlight( entindex() );
		ClearFlashlightParticles();
		return;
	}
	if ( !IsEffectActive( EF_DIMLIGHT ) || !r_draw_flashlight_3rd_person.GetBool() )
	{
		RemoveTerrorTeammateFlashlight( entindex() );
		TurnOffFlashlight();
		ClearFlashlightParticles();
		return;
	}
	const int team = GetRenderTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
	{
		RemoveTerrorTeammateFlashlight( entindex() );
		TurnOffFlashlight();
		ClearFlashlightParticles();
		return;
	}
	TurnOffFlashlight(); // Genuine C10067B00 physical-manager path, NOT UpdateVisibility.
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( !weapon || !weapon->ShouldDrawWeapon() )
	{
		RemoveTerrorTeammateFlashlight( entindex() );
		ClearFlashlightParticles();
		return;
	}
	const int attachment = weapon->m_iLaserSightAttachment;
	if ( attachment <= 0 )
	{
		RemoveTerrorTeammateFlashlight( entindex() );
		ClearFlashlightParticles();
		return;
	}
	weapon->EnsureCorrectRenderingModel();
	// Keep the PRE-model-update attachment snapshot even if OnNewModel changes3296.
	Vector start;
	QAngle angles;
	weapon->GetAttachment( attachment, start, angles ); // Native ignores the bool.
	Vector forward, right, up;
	AngleVectors( angles, &forward, &right, &up );
	const float range = r_flashlight_3rd_person_range.GetFloat();
	Vector end;
	end.x = forward.x * range + start.x;
	end.y = forward.y * range + start.y;
	end.z = forward.z * range + start.z;
	trace_t trace;
	UTIL_TraceLine( start, end, 0x06004003, this, COLLISION_GROUP_NONE, &trace );
	// The native third-person beam's CP1 forward is attachment +Z (below),
	// not the attachment +X used for its short obstruction trace.
	UpdateTerrorTeammateFlashlight( entindex(), start, up, right, -forward );
	if ( !m_hThirdPersonFlashlightParticles[user] )
	{
		m_hThirdPersonFlashlightParticles[user] = ParticleProp()->Create(
			"flashlight_thirdperson", PATTACH_POINT_FOLLOW, attachment );
		ParticleProp()->AddControlPoint( m_hThirdPersonFlashlightParticles[user],
			1, this, PATTACH_CUSTOMORIGIN );
		ParticleProp()->AddControlPoint( m_hThirdPersonFlashlightParticles[user],
			2, this, PATTACH_CUSTOMORIGIN );
		ParticleProp()->AddControlPoint( m_hThirdPersonFlashlightParticles[user],
			3, C_BasePlayer::GetLocalPlayer(), PATTACH_EYES_FOLLOW );
		m_hThirdPersonFlashlightParticles[user]->SetDrawOnlyForLocalUser( user );
	}
	m_hThirdPersonFlashlightParticles[user]->SetControlPoint( 1, start );
	m_hThirdPersonFlashlightParticles[user]->SetControlPoint( 2, trace.endpos );
	m_hThirdPersonFlashlightParticles[user]->SetControlPointForwardVector( 1, up );
}

// BLOCK B. Five real native parameter ConVars. Input0 -> native effective2
// before client module flags; texture inputCHEAT already passes0x84280 mask.
static ConVar r_flashlightinfectedfov( "r_flashlightinfectedfov", "90", 0x0000000A );
static ConVar r_flashlightinfectedfar( "r_flashlightinfectedfar", "750", 0x0000000A );
static ConVar r_flashlightinfectedlinear( "r_flashlightinfectedlinear", "100", 0x0000000A );
static ConVar r_flashlightinfectedtexture( "r_flashlightinfectedtexture", "effects/flashlight001_infected", 0x00004008 );
static ConVar r_flashlightinfectedshadows( "r_flashlightinfectedshadows", "0", 0x0000000A );

const char *C_TerrorPlayer::GetFlashlightTextureName() // Native251DA0.
{
	return GetRenderTeamNumber() == TEAM_INFECTED ? r_flashlightinfectedtexture.GetString() : NULL;
}
float C_TerrorPlayer::GetFlashlightFOV() // Native251DE0.
{
	return GetRenderTeamNumber() == TEAM_INFECTED ? r_flashlightinfectedfov.GetFloat() : 0.0f;
}
float C_TerrorPlayer::GetFlashlightFarZ() // Native251E00.
{
	return GetRenderTeamNumber() == TEAM_INFECTED ? r_flashlightinfectedfar.GetFloat() : 0.0f;
}
float C_TerrorPlayer::GetFlashlightLinearAttenuation() // Native251E20.
{
	return GetRenderTeamNumber() == TEAM_INFECTED ? r_flashlightinfectedlinear.GetFloat() : 0.0f;
}
bool C_TerrorPlayer::CastsFlashlightShadows() // Native2530A0.
{
	return GetRenderTeamNumber() != TEAM_INFECTED || r_flashlightinfectedshadows.GetBool();
}

// Whole native252160: survivor2/4 consume actual7136; other teams delegate
// genuine EC940 default. Native multiply/add order is UP+RIGHT, then FORWARD.
void C_TerrorPlayer::ComputeFlashlightOffset( const Vector &forward, const Vector &right,
	const Vector &up, Vector &offset )
{
	const int team = GetRenderTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
	{
		C_BasePlayer::ComputeFlashlightOffset( forward, right, up, offset );
		return;
	}
	const float offsetForward = m_vecFlashlightOffset.z;
	const float offsetUp = m_vecFlashlightOffset.y;
	const float offsetRight = m_vecFlashlightOffset.x;
	offset.x = ( up.x * offsetUp + right.x * offsetRight ) + forward.x * offsetForward;
	offset.y = ( up.y * offsetUp + right.y * offsetRight ) + forward.y * offsetForward;
	offset.z = ( up.z * offsetUp + right.z * offsetRight ) + forward.z * offsetForward;
}


// C25D260. OnNewParticleEffect is the only acquisition writer for cigarette.
void C_TerrorPlayer::UpdateCigaretteParticle()
{
	if ( m_lifeState == LIFE_ALIVE && GetRenderTeamNumber() == TEAM_SURVIVOR &&
		GetTerrorSurvivorDisplayCharacter( m_survivorCharacter ) == SurvivorCharacter_NamVet &&
		ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ) != this && GetRenderAlphaModulation() == 255 )
	{
		if ( !m_hCigaretteSmoke )
		{
			MDLCACHE_CRITICAL_SECTION();
			DispatchParticleEffect( "cigarette_smoke", PATTACH_POINT_FOLLOW, this, "cigarette", false, -1 );
		}
	}
	else if ( m_hCigaretteSmoke )
	{
		m_hCigaretteSmoke->StopEmission( false, false, false );
		m_hCigaretteSmoke->SetRemoveFlag();
		m_hCigaretteSmoke = NULL;
	}
}

// C25D330. Preserve the pointer snapshot BEFORE the virtual origin query.
void C_TerrorPlayer::UpdateSmokerSporeTrail()
{
	if ( m_lifeState != LIFE_ALIVE || GetRenderTeamNumber() != TEAM_INFECTED ||
		GetZombieClass() != 1 || IsGhost() )
	{
		if ( m_hSmokerSporeTrail )
		{
			m_hSmokerSporeTrail->StopEmission( false, false, false );
			m_hSmokerSporeTrail->SetRemoveFlag();
			m_hSmokerSporeTrail = NULL;
		}
	}
	else if ( m_hSmokerSporeTrail )
	{
		m_hSmokerSporeTrail->SetNeedsBBoxUpdate( true );
		CNewParticleEffect *effect = m_hSmokerSporeTrail;
		const Vector &origin = GetAbsOrigin();
		effect->SetSortOrigin( origin );
	}
	else
	{
		MDLCACHE_CRITICAL_SECTION();
		m_hSmokerSporeTrail = ParticleProp()->Create( "smoker_spore_trail", PATTACH_ABSORIGIN_FOLLOW );
		if ( m_hSmokerSporeTrail )
		{
			ParticleProp()->AddControlPoint( m_hSmokerSporeTrail, 1, this, PATTACH_ABSORIGIN_FOLLOW );
			const Vector &firstOrigin = GetAbsOrigin();
			m_hSmokerSporeTrail->SetControlPoint( 0, firstOrigin );
			const Vector &secondOrigin = GetAbsOrigin();
			m_hSmokerSporeTrail->SetControlPoint( 1, secondOrigin );
			m_hSmokerSporeTrail->SetControlPointEntity( 0, this );
			m_hSmokerSporeTrail->SetControlPointEntity( 1, this );
		}
	}
}

// C25D550. Native SetControlPointEntity is deliberately unconditional on create.
void C_TerrorPlayer::UpdateSpitterSlimeTrail()
{
	if ( m_lifeState != LIFE_ALIVE || GetRenderTeamNumber() != TEAM_INFECTED ||
		GetZombieClass() != 4 || IsGhost() ||
		ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ) == this || GetRenderAlphaModulation() != 255 )
	{
		if ( m_hSpitterSlimeTrail )
		{
			m_hSpitterSlimeTrail->StopEmission( false, true, true );
			m_hSpitterSlimeTrail->SetRemoveFlag();
			m_hSpitterSlimeTrail = NULL;
		}
	}
	else if ( !m_hSpitterSlimeTrail )
	{
		MDLCACHE_CRITICAL_SECTION();
		m_hSpitterSlimeTrail = ParticleProp()->Create( "spitter_slime_trail", PATTACH_POINT_FOLLOW, "mouth" );
		m_hSpitterSlimeTrail->SetControlPointEntity( 0, this );
	}
}

// C25DB70. Normal stop only: unlike the trail paths there is NO SetRemoveFlag.
void C_TerrorPlayer::UpdateSmokerSporeAttack()
{
	bool eligible = false;
	if ( m_lifeState == LIFE_ALIVE )
	{
		const int team = GetRenderTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			IsTongueVictim() && m_reachedTongueOwner )
			eligible = true;
	}
	if ( eligible )
	{
		if ( !m_hSmokerSporeAttack )
		{
			MDLCACHE_CRITICAL_SECTION();
			m_hSmokerSporeAttack = ParticleProp()->Create( "smoker_spore_attack", PATTACH_POINT_FOLLOW, "mouth" );
			CNewParticleEffect *effect = m_hSmokerSporeAttack;
			if ( effect )
			{
				C_BaseEntity *owner = m_tongueOwner.Get();
				ParticleProp()->AddControlPoint( effect, 1, owner, PATTACH_POINT_FOLLOW, "smoker_mouth" );
			}
		}
	}
	else if ( m_hSmokerSporeAttack )
	{
		m_hSmokerSporeAttack->StopEmission( false, false, false );
		m_hSmokerSporeAttack = NULL;
	}
}

// C252FC0. A rejected pulse is consumed too, including dead/non-viewed actors.
void C_TerrorPlayer::UpdateSmokerScreenPulse()
{
	if ( m_lifeState == LIFE_ALIVE )
	{
		const int team = GetRenderTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			m_salivaStart != 0.0f && IsCurrentInEyeViewPlayer() )
		{
			const int user = GetSplitScreenPlayerSlot();
			DispatchParticleEffect( "smoker_screen_effect", PATTACH_CUSTOMORIGIN, this, -1, false, user );
		}
	}
	m_salivaStart = 0.0f;
}

// C253030. Uses actual local identity, NOT the broader observed-player selector.
void C_TerrorPlayer::UpdateBashedScreenPulse()
{
	if ( m_lifeState == LIFE_ALIVE && GetRenderTeamNumber() == TEAM_INFECTED &&
		m_bashedStart != 0.0f && IsLocalPlayer() )
	{
		const int user = GetSplitScreenPlayerSlot();
		DispatchParticleEffect( "screen_bashed", PATTACH_CUSTOMORIGIN, this, -1, false, user );
	}
	m_bashedStart = 0.0f;
}

// C25D100. No added life/team test; the actual effect-entity handle is the gate.
void C_TerrorPlayer::UpdateBurningCharacterScreen()
{
	if ( IsCurrentInEyeViewPlayer() && GetEffectEntity() != NULL )
	{
		if ( !m_hBurningCharacterScreen )
		{
			MDLCACHE_CRITICAL_SECTION();
			const int user = GetSplitScreenPlayerSlot();
			DispatchParticleEffect( "burning_character_screen", PATTACH_CUSTOMORIGIN, this, -1, false, user );
		}
	}
	else if ( m_hBurningCharacterScreen )
	{
		m_hBurningCharacterScreen->StopEmission( false, false, false );
		m_hBurningCharacterScreen = NULL;
	}
}

// C25D1C0; real M4 adrenaline predicate/received state is the sole provider.
void C_TerrorPlayer::UpdateAdrenalineScreen()
{
	if ( IsCurrentInEyeViewPlayer() && IsAdrenalineActive() )
	{
		if ( !m_hAdrenalineScreen )
		{
			MDLCACHE_CRITICAL_SECTION();
			const int user = GetSplitScreenPlayerSlot();
			DispatchParticleEffect( "screen_adrenaline", PATTACH_CUSTOMORIGIN, this, -1, false, user );
		}
	}
	else if ( m_hAdrenalineScreen )
	{
		m_hAdrenalineScreen->StopEmission( false, false, false );
		m_hAdrenalineScreen = NULL;
	}
}

// Whole C261360, primary964. The base is QUALIFIED, not a second virtual call.
void C_TerrorPlayer::UpdateClientSideParticleEffects()
{
	C_BaseCombatCharacter::UpdateClientSideParticleEffects();
	UpdateCigaretteParticle();
	UpdateLineOfScrimmagePresentation(); // SECOND actor leaf, after cigarette.
	UpdateSmokerSporeTrail();
	UpdateSpitterSlimeTrail();
	UpdateVomitParticleEffects(); // Sole actual M4 C25D6B0 body, exactly once.
	UpdateSmokerSporeAttack();
	UpdateSmokerScreenPulse();
	UpdateBashedScreenPulse();
	UpdateBurningCharacterScreen();
	UpdateAdrenalineScreen();
	if ( m_bBandageEffectActive && IsCurrentInEyeViewPlayer() &&
		!IsUseActionTarget( 1 ) && !IsPerformingSelfUseAction( 1 ) )
	{
		const int user = GetSplitScreenPlayerSlot();
		ParticleProp()->StopParticlesNamed( "bandage_01", true, user );
		m_bBandageEffectActive = false;
	}
	// M7 owns the query/create THEN life/team/dormancy cleanup suffix.
	UpdateSpeechVoicePresentation();
}

extern ConVar r_flashlight_attach_to_viewmodel;
extern ConVar r_flashlightoffsetright;
extern ConVar r_flashlightoffsetup;
extern ConVar r_flashlightoffsetforward;
extern ConVar r_flashlightoffsetright_low;
extern ConVar r_flashlightoffsetup_low;
extern ConVar r_flashlightoffsetforward_low;
extern ConVar r_visualizetraces;

bool C_TerrorPlayer::Simulate()
{
	UpdateLowPerformanceReports();

	// Fresh resolutions at each native test; no combined pin-state snapshot.
	if ( ( m_jockeyAttacker.Get() || m_jockeyVictim.Get() ) && IsLocalPlayer() &&
		!m_bJockeyPredictionSuppressed )
	{
		m_bJockeyPredictionSuppressed = true;
		ShutdownPredictable();
	}
	if ( !m_jockeyAttacker.Get() && !m_jockeyVictim.Get() && IsLocalPlayer() &&
		m_bJockeyPredictionSuppressed )
	{
		m_bJockeyPredictionSuppressed = false;
		// Native virtual492 is retained. Main's actual-user0 projection selects
		// the existing SDK singleton prediction pool/no-argument real backend.
		(void)GetPredictionOwner();
		InitPredictable();
		SetParent( NULL ); // C1679D0 is REPARENTING, not CPrediction restart.
	}
	UpdateJockeyAttachmentView(); // Sole35F7 deadline10064 writer/cvar owner.
	UpdateRescueAnimation();
	UpdateHunterPounceSound();
	UpdateTankAttachmentImpacts();

	C_FuncElevator *elevator = static_cast< C_FuncElevator * >( GetElevator() );
	if ( elevator && elevator->IsMoving() )
		DisableJiggleBones();
	else
		EnableJiggleBones();

	if ( IsLocalPlayer() )
	{
		const int oldWeaponID = m_iLastSimulatedWeaponID;
		CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
		m_iLastSimulatedWeaponID = weapon ? weapon->GetWeaponID() : 0;
		if ( m_iLastSimulatedWeaponID != oldWeaponID )
			m_weaponChangeInterval.Start();
	}

	// Native100B22F0/100A8500 is the engine locally-resolvable/TLS guard.
	// Main authorizes actual user0 and omits that unsupported save/set/restore
	// family. Do not replace it with a model/bone lock or synthetic slot state.
	UpdateLaserSightParticles();
	if ( IsCurrentInEyeViewPlayer() )
	{
		Vector origin, forward, right, up;
		const Vector &initialRenderOrigin = GetRenderOrigin();
		origin.x = GetViewOffset().x + initialRenderOrigin.x;
		origin.y = initialRenderOrigin.y + GetViewOffset().y;
		origin.z = initialRenderOrigin.z + GetViewOffset().z;
		const int team = GetRenderTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			r_flashlight_attach_to_viewmodel.GetInt() != 0 )
		{
			C_BaseViewModel *viewModel = GetViewModel( 0 );
			C_BaseCombatWeapon *weapon = GetActiveWeapon();
			if ( !weapon || !viewModel )
			{
				// Missing VM/weapon does NOT reset the7136 offset in the native path.
				EyeVectors( &forward, &right, &up );
			}
			else
			{
				m_vecFlashlightOffset = vec3_origin;
				int attachment = -1;
				bool fallback;
				if ( static_cast< unsigned int >( weapon->GetSlot() ) > 1u )
					fallback = true;
				else
				{
					attachment = viewModel->LookupAttachment( "flashlight" );
					fallback = GetMoveType() == MOVETYPE_LADDER;
				}
				if ( !fallback && attachment != -1 )
				{
					QAngle attachmentAngles;
					viewModel->GetAttachment( attachment, origin, attachmentAngles );
					AngleVectors( attachmentAngles, &forward, &right, &up );
				}
				else
				{
					if ( fallback )
					{
						EyeVectors( &forward, &right, &up );
						(void)GetRenderOrigin(); // Deliberately discarded native callback.
						m_vecFlashlightOffset.x += 5.0f;
						m_vecFlashlightOffset.y -= 5.0f;
					}
					EyeVectors( &forward, &right, &up );
					const Vector &renderOrigin = GetRenderOrigin();
					origin.x = GetViewOffset().x + renderOrigin.x;
					origin.y = renderOrigin.y + GetViewOffset().y;
					origin.z = renderOrigin.z + GetViewOffset().z;
				}
			}
			m_vecFlashlightOrigin = origin;
			m_vecFlashlightForward = forward;
			m_vecFlashlightRight = right;
			m_vecFlashlightUp = up;
		}
		else
		{
			// Original resets high BEFORE blending; this is not temporal easing.
			m_vecFlashlightOffset.x = r_flashlightoffsetright.GetFloat();
			m_vecFlashlightOffset.y = r_flashlightoffsetup.GetFloat();
			m_vecFlashlightOffset.z = r_flashlightoffsetforward.GetFloat();
			EyeVectors( &forward, &right, &up );
			const Vector &renderOrigin = GetRenderOrigin();
			const float baseX = GetViewOffset().x + renderOrigin.x;
			const float baseY = renderOrigin.y + GetViewOffset().y;
			const float baseZ = renderOrigin.z + GetViewOffset().z;
			const float lowRight = r_flashlightoffsetright_low.GetFloat();
			const float highRight = r_flashlightoffsetright.GetFloat();
			const float highUp = r_flashlightoffsetup.GetFloat();
			const float highForward = r_flashlightoffsetforward.GetFloat();
			const float lowForward = r_flashlightoffsetforward_low.GetFloat();
			const float lowUp = r_flashlightoffsetup_low.GetFloat();
			origin.x = ( ( right.x * lowRight + baseX ) + up.x * lowUp ) + forward.x * lowForward;
			origin.y = ( ( right.y * lowRight + baseY ) + up.y * lowUp ) + forward.y * lowForward;
			origin.z = ( ( right.z * lowRight + baseZ ) + up.z * lowUp ) + forward.z * lowForward;
			m_vecFlashlightOrigin.x = baseX;
			m_vecFlashlightOrigin.y = baseY;
			m_vecFlashlightOrigin.z = baseZ;
			m_vecFlashlightForward = forward;
			m_vecFlashlightRight = right;
			m_vecFlashlightUp = up;

			C_BaseViewModel *viewModel = GetViewModel( 0 );
			CTraceFilterSkipTwoEntities filter( this, viewModel, COLLISION_GROUP_NONE );
			Vector end;
			end.x = forward.x * 64.0f + origin.x;
			end.y = forward.y * 64.0f + origin.y;
			end.z = forward.z * 64.0f + origin.z;
			Ray_t ray;
			ray.Init( origin, end, Vector( -4.0f, -4.0f, -4.0f ), Vector( 4.0f, 4.0f, 4.0f ) );
			trace_t trace;
			enginetrace->TraceRay( ray, 0x02004083u, &filter, &trace );
			if ( r_visualizetraces.GetInt() != 0 )
				DebugDrawLine( trace.startpos, trace.endpos, 255, 255, 0, true, -1.0f );

			float amount, keep, xProduct, yProduct, zProduct;
			if ( trace.fraction < 1.0f || trace.allsolid || trace.startsolid )
			{
				amount = 0.10000002384185791015625f;
				keep = 0.89999997615814208984375f;
				xProduct = highRight * amount;
				yProduct = highUp * amount;
				zProduct = highForward * amount;
			}
			else
			{
				amount = 0.050000011920928955078125f;
				keep = 0.949999988079071044921875f;
				xProduct = lowRight * amount;
				yProduct = lowUp * amount;
				zProduct = lowForward * amount;
			}
			const float oldX = m_vecFlashlightOffset.x;
			const float oldY = m_vecFlashlightOffset.y;
			m_vecFlashlightOffset.z = m_vecFlashlightOffset.z * keep + zProduct;
			m_vecFlashlightOffset.y = oldY * keep + yProduct;
			m_vecFlashlightOffset.x = oldX * keep + xProduct;
		}
	}

	m_flCurrentEyeHeight = 0.0f;
	if ( m_iEyeAttachment > 0 )
	{
		Vector attachment;
		GetAttachment( m_iEyeAttachment, attachment );
		const Vector &renderOrigin = GetRenderOrigin();
		m_flCurrentEyeHeight = attachment.z - renderOrigin.z;
	}
	else
	{
		const Vector eye = EyePosition();
		const float height = eye.z;
		const Vector &origin = GetAbsOrigin();
		m_flCurrentEyeHeight = height - origin.z;
	}
	if ( !IsLocalPlayer() || !cl_predict->GetBool() )
		UpdateClientMotionTimers();
	UpdateFlashlightParticles();
	UpdateSimulationDLight();
	BaseClass::Simulate();
	return true; // Native10260F82 MOV AL,1, after the discarded CS result.
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

ConVar z_jockey_attach_delay( "z_jockey_attach_delay", "0.2", 0x00004008,
	"Time between hitting a player and visually attaching" );

// Actual CLIENT raw deadline live writer C2604A0..C2604FF in Simulate25FE90.
// Its integration callsite is mandatory; not a movement-created snapshot.
void C_TerrorPlayer::UpdateJockeyAttachmentView()
{
	if ( m_jockeyAttacker.Get() )
	{
		if ( m_jockeyAttachmentViewTime == 0.0f )
			m_jockeyAttachmentViewTime = z_jockey_attach_delay.GetFloat() + gpGlobals->curtime;
	}
	else
		m_jockeyAttachmentViewTime = 0.0f;
}

// C02CEA0/C02EF90. Existing server owners remain in Hunter/Jockey source.
// Reuse received handles; these are native queries, not synthetic pin state.
bool C_TerrorPlayer::IsPouncing() const
{
	return m_pounceVictim.Get() != NULL;
}

bool C_TerrorPlayer::IsJockeyRiding() const
{
	return m_jockeyVictim.Get() != NULL;
}

// C252790 is the genuine Terror no-op client-footstep callback.
void C_TerrorPlayer::OnClientFootstepEvent()
{
}

// Insert into the existing c_terrorplayer.cpp owner; this is not another TU.
// C255CB0/255EB0/2565E0, reached C253620/253670/2536C0 and fly commands.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern "C" double L4D2_GlowNormalize( Vector *direction );
extern "C" void L4D2_ObserverAngleQuaternion( const RadianEuler *angles, Quaternion *out );
extern "C" void L4D2_ObserverNormalizeQuaternion( Quaternion *q );
extern "C" void L4D2_ObserverMatrixAngles( const matrix3x4_t *matrix, QAngle *angles );
extern ConVar spec_chasedistance;
static ConVar spec_cinematic( "spec_cinematic", "0", 0x00000288 );
static ConVar spec_lockedchasecam( "spec_lockedchasecam", "1", 0x0000000A,
	"Spectator chase camera centers behind the spectated player" );
static ConVar spec_lockedchasecam_center_speed( "spec_lockedchasecam_center_speed", "1.5", 0x0000000A,
	"Controls rate that camera is centered on the target", true, 0.01f, false, 0.0f );
static ConVar spec_lockedchasecam_yaw( "spec_lockedchasecam_yaw", "15", 0x0000000A );
static ConVar spec_lockedchasecam_pitch( "spec_lockedchasecam_pitch", "15", 0x0000000A );
static ConVar spec_lockedchasecam_delay( "spec_lockedchasecam_delay", "3", 0x0000000A,
	"Locked chasecam doesn't auto-center this many seconds after mouse movement" );
static ConVar spec_closeup_range( "spec_closeup_range", "10", 0x0000000A,
	"Range at which we framing the head of our subject" );
static ConVar spec_longshot_range( "spec_longshot_range", "75", 0x0000000A,
	"Range at which we framing the whole body of our subject" );
static ConVar spec_max_tracking_error( "spec_max_tracking_error", "1000", 0x0000000A );
static ConVar spec_tracking_power( "spec_tracking_power", "5", 0x0000000A );
static ConVar fly_speed( "fly_speed", "200", 0x0000000A );

namespace
{
// Native elements are six POD floats, 24 bytes, with no element constructors.
struct ObserverFlyKeyframe
{
	float origin[3];
	float angles[3];
};
// These are module-storage globals, NOT per-player allocation-zero assumptions.
static bool s_bObserverFlyActive;             // C107B9F50, loader zero.
static float s_flObserverFlyFraction;         // C107B9F54, loader zero.
static int s_nObserverFlySegment = -1;        // C106E6B78, initialized data.
static CUtlVector< ObserverFlyKeyframe > s_ObserverFlyKeyframes; // C107BBC60.
static ObserverFlyKeyframe s_ObserverFlyFirstKeyframe;           // C107BBC74.

static void ObserverNormalizeAngles( QAngle &angles ) // C253310.
{
	angles.x = AngleNormalize( angles.x );
	angles.y = AngleNormalize( angles.y );
	angles.z = AngleNormalize( angles.z );
}

static float ObserverFlySegmentLength( const ObserverFlyKeyframe *frame )
{
	const float x = frame[1].origin[0] - frame[0].origin[0];
	const float y = frame[1].origin[1] - frame[0].origin[1];
	const float z = frame[1].origin[2] - frame[0].origin[2];
	return sqrtf( ( y * y + z * z ) + x * x );
}

// C361500: degree conversion has TWO F32 multiplications, then RadianEuler
// (roll,pitch,yaw) ordering. QuaternionSlerp is NOT the reached interpolator.
static void ObserverFlyAngleQuaternion( const float *angles, Quaternion &out )
{
	const float pitch = ( angles[0] * 3.1415927f ) * 0.0055555557f;
	const float yaw = ( angles[1] * 3.1415927f ) * 0.0055555557f;
	const float roll = ( angles[2] * 3.1415927f ) * 0.0055555557f;
	const RadianEuler radians( roll, pitch, yaw );
	L4D2_ObserverAngleQuaternion( &radians, &out );
}

static void ObserverFlyAlignQuaternion( const Quaternion &reference, const Quaternion &value, Quaternion &out )
{
	const float dx = reference.x - value.x, dy = reference.y - value.y;
	const float dz = reference.z - value.z, dw = reference.w - value.w;
	const float sx = reference.x + value.x, sy = reference.y + value.y;
	const float sz = reference.z + value.z, sw = reference.w + value.w;
	if ( ( ( dx * dx + dy * dy ) + dz * dz ) + dw * dw >
		( ( sx * sx + sy * sy ) + sz * sz ) + sw * sw )
	{
		out.x = -value.x; out.y = -value.y; out.z = -value.z; out.w = -value.w;
	}
	else
		out = value;
}

static void ObserverFlyNormalizeQuaternion( Quaternion &q )
{
	L4D2_ObserverNormalizeQuaternion( &q );
}

static void ObserverFlyBlendQuaternion( const Quaternion &p, const Quaternion &q, float t, Quaternion &out ) // C3653F0.
{
	Quaternion aligned;
	ObserverFlyAlignQuaternion( p, q, aligned );
	const float oneMinusT = 1.0f - t;
	out.x = p.x * oneMinusT + aligned.x * t;
	out.y = p.y * oneMinusT + aligned.y * t;
	out.z = aligned.z * t + oneMinusT * p.z;
	out.w = p.w * oneMinusT + aligned.w * t;
	ObserverFlyNormalizeQuaternion( out );
}

static void ObserverFlyHermitePosition( const ObserverFlyKeyframe *p, float t, Vector &out ) // C361FA0.
{
	const float square = t * t;
	const float cube = square * t;
	const float b1 = ( cube * 2.0f - square * 3.0f ) + 1.0f;
	const float b2 = 1.0f - b1;
	const float b3 = ( cube - square * 2.0f ) + t;
	const float b4 = cube - square;
	for ( int i = 0; i < 3; ++i )
	{
		out[i] = p[1].origin[i] * b1;
		out[i] = p[2].origin[i] * b2 + out[i];
		out[i] = ( b3 * ( p[1].origin[i] - p[0].origin[i] ) + out[i] ) +
			b4 * ( p[2].origin[i] - p[1].origin[i] );
	}
}

static void ObserverFlyHermiteQuaternion( const Quaternion &p0, const Quaternion &p1,
	const Quaternion &p2, float t, Quaternion &out ) // C362120.
{
	Quaternion a0, a1;
	ObserverFlyAlignQuaternion( p2, p0, a0 );
	ObserverFlyAlignQuaternion( p2, p1, a1 );
	const float square = t * t;
	const float cube = square * t;
	const float b1 = ( cube * 2.0f - square * 3.0f ) + 1.0f;
	const float b2 = 1.0f - b1;
	const float b3 = ( cube - square * 2.0f ) + t;
	const float b4 = cube - square;
	for ( int i = 0; i < 4; ++i )
		out[i] = ( ( b2 * p2[i] + b1 * a1[i] ) + b3 * ( a1[i] - a0[i] ) ) + b4 * ( p2[i] - a1[i] );
	ObserverFlyNormalizeQuaternion( out );
}

// C364AB0 -> C35F600. Keep matrix construction's F32 order, but the
// horizontal length and atan2/degree products remain wide until output stores.
static void ObserverFlyQuaternionAngles( const Quaternion &q, QAngle &angles )
{
	const float x2 = q.x * 2.0f, y2 = q.y * 2.0f, z2 = q.z * 2.0f, w2 = q.w * 2.0f;
	const float yy = y2 * q.y, zz = z2 * q.z;
	const float m00 = ( 1.0f - yy ) - zz;
	const float xy = x2 * q.y, wz = w2 * q.z;
	const float m10 = xy + wz, m01 = xy - wz;
	const float xz = x2 * q.z, yz = q.z * y2;
	const float oneMinusXX = 1.0f - x2 * q.x;
	const float m11 = oneMinusXX - zz;
	const float xw = q.x * w2, wy = w2 * q.y;
	const float m20 = xz - wy, m21 = xw + yz, m22 = oneMinusXX - yy;
	matrix3x4_t matrix;
	matrix[0][0] = m00; matrix[0][1] = m01; matrix[0][2] = wy + xz; matrix[0][3] = 0.0f;
	matrix[1][0] = m10; matrix[1][1] = m11; matrix[1][2] = yz - xw; matrix[1][3] = 0.0f;
	matrix[2][0] = m20; matrix[2][1] = m21; matrix[2][2] = m22; matrix[2][3] = 0.0f;
	L4D2_ObserverMatrixAngles( &matrix, &angles );
}
}

CON_COMMAND_F( fly_add_keyframe, "" , 0x00000008) // C259E40.
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer( 0 );
	if ( !player )
		return;
	ObserverFlyKeyframe frame;
	const Vector origin = player->EyePosition();
	frame.origin[0] = origin.x; frame.origin[1] = origin.y; frame.origin[2] = origin.z;
	const QAngle angles = player->EyeAngles();
	frame.angles[0] = angles.x; frame.angles[1] = angles.y; frame.angles[2] = angles.z;
	s_ObserverFlyKeyframes.AddToTail( frame );
	NDebugOverlay::Axis( origin, angles, 20.0f, true, 5.0f );
}

CON_COMMAND_F( fly_clear_all_keyframes, "" , 0x00000008) // C258CB0: retain allocation, active/index/fraction.
{
	s_ObserverFlyKeyframes.RemoveAll();
}

CON_COMMAND_F( fly_start, "" , 0x00000008) // C251650: do not reset fraction here.
{
	s_bObserverFlyActive = true;
	s_nObserverFlySegment = -1;
}

CON_COMMAND_F( fly_show_path, "" , 0x00000008) // C254520.
{
	if ( s_ObserverFlyKeyframes.Count() <= 0 )
		return;
	int i = 0;
	for ( ; i < s_ObserverFlyKeyframes.Count() - 1; ++i )
	{
		const ObserverFlyKeyframe *frames = s_ObserverFlyKeyframes.Base();
		NDebugOverlay::Axis( Vector( frames[i].origin[0], frames[i].origin[1], frames[i].origin[2] ),
			QAngle( frames[i].angles[0], frames[i].angles[1], frames[i].angles[2] ), 20.0f, true, 999999.875f );
		frames = s_ObserverFlyKeyframes.Base();
		NDebugOverlay::Line( Vector( frames[i].origin[0], frames[i].origin[1], frames[i].origin[2] ),
			Vector( frames[i + 1].origin[0], frames[i + 1].origin[1], frames[i + 1].origin[2] ), 0, 255, 0, true, 99999.8984375f );
	}
	const ObserverFlyKeyframe *frames = s_ObserverFlyKeyframes.Base();
	NDebugOverlay::Axis( Vector( frames[i].origin[0], frames[i].origin[1], frames[i].origin[2] ),
		QAngle( frames[i].angles[0], frames[i].angles[1], frames[i].angles[2] ), 20.0f, true, 999999.875f );
}

void C_TerrorPlayer::OverrideMouseInput( float *x, float *y ) // C253620, primary1056.
{
	if ( *x != m_flLastObserverMouseX || *y != m_flLastObserverMouseY )
	{
		m_flLastObserverMouseX = *x;
		m_flLastObserverMouseY = *y;
		m_flLastObserverInputTime = gpGlobals->curtime;
	}
}

void C_TerrorPlayer::OverrideJoystickInput( float *pitch, float *yaw ) // C253670, primary1060.
{
	if ( !( fabs( static_cast< double >( *pitch ) ) <= static_cast< double >( 0.001f ) ) ||
		!( fabs( static_cast< double >( *yaw ) ) <= static_cast< double >( 0.001f ) ) )
		m_flLastObserverInputTime = gpGlobals->curtime;
}

Vector C_TerrorPlayer::GetChaseCamViewOffset( C_BaseEntity *target ) // Whole C2536C0.
{
	if ( !target || !target->IsPlayer() )
		return g_pGameRules->GetViewVectors()->m_vView;
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( target );
	Vector mins, maxs;
	target->GetRenderBounds( mins, maxs );
	Vector body;
	body.x = target->GetAbsOrigin().x;
	body.y = target->GetAbsOrigin().y;
	body.z = ( maxs.z - mins.z ) * 0.7f + target->GetAbsOrigin().z;
	Vector head;
	QAngle attachmentAngles;
	C_BaseEntity::PushEnableAbsRecomputations( false );
	if ( player->GetAttachment( "forward", head, attachmentAngles ) && spec_cinematic.GetInt() != 0 )
	{
		head.x = target->GetAbsOrigin().x;
		head.y = target->GetAbsOrigin().y;
	}
	else
	{
		head = target->GetAbsOrigin();
		head.z = target->GetViewOffset().z + head.z;
		if ( ( IsSurvivorTeam( target->GetTeamNumber() ) &&
			( player->m_survivorCharacter == 2 || player->m_survivorCharacter == 6 ) ) ||
			( target->GetTeamNumber() == 3 && player->m_zombieClass == 8 ) )
			head.z = head.z + 5.0f;
	}
	C_BaseEntity::PopEnableAbsRecomputations();
	const float distance = spec_chasedistance.GetFloat();
	Vector point;
	if ( player->m_usingMountedWeapon || spec_closeup_range.GetFloat() > distance )
		point = head;
	else
	{
		const float longshot = spec_longshot_range.GetFloat();
		if ( distance > longshot )
			point = body;
		else
		{
			const double closeup = spec_closeup_range.GetFloat();
			const float blend = static_cast< float >( 1.0 - cos( ( static_cast< double >( distance ) - closeup ) /
				( static_cast< double >( longshot ) - closeup ) * static_cast< double >( 1.5700001f ) ) );
			point.x = ( body.x - head.x ) * blend + head.x;
			point.y = ( body.y - head.y ) * blend + head.y;
			point.z = ( body.z - head.z ) * blend + head.z;
		}
	}
	const Vector origin = target->GetObserverCamOrigin();
	return Vector( point.x - origin.x, point.y - origin.y, point.z - origin.z );
}

void C_TerrorPlayer::CalcChaseCamView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov ) // Whole C255CB0, primary1308.
{
	m_hLastObserverCameraTarget = NULL;
	m_flCurrentThirdPersonCameraDistance = 0.0f;
	if ( spec_lockedchasecam.GetInt() != 0 &&
		gpGlobals->curtime > spec_lockedchasecam_delay.GetFloat() + m_flLastObserverInputTime )
	{
		C_BaseEntity *target = GetObserverTarget();
		if ( target && target->IsPlayer() )
		{
			Vector forward;
			static_cast< C_BasePlayer * >( target )->EyeVectors( &forward, NULL, NULL );
			VectorAngles( forward, eyeAngles );
			eyeAngles.y = AngleNormalize( spec_lockedchasecam_yaw.GetFloat() + eyeAngles.y );
			eyeAngles.x = AngleNormalize( eyeAngles.x + spec_lockedchasecam_pitch.GetFloat() );
			QAngle current;
			engine->GetViewAngles( current );
			QAngle delta( eyeAngles.x - current.x, eyeAngles.y - current.y, eyeAngles.z - current.z );
			ObserverNormalizeAngles( delta );
			const float speed = spec_lockedchasecam_center_speed.GetFloat();
			const float x = delta.x * speed, y = delta.y * speed, z = delta.z * speed;
			const float frameTime = gpGlobals->frametime;
			QAngle centered( x * frameTime + current.x, y * frameTime + current.y, z * frameTime + current.z );
			ObserverNormalizeAngles( centered );
			if ( gpGlobals->curtime > spec_lockedchasecam_delay.GetFloat() + m_flLastObserverInputTime )
				engine->SetViewAngles( centered );
		}
	}
	C_BasePlayer::CalcChaseCamView( eyeOrigin, eyeAngles, fov );
	engine->GetViewAngles( eyeAngles );
	eyeAngles.z = 0.0f;
	engine->SetViewAngles( eyeAngles );
}

void C_TerrorPlayer::CalcRoamingView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov ) // Whole C2565E0, primary1316.
{
	m_hLastObserverCameraTarget = NULL;
	m_flCurrentThirdPersonCameraDistance = 0.0f;
	C_BasePlayer::CalcRoamingView( eyeOrigin, eyeAngles, fov );
	engine->GetViewAngles( eyeAngles );
	eyeAngles.z = 0.0f;
	engine->SetViewAngles( eyeAngles );
}

void C_TerrorPlayer::CalcInEyeCamView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov ) // Whole C255EB0, primary1320.
{
	m_hLastObserverCameraTarget = NULL;
	m_flCurrentThirdPersonCameraDistance = 0.0f;
	if ( s_bObserverFlyActive )
	{
		if ( s_ObserverFlyKeyframes.Count() == 0 )
		{
			DevMsg( "Can't fly - no keyframes\n" );
			s_bObserverFlyActive = false;
			return;
		}
		ObserverFlyKeyframe *frames = s_ObserverFlyKeyframes.Base();
		if ( s_nObserverFlySegment < 0 )
		{
			eyeOrigin.Init( frames[0].origin[0], frames[0].origin[1], frames[0].origin[2] );
			s_nObserverFlySegment = 0;
			s_flObserverFlyFraction = 0.0f;
			eyeAngles.Init( frames[0].angles[0], frames[0].angles[1], frames[0].angles[2] );
			s_ObserverFlyFirstKeyframe = frames[0];
			return;
		}
		const int segment = s_nObserverFlySegment;
		const float length = ObserverFlySegmentLength( frames + segment );
		s_flObserverFlyFraction = ( fly_speed.GetFloat() * 0.01f ) / length + s_flObserverFlyFraction;
		Quaternion rotation;
		if ( segment > 0 )
		{
			ObserverFlyHermitePosition( frames + segment - 1, s_flObserverFlyFraction, eyeOrigin );
			Quaternion previous, current, next;
			ObserverFlyAngleQuaternion( s_ObserverFlyKeyframes.Base()[s_nObserverFlySegment - 1].angles, previous );
			ObserverFlyAngleQuaternion( s_ObserverFlyKeyframes.Base()[s_nObserverFlySegment].angles, current );
			ObserverFlyAngleQuaternion( s_ObserverFlyKeyframes.Base()[s_nObserverFlySegment + 1].angles, next );
			ObserverFlyHermiteQuaternion( previous, current, next, s_flObserverFlyFraction, rotation );
		}
		else
		{
			const float x = frames[segment + 1].origin[0] - frames[segment].origin[0];
			const float y = frames[segment + 1].origin[1] - frames[segment].origin[1];
			const float z = frames[segment + 1].origin[2] - frames[segment].origin[2];
			eyeOrigin.x = frames[segment].origin[0] + x * s_flObserverFlyFraction;
			eyeOrigin.y = frames[segment].origin[1] + y * s_flObserverFlyFraction;
			eyeOrigin.z = frames[segment].origin[2] + z * s_flObserverFlyFraction;
			Quaternion current, next;
			ObserverFlyAngleQuaternion( s_ObserverFlyKeyframes.Base()[s_nObserverFlySegment].angles, current );
			ObserverFlyAngleQuaternion( s_ObserverFlyKeyframes.Base()[s_nObserverFlySegment + 1].angles, next );
			ObserverFlyBlendQuaternion( current, next, s_flObserverFlyFraction, rotation );
		}
		ObserverFlyQuaternionAngles( rotation, eyeAngles );
		if ( s_flObserverFlyFraction > 1.0f )
		{
			++s_nObserverFlySegment;
			if ( s_nObserverFlySegment >= s_ObserverFlyKeyframes.Count() - 1 )
			{
				s_bObserverFlyActive = false;
				return;
			}
			const float nextLength = ObserverFlySegmentLength( s_ObserverFlyKeyframes.Base() + s_nObserverFlySegment );
			s_flObserverFlyFraction = ( ( s_flObserverFlyFraction - 1.0f ) * length ) / nextLength;
		}
		return; // Native fly arm never writes fov or the engine view angles.
	}

	eyeOrigin = EyePosition();
	C_BaseEntity *target = GetObserverTarget();
	if ( target && spec_cinematic.GetInt() != 0 )
	{
		const Vector offset = GetChaseCamViewOffset( target );
		const Vector targetOrigin = target->GetObserverCamOrigin();
		const Vector desired( offset.x + targetOrigin.x, offset.y + targetOrigin.y, offset.z + targetOrigin.z );
		Vector direction( desired.x - m_vecRoamingObserverLookTarget.x,
			desired.y - m_vecRoamingObserverLookTarget.y, desired.z - m_vecRoamingObserverLookTarget.z );
		const double wideLength = L4D2_GlowNormalize( &direction );
		const float length = static_cast< float >( wideLength );
		if ( wideLength > static_cast< double >( spec_max_tracking_error.GetFloat() ) )
			m_vecRoamingObserverLookTarget = desired;
		else
		{
			const float power = spec_tracking_power.GetFloat();
			const float x = ( direction.x * length ) * power;
			const float y = ( direction.y * length ) * power;
			const float z = ( direction.z * length ) * power;
			const float frameTime = gpGlobals->frametime;
			m_vecRoamingObserverLookTarget.x = x * frameTime + m_vecRoamingObserverLookTarget.x;
			m_vecRoamingObserverLookTarget.y = y * frameTime + m_vecRoamingObserverLookTarget.y;
			m_vecRoamingObserverLookTarget.z = z * frameTime + m_vecRoamingObserverLookTarget.z;
		}
		Vector look( m_vecRoamingObserverLookTarget.x - eyeOrigin.x,
			m_vecRoamingObserverLookTarget.y - eyeOrigin.y, m_vecRoamingObserverLookTarget.z - eyeOrigin.z );
		VectorAngles( look, eyeAngles );
		engine->SetViewAngles( eyeAngles );
	}
	else
		eyeAngles = EyeAngles();
	fov = GetFOV();
	engine->GetViewAngles( eyeAngles );
	eyeAngles.z = 0.0f;
	engine->SetViewAngles( eyeAngles );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C26CD30, complete ordering. Keep both independent activation-button calls:
// a press callback may change the result used by the later release test.
void C_TerrorPlayer::ItemPostFrame()
{
	C_BaseAbility *ability = GetCustomAbility();
	if ( ability )
	{
		if ( ability->GetActivationButton() & m_afButtonPressed )
			ability->TryActivate();
		if ( ability->GetActivationButton() & m_afButtonReleased )
			ability->OnButtonReleased();
		ability->Update();
	}
	UpdateClientMotionTimers();
	ProcessFlashlightImpulse();
	C_BasePlayer::ItemPostFrame();
}

// C226EB0 reads the second signed short only after the player gate.
static void __MsgFunc_ReloadEffect( bf_read &msg )
{
	C_BaseEntity *entity = cl_entitylist->GetEnt( msg.ReadShort() );
	if ( !entity || !entity->IsPlayer() ) return;
	static_cast< C_TerrorPlayer * >( entity )->ReloadEffect( msg.ReadShort() );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_TerrorPlayer::ReloadEffect( int originalActivity )
{
	// C226CE0. Local predicted sounds are intentionally not scheduled here.
	if ( this == C_BasePlayer::GetLocalPlayer() ) return;
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( !weapon ) return;
	const CTerrorWeaponInfo &info = weapon->GetTerrorWpnData();
	const char *name = info.GetCharacterViewModel( GetSurvivorCharacterName( static_cast< SurvivorCharacterType >( m_survivorCharacter ) ), weapon->IsDualWielding() );
	const model_t *model = modelinfo->GetModel( modelinfo->GetModelIndex( name ) );
	if ( !model ) return;
	CStudioHdr studioHdr( modelinfo->GetStudiomodel( model ), mdlcache );
	if ( !studioHdr.IsValid() ) return;
	const Activity activity = weapon->TranslateViewModelActivity( TerrorActivityFromOriginal( originalActivity ) );
	for ( int sequence = 0; sequence < studioHdr.GetNumSeq(); ++sequence )
	{
		mstudioseqdesc_t &description = studioHdr.pSeqdesc( sequence );
		if ( description.activity != activity ) continue;
		float poseParameters[MAXSTUDIOPOSEPARAM] = {};
		const float cycleRate = Studio_CPS( &studioHdr, description, sequence, poseParameters );
		for ( int eventIndex = 0; eventIndex < description.numevents; ++eventIndex )
		{
			mstudioevent_t *event = description.pEvent( eventIndex );
			if ( event->event != 5004 ) continue;
			DelayedSound_t sound;
			sound.m_soundName = event->options;
			sound.m_flPlaybackTime = event->cycle / cycleRate + gpGlobals->curtime;
			// C2261D0 is AddToHead: equal deadlines run in reverse order.
			m_DelayedSounds.AddToHead( sound );
		}
		break;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C269740, paired with the existing server inventory policy.
bool C_TerrorPlayer::IsWeaponAllowedForTeam( int weaponID ) const
{
	const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
	if ( !info ) return true;
	int team = GetTeamNumber();
	const int predicateTeam = GetTeamNumber();
	if ( predicateTeam == TEAM_SURVIVOR || predicateTeam == TEAM_L4D1_SURVIVOR )
		team = TEAM_SURVIVOR;
	return info->m_iTeam == TEAM_UNASSIGNED || info->m_iTeam == team;
}

USER_MESSAGE_REGISTER( ReloadEffect );
