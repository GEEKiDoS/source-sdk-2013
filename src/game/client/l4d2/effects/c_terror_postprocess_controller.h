//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_TERROR_POSTPROCESS_CONTROLLER_H
#define L4D2_C_TERROR_POSTPROCESS_CONTROLLER_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"

// Actual CPostProcessController / DT_PostProcessController receive class.
// Receive registration remains in viewpostprocess.cpp. Native lifecycle:
// constructor C8D410; PostDataUpdate C8D4B0; deleting destructor C8D530.
class C_TerrorPostProcessController : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_TerrorPostProcessController, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	C_TerrorPostProcessController()
	{
		const float initial[8] = { 0, 0, 1, 2, 0, 0, 1, 1 };
		Q_memcpy( m_Values, initial, sizeof( m_Values ) );
		m_fFadeTime = 2.0f;
		m_bMaster = false;
		if ( !s_pMaster ) s_pMaster = this;
	}
	virtual ~C_TerrorPostProcessController()
	{
		if ( s_pMaster == this ) s_pMaster = NULL;
	}
	virtual void PostDataUpdate( DataUpdateType_t type ) OVERRIDE
	{
		BaseClass::PostDataUpdate( type );
		if ( m_bMaster ) s_pMaster = this;
	}
	static C_TerrorPostProcessController *GetMaster() { return s_pMaster; }
	const float *GetValues() const { return m_Values; }
	float GetFadeTime() const { return m_fFadeTime; }

	float m_Values[8];
	float m_fFadeTime;
	bool m_bMaster;
private:
	static C_TerrorPostProcessController *s_pMaster;
};
#endif // L4D2_C_TERROR_POSTPROCESS_CONTROLLER_H
