//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/c_infected.h"
#include "animation.h"
#include "animationlayer.h"
#include "dt_utlvector_recv.h"
#include "l4d2/effects/terror_materials.h"
#include "cliententitylist.h"
#include "particle_parse.h"

#include "engine/ivmodelrender.h"
#include "activitylist.h"
#include "l4d2/infected/infected_activities.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

ConVar z_speed( "z_speed", "250", 0x00006008 );
static ConVar z_burn_effect_duration( "z_burn_effect_duration", "10.0f", 0x00004008 );
static ConVar z_burn_effect_strength( "z_burn_effect_strength", "0.6f", 0x00004008 );
static ConVar z_infected_decals( "z_infected_decals", "0", 0x0000000A );
ConVar z_infected_shadows( "z_infected_shadows", "1", 0x0000000A );
extern ConVar z_mob_simple_shadows;
extern ConVar NextBotStop;
extern ConVar cl_showanimstate;
static ConVar cl_infected_animinsim( "cl_infected_animinsim", "0", 0x0000000A );
static ConVar z_debug_infected_anim( "z_debug_infected_anim", "0", 0x0000000A );

static void RecvProxy_InfectedLayerSequence( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast<C_InfectedAnimationLayer *>( pStruct )->ReceiveSequence( pData->m_Value.m_Int );
}

static void RecvProxy_InfectedLayerStartTime( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast<C_InfectedAnimationLayer *>( pStruct )->ReceiveStartTime( pData->m_Value.m_Float );
}

static void RecvProxy_InfectedLayerOrder( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast<C_InfectedAnimationLayer *>( pStruct )->ReceiveOrder( pData->m_Value.m_Int );
}

static void RecvProxy_InfectedSequenceStartTime( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast<C_Infected *>( pStruct )->ReceiveSequenceStartTime( pData->m_Value.m_Float );
}

static void ResizeInfectedAnimationLayers( void *pStruct, int offsetToUtlVector, int nCount )
{
	static_cast<C_Infected *>( pStruct )->ResizeClientAnimatedOverlays( nCount );
}

