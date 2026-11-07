//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Color correction entity.
//
 // $NoKeywords: $
//===========================================================================//
#include "cbase.h"

#include "filesystem.h"
#include "cdll_client_int.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "colorcorrectionmgr.h"
#if defined( L4D2 )
#include "tier1/utlvector.h"
#include "l4d2/entities/brush_roots_client.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( L4D2 )
class C_ColorCorrectionVolume;
static CUtlVector< C_ColorCorrectionVolume * > s_TerrorColorCorrectionVolumes;
class C_ColorCorrectionVolume : public C_BaseTrigger
{
public:
	DECLARE_CLASS( C_ColorCorrectionVolume, C_BaseTrigger );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	C_ColorCorrectionVolume() : m_CCHandle( INVALID_CLIENT_CCHANDLE ) {}
	virtual ~C_ColorCorrectionVolume();
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE { return false; }
	virtual void StartTouch( C_BaseEntity *other ) OVERRIDE;
	virtual void EndTouch( C_BaseEntity *other ) OVERRIDE;
	void UpdateWeight( C_BasePlayer *player, float scale );
private:
	float m_flFadeInStartWeight, m_flFadeInStartTime; //1600,1604
	float m_flFadeOutStartWeight, m_flFadeOutStartTime; //1608,1612
	bool m_bEnabled; //1616
	float m_MaxWeight, m_FadeDuration, m_Weight; //1620,1624,1628
	char m_lookupFilename[MAX_PATH]; //1632
	ClientCCHandle_t m_CCHandle; //1892
};
IMPLEMENT_CLIENTCLASS_DT( C_ColorCorrectionVolume, DT_ColorCorrectionVolume, CColorCorrectionVolume )
	RecvPropBool( RECVINFO( m_bEnabled ) ),
	RecvPropFloat( RECVINFO( m_MaxWeight ) ),
	RecvPropFloat( RECVINFO( m_FadeDuration ) ),
	RecvPropString( RECVINFO( m_lookupFilename ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_ColorCorrectionVolume )
	DEFINE_PRED_FIELD( m_Weight, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

C_ColorCorrectionVolume::~C_ColorCorrectionVolume()
{
	g_pColorCorrectionMgr->RemoveColorCorrection( m_CCHandle );
	const int index = s_TerrorColorCorrectionVolumes.Find( this );
	if ( index != s_TerrorColorCorrectionVolumes.InvalidIndex() ) s_TerrorColorCorrectionVolumes.FastRemove( index );
}
void C_ColorCorrectionVolume::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED && m_CCHandle == INVALID_CLIENT_CCHANDLE )
	{
		char stripped[MAX_PATH], name[MAX_PATH];
		Q_StripExtension( m_lookupFilename, stripped, sizeof( stripped ) );
		Q_snprintf( name, sizeof( name ), "%s_%d", stripped, entindex() );
		m_CCHandle = g_pColorCorrectionMgr->AddColorCorrection( name, m_lookupFilename );
		if ( m_CCHandle != INVALID_CLIENT_CCHANDLE ) s_TerrorColorCorrectionVolumes.AddToTail( this );
		CollisionProp()->SetSolid( SOLID_BSP );
		CollisionProp()->SetSolidFlags( FSOLID_NOT_SOLID | FSOLID_TRIGGER );
	}
}
void C_ColorCorrectionVolume::StartTouch( C_BaseEntity * )
{
	m_flFadeInStartTime = gpGlobals->curtime;
	m_flFadeInStartWeight = m_Weight;
}
void C_ColorCorrectionVolume::EndTouch( C_BaseEntity * )
{
	m_flFadeOutStartTime = gpGlobals->curtime;
	m_flFadeOutStartWeight = m_Weight;
}
#pragma float_control( precise, on, push )
void C_ColorCorrectionVolume::UpdateWeight( C_BasePlayer *player, float scale )
{
	if ( player )
	{
		const bool inside = CollisionProp()->IsPointInBounds( player->EyePosition() );
		const bool wasInside = m_flFadeInStartTime > m_flFadeOutStartTime;
		if ( inside ) { if ( !wasInside ) StartTouch( player ); }
		else if ( wasInside ) EndTouch( player );
	}
	if ( !m_bEnabled ) m_Weight = 0.0f;
	else if ( m_flFadeInStartTime > m_flFadeOutStartTime )
	{
		if ( 1.0f > m_Weight )
		{
			float value = ( gpGlobals->curtime - m_flFadeInStartTime ) /
				( ( 1.0f - m_flFadeInStartWeight ) * m_FadeDuration ) + m_flFadeInStartWeight;
			if ( value > 1.0f ) value = 1.0f;
			m_Weight = value;
		}
	}
	else if ( m_Weight > 0.0f )
	{
		float value = ( gpGlobals->curtime - m_flFadeOutStartTime ) /
			( m_FadeDuration * m_flFadeOutStartWeight ) + ( 1.0f - m_flFadeOutStartWeight );
		if ( value > 1.0f ) value = 1.0f;
		m_Weight = 1.0f - value;
	}
	// Retail receives m_MaxWeight but never multiplies it into this result.
	g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, m_Weight * scale, false );
}
#pragma float_control( pop )
void UpdateTerrorColorCorrectionVolumes( C_BasePlayer *player, float scale )
{
	FOR_EACH_VEC( s_TerrorColorCorrectionVolumes, i ) s_TerrorColorCorrectionVolumes[i]->UpdateWeight( player, scale );
}
#else

