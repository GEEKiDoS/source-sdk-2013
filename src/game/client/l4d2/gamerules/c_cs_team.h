//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef C_CS_TEAM_H
#define C_CS_TEAM_H
#ifdef _WIN32
#pragma once
#endif

#include "c_team.h"

class C_CSTeam : public C_Team
{
	DECLARE_CLASS( C_CSTeam, C_Team );
public:
	DECLARE_CLIENTCLASS();
};

#endif // C_CS_TEAM_H
