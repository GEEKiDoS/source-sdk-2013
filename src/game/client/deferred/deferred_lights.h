//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Editor lights for the DX12 forward renderer. The vector is the
//          live set; ShadowMapsDX12 consumes it on the next frame.
//
//=============================================================================//
#ifndef DEFERRED_LIGHTS_H
#define DEFERRED_LIGHTS_H
#ifdef _WIN32
#pragma once
#endif

#include "igamesystem.h"
#include "mathlib/vector.h"
#include "tier1/utlvector.h"

struct DeferredLight_t
{
	Vector origin;
	QAngle angles;
	Vector color;		// linear 0..1
	float intensity;	// radiance = color * intensity
	float radius;
	float innerCone;	// degrees, half-angle (spot)
	float outerCone;	// degrees, half-angle (spot)
	bool spot;
	bool shadow;
	bool volumetric;	// §8 opt-in; volume still requires shadow and actual residency
	char cookie[64];	// texture name; empty = no cookie (spot only)
	int id;				// unique per map, assigned by the manager
};

class CDeferredLightManager : public CAutoGameSystemPerFrame
{
public:
	CDeferredLightManager();

	CUtlVector<DeferredLight_t> &Lights() { return m_Lights; }
	bool MapFile( char *path, int pathBytes ) const; // maps/<mapname>_lights.txt
	DeferredLight_t &Add( const DeferredLight_t &light );
	void Remove( int id );
	DeferredLight_t *Find( int id );
	bool Load();
	bool Save();

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Update( float frametime );

private:
	void UpdateCookies();
	void DestroyCookies();

	CUtlVector<DeferredLight_t> m_Lights;
	int m_NextId;
};

CDeferredLightManager &DeferredLights();

#endif
