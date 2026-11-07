//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_SURVIVOR_DEATH_MODEL_H
#define C_SURVIVOR_DEATH_MODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseflex.h"
#include "l4d2/terror_shareddefs.h"
#include "tier1/utlvector.h"

class C_TerrorPlayer;

class C_SurvivorDeathModel : public C_BaseFlex
{
public:
	DECLARE_CLASS( C_SurvivorDeathModel, C_BaseFlex );
	DECLARE_CLIENTCLASS();

	C_SurvivorDeathModel();
	virtual ~C_SurvivorDeathModel();
	virtual void ReceiveMessage( int classID, bf_read &msg ) OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;

	int GetCharacterType() const { return m_nCharacterType; }
	C_TerrorPlayer *GetSurvivor() const;
	static C_SurvivorDeathModel *FindForPlayer( const C_TerrorPlayer *pPlayer );
	static const CUtlVector< C_SurvivorDeathModel * > &GetAll();

private:
	int m_nCharacterType;
};

#endif // C_SURVIVOR_DEATH_MODEL_H
