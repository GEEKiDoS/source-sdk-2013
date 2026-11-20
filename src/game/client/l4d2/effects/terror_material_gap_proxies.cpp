//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "materialsystem/imaterial.h"
#include "c_baseanimatingoverlay.h"
#include "c_baseviewmodel.h"
#include "view.h"
#include <math.h>

#include "tier0/memdbgon.h"
#pragma float_control( precise, on, push )

// C0DC7E0/C0DC8C0/C4FE2E0. Release really is a nullsub in this class.
class CConVarMaterialProxy : public IMaterialProxy
{
public:
	CConVarMaterialProxy() : m_pResult( NULL ), m_ConVar( "", true ) {}
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		const char *pResult = pKeys->GetString( "resultVar", "" );
		bool bFound;
		m_pResult = pMaterial->FindVar( pResult, &bFound );
		if ( !bFound ) { m_pResult = NULL; return false; }
		if ( !Q_stricmp( pResult, "$alpha" ) )
			pMaterial->SetMaterialVarFlag( MATERIAL_VAR_IGNORE_ALPHA_MODULATION, true );
		const char *pConVar = pKeys->GetString( "convar", "" );
		m_ConVar.Init( pConVar, false );
		return m_ConVar.IsValid();
	}
	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		if ( m_pResult->GetType() == MATERIAL_VAR_TYPE_VECTOR )
		{
			const float value = m_ConVar.GetFloat();
			const float values[4] = { value, value, value, value };
			m_pResult->SetVecValue( values, m_pResult->VectorSize() );
		}
		else if ( m_pResult->GetType() == MATERIAL_VAR_TYPE_INT )
			m_pResult->SetIntValue( m_ConVar.GetInt() );
		else
			m_pResult->SetFloatValue( m_ConVar.GetFloat() );
	}
	virtual void Release() OVERRIDE {}
	virtual IMaterial *GetMaterial() OVERRIDE { return m_pResult->GetOwningMaterial(); }
private:
	IMaterialVar *m_pResult;
	ConVarRef m_ConVar;
};
EXPOSE_INTERFACE( CConVarMaterialProxy, IMaterialProxy, "ConVar" IMATERIAL_PROXY_INTERFACE_VERSION );

static C_AnimationLayer *MaterialAnimationLayer( C_BaseAnimating *pAnimating, int index )
{
	if ( index < 0 )
		return NULL;
	C_BaseAnimatingOverlay *pOverlay = dynamic_cast<C_BaseAnimatingOverlay *>( pAnimating );
	if ( !pOverlay || index >= pOverlay->GetNumAnimOverlays() )
		return NULL;
	return pOverlay->GetAnimOverlay( index );
}

// C0DC9C0/C0DCAD0/C4FE300. CEntityMaterialProxy is the native bind dispatcher.
class CCycleMaterialProxy : public CEntityMaterialProxy
{
public:
	CCycleMaterialProxy() : m_pMaterial( NULL ), m_pResult( NULL ), m_bEaseIn( false ), m_bEaseOut( false ), m_flStart( 0.0f ), m_flEnd( 1.0f ) {}
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		m_pMaterial = pMaterial;
		m_nLayer = pKeys->GetInt( "layer", -1 );
		const char *pResult = pKeys->GetString( "resultVar", NULL );
		if ( !pResult ) return false;
		bool bFound;
		m_pResult = pMaterial->FindVar( pResult, &bFound );
		if ( !bFound ) { m_pResult = NULL; return false; }
		if ( !Q_stricmp( pResult, "$alpha" ) )
			pMaterial->SetMaterialVarFlag( MATERIAL_VAR_IGNORE_ALPHA_MODULATION, true );
		if ( pKeys->GetInt( "easein", 0 ) ) m_bEaseIn = true;
		if ( pKeys->GetInt( "easeout", 0 ) ) m_bEaseOut = true;
		if ( pKeys->FindKey( "start" ) ) m_flStart = pKeys->GetFloat( "start" );
		if ( pKeys->FindKey( "end" ) ) m_flEnd = pKeys->GetFloat( "end" );
		return true;
	}
	virtual IMaterial *GetMaterial() OVERRIDE { return m_pMaterial; }
