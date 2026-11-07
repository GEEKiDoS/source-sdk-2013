//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_VISION_H
#define L4D2_SURVIVOR_VISION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotVisionInterface.h"

class SurvivorVision : public IVision
{
public:
	explicit SurvivorVision( INextBot *bot );
	virtual void Reset() OVERRIDE;
	virtual float GetMaxVisionRange() const OVERRIDE;
	virtual float GetMinRecognizeTime() const OVERRIDE;
	virtual bool IsIgnored( CBaseEntity *subject ) const OVERRIDE;
	virtual bool IsVisibleEntityNoticed( CBaseEntity *subject ) const OVERRIDE;
	virtual float GetDefaultFieldOfView() const OVERRIDE;
	virtual float GetFieldOfView() const OVERRIDE;
	virtual void SetFieldOfView( float angle ) OVERRIDE;

private:
	// These are the original SurvivorVision overrides' fields (328..336),
	// independent of IVision's default field-of-view cache.
	float m_fieldOfView;
	float m_cosHalfFieldOfView;
	float m_minRecognizeTime;
};

#endif // L4D2_SURVIVOR_VISION_H
