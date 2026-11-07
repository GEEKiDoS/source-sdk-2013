//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef CLIENTMODE_TERROR_H
#define CLIENTMODE_TERROR_H
#ifdef _WIN32
#pragma once
#endif
#include "clientmode_shared.h"
#include "colorcorrectionmgr.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "l4d2/effects/terror_postprocess_state.h"
#include "vgui_controls/PHandle.h"

class CHudCrosshair;
class C_TerrorPlayer;
namespace vgui { class Panel; }

// The existing normal mode is the sole TerrorClientMode owner. Original
// offsets in comments are provenance, never target layout requirements.
class ClientModeTerrorNormal : public ClientModeShared
{
	DECLARE_CLASS( ClientModeTerrorNormal, ClientModeShared );
public:
	ClientModeTerrorNormal();
	virtual ~ClientModeTerrorNormal();
	virtual void InitViewport() OVERRIDE;
	virtual void Init() OVERRIDE;
	virtual void VGui_Shutdown() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual void Disable() OVERRIDE;
	virtual void LevelInit( const char *newmap ) OVERRIDE;
	virtual void LevelShutdown() OVERRIDE;
	virtual void OverrideView( CViewSetup *setup ) OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	virtual bool DoPostScreenSpaceEffects( const CViewSetup *setup ) OVERRIDE;
	virtual bool CanRecordDemo( char *errorMsg, int length ) const OVERRIDE;
	virtual bool ShouldDrawViewModel() OVERRIDE;
	virtual float GetViewModelFOV() OVERRIDE;
	void InitHUD(); // native2740E0, actual CHud::ResetHUD final callback
	virtual int HudElementKeyInput( int down, ButtonCode_t key, const char *binding ) OVERRIDE;

	// Native 271530 is invoked by the actual correction manager, after reset.
	void OnColorCorrectionWeightsReset();
	void OnColorCorrectionEntityDeleted( C_BaseEntity *entity );
	double GetModeSaturation() const;
	const float *GetPostProcessValues() const { return m_PostProcess.GetValues(); }
	void ReloadPostProcessParams( const char *filename );

	// The real viewport panels publish these fields; no inferred visibility.
	void AddPanelOpenFlag( int flag ) { m_nPanelOpenFlags |= flag; }
	void RemovePanelOpenFlag( int flag ) { m_nPanelOpenFlags &= ~flag; }
	void NotifyDeathPanelVisible() { m_flObserverTransitionTime = gpGlobals->realtime; }
	void SetTransitionStatsPanelOpen( bool state ) { m_bTransitionStatsPanelOpen = state; }
	bool IsTransitionStatsPanelOpen() const { return m_bTransitionStatsPanelOpen; }
	bool AreEndOfRoundStatsRequested() const { return m_bStatsRequested; }
	void ShowStats();
	void StartTransitionFade( bool hold );
	void StopTransitionFade();
	float GetTransitionFadeScale() const;
	void SetBlurScale( float scale );
	float GetBlurScale() const { return m_flBlurScale; }

private:
	void UpdatePanelChatPosition();
	void UpdateCinematicLight();
	void ReleaseCinematicLight();
	void UpdateModeColorWeights();
	void UpdatePostProcessing();
	void ResetRoundPresentation();
	void ProcessCSEvent( IGameEvent *event );
	void RecordSurvivalTime( uint64 centiseconds );

	float m_flColorWeight[4];                         // 48..60
	ClientCCHandle_t m_hColorCorrection[4];           // 64..76
	float m_flObserverTransitionTime;                 // 80, realtime
	float m_flObserverYaw;                            // 84
	int m_nPanelOpenFlags, m_nPreviousPanelOpenFlags;  // 88,92
	bool m_bCinematicLightActive;                     // 96
	float m_flSavedExposure;                          // 100
	bool m_bCinematicExposureActive;                  // 104
	CTextureReference m_CinematicTexture;             // 108
	CountdownTimer m_CinematicTimer;                  // 116
	bool m_bStatsRequested;                           // 128
	bool m_bTransitionFadeActive, m_bHoldTransitionFade; // 129,130
	CountdownTimer m_TransitionFadeTimer;              // 132
	float m_flBlurScale;                              // 144
	TerrorPostProcessPresets_t m_PostPresets;
	CTerrorPostProcessState m_PostProcess;             // native 148..288
	bool m_bDominationPostProcess;                    // 292
	bool m_bTransitionStatsPanelOpen;                 // 293
	CHudCrosshair *m_pCrosshair;                       // 296
	C_BaseEntity *m_pActiveColorCorrection;            // 300, borrowed native pointer
	float m_flShowStatsTime;                          // 304
	CUtlVector< vgui::PHandle > m_SpawnMenus; // native308..324, no owning panel pointers
};

IClientMode *GetClientModeNormal();
IClientMode *GetClientMode();
void RegisterClientGameUIPanel( vgui::Panel *panel );
void ProcessTerrorClientMessageQueue();

#endif // CLIENTMODE_TERROR_H
