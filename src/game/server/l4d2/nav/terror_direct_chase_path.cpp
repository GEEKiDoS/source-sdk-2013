//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "NextBot/NextBotBodyInterface.h"
#include "nav_mesh.h"
#include "tier0/memdbgon.h"

ConVar nb_direct_chase_nav_check( "nb_direct_chase_nav_check", "1", FCVAR_CHEAT | FCVAR_GAMEDLL,
	"Set to zero to stop bots that use the direct chase path follower from performing a 2D check through the nav to validate a direct chase." );

TerrorDirectChasePath::TerrorDirectChasePath( SubjectChaseType chaseHow ) :
	DirectChasePath( chaseHow ), m_isReachable( false )
{
}

// Original server 103BAA30. In particular, an invalid chase is not silently
// replaced by a straight-line movement command.
void TerrorDirectChasePath::Update( INextBot *me, CBaseEntity *subject, const IPathCost &cost, Vector *predictedSubjectPosition )
{
	Assert( !predictedSubjectPosition );
	bool predictedPositionComputed;
	Vector predictedPosition;
	if ( DirectChaseSubject( &predictedPositionComputed, &predictedPosition, me, subject ) )
	{
		m_isReachable = true;
	}
	else
	{
		ChasePath::Update( me, subject, cost, predictedPositionComputed ? &predictedPosition : NULL );
		m_isReachable = IsValid();
	}
	NotifyVictim( me, subject );
}

// Original server 103B8BD0. The predicted position is retained even when a
// traversability or nav-connection test rejects the direct chase.
bool TerrorDirectChasePath::DirectChaseSubject( bool *predictedPositionComputed, Vector *predictedPosition, INextBot *me, CBaseEntity *subject )
{
	*predictedPositionComputed = false;
	ILocomotion *mover = me->GetLocomotionInterface();
	if ( me->IsImmobile() || mover->IsScrambling() )
		return false;
	if ( IsDiscontinuityAhead( me, CLIMB_UP ) )
		return false;
	if ( IsDiscontinuityAhead( me, JUMP_OVER_GAP ) )
		return false;

	const Vector leadSubjectPosition = PredictSubjectPosition( me, subject );
	*predictedPositionComputed = true;
	*predictedPosition = leadSubjectPosition;
	if ( !mover->IsPotentiallyTraversable( mover->GetFeet(), leadSubjectPosition, ILocomotion::IMMEDIATELY, NULL ) )
		return false;
	if ( nb_direct_chase_nav_check.GetBool() && !TheNavMesh->IsConnected( mover->GetFeet(), leadSubjectPosition ) )
		return false;

	mover->FaceTowards( leadSubjectPosition );
	mover->Approach( leadSubjectPosition, 1.0f );
	me->GetBodyInterface()->AimHeadTowards( subject, IBody::BORING, 0.0f, NULL, NULL, true, 0.25f );
	Invalidate();
	return true;
}
