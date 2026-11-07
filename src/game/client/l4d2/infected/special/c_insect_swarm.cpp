//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_inferno.h"
#include "tier0/memdbgon.h"

class C_InsectSwarm : public C_Inferno
{
public:
	DECLARE_CLASS( C_InsectSwarm, C_Inferno );
	DECLARE_CLIENTCLASS();
protected:
	virtual const char *GetParticleEffectName() const OVERRIDE { return "spitter_areaofdenial"; }
};

IMPLEMENT_CLIENTCLASS_DT( C_InsectSwarm, DT_InsectSwarm, CInsectSwarm )
END_RECV_TABLE()
