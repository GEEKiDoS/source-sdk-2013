//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/c_survivor_death_model.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "c_playerresource.h"
#include "activitylist.h"

#include "tier0/memdbgon.h"

// C:0x250E10, table0x7B9E10: DT_BaseFlex plus this one signed Int32.
IMPLEMENT_CLIENTCLASS_DT( C_SurvivorDeathModel, DT_SurvivorDeathModel, CSurvivorDeathModel )
	RecvPropInt( RECVINFO( m_nCharacterType ) ),
END_RECV_TABLE()

static CUtlVector< C_SurvivorDeathModel * > s_SurvivorDeathModels;

const CUtlVector< C_SurvivorDeathModel * > &C_SurvivorDeathModel::GetAll()
{
	return s_SurvivorDeathModels;
}

static Activity SurvivorDeathActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// C:0x251080/0x251120 register pointers before client entity handles exist.
C_SurvivorDeathModel::C_SurvivorDeathModel()
{
	SurvivorDeathActivity( "ACT_TERROR_DEFIBRILLATOR_SHOCK" );
	s_SurvivorDeathModels.AddToTail( this );
}

C_SurvivorDeathModel::~C_SurvivorDeathModel()
{
	s_SurvivorDeathModels.FindAndRemove( this );
}


int C_SurvivorDeathModel::ObjectCaps()
{
	return BaseClass::ObjectCaps() | FCAP_DONT_SAVE;
}

C_TerrorPlayer *C_SurvivorDeathModel::GetSurvivor() const
{
	return GetTerrorPlayerBySurvivorCharacter( GetCharacterType() );
}

C_SurvivorDeathModel *C_SurvivorDeathModel::FindForPlayer( const C_TerrorPlayer *pPlayer )
{
	if ( !pPlayer )
		return NULL;

	FOR_EACH_VEC( s_SurvivorDeathModels, i )
	{
		C_SurvivorDeathModel *pBody = s_SurvivorDeathModels[i];
		if ( pBody && pBody->GetSurvivor() == pPlayer )
			return pBody;
	}
	return NULL;
}

// C:0x250EA0 does not read or branch on the byte payload. Keep the unreliable
// presentation message separate from authoritative player restoration.
void C_SurvivorDeathModel::ReceiveMessage( int classID, bf_read &msg )
{
	if ( classID != GetClientClass()->m_ClassID )
	{
		BaseClass::ReceiveMessage( classID, msg );
		return;
	}

	SetSequence( SelectWeightedSequence( SurvivorDeathActivity( "ACT_TERROR_DEFIBRILLATOR_SHOCK" ) ) );
	ResetSequenceInfo();
	SetCycle( 0.0f );
}