protected:
	virtual void OnBind( C_BaseEntity *pEntity ) OVERRIDE
	{
		C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
		if ( !pAnimating ) return;
		C_AnimationLayer *pLayer = MaterialAnimationLayer( pAnimating, m_nLayer );
		const float cycle = pLayer ? pLayer->m_flCycle : pAnimating->GetCycle();
		float value = m_flStart == m_flEnd ? ( cycle >= m_flEnd ? 1.0f : 0.0f ) : ( cycle - m_flStart ) / ( m_flEnd - m_flStart );
		if ( value < 0.0f ) value = 0.0f;
		else if ( value > 1.0f ) value = 1.0f;
		if ( m_bEaseIn )
		{
			if ( m_bEaseOut ) value = ( value * value ) * 3.0f - ( ( value * value ) * 2.0f ) * value;
			else value = sin( static_cast< double >( value ) * M_PI * 0.5 );
		}
		else if ( m_bEaseOut ) value = 1.0 - sin( static_cast< double >( value ) * M_PI * 0.5 + M_PI * 0.5 );
		if ( m_pResult->GetType() == MATERIAL_VAR_TYPE_VECTOR )
		{
			const float values[4] = { value, value, value, value };
			m_pResult->SetVecValue( values, m_pResult->VectorSize() );
		}
		else m_pResult->SetFloatValue( value );
	}
private:
	IMaterial *m_pMaterial;
	IMaterialVar *m_pResult;
	int m_nLayer;
	bool m_bEaseIn, m_bEaseOut;
	float m_flStart, m_flEnd;
};
EXPOSE_INTERFACE( CCycleMaterialProxy, IMaterialProxy, "Cycle" IMATERIAL_PROXY_INTERFACE_VERSION );

// C1C4540/C1C4570/C507B10. C6DF5B8 is a read-only 1.0f (one xref).
class CBloomAddMaterialProxy : public CEntityMaterialProxy
{
public:
	CBloomAddMaterialProxy() : m_pResult( NULL ) {}
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		bool bFound;
		m_pResult = pMaterial->FindVar( "$c0_x", &bFound, false );
		return true;
	}
	virtual IMaterial *GetMaterial() OVERRIDE { return m_pResult ? m_pResult->GetOwningMaterial() : NULL; }
protected:
	virtual void OnBind( C_BaseEntity *pEntity ) OVERRIDE { if ( m_pResult ) m_pResult->SetFloatValue( 1.0f ); }
private:
	IMaterialVar *m_pResult;
};
EXPOSE_INTERFACE( CBloomAddMaterialProxy, IMaterialProxy, "BloomAdd" IMATERIAL_PROXY_INTERFACE_VERSION );

// C2863C0; exact case-insensitive aliases, including unknown -> 9.
static int MaterialProxyZombieClassFromName( const char *pName )
{
	if ( !Q_stricmp( pName, "Tank" ) ) return 8;
	if ( !Q_stricmp( pName, "Spitter" ) ) return 4;
	if ( !Q_stricmp( pName, "Hunter" ) ) return 3;
	if ( !Q_stricmp( pName, "Infected" ) ) return 0;
	if ( !Q_stricmp( pName, "Witch" ) ) return 7;
	if ( !Q_stricmp( pName, "Boomer" ) || !Q_stricmp( pName, "Exploding" ) ) return 2;
	if ( !Q_stricmp( pName, "Smoker" ) || !Q_stricmp( pName, "Gas" ) ) return 1;
	if ( !Q_stricmp( pName, "Charger" ) ) return 6;
	if ( !Q_stricmp( pName, "Jockey" ) ) return 5;
	return 9;
}

class CPlayerTeamProxy : public CResultProxy
{
public:
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		if ( !CResultProxy::Init( pMaterial, pKeys ) ) return false;
		m_nTeam = pKeys->GetInt( "team", 0 );
		return true;
	}
	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BasePlayer *pPlayer = TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer() );
		if ( pPlayer ) SetFloatResult( pPlayer->GetTeamNumber() == m_nTeam ? 1.0f : 0.0f );
	}
