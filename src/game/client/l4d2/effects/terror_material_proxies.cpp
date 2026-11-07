//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_baseviewmodel.h"
#include "l4d2/infected/common/c_infected.h"
#include "functionproxy.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "l4d2/effects/terror_material_proxy_state.h"
#include "proxyentity.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialproxyfactory.h"
#include "igamesystem.h"
#include "tier1/interface.h"

#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_viewmodel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Native C13AB40/C13A990 uses case-insensitive proxy names. Keep the target's
// EXPOSE_INTERFACE entries as the sole registry, through its supported factory.
class CTerrorMaterialProxyFactory : public CAutoGameSystem, public IMaterialProxyFactory
{
public:
	CTerrorMaterialProxyFactory() : CAutoGameSystem( "TerrorMaterialProxyFactory" ), m_pEngineFactory( NULL ) {}

	virtual bool Init() OVERRIDE
	{
		m_pEngineFactory = materials->GetMaterialProxyFactory();
		materials->SetMaterialProxyFactory( this );
		return true;
	}

	virtual void Shutdown() OVERRIDE
	{
		if ( materials->GetMaterialProxyFactory() == this )
			materials->SetMaterialProxyFactory( m_pEngineFactory );
		m_pEngineFactory = NULL;
	}

	virtual IMaterialProxy *CreateProxy( const char *pName ) OVERRIDE
	{
		if ( !pName )
			return NULL;
		for ( InterfaceReg *pReg = InterfaceReg::s_pInterfaceRegs; pReg; pReg = pReg->m_pNext )
		{
			if ( NameMatches( pReg->m_pName, pName ) )
			{
				// InterfaceReg prepends entries: first match is the last registered
				// creator, matching native C13AAF0's replacement of an existing name.
				return static_cast< IMaterialProxy * >( pReg->m_CreateFn() );
			}
		}

		// Native lookup misses return NULL. The saved target factory additionally
		// owns non-client/tool proxies; retain that genuine target lookup on misses.
		return m_pEngineFactory ? m_pEngineFactory->CreateProxy( pName ) : NULL;
	}

	virtual void DeleteProxy( IMaterialProxy *pProxy ) OVERRIDE
	{
		// Target engine 0x1C7CE0 releases both client and tool-created proxies.
		if ( pProxy )
			pProxy->Release();
	}

private:
	// Native C3D9A90 calls C3D17E0: fold ASCII A..Z only. The SDK's
	// Q_stricmp/Q_strnicmp have a non-ASCII CRT/locale fallback; do not use
	// that to turn a native dictionary miss into a different proxy creator.
	static bool NameMatches( const char *pRegisteredName, const char *pProxyName )
	{
		while ( *pProxyName )
		{
			unsigned char registered = static_cast< unsigned char >( *pRegisteredName );
			unsigned char requested = static_cast< unsigned char >( *pProxyName );
			if ( registered >= 'A' && registered <= 'Z' )
				registered += 'a' - 'A';
			if ( requested >= 'A' && requested <= 'Z' )
				requested += 'a' - 'A';
			if ( registered != requested )
				return false;
			++pRegisteredName;
			++pProxyName;
		}
		// The suffix is registration metadata, not part of the native name.
		return !Q_strcmp( pRegisteredName, IMATERIAL_PROXY_INTERFACE_VERSION );
	}

	IMaterialProxyFactory *m_pEngineFactory;
};

static CTerrorMaterialProxyFactory s_TerrorMaterialProxyFactory;

static ConVar survivor_it_fade_in_interval( "survivor_it_fade_in_interval", "0.2", 0x0000000A );
static ConVar survivor_it_fade_out_interval( "survivor_it_fade_out_interval", "2", 0x0000000A );

// C:0x253C80. The leading edge takes precedence if the two fades overlap.
// These are the received CountdownTimer's duration and absolute timestamp, not
// an independently started rendering timer.
float C_TerrorPlayer::GetITBlend() const
{
	const float flTimestamp = GetITTimerTimestamp();
	if ( flTimestamp <= 0.0f || gpGlobals->curtime >= flTimestamp )
		return 0.0f;

	const float flElapsed = MAX( 0.0f, gpGlobals->curtime - flTimestamp + GetITTimerDuration() );
	const float flFadeIn = survivor_it_fade_in_interval.GetFloat();
	if ( flFadeIn > flElapsed )
		return flElapsed / flFadeIn;

	const float flRemaining = flTimestamp - gpGlobals->curtime;
	const float flFadeOut = survivor_it_fade_out_interval.GetFloat();
	if ( flFadeOut <= flRemaining )
		return 1.0f;

	return flRemaining / flFadeOut;
}

