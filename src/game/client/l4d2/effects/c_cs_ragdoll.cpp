//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/c_cs_ragdoll.h"
#include "l4d2/player/c_cs_player.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_fire_smoke.h"
#include "physics_shared.h"
#include "ragdoll_shared.h"
#include "particle_parse.h"
#include "util_shared.h"
#include "bone_setup.h"
#include "tier0/memdbgon.h"

// C50CC70/C50CE50; actual registrations, not dangling ConVarRefs.
static ConVar cl_ragdoll_physics_enable( "cl_ragdoll_physics_enable", "1", 0x0000000A, "Enable/disable ragdoll physics." );
static ConVar z_boomer_gibs( "z_boomer_gibs", "1", 0x00004008 );

C_EntityFlame *FireEffect( C_BaseAnimating *, C_BaseEntity *, float *, float *, float * );

IMPLEMENT_CLIENTCLASS_DT_NOBASE( C_CSRagdoll, DT_CSRagdoll, CCSRagdoll )
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropVector( RECVINFO( m_vecRagdollOrigin ) ),
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropInt( RECVINFO( m_nModelIndex ) ),
	RecvPropInt( RECVINFO( m_nForceBone ) ),
	RecvPropVector( RECVINFO( m_vecForce ) ),
	RecvPropVector( RECVINFO( m_vecRagdollVelocity ) ),
	RecvPropInt( RECVINFO( m_iDeathPose ) ),
	RecvPropInt( RECVINFO( m_iDeathFrame ) ),
	RecvPropInt( RECVINFO( m_iTeamNum ) ),
	RecvPropBool( RECVINFO( m_bClientSideAnimation ) ),
	RecvPropBool( RECVINFO( m_bOnFire ) ),
	RecvPropBool( RECVINFO( m_bInterpOrigin ) ),
	RecvPropInt( RECVINFO( m_ragdollType ) ),
	RecvPropInt( RECVINFO( m_zombieClass ) ),
	RecvPropInt( RECVINFO( m_survivorCharacter ) ),
END_RECV_TABLE()

// C224B20. Everything else retains allocator-zero/base-constructor state.
C_CSRagdoll::C_CSRagdoll()
{
	m_hPlayer = NULL;
	m_hCreatedRagdoll = NULL;
	m_bCreated = false;
	m_ragdollType = 1;
}

C_CSRagdoll::~C_CSRagdoll()
{
	PhysCleanupFrictionSounds( this ); // C224E20 -> C160460.
}

void C_CSRagdoll::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( g_RagdollLVManager.IsLowViolence() )
			CreateLowViolenceRagdoll();
		else
			CreateCSRagdoll();
	}
	else if ( !cl_ragdoll_physics_enable.GetBool() )
	{
		m_bClientSideRagdoll = false; // Networkable-this +349 == entity +357.
	}
}

// C222720. The existing root factory owns admission, category storage,
// registration and fading; never construct a second player-corpse subclass.
C_BaseAnimating *C_CSRagdoll::CreateRagdollFromModel( int category, int modelIndex, const Vector &force )
{
	C_BaseAnimating *ragdoll = CreateRagdollCopy( category, true );
	if ( !ragdoll )
		return NULL;

	matrix3x4_t sourceDelta0[MAXSTUDIOBONES], sourceDelta1[MAXSTUDIOBONES], sourceCurrent[MAXSTUDIOBONES];
	GetRagdollInitBoneArrays( sourceDelta0, sourceDelta1, sourceCurrent, 0.1f );
	ragdoll->SetModelIndex( modelIndex );
	CStudioHdr *targetHdr = ragdoll->GetModelPtr();
	CStudioHdr *sourceHdr = GetModelPtr();
	matrix3x4_t delta0[MAXSTUDIOBONES], delta1[MAXSTUDIOBONES], current[MAXSTUDIOBONES];
	Q_memset( delta0, 0, sizeof( delta0 ) );
	Q_memset( delta1, 0, sizeof( delta1 ) );
	Q_memset( current, 0, sizeof( current ) );
	// Native immediately dereferences a missing target header. Skip only that
	// invalid remap; retain the allocated/admitted corpse and the later steps.
	if ( targetHdr )
	{
		for ( int bone = 0; bone < targetHdr->numbones(); ++bone )
		{
			const int sourceBone = Studio_BoneIndexByName( sourceHdr, targetHdr->pBone( bone )->pszName() );
			if ( sourceBone >= 0 )
			{
				if ( bone >= MAXSTUDIOBONES || sourceBone >= MAXSTUDIOBONES )
					continue; // skip only native out-of-array matrix copies
				MatrixCopy( sourceDelta0[sourceBone], delta0[bone] );
				MatrixCopy( sourceDelta1[sourceBone], delta1[bone] );
				MatrixCopy( sourceCurrent[sourceBone], current[bone] );
			}
		}
	}

	Vector direction = force;
	VectorNormalize( direction );
	for ( int axis = 0; axis < 3; ++axis )
	{
		delta0[0][axis][3] -= direction[axis] * 500.0f;
		delta0[1][axis][3] -= direction[axis] * 50.0f;
	}
	// Native writes the SOURCE force only AFTER the root copy. Its newly copied
	// corpse keeps the previous source force; do not repair this ordering bug.
	m_vecForce = force;
	m_nForceBone = 0;
	ragdoll->InitAsClientRagdoll( delta0, delta1, current, 0.1f );
	if ( m_bOnFire )
		ragdoll->SetEffectEntity( FireEffect( ragdoll, NULL, NULL, NULL, NULL ) );
	return ragdoll;
}

