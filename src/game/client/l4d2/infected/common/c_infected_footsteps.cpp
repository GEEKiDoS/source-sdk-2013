//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/c_infected.h"
#include "l4d2/infected/infected_activities.h"
#include "l4d2/sound/terror_footstep_surface.h"
#include "collisionproperty.h"
#include "debugoverlay_shared.h"
#include "eventlist.h"
#include "movevars_shared.h"
#include "physics.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "c_basecombatcharacter.h"
#include "l4d2/player/c_cs_player.h"
#include "l4d2/player/c_terrorplayer.h"
#include "particle_parse.h"
#include "tier0/memdbgon.h"

// Original client 0x4FEBB0; the server owns the matching replicated ConVar.
static ConVar sv_footstepinterval( "sv_footstepinterval", "0.22", 0x00006008 );


static ConVar sv_showfootsteps( "sv_showfootsteps", "0", 0x00006008,
	"Shows client (red) and server (green) footsteps (1=client-only, 2=server-only, 3=both)" );
static ConVar cl_footstep_fx( "cl_footstep_fx", "0", 0x0000000A );


surfacedata_t *C_BaseCombatCharacter::GetTerrorFootstepSurface( int &surfaceIndex )
{
	Vector mins, maxs;
	CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	trace_t trace;
	Ray_t ray;
	ray.Init( vec3_origin, Vector( 0.0f, 0.0f, -64.0f ), mins, maxs );
	CTraceFilterSimple filter( this, COLLISION_GROUP_PLAYER_MOVEMENT );
	enginetrace->TraceRay( ray, MASK_SOLID, &filter, &trace );
	// Native12F9D uses RED here; the generic SDK hull helper uses yellow.
	if ( r_visualizetraces.GetInt() != 0 )
		DebugDrawLine( trace.startpos, trace.endpos, 255, 0, 0, true, -1.0f );
	if ( trace.fraction != 1.0f )
	{
		surfaceIndex = trace.surface.surfaceProps;
		return physprops->GetSurfaceData( surfaceIndex );
	}
	surfaceIndex = -1;
	return NULL;
}

const char *C_BaseCombatCharacter::GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force )
{
	return sound;
}

bool C_BaseCombatCharacter::ShouldPlayTerrorFootstepSound() const
{
	return true;
}

bool C_BaseCombatCharacter::IsTerrorFootstepSurfaceValid( float speed, bool force ) const
{
	return true;
}

float C_BaseCombatCharacter::GetTerrorFootstepRunThreshold() const
{
	return 110.0f;
}

void C_BaseCombatCharacter::OnTerrorFootstep( const Vector &origin, bool left, bool force )
{
	// Native1001E060 is genuinely RET12 on ALL25 recovered combat families.
}