BEGIN_RECV_TABLE_NOBASE( C_InfectedAnimationLayer, DT_InfectedAnimationLayer )
	RecvPropInt( RECVINFO( m_nSequence ), 0, RecvProxy_InfectedLayerSequence ),
	RecvPropFloat( RECVINFO( m_flStartTime ), 0, RecvProxy_InfectedLayerStartTime ),
	RecvPropInt( RECVINFO( m_nOrder ), 0, RecvProxy_InfectedLayerOrder ),
	RecvPropBool( RECVINFO( m_bLooping ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_Infected, DT_Infected, Infected )
	RecvPropUtlVector( RECVINFO_UTLVECTOR_SIZEFN( m_clientAnimatedOverlay, ResizeInfectedAnimationLayers ), 6,
		RecvPropDataTable( NULL, 0, 0, &REFERENCE_RECV_TABLE( DT_InfectedAnimationLayer ) ) ),
	RecvPropInt( RECVINFO( m_fFlags ) ),
	RecvPropInt( RECVINFO( m_nWaterLevel ) ),
	RecvPropFloat( RECVINFO( m_sequenceStartTime ), 0, RecvProxy_InfectedSequenceStartTime ),
	RecvPropEHandle( RECVINFO( m_clientLookatTarget ) ),
	RecvPropFloat( RECVINFO( m_clientLeanYaw ) ),
	RecvPropVectorXY( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropFloat( RECVINFO_NAME( m_vecNetworkOrigin[2], m_vecOrigin[2] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[0], m_angRotation[0] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[1], m_angRotation[1] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[2], m_angRotation[2] ) ),
	RecvPropVector( RECVINFO( m_gibbedLimbForce ) ),
	RecvPropInt( RECVINFO( m_originalBody ) ),
	RecvPropBool( RECVINFO( m_mobRush ) ),
	RecvPropBool( RECVINFO( m_bIsBurning ) ),
	RecvPropInt( RECVINFO( m_iRequestedWound1 ) ),
	RecvPropInt( RECVINFO( m_iRequestedWound2 ) ),
	RecvPropInt( RECVINFO( m_nFallenFlags ) ),
END_RECV_TABLE()

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
C_InfectedAnimationLayer::C_InfectedAnimationLayer()
{
	m_nSequence = 0;
	m_flStartTime = 0.0f;
	m_nOrder = 6;
	m_bLooping = false;
	m_pOwner = NULL;
	m_nIndex = 0;
	m_flNetworkStartTime = 0.0f;
	// Native316B80 omits cached activity8 and dirty33; new-record policy is
	// applied only by ResizeClientAnimatedOverlays, not by this constructor.
}

void C_InfectedAnimationLayer::StartFade( float flDuration )
{
	C_AnimationLayer *pLayer = m_pOwner->GetAnimOverlay( m_nIndex );
	if ( m_nOrder < 6 && pLayer && pLayer->m_flLayerFadeOuttime == 0.0f )
	{
		pLayer->m_flLayerAnimtime = gpGlobals->curtime;
		pLayer->m_flLayerFadeOuttime = flDuration;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif


void C_InfectedAnimationLayer::ReceiveSequence( int nSequence )
{
	if ( m_nSequence == nSequence )
		return;

	if ( nSequence == 0 )
	{
		StartFade( 0.3f );
	}
	else
	{
		m_nSequence = nSequence;
		m_bDirty = true;
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_InfectedAnimationLayer::ReceiveStartTime( float flStartTime )
{
	if ( m_flNetworkStartTime == flStartTime )
		return;

	m_bDirty = true;
	if ( gpGlobals->curtime > m_pOwner->GetLastDormantTime() + 1.0f )
		m_flStartTime = gpGlobals->curtime;
	else
		m_flStartTime = flStartTime;
	m_flNetworkStartTime = flStartTime;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void C_InfectedAnimationLayer::ReceiveOrder( int nOrder )
{
	if ( m_nOrder == nOrder )
		return;

	if ( nOrder < 6 || m_nOrder >= 6 )
	{
		m_nOrder = nOrder;
		m_bDirty = true;
	}
	else
	{
		StartFade( 0.5f );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_InfectedAnimationLayer::ResetOverlay()
{
	C_AnimationLayer *pLayer = m_pOwner->GetAnimOverlay( m_nIndex );
	m_bDirty = false;
	pLayer->Reset();
	pLayer->SetOrder( C_BaseAnimatingOverlay::MAX_OVERLAYS );
}

void C_InfectedAnimationLayer::Update( CStudioHdr *pStudioHdr )
{
	if ( m_nOrder >= 6 )
	{
		ResetOverlay();
		return;
	}

	C_AnimationLayer *pLayer = m_pOwner->GetAnimOverlay( m_nIndex );
	if ( m_bDirty )
	{
		m_bDirty = false;
		m_nActivity = !pStudioHdr || !pStudioHdr->SequencesAvailable() ||
			m_nSequence < 0 || m_nSequence >= pStudioHdr->GetNumSeq() ? 0 :
			::GetSequenceActivity( pStudioHdr, m_nSequence, NULL );
		pLayer->SetSequence( m_nSequence );
		pLayer->m_flPrevCycle = 0.0f;
		pLayer->SetWeight( 1.0f );
		pLayer->SetOrder( m_nOrder );
		pLayer->m_flPlaybackRate = 1.0f;
		pLayer->SetCycle( 0.0f );
		pLayer->m_flLayerAnimtime = 0.0f;
		pLayer->m_flLayerFadeOuttime = 0.0f;
		return;
	}

	if ( m_nSequence < 0 || m_nSequence >= pStudioHdr->GetNumSeq() )
	{
		ResetOverlay();
		return;
	}

	const float flElapsed = gpGlobals->curtime - m_flStartTime;
	const float flRate = m_pOwner->GetSequenceCycleRate( pStudioHdr, m_nSequence );
	const double flWideCycle = static_cast< double >( flRate ) * static_cast< double >( flElapsed );
	float flCycle = static_cast< float >( flWideCycle );
	// Native FST retains the unspilled product for this comparison.
	if ( flWideCycle >= 1.0 )
	{
		if ( !m_bLooping )
		{
			ResetOverlay();
			m_nOrder = C_BaseAnimatingOverlay::MAX_OVERLAYS;
			return;
		}
		const int nWholeCycle = _mm_cvtt_ss2si( _mm_set_ss( flCycle ) );
		flCycle = flCycle - static_cast< float >( nWholeCycle );
	}

	pLayer->SetOrder( m_nIndex );
	pLayer->SetCycle( flCycle );
	float flWeight = 1.0f;
	if ( pLayer->m_flLayerFadeOuttime > 0.0f )
	{
		flWeight = pLayer->GetFadeout( gpGlobals->curtime );
		if ( flWeight <= 0.0f )
		{
			ResetOverlay();
			m_nOrder = C_BaseAnimatingOverlay::MAX_OVERLAYS;
			return;
		}
	}

	mstudioseqdesc_t &sequence = pStudioHdr->pSeqdesc( m_nSequence );
	if ( !( sequence.flags & STUDIO_SNAP ) )
	{
		const double flWideDuration = m_pOwner->SequenceDuration( pStudioHdr, m_nSequence );
		const float flDuration = static_cast< float >( flWideDuration );
		const float flSequenceTime = static_cast< float >( flWideDuration * static_cast< double >( flCycle ) );
		// Native COMISS/JA rejects only ordered elapsed > half duration.
		if ( !m_bLooping || !( gpGlobals->curtime - m_flStartTime > flDuration * 0.5f ) )
		{
			if ( sequence.fadeintime > flSequenceTime )
			{
				pLayer->SetWeight( ( 1.0f - ( sequence.fadeintime - flSequenceTime ) / sequence.fadeintime ) * flWeight );
				return;
			}
			if ( sequence.fadeouttime > flDuration - flSequenceTime )
			{
				pLayer->SetWeight( ( 1.0f - ( sequence.fadeouttime - flDuration + flSequenceTime ) / sequence.fadeouttime ) * flWeight );
				return;
			}
		}
	}
	pLayer->SetWeight( flWeight );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif


InfectedWoundRenderData_t::InfectedWoundRenderData_t()
{
	m_bEnabled = false;
	m_vecPosition.Init();
	for ( int i = 0; i < ARRAYSIZE( m_vecAxis ); ++i )
		m_vecAxis[i].Init();
	m_vecUVMapping.Init();
	m_vecUVScale.Init();
}

InfectedRenderState_t::InfectedRenderState_t()
{
	m_nSkinTint = 0;
	m_nClothingTint = 0;
	m_nBloodMask = 0;
	m_flBurnStartTime = -1.0f;
	m_bIsBurning = false;
	m_iRequestedWound[0] = m_iRequestedWound[1] = -1;
	m_iWound[0] = m_iWound[1] = -1;
	m_nOriginalBody = 0;
	m_nModelType = 0;
}

C_Infected *C_Infected::MyInfectedPointer()
{
	return this;
}

// C314E20: mob quality forces persist; other states do not clear the override.
ShadowType_t C_Infected::ShadowCastType()
{
	if ( m_mobRush )
	{
		const int mode = z_mob_simple_shadows.GetInt();
		if ( mode != 0 && z_infected_shadows.GetInt() != 0 )
		{
			if ( mode == 1 )
				ForceShadowCastType( true, SHADOWS_SIMPLE );
			else if ( mode == 2 )
				ForceShadowCastType( true, SHADOWS_NONE );
		}
	}
	return BaseClass::ShadowCastType();
}

C_Infected::C_Infected() : m_iv_clientLeanYaw( "C_Infected:m_iv_clientLeanYaw" )
{
	AddVar( &m_clientLeanYaw, &m_iv_clientLeanYaw, LATCH_SIMULATION_VAR );
	m_nNextGib = 0;
	m_gibbedLimbForce.Init();
	SetClassname( "infected" );
	m_mobRush = false;
	m_bIsBurning = false;
	m_bSequenceStartTimeChanged = false;
	m_flLastDormantTime = 0.0f;
	m_RenderState.m_nSkinTint = RandomInt( 0, 7 );
	m_RenderState.m_nClothingTint = RandomInt( 8, 15 );
	m_RenderState.m_nBloodMask = RandomInt( 0, 3 );
	m_iRequestedWound1 = -1;
	m_iRequestedWound2 = -1;
	m_RenderState.m_iWound[0] = -1;
	m_RenderState.m_iWound[1] = -1;
	m_nFallenFlags = 0;
	for ( int i = 0; i < ARRAYSIZE( m_pUncommonItem ); ++i )
	{
		m_pUncommonItem[i] = NULL;
		m_pUncommonPocket[i] = NULL;
	}
	m_bWoundDecalsDirty = false;
	m_iForwardAttachment = -1;
	m_iMoveX = -1;
	m_iMoveY = -1;
	m_iBodyYaw = -1;
	m_iBodyPitch = -1;
	m_iLeanYaw = -1;
	m_iLeanPitch = -1;
	if ( !z_infected_shadows.GetBool() )
		ForceShadowCastType( true, SHADOWS_NONE );
	m_hWoundModel[0] = NULL;
	m_hWoundModel[1] = NULL;
	m_RenderState.m_Wound[0].m_bEnabled = false;
	m_RenderState.m_Wound[1].m_bEnabled = false;
	m_pFaceplate = NULL;
	m_bRagdollTransferred = false;
	m_allowWounds = true;
}

C_Infected::~C_Infected()
{
	ReleaseWoundModels();
	ReleaseUncommonModels();
}

void C_Infected::ResizeClientAnimatedOverlays( int nCount )
{
	const int oldCount = m_clientAnimatedOverlay.Count();
	m_clientAnimatedOverlay.SetCount( nCount );
	SetNumAnimOverlays( nCount );
	for ( int i = 0; i < nCount; ++i )
	{
		m_clientAnimatedOverlay[i].m_pOwner = this;
		m_clientAnimatedOverlay[i].m_nIndex = i;
		if ( i >= oldCount )
		{
			// Main-approved zero storage for each newly live record, not a
			// native constructor write or a reset of surviving records.
			m_clientAnimatedOverlay[i].m_nActivity = 0;
			m_clientAnimatedOverlay[i].m_bDirty = false;
		}
	}
}

CStudioHdr *C_Infected::OnNewModel()
{
	CStudioHdr *pStudioHdr = BaseClass::OnNewModel();
	m_iForwardAttachment = LookupAttachment( "forward" );
	m_iMoveX = LookupPoseParameter( "move_x" );
	m_iMoveY = LookupPoseParameter( "move_y" );
	m_iBodyYaw = LookupPoseParameter( "body_yaw" );
	m_iBodyPitch = LookupPoseParameter( "body_pitch" );
	m_iLeanYaw = LookupPoseParameter( "lean_yaw" );
	m_iLeanPitch = LookupPoseParameter( "lean_pitch" );
	m_nLastSequence = -1;
	return pStudioHdr;
}

void C_Infected::ReceiveSequenceStartTime( float flStartTime )
{
	if ( m_sequenceStartTime != flStartTime )
	{
		m_sequenceStartTime = flStartTime;
		m_bSequenceStartTimeChanged = true;
	}
}

void C_Infected::OnDataChanged( DataUpdateType_t updateType )
{
	if ( updateType == DATA_UPDATE_CREATED )
		m_bSequenceStartTimeChanged = true;
	m_RenderState.m_iRequestedWound[0] = m_iRequestedWound1;
	m_RenderState.m_iRequestedWound[1] = m_iRequestedWound2;
	m_RenderState.m_nOriginalBody = m_originalBody;
	m_RenderState.m_bIsBurning = m_bIsBurning;
	m_RenderState.m_nModelType = m_Gender;
	BaseClass::OnDataChanged( updateType );
}

// C313560: low-violence snapshots restore the authoritative uncut body.
void C_Infected::PostDataUpdate( DataUpdateType_t updateType )
{
	C_BaseCombatCharacter::PostDataUpdate( updateType );
	if ( UTIL_IsLowViolence() )
		SetBody( m_originalBody );
}

void C_Infected::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	if ( state == SHOULDTRANSMIT_END )
		m_flLastDormantTime = gpGlobals->curtime;
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		if ( m_hWoundModel[i] )
		{
			if ( state == SHOULDTRANSMIT_END )
				m_hWoundModel[i]->AddEffects( EF_NODRAW );
			else
				m_hWoundModel[i]->RemoveEffects( EF_NODRAW );
		}
	}
	for ( int i = 0; i < ARRAYSIZE( m_pUncommonItem ); ++i )
	{
		if ( m_pUncommonItem[i] )
		{
			if ( state == SHOULDTRANSMIT_END ) m_pUncommonItem[i]->AddEffects( EF_NODRAW );
			else m_pUncommonItem[i]->RemoveEffects( EF_NODRAW );
		}
		if ( m_pUncommonPocket[i] )
		{
			if ( state == SHOULDTRANSMIT_END ) m_pUncommonPocket[i]->AddEffects( EF_NODRAW );
			else m_pUncommonPocket[i]->RemoveEffects( EF_NODRAW );
		}
	}
	if ( m_pFaceplate )
	{
		if ( state == SHOULDTRANSMIT_END ) m_pFaceplate->AddEffects( EF_NODRAW );
		else m_pFaceplate->RemoveEffects( EF_NODRAW );
	}
	BaseClass::NotifyShouldTransmit( state );
}

struct InfectedActivityFlags_t
{
	const char *m_pszName;
	int m_nFlags;
};

// Recovered activity policy; compare registered names, never retail enum values.
static const InfectedActivityFlags_t s_InfectedActivityFlags[] =
{
	{ "ACT_TERROR_IDLE_NEUTRAL", 1 }, { "ACT_TERROR_IDLE_ALERT", 1 },
	{ "ACT_TERROR_IDLE_ALERT_INJURED_AHEAD", 17 }, { "ACT_TERROR_IDLE_ALERT_AHEAD", 17 },
	{ "ACT_TERROR_IDLE_ALERT_INJURED_BEHIND", 17 }, { "ACT_TERROR_IDLE_ALERT_BEHIND", 17 },
	{ "ACT_TERROR_IDLE_ALERT_INJURED_LEFT", 17 }, { "ACT_TERROR_IDLE_ALERT_LEFT", 17 },
	{ "ACT_TERROR_IDLE_ALERT_INJURED_RIGHT", 17 }, { "ACT_TERROR_IDLE_ALERT_RIGHT", 17 },
	{ "ACT_TERROR_IDLE_ACQUIRE", 1 },
	{ "ACT_TERROR_LEAN_FORWARD_IDLE", 1 }, { "ACT_TERROR_LEAN_BACKWARD_IDLE", 1 },
	{ "ACT_TERROR_LEAN_LEFTWARD_IDLE", 1 }, { "ACT_TERROR_LEAN_RIGHTWARD_IDLE", 1 },
	{ "ACT_TERROR_ALERT_TO_NEUTRAL", 1 }, { "ACT_TERROR_FACE_LEFT_NEUTRAL", 1 },
	{ "ACT_TERROR_FACE_RIGHT_NEUTRAL", 1 }, { "ACT_TERROR_ABOUT_FACE_NEUTRAL", 1 },
	{ "ACT_TERROR_SHAMBLE", 1 }, { "ACT_TERROR_WALK_NEUTRAL", 1 },
	{ "ACT_TERROR_RUN_INTENSE_TO_STAND_ALERT", 1 }, { "ACT_TERROR_RUN_ON_FIRE", 1 },
	{ "ACT_TERROR_ATTACK_CONTINUOUSLY", 1 }, { "ACT_TERROR_ATTACK_LOW_CONTINUOUSLY", 1 },
	{ "ACT_TERROR_ATTACK_DOOR_CONTINUOUSLY", 1 }, { "ACT_TERROR_UNABLE_TO_REACH_TARGET", 1 },
	{ "ACT_TERROR_REACH_THROUGH_DOOR", 1 },
	{ "ACT_TERROR_SHOVED_FORWARD", 1 }, { "ACT_TERROR_SHOVED_BACKWARD", 1 },
	{ "ACT_TERROR_SHOVED_LEFTWARD", 1 }, { "ACT_TERROR_SHOVED_RIGHTWARD", 1 },
	{ "ACT_TERROR_SHOVED_BACKWARD_INTO_WALL", 1 }, { "ACT_TERROR_SHOVED_FORWARD_INTO_WALL", 1 },
	{ "ACT_TERROR_SHOVED_LEFTWARD_INTO_WALL", 1 }, { "ACT_TERROR_SHOVED_RIGHTWARD_INTO_WALL", 1 },
	{ "ACT_TERROR_SHOVED_BACKWARD_FROM_SIT", 1 }, { "ACT_TERROR_SHOVED_RIGHTWARD_FROM_SIT", 1 },
	{ "ACT_TERROR_SHOVED_LEFTWARD_FROM_SIT", 1 },
	{ "ACT_TERROR_SIT_IDLE", 1 }, { "ACT_TERROR_SIT_FROM_STAND", 1 },
	{ "ACT_TERROR_SIT_TO_STAND", 1 }, { "ACT_TERROR_SIT_TO_STAND_ALERT", 1 },
	{ "ACT_TERROR_SIT_TO_LIE", 1 }, { "ACT_TERROR_LIE_IDLE", 1 },
	{ "ACT_TERROR_LIE_FROM_STAND", 1 }, { "ACT_TERROR_LIE_TO_STAND", 1 },
	{ "ACT_TERROR_LIE_TO_STAND_ALERT", 1 }, { "ACT_TERROR_LIE_TO_SIT", 1 },
	{ "ACT_TERROR_JUMP_LANDING", 5 }, { "ACT_TERROR_JUMP_LANDING_HARD", 5 },
	{ "ACT_TERROR_JUMP_LANDING_NEUTRAL", 5 }, { "ACT_TERROR_JUMP_LANDING_HARD_NEUTRAL", 5 },
	{ "ACT_TERROR_DIE_FROM_STAND", 5 }, { "ACT_TERROR_DIE_WHILE_RUNNING", 5 },
	{ "ACT_TERROR_DIE_BACKWARD_FROM_SHOTGUN", 5 }, { "ACT_TERROR_DIE_FORWARD_FROM_SHOTGUN", 5 },
	{ "ACT_TERROR_DIE_LEFTWARD_FROM_SHOTGUN", 5 }, { "ACT_TERROR_DIE_RIGHTWARD_FROM_SHOTGUN", 5 },
	{ "ACT_BLUDGEON_DEATH_BACK", 5 }, { "ACT_BLUDGEON_DEATH_FORWARD", 5 },
	{ "ACT_BLUDGEON_DEATH_LEFT", 5 }, { "ACT_BLUDGEON_DEATH_RIGHT", 5 },
	{ "ACT_SLICING_DEATH_BACK", 5 }, { "ACT_SLICING_DEATH_FORWARD", 5 },
	{ "ACT_SLICING_DEATH_LEFT", 5 }, { "ACT_SLICING_DEATH_RIGHT", 5 },
	{ "ACT_CLIMB_UP", 2 }, { "ACT_CLIMB_DOWN", 2 },
};

// C313B60..313C57. C_Infected and C_Witch both use this SAME native override;
// Terror and all native base/predicted/Terror viewmodels inherit C345B0 instead.
void C_Infected::OnSequenceChanged()
{
	BaseClass::OnSequenceChanged();
	CStudioHdr *studio = GetModelPtr();
	if ( studio && studio->IsValid() && m_iMoveX >= 0 && m_iMoveY >= 0 )
	{
		const float oldX = static_cast< float >( GetPoseParameter( studio, m_iMoveX ) );
		const float oldY = static_cast< float >( GetPoseParameter( studio, m_iMoveY ) );
		SetPoseParameter( studio, m_iMoveX, 1.0f );
		SetPoseParameter( studio, m_iMoveY, 0.0f );
		m_flNominalGroundSpeed = GetSequenceGroundSpeed( studio, GetSequence() );
		SetPoseParameter( studio, m_iMoveX, oldX );
		SetPoseParameter( studio, m_iMoveY, oldY );
	}
}
int C_Infected::GetBodyActivityFlags()
{
	if ( m_nLastSequence == GetSequence() )
		return m_nActivityFlags;

	m_nLastSequence = GetSequence();
	m_nActivityFlags = 0;
	const char *pszActivity = GetSequenceActivityName( GetSequence() );
	for ( int i = 0; i < ARRAYSIZE( s_InfectedActivityFlags ); ++i )
	{
		if ( !Q_strcmp( pszActivity, s_InfectedActivityFlags[i].m_pszName ) )
		{
			m_nActivityFlags = s_InfectedActivityFlags[i].m_nFlags;
			break;
		}
	}

	return m_nActivityFlags;
}

void C_Infected::UpdateMovementPose( const Vector &vecLocalVelocity, bool bIterate )
{
	float flX = 0.0f;
	float flY = 0.0f;
	if ( vecLocalVelocity.x != 0.0f && fabs( vecLocalVelocity.x ) > fabs( vecLocalVelocity.y ) )
	{
		flX = vecLocalVelocity.x < 0.0f ? -1.0f : 1.0f;
		flY = -vecLocalVelocity.y / fabs( vecLocalVelocity.x );
	}
	else if ( vecLocalVelocity.y != 0.0f )
	{
		flX = vecLocalVelocity.x / fabs( vecLocalVelocity.y );
		flY = vecLocalVelocity.y < 0.0f ? 1.0f : -1.0f;
	}

	SetPoseParameter( m_iMoveX, flX );
	SetPoseParameter( m_iMoveY, flY );
	if ( flX == 0.0f && flY == 0.0f )
		return;

	int nIteration = 0;
	bool bAgain;
	do
	{
		Vector vecAnimationVelocity;
		GetBlendedLinearVelocity( &vecAnimationVelocity );
		flX = fabs( vecAnimationVelocity.x ) <= 0.001f ? 0.0f : vecLocalVelocity.x / vecAnimationVelocity.x * flX;
		flY = fabs( vecAnimationVelocity.y ) <= 0.001f ? 0.0f : vecLocalVelocity.y / vecAnimationVelocity.y * flY;
		SetPoseParameter( m_iMoveX, flX );
		SetPoseParameter( m_iMoveY, flY );
		if ( !bIterate )
			break;

		float flDeltaX = vecLocalVelocity.x - vecAnimationVelocity.x;
		float flDeltaY = vecLocalVelocity.y - vecAnimationVelocity.y;
		bAgain = ( flX < 1.0f && flX > -1.0f && ( flDeltaX < -1.0f || flDeltaX > 1.0f ) ) ||
			( flY < 1.0f && flY > -1.0f && ( flDeltaY < -1.0f || flDeltaY > 1.0f ) );
	}
	while ( nIteration++ < 5 && bAgain );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void C_Infected::UpdatePoseParameters()
{
	int nActivityFlags = GetBodyActivityFlags();
	Vector vecVelocity;
	EstimateAbsVelocity( vecVelocity );
	Vector vecForward, vecRight, vecUp;
	AngleVectors( GetAbsAngles(), &vecForward, &vecRight, &vecUp );

	if ( m_iBodyYaw >= 0 )
	{
		C_BaseEntity *pTarget = m_clientLookatTarget.Get();
		Vector vecTarget = pTarget && pTarget->m_lifeState == LIFE_ALIVE ? pTarget->EyePosition() : EyePosition() + vecForward * 100.0f;
		Vector vecDirection = vecTarget - EyePosition();
		VectorNormalize( vecDirection );
		QAngle angTarget;
		VectorAngles( vecDirection, angTarget );
		SetPoseParameter( m_iBodyYaw, AngleDiff( angTarget.y, GetAbsAngles().y ) );
		SetPoseParameter( m_iBodyPitch, AngleDiff( angTarget.x, GetAbsAngles().x ) );
	}

	Vector vecLocalVelocity( DotProduct( vecVelocity, vecForward ), -DotProduct( vecVelocity, vecRight ), 0.0f );
	if ( m_iMoveX >= 0 && m_iMoveY >= 0 && TheClientNextBots().GetActiveCount() < 20 )
	{
		UpdateMovementPose( vecLocalVelocity, GetInFrustumDistanceSqr() < 262144.0f );
	}
	else if ( m_flNominalGroundSpeed > 0.0f && m_iMoveX >= 0 && m_iMoveY >= 0 )
	{
		SetPoseParameter( m_iMoveX, vecLocalVelocity.x / m_flNominalGroundSpeed );
		SetPoseParameter( m_iMoveY, vecLocalVelocity.y / m_flNominalGroundSpeed );
	}
	else
	{
		if ( m_iMoveX >= 0 )
			SetPoseParameter( m_iMoveX, vecLocalVelocity.x / z_speed.GetFloat() );
		if ( m_iMoveY >= 0 )
			SetPoseParameter( m_iMoveY, vecLocalVelocity.y / z_speed.GetFloat() );
	}

	if ( m_iLeanYaw >= 0 )
		SetPoseParameter( m_iLeanYaw, m_clientLeanYaw );
	if ( m_iLeanPitch >= 0 )
	{
		float flMin, flMax;
		GetPoseParameterRange( m_iLeanPitch, flMin, flMax );
		SetPoseParameter( m_iLeanPitch, ( flMin + flMax ) * 0.5f );
	}

	if ( nActivityFlags & IBodyClient::ENTINDEX_PLAYBACK_RATE )
	{
		volatile float flPlaybackRate = _SSE_cos( static_cast< float >( entindex() ) );
		flPlaybackRate = flPlaybackRate - ( -1.0f );
		flPlaybackRate = flPlaybackRate * 0.20000005f;
		flPlaybackRate = flPlaybackRate * 0.5f;
		flPlaybackRate = flPlaybackRate + 0.89999998f;
		SetPlaybackRate( flPlaybackRate );
	}
	else
	{
		float flGroundSpeed = GetSequenceGroundSpeed( GetSequence() );
		float flSpeed = vecVelocity.Length();
		if ( ( nActivityFlags & IBodyClient::MOTION_CONTROLLED_XY ) || flGroundSpeed >= flSpeed ||
			flGroundSpeed <= 0.0f || !( GetFlags() & FL_ONGROUND ) )
			SetPlaybackRate( 1.0f );
		else
			SetPlaybackRate( clamp( flSpeed / flGroundSpeed, -4.0f, 4.0f ) );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#pragma float_control( precise, on, push )
void C_Infected::UpdateBaseAnimation( CStudioHdr *studio )
{
    if ( !m_bSequenceStartTimeChanged && IsSequenceFinished() && !SequenceLoops() )
    {
        m_bSequenceStartTimeChanged = true;
        m_sequenceStartTime = gpGlobals->curtime;
        ResetSequenceInfo();
    }
    if ( !m_bSequenceStartTimeChanged )
        return;
    m_bSequenceStartTimeChanged = false;
    float elapsed = gpGlobals->curtime - m_sequenceStartTime;
    if ( elapsed < 0.0f )
        elapsed = 0.0f;
    if ( gpGlobals->curtime > m_flLastDormantTime + 1.0f && m_sequenceStartTime + 1.0f > gpGlobals->curtime )
        elapsed = 0.0f;
    if ( !studio || !studio->IsValid() )
        return;
    const int sequence = GetSequence();
    if ( sequence < 0 )
    {
        m_currentActivity = ACT_INVALID;
        return;
    }
    m_currentActivity = !studio->SequencesAvailable() || sequence >= studio->GetNumSeq() ?
        (Activity)0 : (Activity)::GetSequenceActivity( studio, sequence, NULL );
    const char *activity = GetSequenceActivityName( sequence );
    if ( !Q_strcmp( activity, "ACT_TERROR_ATTACK_CONTINUOUSLY" ) ||
         !Q_strcmp( activity, "ACT_TERROR_ATTACK_LOW_CONTINUOUSLY" ) ||
         !Q_strcmp( activity, "ACT_TERROR_ATTACK_DOOR_CONTINUOUSLY" ) ||
         !Q_strcmp( activity, "ACT_TERROR_REACH_THROUGH_DOOR" ) )
        SetCycle( RandomFloat( 0.0f, 0.30000001f ) );
    else
    {
        const double cycles = (double)GetSequenceCycleRate( studio, sequence ) * elapsed;
        const float storedCycle = (float)cycles;
        SetCycle( (float)( cycles - (double)_mm_cvtt_ss2si( _mm_set_ss( storedCycle ) ) ) );
    }
}
#pragma float_control( pop )

void C_Infected::UpdateClientSideAnimation()
{
    // Native315190 has a dormant gate; native3162F0 Simulate DOES NOT.
    if ( IsDormant() || NextBotStop.GetBool() || ( GetFlags() & FL_FROZEN ) )
        return;
    CStudioHdr *studio = GetModelPtr();
    const int count = Min( GetNumAnimOverlays(), m_clientAnimatedOverlay.Count() );
    for ( int i = 0; i < count; ++i )
        m_clientAnimatedOverlay[i].Update( studio );
    if ( !cl_infected_animinsim.GetBool() )
    {
        bool inView = true;
        if ( TheClientNextBots().SetupInFrustumData() )
            inView = IsInFrustum();
        UpdateInfectedAnimation( false, inView );
    }
    BaseClass::UpdateClientSideAnimation();
}

const InfectedRenderState_t &C_Infected::GetInfectedRenderState()
{
	UpdateWounds();
	return m_RenderState;
}

bool C_Infected::Simulate()
{
    // Native shared gib budget is prepared before dirty/base/item work EVEN
    // when nb_stop/frozen would later suppress the gameplay-presentation tail.
    PrepareGibBudgetForSimulate();
    if ( m_bWoundDecalsDirty )
    {
        modelrender->RemoveAllDecals( GetModelInstance() );
        m_bWoundDecalsDirty = false;
    }
    bool inView = true;
    if ( TheClientNextBots().SetupInFrustumData() )
        inView = IsInFrustum();
    BaseClass::Simulate();
    // Exact existing uncommon owner, extended only with the wound no-draw
    // maintenance between primary/pocket work and faceplate work; see packet.
    UpdateUncommonModels();
    if ( !NextBotStop.GetBool() && !( GetFlags() & FL_FROZEN ) )
    {
        if ( inView )
            UpdateForwardViewOffset();
        if ( cl_infected_animinsim.GetBool() )
            UpdateInfectedAnimation( true, inView );
        EmitGibs( this, !m_bRagdollTransferred );
        m_RenderState.m_bIsBurning = m_bIsBurning;
        if ( m_bIsBurning && !( m_RenderState.m_flBurnStartTime > 0.0f ) && !UTIL_IsLowViolence() )
            m_RenderState.m_flBurnStartTime = gpGlobals->curtime;
    }
    return true;
}

// Original C:0x3147F0/corpse0x263900 packed cmd19's burn in the low16
// bits. Keep that quantizer at the typed transport seam, not on the target's
// ordinary global RGB-modulation callback (viewrender.cpp).
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void GetInfectedBloodAndBurn( const InfectedRenderState_t &state, int &bloodSheet, float &burnStrength )
{
	float flBurn = 0.0f;
	if ( state.m_flBurnStartTime > 0.0f )
	{
		const float fraction = ( gpGlobals->curtime - state.m_flBurnStartTime ) / z_burn_effect_duration.GetFloat();
		flBurn = fraction > 1.0f ? 1.0f : fraction;
	}
	// C3148CD/C263A01: x87 product, explicit truncating FISTP qword,
	// low32 ADD and unsigned-to-float publication. This is not __ftol2_sse.
	const double burn = static_cast< double >( z_burn_effect_strength.GetFloat() ) *
		static_cast< double >( flBurn ) * 65535.0;
	const uint32 packed = ( static_cast< uint32 >( state.m_nBloodMask ) << 16 ) +
		static_cast< uint32 >( _mm_cvttsd_si64( _mm_set_sd( burn ) ) );
	const float packet = static_cast< float >( packed );
	// Original shaderapi cmd19 C220E2 also uses FISTP qword. Decode BOTH
	// fields after the real float round-trip, including carry into the sheet.
	const uint32 decoded = static_cast< uint32 >( _mm_cvttsd_si64( _mm_set_sd( static_cast< double >( packet ) ) ) );
	bloodSheet = static_cast< int >( decoded >> 16 );
	// C2221C widens this stored FLOAT32 reciprocal, not an exact division.
	// The shader's final *2 is exact for this range and supplies native c5.x.
	burnStrength = static_cast< float >( static_cast< double >( decoded & 65535u ) *
		static_cast< double >( 1.5259021893143654e-05f ) );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

const char *C_Infected::DamageDecal( int bitsDamageType, int gameMaterial )
{
	if ( !UTIL_IsLowViolence() && z_infected_decals.GetBool() && m_RenderState.m_iWound[0] == -1 )
		return BaseClass::DamageDecal( bitsDamageType, gameMaterial );
	return "";
}

void C_Infected::AddDecal( const Vector &rayStart, const Vector &rayEnd, const Vector &decalCenter,
	int hitbox, int decalIndex, bool doTrace, trace_t &tr, int maxLODToDecal )
{
	if ( !UTIL_IsLowViolence() && z_infected_decals.GetBool() && m_RenderState.m_iWound[0] == -1 )
		BaseClass::AddDecal( rayStart, rayEnd, decalCenter, hitbox, decalIndex, doTrace, tr, maxLODToDecal );
}

C_BaseAnimating *C_Infected::BecomeRagdollOnClient( int category )
{
	StopParticleEffects( this );
	RandomInt( 0, 32767 ); // C3176D3 consumes this draw even if corpse creation fails.
	if ( UTIL_IsLowViolence() )
		m_nBody = m_originalBody;
	UpdateWounds();
	C_BaseAnimating *pRagdoll = BaseClass::BecomeRagdollOnClient( category );
	if ( pRagdoll )
	{
		EmitGibs( pRagdoll, false );
		TransferWoundModels( pRagdoll );
		// C3176C0 transfers the actor's explicit policy after its wound state.
		ShadowType_t shadowType;
		if ( GetForcedShadowCastType( &shadowType ) )
			static_cast< C_ClientRagdoll * >( pRagdoll )->ForceShadowCastType( true, shadowType );
	}
	return pRagdoll;
}

LINK_ENTITY_TO_CLASS( terror_client_ragdoll, C_TerrorClientRagdoll );

BEGIN_DATADESC( C_TerrorClientRagdoll )
	DEFINE_FIELD( m_bHasInfectedState, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flWoundParticleTime, FIELD_TIME ),
	DEFINE_FIELD( m_RenderState.m_nSkinTint, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_nClothingTint, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_nBloodMask, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_flBurnStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_RenderState.m_bIsBurning, FIELD_BOOLEAN ),
	DEFINE_AUTO_ARRAY( m_RenderState.m_iRequestedWound, FIELD_INTEGER ),
	DEFINE_AUTO_ARRAY( m_RenderState.m_iWound, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_nOriginalBody, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_nModelType, FIELD_INTEGER ),
	DEFINE_FIELD( m_RenderState.m_Wound[0].m_bEnabled, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_RenderState.m_Wound[0].m_vecPosition, FIELD_VECTOR ),
	DEFINE_AUTO_ARRAY( m_RenderState.m_Wound[0].m_vecAxis, FIELD_VECTOR ),
	DEFINE_ARRAY( m_RenderState.m_Wound[0].m_vecUVMapping, FIELD_FLOAT, 4 ),
	DEFINE_FIELD( m_RenderState.m_Wound[0].m_vecUVScale, FIELD_VECTOR2D ),
	DEFINE_FIELD( m_RenderState.m_Wound[1].m_bEnabled, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_RenderState.m_Wound[1].m_vecPosition, FIELD_VECTOR ),
	DEFINE_AUTO_ARRAY( m_RenderState.m_Wound[1].m_vecAxis, FIELD_VECTOR ),
	DEFINE_ARRAY( m_RenderState.m_Wound[1].m_vecUVMapping, FIELD_FLOAT, 4 ),
	DEFINE_FIELD( m_RenderState.m_Wound[1].m_vecUVScale, FIELD_VECTOR2D ),
	DEFINE_AUTO_ARRAY( m_hWoundModel, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hFaceplate, FIELD_EHANDLE ),
END_DATADESC()

C_TerrorClientRagdoll::C_TerrorClientRagdoll( bool bRestoring ) : BaseClass( bRestoring )
{
	m_bHasInfectedState = false;
	m_flWoundParticleTime = gpGlobals->curtime + 0.2f;
	m_hFaceplate = NULL;
	SetClassname( "terror_client_ragdoll" );
}

void C_TerrorClientRagdoll::Release()
{
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		C_BaseAnimating *pModel = m_hWoundModel[i].Get();
		if ( pModel )
		{
			ClientEntityList().AddToDeleteList( pModel );
			m_hWoundModel[i] = NULL;
		}
	}
	C_BaseAnimating *pFaceplate = m_hFaceplate.Get();
	if ( pFaceplate )
	{
		ClientEntityList().AddToDeleteList( pFaceplate );
	}
	m_hFaceplate = NULL;
	BaseClass::Release();
}

void C_TerrorClientRagdoll::CopySourceState( const InfectedRenderState_t &state )
{
	m_bHasInfectedState = true;
	m_RenderState = state;
}

void C_TerrorClientRagdoll::AcceptWoundModel( int nWound, C_BaseAnimating *pModel )
{
	m_hWoundModel[nWound] = pModel;
	pModel->FollowEntity( this );
}


C_ClientRagdoll *CreateTerrorClientRagdoll( C_BaseAnimating *pSource )
{
	if ( !dynamic_cast< C_Infected * >( pSource ) )
		return new C_ClientRagdoll( false ); // Native base/CCS193 C3CF70, selected VPhysics1.
	return new C_TerrorClientRagdoll( false );
}

const InfectedRenderState_t *GetInfectedRenderState( C_BaseEntity *pEntity )
{
	C_Infected *pInfected = dynamic_cast<C_Infected *>( pEntity );
	if ( pInfected )
		return &pInfected->GetInfectedRenderState();
	C_TerrorClientRagdoll *pRagdoll = dynamic_cast<C_TerrorClientRagdoll *>( pEntity );
	return pRagdoll && pRagdoll->HasInfectedState() ? &pRagdoll->GetInfectedRenderState() : NULL;
}

bool GetTerrorRagdollMaterialState( C_BaseEntity *pEntity, TerrorMaterialProxyState_t &state )
{
	C_ClientRagdoll *pRagdoll = dynamic_cast<C_ClientRagdoll *>( pEntity );
	if ( pRagdoll )
	{
		state.m_flIT = pRagdoll->m_itBlend;
		state.m_flBloodyHands = pRagdoll->m_bloodyHands;
		return true;
	}
	state = TerrorMaterialProxyState_t();
	return false;
}

struct UncommonAttachment_t
{
    const char *model;
    const char *pocket;
    const char *attachment;
};
static const UncommonAttachment_t s_FallenAttachments[4] =
{
    { "models/w_models/weapons/w_eq_molotov.mdl", "models/infected/cim_fallen_survivor_pocket02.mdl", "molotov" },
    { "models/w_models/Weapons/w_eq_pipebomb.mdl", "models/infected/cim_fallen_survivor_pocket01.mdl", "pipebomb" },
    { "models/w_models/Weapons/w_eq_painpills.mdl", "models/infected/cim_fallen_survivor_pocket03.mdl", "pills" },
    { "models/w_models/Weapons/w_eq_medkit.mdl", "", "medkit" }
};
static const UncommonAttachment_t s_CEDAAttachment = { "models/w_models/Weapons/w_eq_bile_flask.mdl", "", "grenade" };
static const UncommonAttachment_t s_RiotAttachment = { "models/weapons/melee/w_tonfa.mdl", "", "baton" };

static C_BaseAnimating *CreateUncommonModel( C_Infected *actor, const char *path, unsigned char attachment )
{
    C_BaseAnimating *model = new C_BaseAnimating;
    // Native ignores InitializeAsClientEntity's result; do not silently invent a
    // successful fallback model or remove/retry initialized-false entities.
    model->InitializeAsClientEntity( path, false );
    model->SetParent( actor, attachment );
    model->SetMoveType( MOVETYPE_NONE );
    model->AddSolidFlags( FSOLID_NOT_SOLID );
    model->SetLocalOrigin( vec3_origin );
    model->SetLocalAngles( vec3_angle );
    if ( actor->IsDormant() )
        model->AddEffects( EF_NODRAW );
    return model;
}

void C_Infected::UpdateUncommonModels()
{
    if ( m_nFallenFlags != 0 )
    {
        const UncommonAttachment_t *table = NULL;
        int count = 0;
        switch ( m_Gender )
        {
        case 11: table = &s_CEDAAttachment; count = 1; break;
        case 14: table = s_FallenAttachments; count = 4; break;
        case 15:
        case 20: table = &s_RiotAttachment; count = 1; break;
        }
        for ( int i = 0; i < count; ++i )
        {
            if ( ( m_nFallenFlags & ( 1 << i ) ) && !m_pUncommonItem[i] && !m_bRagdollTransferred )
            {
                const unsigned char attachment = (unsigned char)LookupAttachment( table[i].attachment );
                m_pUncommonItem[i] = CreateUncommonModel( this, table[i].model, attachment );
                if ( table[i].pocket[0] )
                    m_pUncommonPocket[i] = CreateUncommonModel( this, table[i].pocket, attachment );
            }
            // Well-defined native C3163FA/316406/3165AF table-offset bug:
            // reparent fallback uses FALLEN attachment[i] even for CEDA/riot.
            // Do not "correct" grenade/baton to their own row here.
            if ( m_pUncommonItem[i] && !m_pUncommonItem[i]->IsFollowingEntity() )
            {
                const unsigned char attachment = (unsigned char)LookupAttachment( s_FallenAttachments[i].attachment );
                m_pUncommonItem[i]->SetParent( this, attachment );
                m_pUncommonItem[i]->SetLocalOrigin( vec3_origin );
                m_pUncommonItem[i]->SetLocalAngles( vec3_angle );
            }
            if ( m_pUncommonPocket[i] && !m_pUncommonPocket[i]->IsFollowingEntity() )
            {
                const unsigned char attachment = (unsigned char)LookupAttachment( s_FallenAttachments[i].attachment );
                m_pUncommonPocket[i]->SetParent( this, attachment );
                m_pUncommonPocket[i]->SetLocalOrigin( vec3_origin );
                m_pUncommonPocket[i]->SetLocalAngles( vec3_angle );
            }
        }
    }
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		C_BaseAnimating *model = m_hWoundModel[i].Get();
		if ( !model )
			continue;
		if ( IsEffectActive( EF_NODRAW ) )
			model->AddEffects( EF_NODRAW );
		else
		{
			model->RemoveEffects( EF_NODRAW );
			model->UpdateVisibility();
		}
	}
    if ( !m_pFaceplate && ( m_Gender == 11 || m_Gender == 15 ) && !m_bRagdollTransferred )
    {
        m_pFaceplate = new C_BaseAnimating;
        m_pFaceplate->InitializeAsClientEntity( m_Gender == 11 ? "models/infected/cim_faceplate.mdl" :
            "models/infected/cim_riot_faceplate.mdl", false );
    }
    if ( m_pFaceplate && !m_pFaceplate->IsFollowingEntity() )
        m_pFaceplate->FollowEntity( this, true );
}

void C_Infected::ReleaseUncommonItemModels()
{
    for ( int i = 0; i < ARRAYSIZE( m_pUncommonItem ); ++i )
    {
        if ( m_pUncommonItem[i] )
        {
            m_pUncommonItem[i]->Release();
            m_pUncommonItem[i] = NULL;
        }
        if ( m_pUncommonPocket[i] )
        {
            m_pUncommonPocket[i]->Release();
            m_pUncommonPocket[i] = NULL;
        }
    }
}

void C_Infected::ReleaseUncommonModels()
{
    if ( m_pFaceplate )
    {
        m_pFaceplate->Release();
        m_pFaceplate = NULL;
    }
    ReleaseUncommonItemModels();
}

void C_Infected::UpdateOnRemove()
{
    ReleaseWoundModels();
    ReleaseUncommonModels();
    BaseClass::UpdateOnRemove();
}

void C_TerrorClientRagdoll::AcceptFaceplate( C_BaseAnimating *model )
{
    m_hFaceplate = model;
    model->StopFollowingEntity();
    model->FollowEntity( this, true );
}

#pragma float_control( precise, on, push )
void C_Infected::UpdateForwardViewOffset()
{
    // Native tests !=0, NOT >0: constructor-1 is eligible until OnNewModel writes.
    if ( m_iForwardAttachment == 0 || !( gpGlobals->curtime >= m_forwardViewTimer.m_timestamp ) )
        return;
    static int lastFrame = -1; // original data106F4BE8 =FFFFFFFF
    static int count = 0;     // original107D1D94
    if ( lastFrame != gpGlobals->framecount )
    {
        count = 0;
        lastFrame = gpGlobals->framecount;
    }
    ++count;
    if ( count > 3 )
        return;
    Vector position;
    // Main-approved undefined-native policy, 2026-10-23:
    // original103153D7 ignores failure,103153E2..15417 reads stack,1541C writes.
    // Skip ONLY SetViewOffset on failure; count and following RNG/Start survive.
    if ( GetAttachment( m_iForwardAttachment, position ) )
        SetViewOffset( position - GetAbsOrigin() );
    m_forwardViewTimer.Start( RandomFloat( 1.0f, 2.0f ) );
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
int C_InfectedAnimationLayer::DrawDebug( C_AnimationLayer *layer, int line ) const
{
	const float fade = layer->GetFadeout( gpGlobals->curtime );
	engine->Con_NPrintf( line, "%s(%d) / seq %d, cycle %f, weight %f, fadeout %f, order %d",
		ActivityList_NameForIndex( m_nActivity ), m_nSequence, layer->m_nSequence,
		layer->m_flCycle, layer->m_flWeight, fade, layer->m_nOrder );
	return line + 1;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void C_Infected::DrawInfectedAnimationDebug( CStudioHdr *studio )
{
    if ( studio && studio->IsValid() && cl_showanimstate.GetInt() == entindex() )
    {
        int line = 30;
        for ( int i = 0; i < studio->GetNumPoseParameters(); ++i )
            engine->Con_NPrintf( line++, "%s: %f", studio->pPoseParameter( i ).pszName(), GetPoseParameter( studio, i ) );
        engine->Con_NPrintf( line++, "%s(%d) cycle %f", ActivityList_NameForIndex( m_currentActivity ), GetSequence(), GetCycle() );
        const int count = Min( GetNumAnimOverlays(), m_clientAnimatedOverlay.Count() );
        for ( int i = 0; i < count; ++i )
            line = m_clientAnimatedOverlay[i].DrawDebug( GetAnimOverlay( i ), line );
    }
    if ( z_debug_infected_anim.GetInt() != 0 )
    {
        // Native314410 mutates the CLIENT animation selector first, not
        // sv_showhitboxes. The separate hitbox reference is used only for >1.
        static ConVarRef showHitboxes( "sv_showhitboxes" );
        static ConVarRef serverAnimation( "z_debug_infected_server_anim" );
        static ConVarRef serverDebug( "z_debug" );
        cl_showanimstate.SetValue( entindex() );
        serverAnimation.SetValue( entindex() );
        if ( serverDebug.GetInt() == 0 )
            serverDebug.SetValue( 1 );
        if ( z_debug_infected_anim.GetInt() > 1 )
            showHitboxes.SetValue( entindex() );
    }
}
void C_Infected::UpdateInfectedAnimation( bool latch, bool inView )
{
    if ( inView )
        UpdatePoseParameters();
    else
        SetPlaybackRate( 1.0f );
    CStudioHdr *studio = GetModelPtr();
    if ( inView )
        DrawInfectedAnimationDebug( studio );
    UpdateBaseAnimation( studio );
    if ( latch )
        OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
}
#pragma float_control( pop )
