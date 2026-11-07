//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_ADDON_PROP_H
#define L4D2_C_ADDON_PROP_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"
#include "c_breakableprop.h"
#include "l4d2/terror_shareddefs.h"

// Original client 2411C0. Cached absolute transforms remain readable while
// absolute queries are disabled during threaded rendering.
class C_AddonProp : public C_BreakableProp
{
public:
	DECLARE_CLASS( C_AddonProp, C_BreakableProp );
	C_AddonProp();
	void SetCopyParentAnimation( bool copy ) { m_bCopyParentAnimation = copy; }
	bool ShouldCopyParentAnimation() const { return m_bCopyParentAnimation; }
	void SetParent( C_BaseEntity *parent, int attachment = 0 );
	Activity TranslateActivity( Activity activity ) const;
	void SetActivityCycle( Activity activity, float cycle );
	virtual bool GetStencilState( ShaderStencilState_t &state ) OVERRIDE;
	virtual const Vector &GetAbsOrigin() const OVERRIDE;
	virtual const QAngle &GetAbsAngles() const OVERRIDE;
	virtual void GetRenderBounds( Vector &mins, Vector &maxs ) OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;

private:
	bool m_bCopyParentAnimation;
	Activity m_activity;
	mutable Vector m_cachedAbsOrigin;
	mutable QAngle m_cachedAbsAngles;
};

#endif
