//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_OPTIONS_H
#define TERROR_BASEMODUI_OPTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "vgui_controls/EditablePanel.h"
#include "tier1/utlvector.h"
#include "ivoicetweak.h"
class IGameUI;
namespace vgui { class Label; class ProgressBar; class SectionedListPanel; }
namespace L4D360UI
{
class CUIGameData
{
public:
	static CUIGameData *Get();
	void SetLookSensitivity( float scale );
	float GetLookSensitivity() const { return m_flLookSensitivity; }
private:
	CUIGameData();
	float m_flLookSensitivity;
};
class SliderControl : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( SliderControl, vgui::EditablePanel );
public:
	SliderControl( vgui::Panel *parent, const char *name );
	void ApplySettings( KeyValues *data ) OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	void SetEnabled( bool enabled ) OVERRIDE;
	void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	void OnMouseReleased( vgui::MouseCode code ) OVERRIDE;
	void OnCursorMoved( int x, int y ) OVERRIDE;
	void NavigateTo() OVERRIDE;
	void NavigateFrom() OVERRIDE;
	void SetValue( float value, bool silent = true );
	float GetValue() const { return m_flValue; }
	void Reset();
	bool IsDirty() const { return m_bDirty; }
protected:
	void PerformLayout() OVERRIDE;
	void PaintBackground() OVERRIDE;
private:
	void SetMouseValue( int x );
	void SetDisplayValue( float value );
	float GetDefaultValue() const;
	L4D360HybridButton *m_pButton;
	vgui::Label *m_pLabel;
	vgui::ProgressBar *m_pProgress;
	vgui::Panel *m_pDefaultMark;
	CUtlString m_ConCommand, m_DefaultCommand;
	float m_flMin, m_flMax, m_flStep, m_flValue;
	Color m_BorderColor;
	bool m_bInverse, m_bDragging, m_bDirty;
};
class SpinnerControl : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( SpinnerControl, vgui::Panel );
public:
	SpinnerControl( vgui::Panel *parent, const char *name );
	void ApplySettings( KeyValues *data ) OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	void NavigateTo() OVERRIDE;
	void NavigateFrom() OVERRIDE;
	void AddItem( const char *text, int value );
	void SetCurrentItem( int item );
	int GetCurrentItem() const { return m_nItem; }
	int GetItemCount() const { return m_Items.Count(); }
	int GetCurrentValue() const;
protected:
	void PerformLayout() OVERRIDE;
	void OnThink() OVERRIDE;
private:
	struct Item_t { CUtlString text; int value; };
	CUtlVector<Item_t> m_Items;
	vgui::Label *m_pLeft, *m_pCurrent, *m_pRight;
	Color m_InactiveColor, m_ActiveColor;
	float m_flArrowTime;
	int m_nItem, m_nArrow;
};
struct OptionChoice_t { const char *control, *command, *cvar, *value; };
class NativeOptionsFrame : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( NativeOptionsFrame, CL4DFrame );
public:
	NativeOptionsFrame( vgui::Panel *parent, const char *name, const char *title );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnClose() OVERRIDE;
	vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
protected:
	void PaintBackground() OVERRIDE;
	void Select( const char *control, const char *command );
	void RefreshChoices( const OptionChoice_t *choices, int count );
	bool ApplyChoice( const char *command, const OptionChoice_t *choices, int count );
	void Confirm( const char *title, const char *message, const char *command );
	void OpenChild( const char *name );
	void ResetSliders( vgui::Panel *panel );
	void SaveConfig();
	void CloseWithoutSaving();
	CUtlString m_TitleToken;
	bool m_bRefreshing;
};
class Audio : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( Audio, NativeOptionsFrame );
public:
	Audio( vgui::Panel *parent, const char *name );
	~Audio();
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnClose() OVERRIDE;
	vgui::Panel *NavigateBack() OVERRIDE;
protected:
	void OnThink() OVERRIDE;
