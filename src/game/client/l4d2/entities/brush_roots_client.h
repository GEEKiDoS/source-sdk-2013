//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BRUSH_ROOTS_CLIENT_H
#define L4D2_BRUSH_ROOTS_CLIENT_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/entities/c_buttons.h"

class C_BaseTrigger : public C_BaseToggle
{
	DECLARE_CLASS( C_BaseTrigger, C_BaseToggle );
public:
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // Native primary17 C0A2BF0.
	bool m_bClientSidePredicted;
};
class C_ScriptBaseButton : public C_BaseButton
{
	DECLARE_CLASS( C_ScriptBaseButton, C_BaseButton );
public:
	DECLARE_CLIENTCLASS();
};
#endif // L4D2_BRUSH_ROOTS_CLIENT_H
