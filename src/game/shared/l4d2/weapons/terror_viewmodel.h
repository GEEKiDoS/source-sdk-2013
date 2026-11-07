//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 predicted viewmodel identity and precache contract.
//
//=============================================================================//

#ifndef TERROR_VIEWMODEL_H
#define TERROR_VIEWMODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "predicted_viewmodel.h"

#if defined( CLIENT_DLL )
#include "particles_new.h"
#include "util_shared.h"
class C_ViewmodelAttachmentModel;
#define CTerrorViewModel C_TerrorViewModel
#endif

class CTerrorViewModel : public CPredictedViewModel
{
	DECLARE_CLASS( CTerrorViewModel, CPredictedViewModel );

public:
	DECLARE_NETWORKCLASS();

	virtual void Precache( void ) OVERRIDE;
#ifdef CLIENT_DLL
	CTerrorViewModel();
	virtual ~CTerrorViewModel();
	virtual bool Simulate() OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void OnNewParticleEffect( const char *name, CNewParticleEffect *effect ) OVERRIDE;
	virtual void OnParticleEffectDeleted( CNewParticleEffect *effect ) OVERRIDE;
	virtual void OnDormancyChanged( bool dormant ) OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual void CalcViewModelView( CBasePlayer *owner, const Vector &eyePosition, const QAngle &eyeAngles ) OVERRIDE;
	virtual void CalcViewModelLag( Vector &origin, QAngle &angles, QAngle &original ) OVERRIDE;
	virtual int GetMuzzleFlashAttachment( bool dual, bool right ) OVERRIDE;
	int GetEjectBrassAttachment( bool dual, bool right );
	void UpdateTerrorArms();
	virtual void ProcessMuzzleFlashEvent() OVERRIDE {}

private:
	void RemoveTerrorArms();
	void CacheModelPoseParameters( CStudioHdr *model );
	void UpdateThrowableParticles();
	void UpdateViewModelPoseParameters( CBasePlayer *owner );
	void UpdateViewModelFidget( CBasePlayer *owner );
	int m_nMoveXPoseParameter; // Native3176; constructor write omitted.
	int m_nVerticalAimPoseParameter; // Native3180; constructor write omitted.
	int m_nHorizontalAimPoseParameter; // Native3184; constructor write omitted.
	float m_flLastFacingYaw; // Native3188; constructor write omitted.
	CountdownTimer m_viewmodelFidgetTimer; // Native3192: one genuine0/-1 default.
	HPARTICLEFFECT m_hMolotovWickEffect; // Native3204: one genuine weak-reference default.
	HPARTICLEFFECT m_hThrowableEffect; // Native3216: one genuine weak-reference default.
	CHandle< C_ViewmodelAttachmentModel > m_hTerrorArms; // Native3228: INVALID default.
	char m_szTerrorArmsModel[64]; // Native3232; constructor write omitted.
	int m_nMuzzleAttachment; // Native3296; OnNewModel publishes all six caches.
	int m_nMuzzleRightAttachment;
	int m_nMuzzleLeftAttachment;
	int m_nShellAttachment;
	int m_nShellRightAttachment;
	int m_nShellLeftAttachment;
#endif
};

#endif // TERROR_VIEWMODEL_H
