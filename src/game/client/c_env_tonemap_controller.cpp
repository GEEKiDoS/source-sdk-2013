//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================
#include "cbase.h"
#if defined( L4D2 )
#include "viewpostprocess.h"
#endif

extern bool g_bUseCustomAutoExposureMin;
extern bool g_bUseCustomAutoExposureMax;
extern bool g_bUseCustomBloomScale;
extern float g_flCustomAutoExposureMin;
extern float g_flCustomAutoExposureMax;
extern float g_flCustomBloomScale;
extern float g_flCustomBloomScaleMinimum;
#if defined( L4D2 )
extern float g_flTerrorTonemapPercentTarget, g_flTerrorTonemapPercentBrightPixels, g_flTerrorTonemapMinAvgLum;
#endif

#if !defined( L4D2 )
EHANDLE g_hTonemapControllerInUse = NULL;
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_EnvTonemapController : public C_BaseEntity
{
	DECLARE_CLASS( C_EnvTonemapController, C_BaseEntity );
public:
	DECLARE_CLIENTCLASS();

	C_EnvTonemapController();
	~C_EnvTonemapController();
	virtual void	OnDataChanged( DataUpdateType_t updateType );

private:
	bool m_bUseCustomAutoExposureMin;
	bool m_bUseCustomAutoExposureMax;
	bool m_bUseCustomBloomScale;
	float m_flCustomAutoExposureMin;
	float m_flCustomAutoExposureMax;
	float m_flCustomBloomScale;
	float m_flCustomBloomScaleMinimum;
#if defined( L4D2 )
	float m_flBloomExponent, m_flBloomSaturation;
	float m_flTonemapPercentTarget, m_flTonemapPercentBrightPixels, m_flTonemapMinAvgLum;
	friend void Terror_UpdateTonemapController();
#endif
private:
	C_EnvTonemapController( const C_EnvTonemapController & );
};

IMPLEMENT_CLIENTCLASS_DT( C_EnvTonemapController, DT_EnvTonemapController, CEnvTonemapController )
	RecvPropInt( RECVINFO(m_bUseCustomAutoExposureMin) ),
	RecvPropInt( RECVINFO(m_bUseCustomAutoExposureMax) ),
	RecvPropInt( RECVINFO(m_bUseCustomBloomScale) ),
	RecvPropFloat( RECVINFO(m_flCustomAutoExposureMin) ),
	RecvPropFloat( RECVINFO(m_flCustomAutoExposureMax) ),
	RecvPropFloat( RECVINFO(m_flCustomBloomScale) ),
	RecvPropFloat( RECVINFO(m_flCustomBloomScaleMinimum) ),
#if defined( L4D2 )
	RecvPropFloat( RECVINFO( m_flBloomExponent ) ),
	RecvPropFloat( RECVINFO( m_flBloomSaturation ) ),
	RecvPropFloat( RECVINFO( m_flTonemapPercentTarget ) ),
	RecvPropFloat( RECVINFO( m_flTonemapPercentBrightPixels ) ),
	RecvPropFloat( RECVINFO( m_flTonemapMinAvgLum ) ),
#endif
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_EnvTonemapController::C_EnvTonemapController( void )
{
	m_bUseCustomAutoExposureMin = false;
	m_bUseCustomAutoExposureMax = false;
	m_bUseCustomBloomScale = false;
	m_flCustomAutoExposureMin = 0;
	m_flCustomAutoExposureMax = 0;
	m_flCustomBloomScale = 0.0f;
	m_flCustomBloomScaleMinimum = 0.0f;
#if defined( L4D2 )
	m_flBloomExponent = 2.5f;
	m_flBloomSaturation = 1.0f;
	m_flTonemapPercentTarget = 45.0f;
	m_flTonemapPercentBrightPixels = 1.0f;
	m_flTonemapMinAvgLum = 3.0f;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_EnvTonemapController::~C_EnvTonemapController( void )
{
#if !defined( L4D2 )
	if ( g_hTonemapControllerInUse == this )
	{
		g_bUseCustomAutoExposureMin = false;
		g_bUseCustomAutoExposureMax = false;
		g_bUseCustomBloomScale = false;
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_EnvTonemapController::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged(updateType);
#if !defined( L4D2 )

	g_bUseCustomAutoExposureMin = m_bUseCustomAutoExposureMin;
	g_bUseCustomAutoExposureMax = m_bUseCustomAutoExposureMax;
	g_bUseCustomBloomScale = m_bUseCustomBloomScale;
	g_flCustomAutoExposureMin = m_flCustomAutoExposureMin;
	g_flCustomAutoExposureMax = m_flCustomAutoExposureMax;
	g_flCustomBloomScale = m_flCustomBloomScale;
	g_flCustomBloomScaleMinimum = m_flCustomBloomScaleMinimum;

	g_hTonemapControllerInUse = this;
#endif
}


#if defined( L4D2 )
void Terror_UpdateTonemapController()
 {
  C_BasePlayer *player=C_BasePlayer::GetLocalPlayer();
  C_EnvTonemapController *ctrl=player?dynamic_cast<C_EnvTonemapController*>(player->GetTerrorTonemapController()):NULL;
  if(ctrl)
  {
   g_bUseCustomAutoExposureMin=ctrl->m_bUseCustomAutoExposureMin;
   g_bUseCustomAutoExposureMax=ctrl->m_bUseCustomAutoExposureMax;
   g_bUseCustomBloomScale=ctrl->m_bUseCustomBloomScale;
   g_flCustomAutoExposureMin=ctrl->m_flCustomAutoExposureMin;
   g_flCustomAutoExposureMax=ctrl->m_flCustomAutoExposureMax;
   g_flCustomBloomScale=ctrl->m_flCustomBloomScale;
   g_flCustomBloomScaleMinimum=ctrl->m_flCustomBloomScaleMinimum;
   g_flTerrorTonemapPercentTarget=ctrl->m_flTonemapPercentTarget;
   g_flTerrorTonemapPercentBrightPixels=ctrl->m_flTonemapPercentBrightPixels;
   g_flTerrorTonemapMinAvgLum=ctrl->m_flTonemapMinAvgLum;
  }
  else
  {
   g_bUseCustomAutoExposureMax=false; g_bUseCustomBloomScale=false;
   g_flTerrorTonemapPercentTarget=45.0f;
   g_flTerrorTonemapPercentBrightPixels=1.0f;
   g_flTerrorTonemapMinAvgLum=3.0f;
  }
 }
#endif
