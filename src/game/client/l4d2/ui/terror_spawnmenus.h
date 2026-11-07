//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SPAWNMENUS_H
#define TERROR_SPAWNMENUS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
#include "inputsystem/ButtonCode.h"
class CBindPanel;
namespace vgui { class Label; }

class CSpawnBaseMenu : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CSpawnBaseMenu, CTerrorViewportPanel );
public:
	CSpawnBaseMenu( IViewPort *viewport, const char *name );
	virtual bool HasInputElements() OVERRIDE { return false; } // 2E2340.
	virtual void ShowPanel( bool state ) OVERRIDE; // 2E1750.
	virtual void SetVisible( bool state ) OVERRIDE; // 2E1730.
	virtual bool IsVisible() OVERRIDE; // 2E2060.
	virtual bool HudElementKeyInput( int down, ButtonCode_t key, const char *binding ); // Native v1124, 2E1930.
	bool CanShowPanel( const char *name ) const; // Native IViewPortPanel v32, 2E1A00.
	bool IsAllowedToShow() const; // Native IViewPortPanel v36, 2E14D0.
protected:
	virtual const char *GetLayoutFile() const; // Native v1120.
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
	void CachePanelPointers(); // Native v1128, 2E15A0.
private:
	int m_iZombieClass;
	vgui::Panel *m_pBackground;
	CBindPanel *m_pContinueBind;
	vgui::Label *m_pContinueLabel;
	vgui::PHandle m_Scores;
	vgui::PHandle m_VersusScores;
	vgui::PHandle m_VersusResults;
	vgui::PHandle m_ScavengeScores;
	vgui::PHandle m_ZombiePanel;
	vgui::PHandle m_Vote;
	vgui::PHandle m_Team;
	CPanelAnimationVarAliasType( int, m_iPadding, "padding", "4", "proportional_int" );
};

class CSpawnModeMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnModeMenu, CSpawnBaseMenu );
public:
	explicit CSpawnModeMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawnmode"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
};
class CSpawnHunterMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnHunterMenu, CSpawnBaseMenu );
public:
	explicit CSpawnHunterMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_hunter"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};
class CSpawnSmokerMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnSmokerMenu, CSpawnBaseMenu );
public:
	explicit CSpawnSmokerMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_smoker"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};
class CSpawnBoomerMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnBoomerMenu, CSpawnBaseMenu );
public:
	explicit CSpawnBoomerMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_boomer"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};
class CSpawnChargerMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnChargerMenu, CSpawnBaseMenu );
public:
	explicit CSpawnChargerMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_charger"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};
class CSpawnJockeyMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnJockeyMenu, CSpawnBaseMenu );
public:
	explicit CSpawnJockeyMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_jockey"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};
class CSpawnSpitterMenu : public CSpawnBaseMenu
{
	DECLARE_CLASS_SIMPLE( CSpawnSpitterMenu, CSpawnBaseMenu );
public:
	explicit CSpawnSpitterMenu( IViewPort *viewport );
	virtual const char *GetName() OVERRIDE { return "spawn_spitter"; }
protected:
	virtual const char *GetLayoutFile() const OVERRIDE;
};

class CSpawnModeBar : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CSpawnModeBar, CTerrorViewportPanel );
public:
	explicit CSpawnModeBar( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; } // 2E1C60.
	virtual void SetData( KeyValues *data ) OVERRIDE; // 2E1790.
	virtual void ShowPanel( bool state ) OVERRIDE; // 2E17E0.
private:
	int m_iZombieClass;
};

const char *GetTerrorSpawnMenuName( int index ); // 2E10B0, InitHUD cache order.
IViewPortPanel *CreateTerrorSpawnPanel( IViewPort *viewport, const char *name );
#endif