void C_BaseCombatCharacter::PlayTerrorFootstepSound( const Vector &origin, bool left, bool shallow, bool wading, bool force )
{
	if ( sv_footsteps.GetFloat() == 0.0f )
		return;
	const MoveType_t moveType = GetMoveType();
	if ( moveType == MOVETYPE_NOCLIP || moveType == MOVETYPE_OBSERVER ||
		( gpGlobals->curtime < m_footstepTimer.m_timestamp && !force ) )
		return;
	if ( !ShouldPlayTerrorFootstepSound() )
		return;
	Vector velocity;
	EstimateAbsVelocity( velocity );
	if ( !force )
		velocity.z = 0.0f;
	const float speed = sqrtf( ( velocity.y * velocity.y + velocity.z * velocity.z ) + velocity.x * velocity.x );
	if ( speed < 100.0f && !IsPlayer() && speed * 0.0099999998f < 0.1f )
		return;

	int surfaceIndex;
	surfacedata_t *surface;
	if ( GetMoveType() == MOVETYPE_LADDER )
	{
		C_BasePlayer *player = ToBasePlayer( this );
		if ( player && player->GetLadderSurfaceProps() != 0 )
			surfaceIndex = player->GetLadderSurfaceProps();
		else
			surfaceIndex = physprops->GetSurfaceIndex( "ladder" );
		surface = physprops->GetSurfaceData( surfaceIndex );
	}
	else if ( wading )
	{
		surfaceIndex = physprops->GetSurfaceIndex( "wade" );
		surface = physprops->GetSurfaceData( surfaceIndex );
	}
	else if ( shallow )
	{
		surfaceIndex = physprops->GetSurfaceIndex( "water" );
		surface = physprops->GetSurfaceData( surfaceIndex );
	}
	else
	{
		if ( !IsTerrorFootstepSurfaceValid( speed, force ) )
			return;
		surface = GetTerrorFootstepSurface( surfaceIndex );
	}
	if ( !surface )
		return;
	const double runThreshold = static_cast< double >( GetTerrorFootstepRunThreshold() );
	const char *baseSound = TerrorFootstepSurfaceSound( surfaceIndex, left,
		!( static_cast< double >( speed ) < runThreshold ) );
	if ( !baseSound )
		return;
	const char *soundName = GetTerrorFootstepSoundName( baseSound, !left, speed, force );
	CSoundParameters params;
	if ( !GetParametersForSound( soundName, params, NULL ) && !GetParametersForSound( baseSound, params, NULL ) )
		return;
	m_footstepTimer.Start( sv_footstepinterval.GetFloat() );
	C_RecipientFilter filter;
	filter.AddRecipientsByPAS( origin );
	EmitSound_t sound;
	sound.m_nChannel = params.channel;
	sound.m_pSoundName = params.soundname;
	sound.m_flVolume = params.volume;
	sound.m_SoundLevel = params.soundlevel;
	sound.m_nPitch = params.pitch;
	sound.m_nFlags = 0;
	sound.m_pOrigin = &origin;
	EmitSound( filter, entindex(), sound );

	if ( cl_footstep_fx.GetInt() != 0 )
	{
		const char *effect = NULL;
		switch ( surface->game.material )
		{
		case 10: effect = "footstep_puddle"; break;
		case 68: case 78: case 87: effect = "footstep_dirt"; break;
		case 74: case 79: effect = "footstep_grass"; break;
		case 75: effect = "footstep_mud"; break;
		case 83: effect = "footstep_wet"; break;
		}
		if ( effect )
		{
			Vector effectOrigin;
			QAngle effectAngles;
			GetAttachment( left ? m_leftFootAttachment : m_rightFootAttachment, effectOrigin, effectAngles );
			DispatchParticleEffect( effect, effectOrigin, effectAngles );
		}
	}
	OnTerrorFootstep( origin, left, force );
	if ( sv_showfootsteps.GetInt() & 1 )
	{
		Vector start = origin, end = origin;
		start.z -= 5.0f;
		end.z += 5.0f;
		NDebugOverlay::Line( start, end, 255, 0, 0, true, 6.0f );
		start = origin;
		end = origin;
		start.y -= 5.0f;
		end.y += 5.0f;
		NDebugOverlay::Line( start, end, 255, 0, 0, true, 6.0f );
		start = origin;
		end = origin;
		start.x -= 5.0f;
		end.x += 5.0f;
		NDebugOverlay::Line( start, end, 255, 0, 0, true, 6.0f );
	}
}

// Original C_Infected::FireEvent, client 0x313A10. Server animation events own
// damage; this client consumer only supplies the original animation audio.
void C_Infected::FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options )
{
	if ( event == AE_SV_PLAYSOUND )
	{
		CLocalPlayerFilter filter;
		// Original +284 is 0x1BEE0 (collision OBB center transformed to world),
		// not EyePosition and not the animation dispatch's origin argument.
		Vector vecSoundOrigin = WorldSpaceCenter();
		EmitSound( filter, entindex(), options, &vecSoundOrigin );
		return;
	}

	if ( event == AE_FOOTSTEP_LEFT || event == AE_FOOTSTEP_RIGHT )
	{
		bool bLeftFoot = event == AE_FOOTSTEP_LEFT;
		bool bShallowWater = GetWaterLevel() == WL_Feet;
		bool bWading = GetWaterLevel() >= WL_Waist;
		PlayTerrorFootstepSound( origin, bLeftFoot, bShallowWater, bWading, false );

		// Original model type 16 is the clown. This check follows the helper
		// even if its timer/sv_footsteps gate rejected the ordinary step.
		if ( !bLeftFoot && m_Gender == 16 && !bShallowWater && !bWading )
			EmitSound( "Clown.Default.RunRight" );
		return;
	}

	BaseClass::FireEvent( origin, angles, event, options );
}

const char *C_Infected::GetTerrorFootstepSoundName( const char *surfaceSound, bool rightFoot, float speed, bool force )
{
	static char sound[64];
	Q_snprintf( sound, sizeof( sound ), "Infected.%s", surfaceSound );
	return sound;
}