// C:0x18B940 snapshots the player's evaluated blend into the native corpse.
// Common infected and unrelated entities have no player IT timer.
bool GetTerrorEntityMaterialState( C_BaseEntity *pEntity, TerrorMaterialProxyState_t &state )
{
	state.m_flIT = 0.0f;
	state.m_flBloodyHands = 0.0f;

	C_TerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
	if ( pPlayer )
	{
		state.m_flIT = pPlayer->GetITBlend();
		state.m_flBloodyHands = pPlayer->GetBloodyHandsBlend();
		return true;
	}

	return GetTerrorRagdollMaterialState( pEntity, state );
}

// Reached by gambler_head_it/gambler_body_it through survivors_it_shared.vmt.
// Init/OnBind/factory/registration: C:0x293010/0x293030/0x293100/0x5151F0.
class CProxyIT : public CResultProxy
{
public:
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues ) OVERRIDE
	{
		return CResultProxy::Init( pMaterial, pKeyValues );
	}

	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		// The original leaves the material untouched on a null bind argument.
		if ( !pRenderable )
			return;

		C_BaseEntity *pEntity = BindArgToEntity( pRenderable );
		TerrorMaterialProxyState_t state;
		if ( GetTerrorEntityMaterialState( pEntity, state ) )
		{
			SetFloatResult( state.m_flIT );
			return;
		}

		// C:0x236B0 resolves an animating entity and checks IsViewModel before
		// using its owner. It does not inherit IT from arbitrary entity owners.
		C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
		if ( pAnimating && pAnimating->IsViewModel() )
		{
			C_BaseViewModel *pViewModel = static_cast< C_BaseViewModel * >( pAnimating );
			C_TerrorPlayer *pOwner = ToTerrorPlayer( pViewModel->GetOwner() );
			if ( pOwner )
			{
				SetFloatResult( pOwner->GetITBlend() );
				return;
			}
		}

		SetFloatResult( 0.0f );
	}

private:
	typedef CResultProxy BaseClass;
};

EXPOSE_INTERFACE( CProxyIT, IMaterialProxy, "IT" IMATERIAL_PROXY_INTERFACE_VERSION );

// Game-owned transport for original Infected instance commands 19 and 20.
// The target renderer has no corresponding packed-state commands; a native
// proxy captures the actor's values at material bind. Not a retail proxy name.
class CTerrorInfectedWoundsProxy : public CEntityMaterialProxy
{
public:
	CTerrorInfectedWoundsProxy() : m_pMaterial( NULL ), m_pCullType( NULL )
	{
		Q_memset( m_pWoundVar, 0, sizeof( m_pWoundVar ) );
		Q_memset( m_pInstanceVar, 0, sizeof( m_pInstanceVar ) );
	}

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues ) OVERRIDE
	{
		static const char *s_pVarName[2][NUM_WOUND_VARS] =
		{
			{
				"$ellipsoidcenter", "$ellipsoidlookat", "$ellipsoidup", "$ellipsoidscale",
				"$terrorwoundenabled", "$terrorwounduvmapping", "$terrorwounduvscale"
			},
			{
				"$ellipsoidcenter2", "$ellipsoidlookat2", "$ellipsoidup2", "$ellipsoidscale2",
				"$terrorwoundenabled2", "$terrorwounduvmapping2", "$terrorwounduvscale2"
			}
		};

		m_pMaterial = pMaterial;
		for ( int i = 0; i < 2; ++i )
		{
			for ( int j = 0; j < NUM_WOUND_VARS; ++j )
			{
				bool bFound;
				m_pWoundVar[i][j] = pMaterial->FindVar( s_pVarName[i][j], &bFound );
				if ( !bFound )
					return false;
			}
		}

		bool bFound;
		m_pCullType = pMaterial->FindVar( "$ellipsoid2culltype", &bFound );
		if ( !bFound )
			return false;
		static const char *s_pInstanceVarName[NUM_INSTANCE_VARS] =
		{
			"$staticprop", "$skintintgradient", "$colortintgradient", "$sheetindex", "$burnstrength"
		};
		for ( int i = 0; i < NUM_INSTANCE_VARS; ++i )
		{
			m_pInstanceVar[i] = pMaterial->FindVar( s_pInstanceVarName[i], &bFound );
			if ( !bFound )
				return false;
		}
		m_nAuthoredSkinTint = m_pInstanceVar[SKIN_TINT]->GetIntValue();
		m_nAuthoredClothingTint = m_pInstanceVar[CLOTHING_TINT]->GetIntValue();
		m_nAuthoredSheet = m_pInstanceVar[BLOOD_SHEET]->GetIntValue();
		m_flAuthoredBurn = m_pInstanceVar[BURN_STRENGTH]->GetFloatValue();
		return true;
	}

	virtual IMaterial *GetMaterial() OVERRIDE
	{
		return m_pMaterial;
	}

