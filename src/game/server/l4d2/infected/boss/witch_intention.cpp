//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch_intention.h"
#include "l4d2/infected/boss/witch_behavior.h"

#include "tier0/memdbgon.h"

WitchIntention::WitchIntention( Infected *me ) : InfectedIntention( me )
{
}

// S:0x41ADA0. The inherited intention owns the one Behavior<Infected> stack.
Action< Infected > *WitchIntention::CreateInitialAction()
{
	return new WitchExecAction;
}
