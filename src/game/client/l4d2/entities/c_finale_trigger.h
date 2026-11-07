//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_FINALE_TRIGGER_H
#define C_FINALE_TRIGGER_H
#ifdef _WIN32
#pragma once
#endif
#include "c_breakableprop.h"
class C_FinaleTrigger : public C_BreakableProp
{
public:
	DECLARE_CLASS( C_FinaleTrigger, C_BreakableProp );
	DECLARE_CLIENTCLASS();
	virtual int ObjectCaps() OVERRIDE;
	virtual bool IsPotentiallyUseable() OVERRIDE { return true; } // C:263330
	virtual bool IsUsable( C_BaseEntity *user ) { return true; } // C:263320
	virtual const wchar_t *GetUseString( C_BaseEntity *user );
private:
	bool m_bDisabled;
	int m_triggerState;
};
#endif
