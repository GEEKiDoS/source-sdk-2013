//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "tier0/memdbgon.h"

extern ConVar ammo_pack_use_duration; // Sole GapConsoleClient registration.

class C_ItemAmmoPack : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( C_ItemAmmoPack, CBaseBackpackItem );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	virtual void Precache() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE;
	virtual float GetActionDuration() const OVERRIDE;
	virtual bool CanPerformAction( int mode, CTerrorPlayer *player, CBaseEntity *target ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *player, CBaseEntity *target, float duration ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &count ) OVERRIDE;
};
IMPLEMENT_CLIENTCLASS_DT( C_ItemAmmoPack, DT_AmmoPack, CItemAmmoPack )
END_RECV_TABLE()
#ifndef NO_ENTITY_PREDICTION
datamap_t C_ItemAmmoPack::m_PredMap = { 0, 0, "CItemAmmoPack", &CBaseBackpackItem::m_PredMap };
datamap_t *C_ItemAmmoPack::GetPredDescMap() { return &m_PredMap; }
BEGIN_PREDICTION_DATA_GUTS( C_ItemAmmoPack )
#else
BEGIN_PREDICTION_DATA( C_ItemAmmoPack )
#endif
END_PREDICTION_DATA()

int C_ItemAmmoPack::GetWeaponID() const
{
	return TERROR_WEAPON_AMMO_PACK;
}
float C_ItemAmmoPack::GetActionDuration() const
{
	return ammo_pack_use_duration.GetFloat();
}
void C_ItemAmmoPack::OnActionStarted( int mode, CTerrorPlayer *player, CBaseEntity *target, float duration )
{
	// C2FD8E0 is a genuine native nullsub.
}

void C_ItemAmmoPack::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Player.AmmoPackUse" );
}
bool C_ItemAmmoPack::CanPerformAction( int mode, CTerrorPlayer *player, CBaseEntity *target )
{
	// C2FD8A0: target is a StartAction precondition, no new life/health gate.
	return target->IsPlayer() && BaseClass::CanPerformAction( mode, player, target );
}
Activity C_ItemAmmoPack::TranslateViewModelActivity( Activity activity )
{
	// C2FD8F0: repeated native mapper calls retain their two-stage mappings.
	static const int translations[][2] =
	{
		{ 820, 918 }, { 822, 916 }, { 824, 920 }, { 826, 924 }, { 828, 922 }, { 830, 926 },
		{ 916, 917 }, { 918, 919 }, { 920, 921 }, { 922, 923 }, { 924, 925 }, { 926, 927 },
		{ 179, 1446 }, { 183, 1445 }, { 1250, 1448 }, { 1446, 1447 }, { 1448, 1449 }
	};
	const int original = TerrorActivityToOriginal( activity );
	for ( int i = 0; i < ARRAYSIZE( translations ); ++i )
		if ( original == translations[i][0] ) return TerrorActivityFromOriginal( translations[i][1] );
	return BaseClass::TranslateViewModelActivity( activity );
}
acttable_t *C_ItemAmmoPack::ActivityList( int &count )
{
	// C6F0AB8, C2FD860/2FD870. Same mounted activity registry.
	static const int originals[][2] =
	{
		{ 1, 1050 }, { 45, 1051 }, { 12, 1055 }, { 6, 1054 }, { 10, 1053 }, { 508, 1052 },
		{ 510, 1056 }, { 511, 1057 }, { 512, 1050 }, { 513, 1054 }, { 514, 1053 }, { 29, 520 }
	};
	static acttable_t table[ARRAYSIZE( originals )];
	static bool initialized = false;
	if ( !initialized )
	{
		for ( int i = 0; i < ARRAYSIZE( originals ); ++i )
		{
			table[i].baseAct = TerrorActivityFromOriginal( originals[i][0] );
			table[i].weaponAct = TerrorActivityFromOriginal( originals[i][1] );
			table[i].required = false;
		}
		initialized = true;
	}
	count = ARRAYSIZE( originals );
	return table;
}