private:
	int m_nTeam;
};
EXPOSE_INTERFACE( CPlayerTeamProxy, IMaterialProxy, "PlayerTeam" IMATERIAL_PROXY_INTERFACE_VERSION );

class CPlayerZombieClassProxy : public CResultProxy
{
public:
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		if ( !CResultProxy::Init( pMaterial, pKeys ) ) return false;
		m_nClass = MaterialProxyZombieClassFromName( pKeys->GetString( "class", "" ) );
		return true;
	}
	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		// Native C251BB0 substitutes IN_EYE and CHASE observers, not only IN_EYE.
		C_TerrorPlayer *pPlayer = ToTerrorPlayer( TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer() ) );
		if ( pPlayer ) SetFloatResult( pPlayer->GetTeamNumber() == TEAM_INFECTED && pPlayer->GetZombieClass() == m_nClass && pPlayer->IsAlive() ? 1.0f : 0.0f );
	}
private:
	int m_nClass;
};
EXPOSE_INTERFACE( CPlayerZombieClassProxy, IMaterialProxy, "ZombiePlayerClass" IMATERIAL_PROXY_INTERFACE_VERSION );

// Inputs can be literals or material variables. Resolve the names once but
// read their values at bind time: an earlier proxy can write this bind's input.
class CRemapValClampProxy : public CResultProxy
{
public:
	bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		return CResultProxy::Init( pMaterial, pKeys ) &&
			m_Source.Init( pMaterial, pKeys, "srcVar1" ) &&
			m_InputMin.Init( pMaterial, pKeys, "range_in_min" ) &&
			m_InputMax.Init( pMaterial, pKeys, "range_in_max", 1.0f ) &&
			m_OutputMin.Init( pMaterial, pKeys, "range_out_min" ) &&
			m_OutputMax.Init( pMaterial, pKeys, "range_out_max", 1.0f );
	}

	void OnBind( void *pRenderable ) OVERRIDE
	{
		SetFloatResult( RemapValClamped( m_Source.GetFloat(), m_InputMin.GetFloat(), m_InputMax.GetFloat(),
			m_OutputMin.GetFloat(), m_OutputMax.GetFloat() ) );
	}

private:
	CFloatInput m_Source;
	CFloatInput m_InputMin, m_InputMax;
	CFloatInput m_OutputMin, m_OutputMax;
};
EXPOSE_INTERFACE( CRemapValClampProxy, IMaterialProxy, "RemapValClamp" IMATERIAL_PROXY_INTERFACE_VERSION );

class CPlayerAttachmentProxy : public CResultProxy
{
public:
	bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		m_Name = pKeys->GetString( "name", "" );
		m_nAxis = pKeys->GetInt( "axis", 1 ) - 1;
		return m_Name.Length() && m_nAxis >= 0 && m_nAxis < 3 && CResultProxy::Init( pMaterial, pKeys );
	}

	void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BaseEntity *pEntity = BindArgToEntity( pRenderable );
		C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
		matrix3x4_t attachment;
		const int index = pAnimating ? pAnimating->LookupAttachment( m_Name.Get() ) : 0;
		float facing = 0.0f;
		if ( index > 0 && pAnimating->GetAttachment( index, attachment ) )
		{
			Vector origin, axis;
			MatrixGetColumn( attachment, 3, origin );
			MatrixGetColumn( attachment, m_nAxis, axis );
			Vector towardsModel = origin - CurrentViewOrigin();
			VectorNormalize( towardsModel );
			VectorNormalize( axis );
			facing = DotProduct( towardsModel, axis );
		}
		SetFloatResult( facing );
	}

private:
	CUtlString m_Name;
	int m_nAxis;
};
EXPOSE_INTERFACE( CPlayerAttachmentProxy, IMaterialProxy, "PlayerAttachment" IMATERIAL_PROXY_INTERFACE_VERSION );

class CModuloMaterialProxy : public CResultProxy
{
public:
	bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		return CResultProxy::Init( pMaterial, pKeys ) &&
			m_Dividend.Init( pMaterial, pKeys, "srcVar1" ) && m_Divisor.Init( pMaterial, pKeys, "srcVar2" );
	}

	void OnBind( void *pRenderable ) OVERRIDE
	{
		const float divisor = m_Divisor.GetFloat();
		SetFloatResult( divisor != 0.0f ? fmodf( m_Dividend.GetFloat(), divisor ) : 0.0f );
	}

