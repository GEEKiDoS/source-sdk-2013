//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_SOUND_MIX_LAYER_H
#define L4D2_C_SOUND_MIX_LAYER_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
class C_SoundMixLayer : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_SoundMixLayer, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	virtual datamap_t *GetPredDescMap() OVERRIDE;
	C_SoundMixLayer();
	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;
private:
	char m_iszMixLayerName[260];
	float m_fMixLayerLevel;
};
#endif // L4D2_C_SOUND_MIX_LAYER_H