//------------------------------------------------------------------------------
// FIXME: This really should inherit from something	more lightweight
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// Purpose : Shadow control entity
//------------------------------------------------------------------------------
class C_ColorCorrectionVolume : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_ColorCorrectionVolume, C_BaseEntity );

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	C_ColorCorrectionVolume();
	virtual ~C_ColorCorrectionVolume();

	void OnDataChanged(DataUpdateType_t updateType);
	bool ShouldDraw();

	void ClientThink();

private:
	float	m_Weight;
	char	m_lookupFilename[MAX_PATH];

	ClientCCHandle_t m_CCHandle;
};

IMPLEMENT_CLIENTCLASS_DT(C_ColorCorrectionVolume, DT_ColorCorrectionVolume, CColorCorrectionVolume)
	RecvPropFloat( RECVINFO(m_Weight) ),
	RecvPropString( RECVINFO(m_lookupFilename) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_ColorCorrectionVolume )
	DEFINE_PRED_FIELD( m_Weight, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()


//------------------------------------------------------------------------------
// Constructor, destructor
//------------------------------------------------------------------------------
C_ColorCorrectionVolume::C_ColorCorrectionVolume()
{
	m_CCHandle = INVALID_CLIENT_CCHANDLE;
}

C_ColorCorrectionVolume::~C_ColorCorrectionVolume()
{
	g_pColorCorrectionMgr->RemoveColorCorrection( m_CCHandle );
}


//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void C_ColorCorrectionVolume::OnDataChanged(DataUpdateType_t updateType)
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_CCHandle == INVALID_CLIENT_CCHANDLE )
		{
			char filename[MAX_PATH];
			Q_strncpy( filename, m_lookupFilename, MAX_PATH );

			m_CCHandle = g_pColorCorrectionMgr->AddColorCorrection( filename );
			SetNextClientThink( ( m_CCHandle != INVALID_CLIENT_CCHANDLE ) ? CLIENT_THINK_ALWAYS : CLIENT_THINK_NEVER );
		}
	}
}

//------------------------------------------------------------------------------
// We don't draw...
//------------------------------------------------------------------------------
bool C_ColorCorrectionVolume::ShouldDraw()
{
	return false;
}

void C_ColorCorrectionVolume::ClientThink()
{
	Vector entityPosition = GetAbsOrigin();
	g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, m_Weight );
}













#endif // L4D2
