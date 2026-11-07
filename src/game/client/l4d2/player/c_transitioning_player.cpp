//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_transitioning_player.h"
#include "l4d2/weapons/c_addon_prop.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "engine/IEngineTrace.h"
#include "l4d2/hud/terror_client_colors.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT_NOBASE( C_TransitioningPlayer, DT_TransitioningPlayer, CTransitioningPlayer )
	RecvPropDataTable( "baseclass", 0, 0, C_BaseFlex::m_pClassRecvTable, DataTableRecvProxy_StaticDataTable ),
	RecvPropInt( RECVINFO( m_loadingProgress ) ),
	RecvPropInt( RECVINFO( m_userID ) ),
	RecvPropInt( RECVINFO( m_score ) ),
	RecvPropInt( RECVINFO( m_deaths ) ),
	RecvPropInt( RECVINFO( m_iHealth ) ),
	RecvPropInt( RECVINFO( m_healthBuffer ) ),
	RecvPropBool( RECVINFO( m_isAlive ) ),
	RecvPropBool( RECVINFO( m_isDying ) ),
	RecvPropBool( RECVINFO( m_isIncapacitated ) ),
	RecvPropBool( RECVINFO( m_isIT ) ),
	RecvPropString( RECVINFO( m_playerName ) ),
	RecvPropInt( RECVINFO( m_character ) ),
	RecvPropBool( RECVINFO( m_hasMolotov ) ),
	RecvPropBool( RECVINFO( m_hasGrenade ) ),
	RecvPropBool( RECVINFO( m_hasFirstAidKit ) ),
	RecvPropBool( RECVINFO( m_hasPainPills ) ),
	RecvPropInt( RECVINFO( m_primaryWeaponID ) ),
	RecvPropInt( RECVINFO( m_secondaryWeaponID ) ),
END_RECV_TABLE()

static CUtlVector<C_TransitioningPlayer *> s_transitioningPlayers;

void ResetTransitioningPlayers()
{
	s_transitioningPlayers.RemoveAll();
}
static ConVar z_transitioning_player_addon_rebuild( "z_transitioning_player_addon_rebuild", "1", 0x0000000A );

const CUtlVector<C_TransitioningPlayer *> &GetTransitioningPlayers()
{
	return s_transitioningPlayers;
}

// Original client 289A00 / 289B00. The list is ordered and GetTeamColor may
// remove an entry before entity destruction; Remove deliberately tolerates it.
C_TransitioningPlayer::C_TransitioningPlayer()
{
	m_blinkController = LocalFlexController_t( -1 );
	s_transitioningPlayers.AddToTail( this );
	SetViewOffset( GameRules()->GetViewVectors()->m_vView );
}

C_TransitioningPlayer::~C_TransitioningPlayer()
{
	s_transitioningPlayers.FindAndRemove( this );
	ClearAddonModels();
}

void C_TransitioningPlayer::ClearAddonModels()
{
	FOR_EACH_VEC( m_addonModels, i )
	{
		C_AddonProp *addon = m_addonModels[i].Get();
		if ( addon )
			addon->Release();
	}
	m_addonModels.RemoveAll();
}

// Original 28A0A0 has no base callback. Only TRANSMIT_END clears addons.
void C_TransitioningPlayer::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	if ( state == SHOULDTRANSMIT_END )
		ClearAddonModels();
}

CStudioHdr *C_TransitioningPlayer::OnNewModel()
{
	ClearAddonModels();
	SetNextClientThink( gpGlobals->curtime + 0.1f );
	CStudioHdr *hdr = BaseClass::OnNewModel();
	m_blinkController = FindFlexController( "blink" );
	SetFlexWeight( m_blinkController, 0.0f );
	return hdr;
}


