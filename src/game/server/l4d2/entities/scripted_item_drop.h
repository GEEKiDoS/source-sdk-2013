#pragma once
#include "cbase.h"
#include "items.h"

class CScriptItem : public CItem
{
public:
	DECLARE_CLASS( CScriptItem, CItem );
	DECLARE_DATADESC();
	CScriptItem();
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
	virtual bool MyTouch( CBasePlayer *player ) OVERRIDE; // Native10DB00.
	virtual void OnPlayerPickup( CBaseEntity *other, CBasePlayer *player ) OVERRIDE;
	virtual bool CreateItemVPhysicsObject() OVERRIDE;
private:
	COutputEvent m_OnPlayerPickup;
	int m_iDesiredMoveType; // Native5208: explicit constructor6, not a save/key descriptor.
};
