//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// Native factories3189D0..318D90 allocate only C_TerrorPlayer storage,
// call its25BE90 constructor, publish each genuine leaf's five vtables,
// then invoke inherited Init. Native deleting destructors call25F5D0.
// The365-entry primary tables differ only at the destructor and a leaf
// RET0 with the same behavior as inherited251A20. No leaf state, animation,
// prediction, damage presentation or removal owner is duplicated here.
// Full constructor/destructor/table proof: native_client_actor_leaf_lifetime_reuse.json.

// C:3189D0 factory, 318A40 destructor, 1060CD8C primary.
class C_Tank : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Tank, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Tank, DT_Tank, Tank ) // C:318600, one real baseclass property.
END_RECV_TABLE()

// C:318A70 factory, 318AE0 destructor, 1060D454 primary.
class C_Boomer : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Boomer, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Boomer, DT_Boomer, Boomer ) // C:318690, one real baseclass property.
END_RECV_TABLE()

// C:318B10 factory, 318B80 destructor, 1060DB24 primary.
class C_Smoker : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Smoker, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Smoker, DT_Smoker, Smoker ) // C:318720, one real baseclass property.
END_RECV_TABLE()

// C:318BB0 factory, 318C20 destructor, 1060E1F4 primary.
class C_Hunter : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Hunter, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Hunter, DT_Hunter, Hunter ) // C:3187B0, one real baseclass property.
END_RECV_TABLE()

// C:318C50 factory, 318CC0 destructor, 1060E8C4 primary.
class C_Charger : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Charger, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Charger, DT_Charger, Charger ) // C:318840, one real baseclass property.
END_RECV_TABLE()

// C:318CF0 factory, 318D60 destructor, 1060EF94 primary.
class C_Spitter : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Spitter, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Spitter, DT_Spitter, Spitter ) // C:3188D0, one real baseclass property.
END_RECV_TABLE()

// C:318D90 factory, 318E00 destructor, 1060F664 primary.
class C_Jockey : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_Jockey, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();
};
IMPLEMENT_CLIENTCLASS_DT( C_Jockey, DT_Jockey, Jockey ) // C:318960, one real baseclass property.
END_RECV_TABLE()

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
