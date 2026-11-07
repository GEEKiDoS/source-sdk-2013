//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_READY_COUNTDOWN_H
#define TERROR_READY_COUNTDOWN_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
class CReadyCountdown : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CReadyCountdown, CTerrorViewportPanel );
public:
	explicit CReadyCountdown( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; } // C2DD8C0.
	virtual bool NeedsUpdate() OVERRIDE { return true; } // C2DDD30.
	virtual void Update() OVERRIDE;
	virtual void ShowPanel( bool state ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	vgui::Panel *m_background, *m_fill;
	vgui::Label *m_seconds1, *m_seconds2, *m_countdown;
};
#endif
