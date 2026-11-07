//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/demo/server_demo_recording_state.h"
#include "l4d2/demo/server_demo_recording_file.h"
#include "tier0/vprof.h"
#include "tier0/memdbgon.h"

COMPILE_TIME_ASSERT( sizeof( ServerDemoBaseEntityState_t ) == 56 );
COMPILE_TIME_ASSERT( sizeof( ServerDemoAnimatingState_t ) == 3728 );
COMPILE_TIME_ASSERT( sizeof( ServerDemoAnimatingOverlayState_t ) == 504 );

ServerDemoBaseEntityState_t::ServerDemoBaseEntityState_t() :
	m_nFieldMask( 0 ), m_flTime( 0.0f ), m_nEntityIndex( 0 ),
	m_nModelName( 0xFFFF ), m_bVisible( false ), m_nReserved15( 0 ),
	m_nOwner( -1 ), m_nEffects( 0 ), m_bFinalVisibleSample( false ),
	m_nFollowParentIndex( -1 )
{
	m_nReserved25[0] = m_nReserved25[1] = m_nReserved25[2] = 0;
	m_vecOrigin.Init();
	m_angAngles.Init();
}

// S:1CE780. Effects, entity index and model name are NOT compared here.
// Return whether a compared field changed BEFORE the unconditional mask|=3.
bool ServerDemoBaseEntityState_t::UpdateFrom( const ServerDemoBaseEntityState_t &state )
{
	m_nFieldMask = 0;
	if ( m_nOwner != state.m_nOwner )
	{
		m_nOwner = state.m_nOwner;
		m_nFieldMask = 4;
	}
	if ( m_vecOrigin.x != state.m_vecOrigin.x || m_vecOrigin.y != state.m_vecOrigin.y || m_vecOrigin.z != state.m_vecOrigin.z )
	{
		m_vecOrigin = state.m_vecOrigin;
		m_nFieldMask |= 0x40;
	}
	if ( m_angAngles.x != state.m_angAngles.x || m_angAngles.y != state.m_angAngles.y || m_angAngles.z != state.m_angAngles.z )
	{
		m_angAngles = state.m_angAngles;
		m_nFieldMask |= 0x80;
	}
	if ( m_bVisible != state.m_bVisible )
	{
		m_bVisible = state.m_bVisible;
		m_nFieldMask |= 0x10;
	}
	if ( m_bFinalVisibleSample != state.m_bFinalVisibleSample )
	{
		m_bFinalVisibleSample = state.m_bFinalVisibleSample;
		m_nFieldMask |= 0x20;
	}
	if ( m_nFollowParentIndex != state.m_nFollowParentIndex )
	{
		m_nFollowParentIndex = state.m_nFollowParentIndex;
		m_nFieldMask |= 0x400;
	}
	bool changed = m_nFieldMask != 0;
	m_nFieldMask |= 3;
	return changed;
}

// S:05EBD0. A genuine virtual recording callback, NOT a frame-created shadow
// cache or a query-time refresh. Keep the original unbound recorder root
// unbound: this source does not register or force calls to this callback.
bool CBaseEntity::GetDemoRecordingState( KeyValues *message )
{
	VPROF_BUDGET( "CBaseEntity::GetDemoRecordingState", VPROF_BUDGETGROUP_TOOLS );
	static ServerDemoBaseEntityState_t state;
	m_DemoRecordingState.m_nEntityIndex = entindex();
	const char *modelName = modelinfo->GetModelName( modelinfo->GetModel( GetModelIndex() ) );
	CServerDemoFile *file = static_cast< CServerDemoFile * >( message->GetPtr( "serverdemo" ) );
	if ( modelName && *modelName && file )
		m_DemoRecordingState.m_nModelName = (unsigned short)file->FindOrAddModelName( modelName );
	else
		m_DemoRecordingState.m_nModelName = 0xFFFF;

	CBaseEntity *owner = GetOwnerEntity();
	state.m_nOwner = owner ? owner->entindex() : -1;
	state.m_nEffects = GetEffects();
	state.m_bVisible = !( GetEffects() & EF_NODRAW );
	state.m_bFinalVisibleSample = false;
	state.m_vecOrigin = GetAbsOrigin();
	state.m_angAngles = GetAbsAngles();
	CBaseEntity *parent = GetMoveParent();
	if ( ( GetEffects() & EF_BONEMERGE ) && GetParentAttachment() == 0 && parent )
		state.m_nFollowParentIndex = parent->entindex();
	else
		state.m_nFollowParentIndex = -1;
	if ( owner && ( owner->GetEffects() & EF_NOINTERP ) ) state.m_nEffects |= EF_NOINTERP;
	for ( CBaseEntity *ancestor = GetMoveParent(); ancestor; ancestor = ancestor->GetMoveParent() )
	{
		if ( ancestor->GetEffects() & EF_NOINTERP )
		{
			state.m_nEffects |= EF_NOINTERP;
			break;
		}
	}

	bool changed;
	if ( m_bForceFullDemoRecording )
	{
		// Native writes entity/model into the cache BEFORE overwriting all 56
		// bytes from the shared sample. Do not "repair" that original ordering.
		Q_memcpy( &m_DemoRecordingState, &state, sizeof( m_DemoRecordingState ) );
		m_DemoRecordingState.m_nFieldMask = 0xFFFFFFFF;
		changed = true;
	}
	else
	{
		m_DemoRecordingState.m_nFieldMask = 0;
		changed = m_DemoRecordingState.UpdateFrom( state );
	}
	message->SetInt( "type", 2 );
	message->SetPtr( "baseentity", &m_DemoRecordingState );
	return changed;
}
