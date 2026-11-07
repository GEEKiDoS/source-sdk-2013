//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_VISION_H
#define L4D2_WITCH_VISION_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/zombiebot_vision.h"

class WitchVision : public ZombieBotVision
{
public:
	DECLARE_CLASS( WitchVision, ZombieBotVision );
	explicit WitchVision( Infected *me );
	virtual bool IsIgnored( CBaseEntity *subject ) const OVERRIDE;
	virtual float GetMaxVisionRange() const OVERRIDE;
};

#endif // L4D2_WITCH_VISION_H