private:
	CFloatInput m_Dividend, m_Divisor;
};
EXPOSE_INTERFACE( CModuloMaterialProxy, IMaterialProxy, "Modulo" IMATERIAL_PROXY_INTERFACE_VERSION );

class CMaterialSequenceProxy : public CResultProxy
{
public:
	bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		m_nLayer = pKeys->GetInt( "layer", -1 );
		return CResultProxy::Init( pMaterial, pKeys );
	}

	void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BaseEntity *pEntity = BindArgToEntity( pRenderable );
		C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
		C_AnimationLayer *pLayer = MaterialAnimationLayer( pAnimating, m_nLayer );
		int sequence = 0;
		if ( pLayer )
			sequence = pLayer->m_nSequence;
		else if ( pAnimating )
			sequence = pAnimating->GetSequence();
		SetFloatResult( sequence );
	}

private:
	int m_nLayer;
};
EXPOSE_INTERFACE( CMaterialSequenceProxy, IMaterialProxy, "Sequence" IMATERIAL_PROXY_INTERFACE_VERSION );

// Use the bound weapon, not the local inventory. A spectator can see the
// same material on another player's weapon.
static C_BaseCombatWeapon *MaterialWeapon( C_BaseEntity *pEntity )
{
	if ( !pEntity )
		return NULL;
	C_BaseViewModel *pViewModel = dynamic_cast<C_BaseViewModel *>( pEntity );
	if ( pViewModel )
		return pViewModel->GetOwningWeapon();
	C_BaseCombatCharacter *pCharacter = pEntity->MyCombatCharacterPointer();
	if ( pCharacter )
		return pCharacter->GetActiveWeapon();
	return pEntity->MyCombatWeaponPointer();
}

class CClipAmmoMaterialProxy : public CResultProxy
{
public:
	void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BaseCombatWeapon *pWeapon = MaterialWeapon( BindArgToEntity( pRenderable ) );
		SetFloatResult( pWeapon ? pWeapon->Clip1() : 0 );
	}
};
EXPOSE_INTERFACE( CClipAmmoMaterialProxy, IMaterialProxy, "ClipAmmo" IMATERIAL_PROXY_INTERFACE_VERSION );

class CRemainingAmmoMaterialProxy : public CResultProxy
{
public:
	void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BaseCombatWeapon *pWeapon = MaterialWeapon( BindArgToEntity( pRenderable ) );
		int ammo = 0;
		if ( pWeapon && pWeapon->GetOwner() && pWeapon->GetPrimaryAmmoType() >= 0 )
			ammo = pWeapon->GetOwner()->GetAmmoCount( pWeapon->GetPrimaryAmmoType() );
		SetFloatResult( ammo );
	}
};
EXPOSE_INTERFACE( CRemainingAmmoMaterialProxy, IMaterialProxy, "RemainingAmmo" IMATERIAL_PROXY_INTERFACE_VERSION );

class CPlayerBottomProxy : public CResultProxy
{
public:
	void OnBind( void *pRenderable ) OVERRIDE
	{
		C_BaseEntity *pEntity = BindArgToEntity( pRenderable );
		C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
		float facing = 0.0f;
		if ( pAnimating && pAnimating->GetModelPtr() && pAnimating->GetModelPtr()->numbones() )
		{
			matrix3x4_t root;
			pAnimating->GetBoneTransform( 0, root );
			Vector origin, axis;
			MatrixGetColumn( root, 3, origin );
			MatrixGetColumn( root, 0, axis );
			Vector towardsModel = origin - CurrentViewOrigin();
			VectorNormalize( towardsModel );
			VectorNormalize( axis );
			facing = DotProduct( towardsModel, axis );
		}
		SetFloatResult( facing );
	}
};
EXPOSE_INTERFACE( CPlayerBottomProxy, IMaterialProxy, "PlayerBottom" IMATERIAL_PROXY_INTERFACE_VERSION );

#pragma float_control( pop )
