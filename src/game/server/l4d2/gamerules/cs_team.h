//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef CS_TEAM_H
#define CS_TEAM_H
#ifdef _WIN32
#pragma once
#endif

#include "team.h"

class CCSTeam : public CTeam
{
	DECLARE_CLASS( CCSTeam, CTeam );
public:
	DECLARE_SERVERCLASS();

	virtual void Precache( void ) OVERRIDE;
	virtual void Think( void ) OVERRIDE;
};

#endif // CS_TEAM_H
