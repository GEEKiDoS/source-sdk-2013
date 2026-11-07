//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SPECTATOR_GUI_H
#define TERROR_SPECTATOR_GUI_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
class C_BaseEntity;
class CTerrorSpectatorGUI : public CTerrorViewportPanel
{
 DECLARE_CLASS_SIMPLE(CTerrorSpectatorGUI,CTerrorViewportPanel);
public:
 explicit CTerrorSpectatorGUI(IViewPort *viewport);
 virtual ~CTerrorSpectatorGUI();
 virtual bool HasInputElements() OVERRIDE {return false;}
 virtual bool NeedsUpdate() OVERRIDE;
 virtual void Update() OVERRIDE;
 virtual void SetData(KeyValues *data) OVERRIDE;
 virtual void ShowPanel(bool state) OVERRIDE;
 virtual bool IsVisible() OVERRIDE;
protected:
 virtual void ApplySchemeSettings(vgui::IScheme *scheme) OVERRIDE;
private:
 void UpdatePlayerLabel(int observerMode,C_BaseEntity *target);
 void UpdateRescueMessage();
 KeyValues *m_deathData;
 IViewPortPanel *m_scoreboard;
 int m_team;
 bool m_teamWritten,m_scoreboardVisible;
};
#endif
