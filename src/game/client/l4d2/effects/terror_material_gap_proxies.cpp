//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "materialsystem/imaterial.h"
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

// C0DC9C0/C0DCAD0/C4FE300. CEntityMaterialProxy is the native bind dispatcher.
class CCycleMaterialProxy : public CEntityMaterialProxy
{
public:
	CCycleMaterialProxy() : m_pMaterial( NULL ), m_pResult( NULL ), m_bEaseIn( false ), m_bEaseOut( false ), m_flStart( 0.0f ), m_flEnd( 1.0f ) {}
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeys ) OVERRIDE
	{
		m_pMaterial = pMaterial;
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
		const float cycle = pAnimating->GetCycle();
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

#pragma float_control( pop )
