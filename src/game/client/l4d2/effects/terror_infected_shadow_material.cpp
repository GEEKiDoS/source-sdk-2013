//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "igamesystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier0/memdbgon.h"

// C51AE40/C3142B0/C3142D0. Native nameless CAutoGameSystem and material lifetime.
// C3139F0 returns this material to the original extended IClientRenderable
// fast-model/shadow renderer when either wound is enabled. That interface is
// absent on target (ledger259); no invented getter/vtable extension is added.
class CInfectedShadowMaterial : public CAutoGameSystem
{
public:
	virtual bool Init() OVERRIDE
	{
		m_Material.Init( "models/infected/common/L4D2/ci_shadowbuild", TEXTURE_GROUP_OTHER, true );
		return true;
	}
	virtual void Shutdown() OVERRIDE
	{
		m_Material.Shutdown();
	}
private:
	CMaterialReference m_Material;
};
static CInfectedShadowMaterial g_InfectedShadowMaterial;
