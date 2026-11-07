//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef INFO_REMARKABLE_H
#define INFO_REMARKABLE_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"
#include "tier1/utllinkedlist.h"

// Native29B670/29B9A0/29B960. No new network class, scripted setter or
// substitute discovery scan: the real Spawn/destructor own this registry.
class CInfoRemarkable : public CBaseEntity
{
public:
	DECLARE_CLASS( CInfoRemarkable, CBaseEntity );
	DECLARE_DATADESC();
	CInfoRemarkable();
	virtual ~CInfoRemarkable();
	virtual void Spawn() OVERRIDE;
	int GetRemarkCount() const { return m_nRemarkCount; }
	const char *GetRemarkContext() const { return STRING( m_szRemarkContext ); }
	void OnRemarkSpoken() { m_nRemarkCount = (int)( (unsigned int)m_nRemarkCount + 1u ); }
	void ResetRemarkCount() { m_nRemarkCount = 0; }
private:
	int m_nRemarkCount; // Native1068, Spawn/round reset0, successful34A0A0++ only.
	string_t m_szRemarkContext; // Native1072, sole contextsubject keyfield.
};
const CUtlLinkedList< CInfoRemarkable *, unsigned short > &GetTerrorRemarkableEntities();
void ResetTerrorIdleSpeechState(); // Native2FD3C0, SAME real idle-turn globals.
#endif
