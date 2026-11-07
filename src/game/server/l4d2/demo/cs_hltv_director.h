//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CS_HLTV_DIRECTOR_H
#define L4D2_CS_HLTV_DIRECTOR_H
#ifdef _WIN32
#pragma once
#endif
#include "hltvdirector.h"

class CCSHLTVDirector : public CHLTVDirector
{
public:
	DECLARE_CLASS( CCSHLTVDirector, CHLTVDirector );
	virtual const char **GetModEvents() OVERRIDE;
protected:
	virtual void CreateShotFromEvent( CHLTVGameEvent *event ) OVERRIDE;
};
#endif