protected:
	virtual void OnBind( C_BaseEntity *pEntity ) OVERRIDE
	{
		const InfectedRenderState_t *pState = GetInfectedRenderState( pEntity );
		if ( !m_pInstanceVar[STATIC_PROP]->GetIntValue() )
		{
			// Original cmd19 (shaderapi 0x220AE..0x222AF) reads the raw
			// C:0x3147F0 / corpse0x263900 packet. ComputeModulationColor
			// only returns material COLOR/COLOR2, not that instance packet.
			m_pInstanceVar[SKIN_TINT]->SetIntValue( pState ? pState->m_nSkinTint : m_nAuthoredSkinTint );
			m_pInstanceVar[CLOTHING_TINT]->SetIntValue( pState ? pState->m_nClothingTint : m_nAuthoredClothingTint );
			int bloodSheet = m_nAuthoredSheet;
			float burnStrength = m_flAuthoredBurn;
			if ( pState )
				GetInfectedBloodAndBurn( *pState, bloodSheet, burnStrength );
			m_pInstanceVar[BLOOD_SHEET]->SetIntValue( bloodSheet );
			m_pInstanceVar[BURN_STRENGTH]->SetFloatValue( burnStrength );
		}
		const bool bHasPacket = pState && !UTIL_IsLowViolence() && z_infected_damage_cutouts.GetBool()
			&& ( pState->m_Wound[0].m_bEnabled || pState->m_Wound[1].m_bEnabled );

		for ( int i = 0; i < 2; ++i )
		{
			const bool bEnabled = bHasPacket && pState->m_Wound[i].m_bEnabled;
			m_pWoundVar[i][WOUND_ENABLED]->SetIntValue( bEnabled ? 1 : 0 );

			if ( bHasPacket )
			{
				// C:0x317360 duplicates slot zero for an inactive slot. The four
				// vectors are center, look direction, up direction and scale.
				// Normalization and reciprocal scale belong to the shader.
				const InfectedWoundRenderData_t &wound = pState->m_Wound[bEnabled ? i : 0];
				m_pWoundVar[i][WOUND_CENTER]->SetVecValue( wound.m_vecPosition.Base(), 3 );
				m_pWoundVar[i][WOUND_LOOKAT]->SetVecValue( wound.m_vecAxis[0].Base(), 3 );
				m_pWoundVar[i][WOUND_UP]->SetVecValue( wound.m_vecAxis[1].Base(), 3 );
				m_pWoundVar[i][WOUND_SCALE]->SetVecValue( wound.m_vecAxis[2].Base(), 3 );
				m_pWoundVar[i][WOUND_UV_MAPPING]->SetVecValue( wound.m_vecUVMapping.Base(), 4 );
				m_pWoundVar[i][WOUND_UV_SCALE]->SetVecValue( wound.m_vecUVScale.Base(), 2 );
			}
			else
			{
				// Original null-command centers/UVs and original shader default
				// orientation/scale. Reset every bind so a shared material cannot
				// retain another actor's cutout or divide by a zero scale.
				m_pWoundVar[i][WOUND_CENTER]->SetVecValue( 1000000.0f, 0.0f, 0.0f );
				m_pWoundVar[i][WOUND_LOOKAT]->SetVecValue( 1.0f, 0.0f, 0.0f );
				m_pWoundVar[i][WOUND_UP]->SetVecValue( 0.0f, 0.0f, 1.0f );
				m_pWoundVar[i][WOUND_SCALE]->SetVecValue( 10.0f, 10.0f, 10.0f );
				m_pWoundVar[i][WOUND_UV_MAPPING]->SetVecValue( 1.0f, 1.0f, 0.0f, 0.0f );
				m_pWoundVar[i][WOUND_UV_SCALE]->SetVecValue( 0.5f, 0.5f );
			}
		}

		// A first wound can own both ellipsoids. A separately applied second
		// wound instead uses the union. This is the packet's byte at +144,
		// not merely the second ellipsoid's enabled bit (C:0x317360).
		const bool bCullType = bHasPacket && pState->m_Wound[1].m_bEnabled && pState->m_iWound[1] == -1;
		m_pCullType->SetIntValue( bCullType ? 1 : 0 );
	}

	virtual void OnBindNotEntity( void *pRenderable ) OVERRIDE
	{
		OnBind( static_cast< C_BaseEntity * >( NULL ) );
	}

