//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_POINT_SCRIPT_USE_TARGET_H
#define C_POINT_SCRIPT_USE_TARGET_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"

class C_PointScriptUseTarget : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_PointScriptUseTarget, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_PointScriptUseTarget();
	const char *GetUseString() const { return m_sUseString; }
	const char *GetUseSubString() const { return m_sUseSubString; }
	float GetPreviousProgressPercent() const { return m_flPreviousProgressPercent; }
	bool CanShowBuildPanel() const { return m_bCanShowBuildPanel; }

private:
	char m_sUseString[64];
	char m_sUseSubString[64];
	float m_flPreviousProgressPercent;
	bool m_bCanShowBuildPanel;
};

#endif // C_POINT_SCRIPT_USE_TARGET_H
