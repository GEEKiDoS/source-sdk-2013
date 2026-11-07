//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_L4D2_WITCH_H
#define C_L4D2_WITCH_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/c_infected.h"

struct dlight_t;
struct ShaderStencilState_t;

class C_Witch : public C_Infected
{
public:
	DECLARE_CLASS( C_Witch, C_Infected );
	DECLARE_CLIENTCLASS();

	C_Witch();
	virtual ~C_Witch();
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void UpdatePoseParameters() OVERRIDE; // C318030, not normal animation202.
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual int BloodColor() OVERRIDE;
	virtual int GetZombieClass() const OVERRIDE { return 7; }
	virtual const char *GetTerrorFootstepSoundName( const char *surfaceSound, bool rightFoot, float speed, bool force ) OVERRIDE;
	virtual float GetTerrorFootstepRunThreshold() const OVERRIDE { return 0.0f; }

	// C318250/C317B40. The actual native outline owner dispatches these;
	// no SDK CGlowObjectManager RGB-only replacement is involved.
	virtual void GetGlowColor( float *r, float *g, float *b, float *a ) OVERRIDE;
	virtual bool GetStencilState( ShaderStencilState_t &state ) OVERRIDE;
	void MarkGlowDrawFrame( int slot ) { m_glowDrawFrame[slot] = gpGlobals->framecount; }
	int GetLastGlowDrawFrame( int slot ) const { return m_glowDrawFrame[slot]; }
	float GetRage() const { return m_rage; }
	float GetWanderRage() const { return m_wanderRage; }

private:
	void UpdateEyeLight();
	int m_ragePose;
	float m_rage;
	float m_smoothedRage;
	int m_wanderRagePose;
	float m_wanderRage;
	float m_smoothedWanderRage;
	dlight_t *m_eyeLight;
	int m_glowDrawFrame[2];
};

#endif // C_L4D2_WITCH_H
