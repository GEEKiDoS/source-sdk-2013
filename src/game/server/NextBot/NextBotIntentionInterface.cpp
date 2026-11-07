// NextBotIntentionInterface.cpp
// Interface for intentional thinking
// Author: Michael Booth, November 2007
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"

#include "NextBotInterface.h"
#include "NextBotIntentionInterface.h"

#if defined( L4D2 )
#include "tier1/fmtstr.h"
#endif
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( L4D2 )
// S39F790: one process-lifetime quiet1024 buffer, cleared per call. No extra
// behavior cache, separator, null filter or parallel formatter is introduced.
const char *IIntention::DebugString( void ) const
{
	static CFmtStrN< 1024, true > text;
	text.Clear();
	for ( INextBotEventResponder *responder = FirstContainedResponder(); responder;
		responder = NextContainedResponder( responder ) )
	{
		text.AppendFormat( "%s", responder->DebugString() );
	}
	return text.Get();
}
#endif


//------------------------------------------------------------------------------------------------------------------------
/**
 * Given a subject, return the world space position we should aim at
 */
Vector IIntention::SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const
{
	for ( INextBotEventResponder *sub = FirstContainedResponder(); sub; sub = NextContainedResponder( sub ) )
	{
		const IContextualQuery *query = dynamic_cast< const IContextualQuery * >( sub );
		if ( query )
		{
			// return the response of the first responder that gives a definitive answer
			Vector result = query->SelectTargetPoint( me, subject );
			if ( result != vec3_origin )
			{
				return result;
			}
		}
	}	

	// no answer, use a reasonable position
	Vector threatMins, threatMaxs;
	subject->CollisionProp()->WorldSpaceAABB( &threatMins, &threatMaxs );
	Vector targetPoint = subject->GetAbsOrigin();
	targetPoint.z += 0.7f * ( threatMaxs.z - threatMins.z );

	return targetPoint;
}


#if !defined( L4D2 )
//------------------------------------------------------------------------------------------------------------------------
/**
 * Given two threats, decide which one is more dangerous
 */
const CKnownEntity *IIntention::SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, const CKnownEntity *threat1, const CKnownEntity *threat2 ) const
{
	if ( !threat1 || threat1->IsObsolete() )
	{
		if ( threat2 && !threat2->IsObsolete() )
			return threat2;

		return NULL;
	}
	else if ( !threat2 || threat2->IsObsolete() )
	{
		return threat1;
	}

	for ( INextBotEventResponder *sub = FirstContainedResponder(); sub; sub = NextContainedResponder( sub ) )
	{
		const IContextualQuery *query = dynamic_cast< const IContextualQuery * >( sub );
		if ( query )
		{
			// return the response of the first responder that gives a definitive answer
			const CKnownEntity *result = query->SelectMoreDangerousThreat( me, subject, threat1, threat2 );
			if ( result )
			{
				return result;
			}
		}
	}

	// no specific decision was made - return closest threat as most dangerous
	float range1 = ( subject->GetAbsOrigin() - threat1->GetLastKnownPosition() ).LengthSqr();
	float range2 = ( subject->GetAbsOrigin() - threat2->GetLastKnownPosition() ).LengthSqr();

	if ( range1 < range2 )
	{
		return threat1;
	}

	return threat2;
}
#endif




