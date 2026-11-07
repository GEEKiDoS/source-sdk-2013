//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "precache_register.h"
#include "l4d2/content/terror_precache_client.h"
#include "l4d2/weapons/terror_precache_weapon.h"
#include "particlemgr.h"
#include "fx.h"
#include "tier0/memdbgon.h"

int g_nInfectedBloodGibIndex;
int g_nChainsawBloodFirstPersonIndex;
int g_nChainsawBloodThirdPersonIndex;
int g_nChainsawSmokeFirstPersonIndex;
int g_nChainsawSmokeThirdPersonIndex;
int g_nMolotovHeldIndex;
int g_nPipeBombFuseIndex;

extern short g_sModelIndexFireball;
extern short g_sModelIndexWExplosion;
extern short g_sModelIndexSmoke;
extern short g_sModelIndexBubbles;
extern short g_sModelIndexBloodSpray;
extern short g_sModelIndexBloodDrop;
extern short g_sModelIndexLaser;
extern short g_sModelIndexLaserDot;

// C17A1C0 type7: PMaterialHandle is a pointer, not a 32-bit resource index.
void PrecacheParticleMaterial( const char *pName, void *pOutput )
{
	PMaterialHandle material = ParticleMgr()->GetPMaterial( pName );
	if ( pOutput )
		*static_cast< PMaterialHandle * >( pOutput ) = material;
}

// C A8910: named effects are not members of the per-level global list.
void PrecacheEffect( const char *pName )
{
	CPrecacheHandler handler;
	CPrecacheRegister::PrecacheSystem( &handler, PRECACHE_SYSTEM_EFFECTS, pName, true, -1, true );
}

// One native wrapper per census class. Explicit CRT ranks preserve the
// original prepend order independently of target translation-unit order.