private:
	enum
	{
		WOUND_CENTER,
		WOUND_LOOKAT,
		WOUND_UP,
		WOUND_SCALE,
		WOUND_ENABLED,
		WOUND_UV_MAPPING,
		WOUND_UV_SCALE,
		NUM_WOUND_VARS
	};
	enum
	{
		STATIC_PROP,
		SKIN_TINT,
		CLOTHING_TINT,
		BLOOD_SHEET,
		BURN_STRENGTH,
		NUM_INSTANCE_VARS
	};

	IMaterial *m_pMaterial;
	IMaterialVar *m_pWoundVar[2][NUM_WOUND_VARS];
	IMaterialVar *m_pCullType;
	IMaterialVar *m_pInstanceVar[NUM_INSTANCE_VARS];
	int m_nAuthoredSkinTint;
	int m_nAuthoredClothingTint;
	int m_nAuthoredSheet;
	float m_flAuthoredBurn;
};

EXPOSE_INTERFACE( CTerrorInfectedWoundsProxy, IMaterialProxy, "TerrorInfectedWounds" IMATERIAL_PROXY_INTERFACE_VERSION );

float C_TerrorPlayer::GetBloodyHandsBlend() const
{
	return UTIL_IsLowViolence() ? 0.0f : static_cast< float >( static_cast< double >( m_iBloodyHandsLevel ) * static_cast< double >( 0.1f ) );
}

// C292AE0/C292B00: a NULL bind deliberately retains the previous value.
class CProxyBloodyHands : public CResultProxy
{
public:
	virtual bool Init( IMaterial *material, KeyValues *keys ) OVERRIDE
	{
		return CResultProxy::Init( material, keys );
	}
	virtual void OnBind( void *renderable ) OVERRIDE
	{
		if ( !renderable )
			return;
		C_BaseEntity *entity = BindArgToEntity( renderable );
		C_TerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player )
		{
			SetFloatResult( player->GetBloodyHandsBlend() );
			return;
		}
		TerrorMaterialProxyState_t state;
		if ( GetTerrorRagdollMaterialState( entity, state ) )
		{
			SetFloatResult( state.m_flBloodyHands );
			return;
		}
		C_TerrorViewModel *viewmodel = dynamic_cast< C_TerrorViewModel * >( entity );
		if ( viewmodel )
		{
			C_TerrorPlayer *owner = ToTerrorPlayer( viewmodel->GetOwner() );
			C_TerrorWeapon *weapon = dynamic_cast< C_TerrorWeapon * >( viewmodel->GetWeapon() );
			const float hands = owner ? owner->GetBloodyHandsBlend() : 0.0f;
			const float blade = weapon ? static_cast< float >( weapon->GetBloodyWeaponBlend() ) : 0.0f;
			SetFloatResult( hands <= blade ? blade : hands );
			return;
		}
		C_TerrorWeapon *weapon = dynamic_cast< C_TerrorWeapon * >( entity );
		SetFloatResult( weapon ? static_cast< float >( weapon->GetBloodyWeaponBlend() ) : 0.0f );
	}
private:
	typedef CResultProxy BaseClass;
};
EXPOSE_INTERFACE( CProxyBloodyHands, IMaterialProxy, "BloodyHands" IMATERIAL_PROXY_INTERFACE_VERSION );

#include "l4d2/effects/terror_ragdoll_material_proxies.inc"
