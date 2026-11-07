//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_glow_entities.h"

#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_SurvivorRescue, DT_SurvivorRescue, CSurvivorRescue )
	RecvPropEHandle( RECVINFO( m_survivor ) ),
END_RECV_TABLE()

// C:2511F0/251470/251520/251300. This is the rescue representation, not the
// survivor-death-model list used by defibrillators.
static CUtlVector< C_SurvivorRescue * > s_RescueModels;

C_SurvivorRescue::C_SurvivorRescue()
{
	m_survivor = NULL;
	s_RescueModels.AddToTail( this );
}

C_SurvivorRescue::~C_SurvivorRescue()
{
	s_RescueModels.FindAndFastRemove( this );
}

C_SurvivorRescue *C_SurvivorRescue::FindForPlayer( const C_TerrorPlayer *pPlayer )
{
	FOR_EACH_VEC( s_RescueModels, i )
	{
		C_SurvivorRescue *pModel = s_RescueModels[i];
		if ( !pModel->IsDormant() && pModel->m_survivor.Get() == pPlayer )
			return pModel;
	}
	return NULL;
}

C_BaseEntity *TerrorGlow_FindRescueModel( const C_TerrorPlayer *pPlayer )
{
	return C_SurvivorRescue::FindForPlayer( pPlayer );
}