void C_CSRagdoll::CreateCSRagdoll()
{
	C_BaseEntity *source = m_hPlayer.Get();
	if ( source && source->IsPlayer() && !source->IsDormant() )
	{
		// C224282 calls IsLocalPlayer on THIS placeholder, not on source.
		// Its inherited IsPlayer=false makes this guard ineffective natively.
		C_BaseEntity *placeholder = this;
		if ( placeholder->IsPlayer() && static_cast< C_BasePlayer * >( placeholder )->IsLocalPlayer() && m_zombieClass == 2 )
			m_bCreated = true;
		if ( !m_bCreated )
		{
			C_CSPlayer *player = dynamic_cast< C_CSPlayer * >( source );
			// Native unconditionally accesses CS/Terror offsets after IsPlayer.
			// Skip only invalid derived-field accesses for an unrelated player.
			if ( player )
			{
				player->m_iDeathPose = m_iDeathPose;
				player->m_iDeathFrame = m_iDeathFrame;
			}
			// C224210 calls C3AD80 directly, bypassing the player's C2223A0 veto.
			C_BaseAnimating *ragdoll = source->GetBaseAnimating()->C_BaseAnimating::BecomeRagdollOnClient( m_ragdollType );
			m_hCreatedRagdoll = ragdoll;
			C_ClientRagdoll *clientRagdoll = dynamic_cast< C_ClientRagdoll * >( ragdoll );
			if ( clientRagdoll )
			{
				C_TerrorPlayer *terrorPlayer = ToTerrorPlayer( source );
				if ( terrorPlayer )
				{
					clientRagdoll->m_burnPercent = terrorPlayer->m_burnPercent;
					clientRagdoll->m_bbqPercent = terrorPlayer->m_bbqPercent;
				}
				clientRagdoll->SetRenderColorA( 255 );
			}
			// Native nullsub_24(ragdoll) has no effect and is elided.
			if ( m_bOnFire && ragdoll )
				ragdoll->SetEffectEntity( FireEffect( ragdoll, NULL, NULL, NULL, NULL ) );
			m_SequenceTransitioner.m_animationQueue.CopyArray( source->GetBaseAnimating()->m_SequenceTransitioner.m_animationQueue.Base(),
				source->GetBaseAnimating()->m_SequenceTransitioner.m_animationQueue.Count() );

			if ( !UTIL_IsLowViolence() && z_boomer_gibs.GetBool() )
			{
				const model_t *model = modelinfo->GetModel( GetModelIndex() );
				const char *name = model ? modelinfo->GetModelName( model ) : NULL;
				if ( name && V_stristr( name, "boomer" ) )
				{
					Vector force( 0.0f, 0.0f, 100.0f );
					CreateRagdollFromModel( 0, modelinfo->GetModelIndex( "models/infected/limbs/exploded_boomer_head.mdl" ), force );
					const char *models[] = {
						"models/infected/limbs/exploded_boomer_rarm.mdl",
						"models/infected/limbs/exploded_boomer_steak1.mdl",
						"models/infected/limbs/exploded_boomer_steak2.mdl",
						"models/infected/limbs/exploded_boomer_steak3.mdl"
					};
					for ( int i = 0; i < ARRAYSIZE( models ); ++i )
					{
						force.x = static_cast< float >( RandomInt( -100, 100 ) );
						force.y = static_cast< float >( RandomInt( -100, 100 ) );
						force.z = static_cast< float >( RandomInt( 25, 75 ) );
						C_BaseAnimating *chunk = CreateRagdollFromModel( 0, modelinfo->GetModelIndex( models[i] ), force );
						DispatchParticleEffect( "boomer_chunk_smoke", PATTACH_ABSORIGIN_FOLLOW, chunk );
					}
					DispatchParticleEffect( "boomer_leg_smoke", PATTACH_ABSORIGIN_FOLLOW, ragdoll );
				}
			}
		}
	}
	else if ( !m_bCreated )
	{
		// C2245F3 passes the native all-zero global, NOT the received velocity.
		CreateRagdollFromModel( m_ragdollType, GetModelIndex(), vec3_origin );
	}
	m_nRenderFX = kRenderFxNone; // Native SetRenderFX's target-owned state.
	SetRenderMode( kRenderNone );
	SetRenderColor( 0, 0, 0, 0 );
	m_bCreated = true;
}

void C_CSRagdoll::CreateLowViolenceRagdoll()
{
	m_nActualModelIndex = GetModelIndex();
	int first = 9999, last = -9999;
	char name[512];
	for ( int i = 1; i < 100; ++i )
	{
		V_snprintf( name, sizeof( name ), "death%d", i );
		if ( LookupSequence( name ) == -1 )
			break;
		if ( first >= i ) first = i;
		if ( last <= i ) last = i;
	}
	if ( first == 9999 )
	{
		CreateCSRagdoll();
		return;
	}
	V_snprintf( name, sizeof( name ), "death%d", RandomInt( first, last ) );
	SetSequence( LookupSequence( name ) );
	UseClientSideAnimation();
	SetNetworkOrigin( m_vecRagdollOrigin );
	SetAbsOrigin( m_vecRagdollOrigin );
	SetAbsVelocity( m_vecRagdollVelocity );
	C_BaseEntity *player = m_hPlayer.Get();
	if ( player && player->IsPlayer() && !player->IsDormant() )
	{
		SetModelIndex( player->GetModelIndex() );
		player->SnatchModelInstance( this );
		SetAbsAngles( player->GetRenderAngles() );
		SetNetworkAngles( player->GetRenderAngles() );
	}
	Interp_Reset( GetVarMapping() );
}
