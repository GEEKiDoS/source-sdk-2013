//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_INGAME_H
#define TERROR_BASEMODUI_INGAME_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "tier1/utlvector.h"

namespace L4D360UI
{
class GenericConfirmation : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( GenericConfirmation, CL4DFrame );
public:
	struct UsageData_t
	{
		UsageData_t();
		const char *pWindowTitle;
		const wchar_t *pWindowTitleW;
		const char *pMessageText;
		const wchar_t *pMessageTextW;
		bool bOkButtonEnabled;
		void ( *pfnOkCallback )();
		bool bCancelButtonEnabled;
		void ( *pfnCancelCallback )();
		bool bCheckBoxEnabled;
		const char *pCheckBoxText;
		const char *pCheckBoxCvarName;
	};
	GenericConfirmation( vgui::Panel *parent, const char *name = "GenericConfirmation" );
	virtual ~GenericConfirmation();
	void SetUsageData( const UsageData_t &data );
	// Transfers ownership, matching Button::SetCommand(KeyValues*).
	void SetOKCommand( KeyValues *command );
	void SetCancelCommand( KeyValues *command );
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
private:
	void Complete( bool accepted );
	void UpdateControls();
	UsageData_t m_Usage;
	CUtlString m_Title, m_Message, m_CheckText, m_CheckCvar;
	CUtlVector<wchar_t> m_WideTitle, m_WideMessage;
	KeyValues *m_pOKCommand, *m_pCancelCommand;
};

class InGameDifficultySelect : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( InGameDifficultySelect, CL4DFrame );
public:
	InGameDifficultySelect( vgui::Panel *parent, const char *name = "InGameDifficultySelect" );
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	void UpdateCurrentDifficulty();
	bool m_bInitialized;
};

class InGameKickPlayerList : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( InGameKickPlayerList, CL4DFrame );
public:
	InGameKickPlayerList( vgui::Panel *parent, const char *name = "InGameKickPlayerList" );
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	void UpdateRoster();
	bool m_bInitialized;
	CUtlVector<int> m_UserIDs;
};

class InGameChapterSelect : public CL4DFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( InGameChapterSelect, CL4DFrame );
public:
	InGameChapterSelect( vgui::Panel *parent, const char *name = "InGameChapterSelect" );
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	virtual void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE;
	virtual void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE;
	virtual void OnFlyoutMenuCancelled() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
private:
	void InitializeControls();
	void SelectMission( const char *campaign );
	void UpdateChapterRows();
	void UpdateLevelImage( const char *campaign = NULL, int chapter = -1, const char *imageOverride = NULL );
	void SubmitVote();
	char m_Campaign[128];
	int m_Chapter;
	bool m_bChangingMission;
};

class InGameMainMenu : public CL4DFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( InGameMainMenu, CL4DFrame );
public:
	InGameMainMenu( vgui::Panel *parent, const char *name = "InGameMainMenu" );
	virtual ~InGameMainMenu();
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	virtual void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE;
	virtual void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE;
	virtual void OnFlyoutMenuCancelled() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	virtual void OnThink() OVERRIDE;
private:
	bool HandleCommand( const char *command );
	void UpdateControls();
	void UpdateFlyoutControls();
	void OpenDialog( CL4DFrame *dialog );
	vgui::DHANDLE<CL4DFrame> m_hDialog;
};

CL4DFrame *CreateBaseModInGameWindow( vgui::Panel *parent, const char *name );
}
#endif // TERROR_BASEMODUI_INGAME_H