// Original 28A200. Keep actual script attachment and model ownership. No
// guessed weapon meshes or model-less placeholder props are created.
void C_TransitioningPlayer::CreateAddonModel( int weaponID )
{
	const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
	if ( !info )
	{
		Warning( "C_TransitioningPlayer::CreateAddonModel: Unable to get weapon info %d.\n", weaponID );
		return;
	}
	const int attachment = LookupAttachment( info->m_szAddonAttachment );
	if ( attachment <= 0 )
		return;

	C_AddonProp *addon = new C_AddonProp;
	addon->InitializeAsClientEntity( info->m_szAddonModel[0] ? info->m_szAddonModel : info->szWorldModel, false );
	addon->SetFadeMinMax( 400.0f, 500.0f );
	addon->SetParent( this, attachment );
	addon->SetLocalOrigin( vec3_origin );
	addon->SetLocalAngles( vec3_angle );
	m_addonModels.AddToTail( addon );
}

void C_TransitioningPlayer::ClientThink()
{
	BaseClass::ClientThink();
	Vector forward;
	AngleVectors( GetAbsAngles(), &forward );
	// These two zero-range random calls are present in 28A370 and still
	// consume the original RNG stream; do not optimize them away.
	const float xJitter = RandomFloat( 0.0f, 0.0f );
	const float yJitter = RandomFloat( 0.0f, 0.0f );
	m_viewtarget = GetAbsOrigin() + 100.0f * forward + Vector( xJitter, yJitter, 70.0f );
	SetFlexWeight( m_blinkController, 0.0f );

	if ( m_addonModels.Count() == 0 || z_transitioning_player_addon_rebuild.GetBool() )
	{
		ClearAddonModels();
		if ( m_primaryWeaponID )
			CreateAddonModel( m_primaryWeaponID );
		if ( m_secondaryWeaponID )
			CreateAddonModel( m_secondaryWeaponID );
		if ( m_hasMolotov )
			CreateAddonModel( 13 );
		if ( m_hasGrenade )
			CreateAddonModel( 14 );
		if ( m_hasFirstAidKit )
			CreateAddonModel( 12 );
		if ( m_hasPainPills )
			CreateAddonModel( 15 );
	}
	SetNextClientThink( gpGlobals->curtime + RandomFloat( 0.5f, 1.5f ) );
}

const char *C_TransitioningPlayer::GetIDString()
{
	static char text[64];
	Q_snprintf( text, sizeof( text ), "%s (LOADING)", m_playerName );
	return text;
}

Color C_TransitioningPlayer::GetTeamColor()
{
	if ( engine->GetPlayerForUserID( m_userID ) == 0 )
		s_transitioningPlayers.FindAndRemove( this );
	const int team = GetTeamNumber();
	return ( team == 2 || team == 4 ) ? g_ColorSurvivor : g_ColorZombie;
}

// Original 289C60: augment an existing trace only for a registry member
// whose center projects onto the segment and lies within 60 units. This is
// genuine hitbox clipping, not a detached ready/snapshot collision object.
void TraceTransitioningPlayers( const Vector &start, const Vector &end, unsigned int mask, ITraceFilter *filter, trace_t *trace )
{
	Ray_t ray;
	ray.Init( start, end );
	Vector direction = end - start;
	const float length = VectorNormalize( direction );
	FOR_EACH_VEC( s_transitioningPlayers, i )
	{
		C_TransitioningPlayer *player = s_transitioningPlayers[i];
		if ( !player || player->IsDormant() || ( filter && !filter->ShouldHitEntity( player, mask ) ) )
			continue;
		const Vector delta = player->WorldSpaceCenter() - start;
		const float along = DotProduct( delta, direction );
		if ( along < 0.0f || along > length )
			continue;
		if ( ( delta - along * direction ).Length() > 60.0f )
			continue;
		trace_t clipped;
		enginetrace->ClipRayToEntity( ray, mask | CONTENTS_HITBOX, player, &clipped );
		if ( clipped.fraction < trace->fraction )
			*trace = clipped;
	}
}
