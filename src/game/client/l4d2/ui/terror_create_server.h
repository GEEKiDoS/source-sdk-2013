//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CREATE_SERVER_H
#define TERROR_CREATE_SERVER_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/Frame.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/Button.h"
#include "tier1/utlvector.h"
class ClientGameUIPanel : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( ClientGameUIPanel, vgui::Frame );
public:
	explicit ClientGameUIPanel( const char *name );
	virtual void Show(); // Native C21E640, real registry show path.
};
class CreateServerPanel;
class CSelectionOverlayPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CSelectionOverlayPanel, vgui::Panel );
public:
	CSelectionOverlayPanel( vgui::Panel *parent, CreateServerPanel *owner, int index );
protected:
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void OnMouseDoublePressed( vgui::MouseCode code ) OVERRIDE;
private:
	CreateServerPanel *m_pOwner;
	int m_iMission;
};
class CMissionPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CMissionPanel, vgui::EditablePanel );
public:
	CMissionPanel( CreateServerPanel *parent, int index, KeyValues *mission );
	void SetSelectionAppearance( bool selected );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	vgui::ImagePanel *m_pBorder;
	vgui::ImagePanel *m_pPicture;
	vgui::Label *m_pTitle;
	Color m_TextColor, m_DisabledColor, m_SelectionColor, m_FillColor;
};
class CreateServerPanel : public ClientGameUIPanel
{
	DECLARE_CLASS_SIMPLE( CreateServerPanel, ClientGameUIPanel );
public:
	CreateServerPanel();
	virtual ~CreateServerPanel();
	void SelectMission( int index );
protected:
	virtual void OnCommand( const char *command ) OVERRIDE;
private:
	void SetPage( int page );
	void Play();
	int m_iPage;
	int m_iSelection;
	CUtlVector< CMissionPanel * > m_Missions;
	vgui::Button *m_pPlay, *m_pNext, *m_pPrev;
};
#endif
