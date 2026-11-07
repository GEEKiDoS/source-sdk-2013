#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "team.h"
#include "usercmd.h"
#include "collisionutils.h"
#include "tier0/memdbgon.h"

ConVar sv_player_max_separation_force( "sv_player_max_separation_force", "500", 0x00000006 );
extern ConVar sb_pushscale;
extern ConVar z_sidespeed;
extern ConVar z_forwardspeed;
extern ConVar z_backspeed;

// S2CA740. This modifies the real user command, not actor position/velocity.
// All state gates belong to their existing player/animation owners.
void CTerrorPlayer::AvoidPlayers( CUserCmd *cmd )
{
	if ( !IsAlive() ||
		!( m_noAvoidanceTimer.m_timestamp.Get() <= 0.0f || gpGlobals->curtime >= m_noAvoidanceTimer.m_timestamp.Get() ) ||
		GetTerrorAnimState()->IsUseGestureActive() || IsBeingPounced() || IsPummelVictim() ||
		GetJockeyVictim() || IsJockeyVictim() || CanBeRevived() || IsGhost() ) return;
	CTeam *team = GetTeam();
	if ( !team ) return;

	Vector center = GetAbsOrigin();
	Vector mins = GetPlayerMins(), maxs = GetPlayerMaxs();
	mins.x += center.x; mins.y += center.y;
	maxs.x += center.x; maxs.y += center.y;
	center.z = ( maxs.z - mins.z ) * 0.5f + center.z;
	mins.z += center.z; maxs.z += center.z;

	CTerrorPlayer *overlap = NULL;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( team->GetPlayer( i ) );
		if ( !player || player == this || player->CanBeRevived() || player->IsGhost() || player->IsSolidFlagSet( FSOLID_NOT_SOLID ) ) continue;
		const Vector &origin = player->GetAbsOrigin();
		Vector otherMins = player->GetPlayerMins(), otherMaxs = player->GetPlayerMaxs();
		const float middleZ = ( otherMaxs.z - otherMins.z ) * 0.5f + origin.z;
		otherMins.x += origin.x; otherMins.y += origin.y; otherMins.z += middleZ;
		otherMaxs.x += origin.x; otherMaxs.y += origin.y; otherMaxs.z += middleZ;
		if ( IsBoxIntersectingBox( mins, maxs, otherMins, otherMaxs ) )
		{
			overlap = player;
			break;
		}
	}
	if ( !overlap || ( !IsBot() && overlap->IsBot() ) ) return;
	const bool botYieldingToHuman = IsBot() && !overlap->IsBot();
	const Vector toPlayer = overlap->WorldSpaceCenter() - center;
	const Vector extent = overlap->WorldAlignMaxs() - overlap->WorldAlignMins();
	const float width = sqrtf( extent.y * extent.y + 0.0f * 0.0f + extent.x * extent.x );
	const float range = sqrtf( ( toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z ) + toPlayer.x * toPlayer.x );
	float amount = RemapValClamped( range, width, 0.0f, 0.0f, (float)sv_player_max_separation_force.GetInt() );
	if ( botYieldingToHuman ) amount *= sb_pushscale.GetFloat();
	if ( !( amount >= 0.01f ) ) return;

	Vector side;
	if ( GetAbsVelocity().Length2DSqr() <= 0.1f )
	{
		const QAngle flatAngles( 0.0f, cmd->viewangles.y, cmd->viewangles.z );
		AngleVectors( flatAngles, NULL, &side, NULL );
	}
	else
	{
		CrossProduct( Vector( 0, 0, 1 ), GetAbsVelocity(), side );
		VectorNormalize( side );
	}
	const float signedAmount = DotProduct( side, toPlayer ) >= 0.0f ? -amount : amount;
	Vector force = side * signedAmount;
	float maximum = MaxSpeed();
	if ( ( GetFlags() & FL_DUCKING ) && GetGroundEntity() ) maximum *= 1.3333334f;
	if ( force.LengthSqr() > maximum * maximum )
	{
		VectorNormalize( force );
		force *= maximum;
	}
	const QAngle flatAngles( 0.0f, cmd->viewangles.y, cmd->viewangles.z );
	Vector forward, right;
	AngleVectors( flatAngles, &forward, &right, NULL );
	Vector direction = force;
	VectorNormalize( direction );
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT );
	const Vector start = GetAbsOrigin() + direction * 20.0f;
	const Vector end = start - Vector( 0, 0, GetStepSize() );
	trace_t trace;
	UTIL_TraceHull( start, end, GetPlayerMins(), GetPlayerMaxs(), PlayerSolidMask( false ), &filter, &trace );
	if ( !trace.DidHit() ) return;

	// Native normalizes the capped vector and still uses the ORIGINAL amount
	// for both projections. Do not repair this into a capped impulse magnitude.
	const float forwardMove = DotProduct( forward, direction ) * amount + cmd->forwardmove;
	const float sideMove = DotProduct( right, direction ) * amount + cmd->sidemove;
	cmd->forwardmove = forwardMove;
	cmd->sidemove = sideMove;
	float forwardScale = 1.0f;
	const float forwardLimit = fabsf( z_forwardspeed.GetFloat() );
	if ( forwardMove <= forwardLimit )
	{
		const float backwardLimit = fabsf( z_backspeed.GetFloat() );
		if ( -backwardLimit > forwardMove ) forwardScale = backwardLimit / fabsf( forwardMove );
	}
	else forwardScale = forwardLimit / forwardMove;
	float sideScale = 1.0f;
	const float absoluteSide = fabsf( sideMove );
	const float sideLimit = fabsf( z_sidespeed.GetFloat() );
	if ( absoluteSide > sideLimit ) sideScale = sideLimit / absoluteSide;
	if ( sideScale <= forwardScale ) forwardScale = sideScale;
	cmd->forwardmove = forwardMove * forwardScale;
	cmd->sidemove = sideMove * forwardScale;
}