private:
	void Refresh();
	void RefreshVoice();
	void EndMicrophoneTest();
	void PopulateLanguages();
	IVoiceTweak *m_pVoice;
	CUtlVector<CUtlString> m_Languages;
	CUtlString m_OriginalLanguage, m_SelectedLanguage;
	vgui::DHANDLE<vgui::Frame> m_hCredits;
	bool m_bTesting, m_bLanguageStateInitialized;
};
class AudioVideo : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( AudioVideo, NativeOptionsFrame );
public:
	AudioVideo( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
};
class Cloud : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( Cloud, NativeOptionsFrame );
public:
	Cloud( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnClose() OVERRIDE;
private:
	bool m_bCloud;
};
class ControllerOptions : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( ControllerOptions, NativeOptionsFrame );
public:
	ControllerOptions( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
};
class ControllerOptionsButtons : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( ControllerOptionsButtons, NativeOptionsFrame );
public:
	ControllerOptionsButtons( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
protected:
	void OnThink() OVERRIDE;
private:
	void RefreshBindings();
};
class ControllerOptionsSticks : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( ControllerOptionsSticks, NativeOptionsFrame );
public:
	ControllerOptionsSticks( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
private:
	void Refresh();
};
class KeyboardMouse : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( KeyboardMouse, NativeOptionsFrame );
public:
	KeyboardMouse( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
private:
	void Refresh();
};
class Multiplayer : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( Multiplayer, NativeOptionsFrame );
public:
	Multiplayer( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
protected:
	void OnThink() OVERRIDE;
private:
	void PopulateSprays();
	CUtlVector<CUtlString> m_Sprays;
	CUtlString m_CurrentLogo;
	int m_nLastFOV;
};
class Options : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( Options, NativeOptionsFrame );
public:
	Options( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
};
class GameOptions : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( GameOptions, NativeOptionsFrame );
public:
	GameOptions( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
private:
	MESSAGE_FUNC_PARAMS( OnSpinnerChanged, "OnSetCurrentItem", data );
};
class Video : public NativeOptionsFrame
{
	DECLARE_CLASS_SIMPLE( Video, NativeOptionsFrame );
public:
	Video( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnClose() OVERRIDE;
	void ApplyVideoSettings();
	void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE;
protected:
	void OnThink() OVERRIDE;
	void PaintBackground() OVERRIDE;
private:
	MESSAGE_FUNC_PARAMS( OnSliderMoved, "SliderMoved", data );
	struct Mode_t { int width, height; };
	struct AAMode_t { int samples, quality; };
	void ReadSettings( bool recommended = false );
	void Refresh();
	void SelectOption( const char *control, const char *command );
	bool StageCommand( const char *command );
	void PopulateModes();
	void PopulateAA();
	CUtlVector<Mode_t> m_Modes;
	CUtlVector<AAMode_t> m_AAModes;
	vgui::DHANDLE<vgui::Frame> m_hCredits;
	vgui::DHANDLE<DropDownMenu> m_hAspectRatio;
	int m_nAspect, m_nWidth, m_nHeight, m_nGPU, m_nGPUMem, m_nCPU, m_nMem;
	int m_nAA, m_nAniso, m_nQueue;
	float m_flOldGamma, m_flOldGrain;
	bool m_bWindowed, m_bBorderless, m_bVSync, m_bTriple, m_bDirty, m_bCancelled, m_bAdvanced;
};
vgui::Panel *CreateBaseModOptionsControl( vgui::Panel *parent, const char *className );
CL4DFrame *CreateNativeOptionsWindow( vgui::Panel *parent, const char *name );
bool IsNativeOptionsCommand( const char *command );
void SetNativeOptionsGameUI( IGameUI *gameUI );
void OpenNativeSprayImport();
void OpenNativeKeyboardBindings( CL4DFrame *caller );
void ShowNativeOptionsCredits( Audio *caller, vgui::DHANDLE<vgui::Frame> &credits );
void ShowNativeOptionsCredits( Video *caller, vgui::DHANDLE<vgui::Frame> &credits );
}
#endif
