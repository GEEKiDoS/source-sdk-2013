//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_SCAVENGE_PROGRESS_DISPLAY_H
#define L4D2_C_SCAVENGE_PROGRESS_DISPLAY_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
#include "hudelement.h"
#include "vgui_controls/EditablePanel.h"
class CHudScavengeProgress : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudScavengeProgress, vgui::EditablePanel );
public:
	CHudScavengeProgress( const char *name );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void LevelInit() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	void SetDisplayActive( bool active );
	virtual void OnThink() OVERRIDE;
private:
	int m_nItemsCollected;
	int m_nItemsGoal;
	bool m_bNativeUnused;
	bool m_bShow;
};
class CScavengeProgressDisplay : public C_BaseEntity
{
public:
	DECLARE_CLASS( CScavengeProgressDisplay, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	DECLARE_DATADESC();
	virtual datamap_t *GetPredDescMap() OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
private:
	int m_bActive;
};
#endif // L4D2_C_SCAVENGE_PROGRESS_DISPLAY_H
