//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_PHYSICSPROP_H
#define C_PHYSICSPROP_H
#ifdef _WIN32
#pragma once
#endif

#include "c_breakableprop.h"
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_PhysicsProp : public C_BreakableProp
{
	typedef C_BreakableProp BaseClass;
public:
	DECLARE_CLIENTCLASS();

	C_PhysicsProp();
	~C_PhysicsProp();

	virtual bool OnInternalDrawModel( ClientModelRenderInfo_t *pInfo );
	virtual void OnDataChanged( DataUpdateType_t type );

	// Authoritative server identity excludes inherited weapon/projectile classes,
	// gibs and non-solid multiplayer modes. Collision solidity remains live.
	bool CanOverrideShadowMapNoShadow() const
	{
		return m_bShadowMapOrdinaryPhysicsProp && IsSolid() && !IsEffectActive( EF_NODRAW );
	}

	// Depth geometry is standard C_BaseAnimating drawing. Do not reuse while
	// the sleep transition can still reject a draw during lighting recomputation.
	bool CanReuseRigidShadowDepth() const { return !m_bAwake && !m_bAwakeLastTime; }

protected:
	// Networked vars.
	bool m_bAwake;
	bool m_bAwakeLastTime;
	bool m_bShadowMapOrdinaryPhysicsProp;
	bool m_bShadowMapOverrideEligibleLast;
};

#endif // C_PHYSICSPROP_H 
