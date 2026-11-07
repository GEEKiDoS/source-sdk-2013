//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_INTENTION_H
#define L4D2_WITCH_INTENTION_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/infected_intention.h"

class WitchIntention : public InfectedIntention
{
public:
	DECLARE_CLASS( WitchIntention, InfectedIntention );
	explicit WitchIntention( Infected *me );

protected:
	virtual Action< Infected > *CreateInitialAction() OVERRIDE;
};

#endif // L4D2_WITCH_INTENTION_H