// C3132A0; object 0x106f4af4; .CRT 52CA8C.
namespace C_InfectedPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "C_Infected", 0x52CA8C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "blood_gib_1", precache, resourceList, &g_nInfectedBloodGibIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C313270; object 0x106f4ae4; .CRT 52CA58.
namespace PrecacheInfectedPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheInfected", 0x52CA58 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "models/infected/common/L4D2/ci_shadowbuild", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0A7000; object 0x106c474c; .CRT 529C00.
namespace WeaponSpritesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "WeaponSprites", 0x529C00 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		int modelIndex;
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/zerogxplode.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexFireball = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/WXplo1.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexWExplosion = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/steam1.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexSmoke = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/bubble.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexBubbles = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/bloodspray.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexBloodSpray = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/blood.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexBloodDrop = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/laserbeam.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexLaser = static_cast< short >( modelIndex );
		pHandler->Precache( RESOURCETYPE_MODEL, "sprites/laserdot.vmt", precache, resourceList, &modelIndex );
		g_sModelIndexLaserDot = static_cast< short >( modelIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F1830; object 0x106d5994; .CRT 529EC0.
namespace BloodImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "BloodImpact", 0x529EC0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "blood_impact_red_01", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C203280; object 0x106e1328; .CRT 52B02C.
namespace BoltImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "BoltImpact", 0x52B02C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MODEL, "models/crossbow_bolt.mdl", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F2D80; object 0x106ef39c; .CRT 52C3F0.
namespace BulletEffectsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "BulletEffects", 0x52C3F0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "impact_ricochet", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22FFF0; object 0x106e335c; .CRT 52B534.
namespace CS_MuzzleFlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "CS_MuzzleFlash", 0x52B534 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230000; object 0x106e336c; .CRT 52B53C.
namespace CS_MuzzleFlash_XPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "CS_MuzzleFlash_X", 0x52B53C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F6CB0; object 0x106ef70c; .CRT 52C454.
namespace C_ChainsawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "C_Chainsaw", 0x52C454 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "blood_chainsaw_constant_fp", precache, resourceList, &g_nChainsawBloodFirstPersonIndex );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "blood_chainsaw_constant_tp", precache, resourceList, &g_nChainsawBloodThirdPersonIndex );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "weapon_chainsaw_smoke_fp", precache, resourceList, &g_nChainsawSmokeFirstPersonIndex );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "weapon_chainsaw_smoke_tp", precache, resourceList, &g_nChainsawSmokeThirdPersonIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C3029A0; object 0x106f1758; .CRT 52C6D4.
namespace C_MolotovPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "C_Molotov", 0x52C6D4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "weapon_molotov_held", precache, resourceList, &g_nMolotovHeldIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C304D10; object 0x106f1ed8; .CRT 52C760.
namespace C_PipeBombPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "C_PipeBomb", 0x52C760 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "weapon_pipebomb_fuse", precache, resourceList, &g_nPipeBombFuseIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F40; object 0x106e2210; .CRT 52B318.
namespace CommandPointerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "CommandPointer", 0x52B318 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230560; object 0x106e34e8; .CRT 52B54C.
namespace EjectBrass_12GaugePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_12Gauge", 0x52B54C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2305A0; object 0x106e3528; .CRT 52B56C.
namespace EjectBrass_338MagPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_338Mag", 0x52B56C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230580; object 0x106e3508; .CRT 52B55C.
namespace EjectBrass_556Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_556", 0x52B55C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230570; object 0x106e34f8; .CRT 52B554.
namespace EjectBrass_57Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_57", 0x52B554 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230590; object 0x106e3518; .CRT 52B564.
namespace EjectBrass_762NatoPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_762Nato", 0x52B564 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C230550; object 0x106e34d8; .CRT 52B544.
namespace EjectBrass_9mmPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "EjectBrass_9mm", 0x52B544 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C209150; object 0x106e184c; .CRT 52B184.
namespace ErrorPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "Error", 0x52B184 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "error", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FC140; object 0x106f05a0; .CRT 52C590.
namespace ExplosionForcePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ExplosionForce", 0x52C590 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F34B0; object 0x106d5c94; .CRT 529ED0.
namespace ExplosionPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "Explosion", 0x529ED0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1F3130; object 0x106e0b40; .CRT 52AF4C.
namespace FX_CacheMaterialHandlesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "FX_CacheMaterialHandles", 0x52AF4C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_wood1", precache, resourceList, &g_Mat_Fleck_Wood[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_wood2", precache, resourceList, &g_Mat_Fleck_Wood[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_cement1", precache, resourceList, &g_Mat_Fleck_Cement[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_cement2", precache, resourceList, &g_Mat_Fleck_Cement[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_glass1", precache, resourceList, &g_Mat_Fleck_Glass[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_glass2", precache, resourceList, &g_Mat_Fleck_Glass[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_tile1", precache, resourceList, &g_Mat_Fleck_Tile[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/fleck_tile2", precache, resourceList, &g_Mat_Fleck_Tile[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "particle/particle_smokegrenade", precache, resourceList, &g_Mat_DustPuff[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "particle/particle_noisesphere", precache, resourceList, &g_Mat_DustPuff[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/blood", precache, resourceList, &g_Mat_BloodPuff[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/blood2", precache, resourceList, &g_Mat_BloodPuff[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/muzzleflash1", precache, resourceList, &g_Mat_SMG_Muzzleflash[0] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/muzzleflash2", precache, resourceList, &g_Mat_SMG_Muzzleflash[1] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/muzzleflash3", precache, resourceList, &g_Mat_SMG_Muzzleflash[2] );
		pHandler->Precache( RESOURCETYPE_PMATERIAL, "effects/muzzleflash4", precache, resourceList, &g_Mat_SMG_Muzzleflash[3] );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAB00; object 0x106d6424; .CRT 529F4C.
namespace FX_GunshotSlimeSplashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_SHARED, "FX_GunshotSlimeSplash", 0x529F4C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Physics.WaterSplash", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAAB0; object 0x106d6414; .CRT 529F48.
namespace FX_GunshotSplashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_SHARED, "FX_GunshotSplash", 0x529F48 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SHARED, "FX_WaterRipple", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/splash2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Physics.WaterSplash", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAA80; object 0x106d6404; .CRT 529F44.
namespace FX_WaterRipplePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_SHARED, "FX_WaterRipple", 0x529F44 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/splashwake1", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C07F060; object 0x106c2cd8; .CRT 5298B8.
namespace GameinstructorIconsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "GameinstructorIcons", 0x5298B8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/iconsheet", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1F32B0; object 0x106e0b50; .CRT 52AF54.
namespace GlassImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "GlassImpact", 0x52AF54 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F30; object 0x106e2200; .CRT 52B310.
namespace GunshipImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "GunshipImpact", 0x52B310 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F35D0; object 0x106d5d40; .CRT 529EE8.
namespace HelicopterMegaBombPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "HelicopterMegaBomb", 0x529EE8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22FC00; object 0x106e3238; .CRT 52B520.
namespace ImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "Impact", 0x52B520 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C291030; object 0x106eaa20; .CRT 52C070.
namespace InfernoMaterialsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "InfernoMaterials", 0x52C070 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/white", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22FD30; object 0x106e327c; .CRT 52B528.
namespace KnifeSlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "KnifeSlash", 0x52B528 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C21BF50; object 0x106e2504; .CRT 52B33C.
namespace ManhackSparksPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ManhackSparks", 0x52B33C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F10; object 0x106e21e0; .CRT 52B300.
namespace MuzzleFlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "MuzzleFlash", 0x52B300 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C085650; object 0x106c2f84; .CRT 52992C.
namespace ParticleEffectPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ParticleEffect", 0x52992C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C085660; object 0x106c2f94; .CRT 529934.
namespace ParticleEffectStopPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ParticleEffectStop", 0x529934 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FA140; object 0x106d6110; .CRT 529F30.
namespace ParticleTracerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ParticleTracer", 0x529F30 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C172C60; object 0x106d8f90; .CRT 52A690.
namespace PhysFrictionEffectPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PhysFrictionEffect", 0x52A690 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "impact_physics_dust", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "impact_physics_sparks", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22E4F0; object 0x106e3144; .CRT 52B4F8.
namespace PrecacheCSViewScenePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheCSViewScene", 0x52B4F8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/flashbang", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/flashbang_white", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/nightvision", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F17B0; object 0x106d5974; .CRT 529EB0.
namespace PrecacheEffectBloodSprayPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectBloodSpray", 0x529EB0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_core", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_gore", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_drop", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_puff", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C06EFB0; object 0x106c1fd8; .CRT 529738.
namespace PrecacheEffectBuildPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectBuild", 0x529738 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/spark", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22F320; object 0x106e31e8; .CRT 52B50C.
namespace PrecacheEffectCSBloodSprayPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectCSBloodSpray", 0x52B50C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_gore", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_drop", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood_puff", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1FB9D0; object 0x106e1068; .CRT 52B00C.
namespace PrecacheEffectDusttrailPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectDusttrail", 0x52B00C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/smokesprites_0001", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F33F0; object 0x106d5c50; .CRT 529EC4.
namespace PrecacheEffectExplosionPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectExplosion", 0x529EC4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/particle_smokegrenade", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/particle_smokegrenade1", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0A0340; object 0x106c3e1c; .CRT 529B10.
namespace PrecacheEffectFlaresPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectFlares", 0x529B10 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/redflare", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/yellowflare", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/yellowflare_noz", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C20A9E0; object 0x106e19c8; .CRT 52B1C8.
namespace PrecacheEffectGlassShatterPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectGlassShatter", 0x52B1C8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_glass1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_glass2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_tile1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_tile2", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C107230; object 0x106d6a24; .CRT 529FC8.
namespace PrecacheEffectGlowPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectGlow", 0x529FC8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sun/overlay", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/light_glow02_add_noz", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1F3080; object 0x106e0b30; .CRT 52AF40.
namespace PrecacheEffectImpactsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectImpacts", 0x52AF40 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_cement1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_cement2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_wood1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/fleck_wood2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/blood2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/bloodspray", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/particle_noisesphere", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C20C350; object 0x106e1b54; .CRT 52B220.
namespace PrecacheEffectMuzzleFlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectMuzzleFlash", 0x52B220 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash3", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash4", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C21BE70; object 0x106e24f4; .CRT 52B330.
namespace PrecacheEffectSparksPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectSparks", 0x52B330 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/spark", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAA30; object 0x106d63e8; .CRT 529F3C.
namespace PrecacheEffectSplashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectSplash", 0x529F3C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/splash1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/splash2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/splash4", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0A4610; object 0x106c4488; .CRT 529BBC.
namespace PrecacheEffectVGuiScreenPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectVGuiScreen", 0x529BBC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "engine/writez", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1D9840; object 0x106dfd14; .CRT 52AD78.
namespace PrecacheEffectWarpPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectWarp", 0x52AD78 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sun/overlay", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22FFB0; object 0x106e334c; .CRT 52B52C.
namespace PrecacheEffect_CS_MuzzleFlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffect_CS_MuzzleFlash", 0x52B52C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflashX", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "sprites/muzzleflash4", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0D5160; object 0x106d27d0; .CRT 529D34.
namespace PrecacheEffectsTestPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheEffectsTest", 0x529D34 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/spark", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2BD010; object 0x106ec0d8; .CRT 52C188.
namespace PrecacheHoldoutSoundsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheHoldoutSounds", 0x52C188 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22D190; object 0x106e2ef4; .CRT 52B464.
namespace PrecacheHudScopePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheHudScope", 0x52B464 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/HUD/scope_sniper_ul", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/HUD/scope_sniper_ur", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/HUD/scope_sniper_lr", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/HUD/scope_sniper_ll", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F7BC0; object 0x106d5eac; .CRT 529F00.
namespace PrecacheImpactsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheImpacts", 0x529F00 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		PrecacheTerrorImpactResources( pHandler, precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2AE2D0; object 0x106eba44; .CRT 52C12C.
namespace PrecacheLocatorTargetPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheLocatorTarget", 0x52C12C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/icon_arrow_left", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/icon_arrow_right", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/icon_arrow_up", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/icon_arrow_down", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "vgui/hud/icon_arrow_plain", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217EA0; object 0x106e21d0; .CRT 52B2F4.
namespace PrecacheMuzzleFlashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheMuzzleFlash", 0x52B2F4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash1", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash2", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash3", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/muzzleflash4", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C086B10; object 0x106c3190; .CRT 529984.
namespace PrecacheOcclusionProxyPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheOcclusionProxy", 0x529984 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "engine/occlusionproxy", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "engine/occlusionproxy_countdraw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C214620; object 0x106e1f34; .CRT 52B264.
namespace PrecachePlayerDecalPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecachePlayerDecal", 0x52B264 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo01", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo02", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo03", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo04", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo05", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo06", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo07", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo08", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo09", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo10", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo11", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo12", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo13", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo14", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo15", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo16", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo17", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo18", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo19", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo20", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo21", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo22", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo23", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo24", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo25", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo26", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo27", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo28", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo29", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo30", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo31", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo32", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo33", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo34", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo35", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo36", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo37", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo38", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo39", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo40", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo41", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo42", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo43", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo44", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo45", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo46", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo47", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo48", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo49", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo40", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo41", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo42", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo43", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo44", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo45", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo46", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo47", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo48", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo49", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo50", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo51", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo52", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo53", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo54", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo55", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo56", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo57", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo58", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo59", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo60", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo61", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo62", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo63", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "decals/playerlogo64", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1C9B10; object 0x106df6ec; .CRT 52AD30.
namespace PrecachePostProcessingEffectsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecachePostProcessingEffects", 0x52AD30 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfiltery_and_add_nohdr", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfilterx", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfilterx_nohdr", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfiltery", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfiltery_nohdr", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurfiltery_nohdr_clear", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/bloomadd", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/downsample", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/downsample_non_hdr", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/no_pixel_write", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/lumcompare", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/floattoscreen_combine", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/copyfullframefb_vanilla", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/copyfullframefb", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/engine_post", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/engine_post_splitscreen", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/motion_blur", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/halo_add_to_screen", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/depth_of_field", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/blurgaussian_3x3", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/fade_blur", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/glow_color", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/glow_downsample", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/glow_blur_x", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "dev/glow_blur_y", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1EB0F0; object 0x106e092c; .CRT 52AE7C.
namespace PrecachePrecipitationPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecachePrecipitation", 0x52AE7C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/rain", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/snow", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "rain_storm", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "rain_storm_screen", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "rain_storm_outer", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "rain", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "rain_outer", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "ash", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "ash_outer", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "bugs_precipitation", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "bugs_precipitation_outer", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "smoke_precipitation", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "smoke_precipitation_screen", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "smoke_precipitation_outer", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C08EC90; object 0x106c3954; .CRT 529A0C.
namespace PrecacheRopesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheRopes", 0x529A0C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "cable/rope_shadowdepth", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2BC3E0; object 0x106ebfd0; .CRT 52C17C.
namespace PrecacheScavengeSoundsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheScavengeSounds", 0x52C17C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C21DE60; object 0x106e2560; .CRT 52B340.
namespace PrecacheSmokeFogOverlayPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheSmokeFogOverlay", 0x52B340 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/screenspace_fog", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C26ED90; object 0x106e8240; .CRT 52BCF8.
namespace PrecacheSpotlightPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheSpotlight", 0x52BCF8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/spotlight", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2C40F0; object 0x106ec570; .CRT 52C1A4.
namespace PrecacheSurvivalSoundsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheSurvivalSounds", 0x52C1A4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Survival.countdown", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Survival.medal", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Survival.player_record", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Survival.team_record", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C1FB4C0; object 0x106e1010; .CRT 52AFE4.
namespace RPGShotDownPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "RPGShotDown", 0x52AFE4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Missile.ShotDown", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F7B50; object 0x106d5e9c; .CRT 529EF8.
namespace RagdollImpactPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "RagdollImpact", 0x529EF8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F9960; object 0x106d6028; .CRT 529F10.
namespace RifleShellEjectPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "RifleShellEject", 0x529F10 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C08EE00; object 0x106c39a4; .CRT 529A7C.
namespace ShakeRopesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ShakeRopes", 0x529A7C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F9930; object 0x106d6018; .CRT 529F08.
namespace ShellEjectPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ShellEject", 0x529F08 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F9990; object 0x106d6038; .CRT 529F18.
namespace ShotgunShellEjectPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "ShotgunShellEject", 0x529F18 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F20; object 0x106e21f0; .CRT 52B308.
namespace SmokePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "Smoke", 0x52B308 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0753A0; object 0x106c22fc; .CRT 5297C4.
namespace SmokeStackMaterialsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "SmokeStackMaterials", 0x5297C4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "particle/SmokeStack", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F50; object 0x106e2220; .CRT 52B320.
namespace TeslaHitboxesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "TeslaHitboxes", 0x52B320 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C217F60; object 0x106e2230; .CRT 52B328.
namespace TeslaZapPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "TeslaZap", 0x52B328 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FA100; object 0x106d6100; .CRT 529F28.
namespace TracerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "Tracer", 0x529F28 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_MATERIAL, "effects/spark", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Bullets.DefaultNearmiss", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FA150; object 0x106d6120; .CRT 529F38.
namespace TracerSoundPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "TracerSound", 0x529F38 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F35C0; object 0x106d5cfc; .CRT 529EDC.
namespace WaterSurfaceExplosionPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "WaterSurfaceExplosion", 0x529EDC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0A3F10; object 0x106c43fc; .CRT 529BB8.
namespace WheelDustPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "WheelDust", 0x529BB8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C30D280; object 0x106f4270; .CRT 52C9F4.
namespace bloodslashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "bloodslash", 0x52C9F4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0F1820; object 0x106d5984; .CRT 529EB8.
namespace bloodsprayPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "bloodspray", 0x529EB8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C22F370; object 0x106e31f8; .CRT 52B518.
namespace csbloodPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "csblood", 0x52B518 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FBFC0; object 0x106f0578; .CRT 52C57C.
namespace grenade_launcher_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "grenade_launcher_projectile", 0x52C57C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "grenade_launcher_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAB80; object 0x106d6454; .CRT 529F64.
namespace gunshotsplashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "gunshotsplash", 0x529F64 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SHARED, "FX_GunshotSlimeSplash", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SHARED, "FX_GunshotSplash", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C287CA0; object 0x106e940c; .CRT 52BECC.
namespace predicted_viewmodelPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "predicted_viewmodel", 0x52BECC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "predicted_viewmodel", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2904A0; object 0x106ea8e0; .CRT 52C064.
namespace startvomitPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "startvomit", 0x52C064 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2904B0; object 0x106ea8f0; .CRT 52C06C.
namespace stopvomitPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "stopvomit", 0x52C06C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FABC0; object 0x106d6464; .CRT 529F6C.
namespace waterripplePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "waterripple", 0x529F6C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAB30; object 0x106d6434; .CRT 529F54.
namespace watersplashPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "watersplash", 0x529F54 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_SHARED, "FX_GunshotSlimeSplash", precache, resourceList );
		pHandler->Precache( RESOURCETYPE_SHARED, "FX_GunshotSplash", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C0FAB70; object 0x106d6444; .CRT 529F5C.
namespace watersplashquietPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_EFFECTS, "watersplashquiet", 0x529F5C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		// Native callback is exactly RET 10h; the named registration is real.
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FD550; object 0x106f0994; .CRT 52C5D0.
namespace weapon_adrenalinePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_adrenaline", 0x52C5D0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_adrenaline", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F0690; object 0x106ee5f0; .CRT 52C348.
namespace weapon_autoshotgunPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_autoshotgun", 0x52C348 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_autoshotgun" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_autoshotgun", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F2D20; object 0x106ef278; .CRT 52C3D4.
namespace weapon_boomer_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_boomer_claw", 0x52C3D4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_boomer_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F6C60; object 0x106ef6fc; .CRT 52C44C.
namespace weapon_chainsawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_chainsaw", 0x52C44C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_chainsaw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F86D0; object 0x106ef878; .CRT 52C46C.
namespace weapon_charger_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_charger_claw", 0x52C46C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_charger_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FAB10; object 0x106efbb4; .CRT 52C4E4.
namespace weapon_cola_bottlesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_cola_bottles", 0x52C4E4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_cola_bottles", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FDDF0; object 0x106f0e74; .CRT 52C61C.
namespace weapon_defibrillatorPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_defibrillator", 0x52C61C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_defibrillator", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FAE80; object 0x106efdc4; .CRT 52C4FC.
namespace weapon_fireworkcratePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_fireworkcrate", 0x52C4FC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_fireworkcrate", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FB130; object 0x106eff9c; .CRT 52C51C.
namespace weapon_first_aid_kitPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_first_aid_kit", 0x52C51C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_first_aid_kit", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FB3D0; object 0x106f016c; .CRT 52C544.
namespace weapon_gascanPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_gascan", 0x52C544 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_gascan", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FB800; object 0x106f0334; .CRT 52C55C.
namespace weapon_gnomePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_gnome", 0x52C55C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_gnome", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FBAB0; object 0x106f04fc; .CRT 52C574.
namespace weapon_grenade_launcherPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_grenade_launcher", 0x52C574 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_grenade_launcher" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_grenade_launcher", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FD260; object 0x106f07c8; .CRT 52C5B4.
namespace weapon_hunter_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_hunter_claw", 0x52C5B4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_hunter_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308540; object 0x106f39e0; .CRT 52C908.
namespace weapon_hunting_riflePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_hunting_rifle", 0x52C908 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_hunting_rifle" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_hunting_rifle", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FEAF0; object 0x106f13a8; .CRT 52C67C.
namespace weapon_jockey_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_jockey_claw", 0x52C67C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_jockey_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C30D140; object 0x106f4260; .CRT 52C9EC.
namespace weapon_meleePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_melee", 0x52C9EC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_melee", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C3026C0; object 0x106f1748; .CRT 52C6D0.
namespace weapon_molotovPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_molotov", 0x52C6D0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_molotov", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C3042C0; object 0x106f1bbc; .CRT 52C720.
namespace weapon_oxygentankPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_oxygentank", 0x52C720 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_oxygentank", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C304570; object 0x106f1d8c; .CRT 52C744.
namespace weapon_pain_pillsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_pain_pills", 0x52C744 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_pain_pills", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C304A70; object 0x106f1ec8; .CRT 52C75C.
namespace weapon_pipe_bombPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_pipe_bomb", 0x52C75C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_pipe_bomb", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C305550; object 0x106f211c; .CRT 52C784.
namespace weapon_pistolPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_pistol", 0x52C784 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_pistol", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C305D70; object 0x106f22e4; .CRT 52C79C.
namespace weapon_pistol_magnumPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_pistol_magnum", 0x52C79C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_pistol_magnum", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306280; object 0x106f24bc; .CRT 52C7B4.
namespace weapon_propanetankPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_propanetank", 0x52C7B4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_propanetank", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306510; object 0x106f25f8; .CRT 52C7CC.
namespace weapon_pumpshotgunPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_pumpshotgun", 0x52C7CC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_pumpshotgun" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_pumpshotgun", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2F0490; object 0x106ee480; .CRT 52C330.
namespace weapon_riflePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_rifle", 0x52C330 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_rifle" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306710; object 0x106f2730; .CRT 52C7E4.
namespace weapon_rifle_ak47Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_rifle_ak47", 0x52C7E4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_rifle_ak47" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_ak47", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306900; object 0x106f2868; .CRT 52C7FC.
namespace weapon_rifle_desertPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_rifle_desert", 0x52C7FC ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_rifle_desert" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_desert", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306CE0; object 0x106f2b2c; .CRT 52C814.
namespace weapon_rifle_m60Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_rifle_m60", 0x52C814 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_rifle_m60" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_m60", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C306FC0; object 0x106f2cfc; .CRT 52C82C.
namespace weapon_rifle_sg552Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_rifle_sg552", 0x52C82C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_rifle_sg552" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_sg552", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C307280; object 0x106f2e38; .CRT 52C844.
namespace weapon_shotgun_chromePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_shotgun_chrome", 0x52C844 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_shotgun_chrome" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_shotgun_chrome", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C307480; object 0x106f2f78; .CRT 52C85C.
namespace weapon_shotgun_spasPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_shotgun_spas", 0x52C85C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_shotgun_spas" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_shotgun_spas", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308C40; object 0x106f3d34; .CRT 52C948.
namespace weapon_smgPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_smg", 0x52C948 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_smg" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_smg", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C3076E0; object 0x106f314c; .CRT 52C874.
namespace weapon_smg_mp5Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_smg_mp5", 0x52C874 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_smg_mp5" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_smg_mp5", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C307A30; object 0x106f3314; .CRT 52C88C.
namespace weapon_smg_silencedPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_smg_silenced", 0x52C88C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_smg_silenced" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_smg_silenced", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C307D30; object 0x106f3458; .CRT 52C8A4.
namespace weapon_smoker_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_smoker_claw", 0x52C8A4 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_smoker_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C307E30; object 0x106f3590; .CRT 52C8C0.
namespace weapon_sniper_awpPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_sniper_awp", 0x52C8C0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_sniper_awp" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_sniper_awp", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308070; object 0x106f375c; .CRT 52C8D8.
namespace weapon_sniper_militaryPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_sniper_military", 0x52C8D8 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_sniper_military" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_sniper_military", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308300; object 0x106f38a0; .CRT 52C8F0.
namespace weapon_sniper_scoutPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_sniper_scout", 0x52C8F0 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( ignoreConditionals || !IsWeaponAllowedForPrecache( "weapon_sniper_scout" ) )
			return;
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_sniper_scout", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308830; object 0x106f3b20; .CRT 52C920.
namespace weapon_spitter_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_spitter_claw", 0x52C920 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_spitter_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C308E70; object 0x106f3e68; .CRT 52C998.
namespace weapon_tank_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_tank_claw", 0x52C998 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_tank_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FE180; object 0x106f0fb8; .CRT 52C634.
namespace weapon_upgradepack_explosivePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_upgradepack_explosive", 0x52C634 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_upgradepack_explosive", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FE380; object 0x106f1110; .CRT 52C64C.
namespace weapon_upgradepack_incendiaryPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_upgradepack_incendiary", 0x52C64C ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_upgradepack_incendiary", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

// C2FE690; object 0x106f1268; .CRT 52C664.
namespace weapon_vomitjarPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "weapon_vomitjar", 0x52C664 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_ENTITY, "weapon_vomitjar", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
