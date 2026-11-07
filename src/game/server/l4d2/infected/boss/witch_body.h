//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_BODY_H
#define L4D2_WITCH_BODY_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/zombiebot_body.h"

class WitchBody : public ZombieBotBody
{
public:
	DECLARE_CLASS( WitchBody, ZombieBotBody );
	explicit WitchBody( Infected *me );
	virtual void OnInjured( const CTakeDamageInfo &info ) OVERRIDE;
};

#endif // L4D2_WITCH_BODY_H
