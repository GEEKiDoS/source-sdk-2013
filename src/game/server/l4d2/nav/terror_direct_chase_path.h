//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_DIRECT_CHASE_PATH_H
#define L4D2_TERROR_DIRECT_CHASE_PATH_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/Path/NextBotChasePath.h"

// The original game's DirectChasePath additionally checks the directed nav
// connection and remembers whether the current chase can reach its subject.
class TerrorDirectChasePath : public DirectChasePath
{
public:
	explicit TerrorDirectChasePath( SubjectChaseType chaseHow = DONT_LEAD_SUBJECT );
	virtual void Update( INextBot *me, CBaseEntity *subject, const IPathCost &cost, Vector *predictedSubjectPosition = NULL ) OVERRIDE;
	bool IsReachable() const { return m_isReachable; }
private:
	bool DirectChaseSubject( bool *predictedPositionComputed, Vector *predictedPosition, INextBot *me, CBaseEntity *subject );
	bool m_isReachable;
};

#endif
