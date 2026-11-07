//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/c_sound_mix_layer.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_SoundMixLayer, DT_SoundMixLayer, CSoundMixLayer )
	RecvPropString( RECVINFO( m_iszMixLayerName ) ),
	RecvPropFloat( RECVINFO( m_fMixLayerLevel ) ),
END_RECV_TABLE()

datamap_t *C_SoundMixLayer::GetPredDescMap() { return NULL; }
C_SoundMixLayer::C_SoundMixLayer() { m_iszMixLayerName[0] = '\0'; }
void C_SoundMixLayer::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate( updateType );
	// Native C248B10 then uses IVEngineClient GetMixLayerIndex(name) and
	// SetMixLayerLevel(index,level). The pinned target supplies neither
	// service. Main approved omission of this one engine mixer operation;
	// retain entity lifecycle and native network fields, not a fake mixer.
}
