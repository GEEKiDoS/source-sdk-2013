//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Light editor mode toolbar.
//
//===========================================================================//

#include "cbase.h"
#include "deferred/vgui_light_editor_controls.h"
#include "deferred/deferred_light_editor.h"
#include "vgui_controls/radiobutton.h"
#include "vgui_controls/button.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

CVGUILightEditor_Controls::CVGUILightEditor_Controls( Panel *pParent )
	: BaseClass( pParent, "LightEditorControls", false, true )
{
	SetScheme( scheme()->LoadSchemeFromFile( "resource/SourceScheme.res", "SourceScheme" ) );
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	SetMenuButtonVisible( false );
	RadioButton *pSelect = new RadioButton( this, "action_select", "Select (X)" );
	RadioButton *pAdd = new RadioButton( this, "action_add", "Add (B)" );
	RadioButton *pTranslate = new RadioButton( this, "action_translate", "Translate (V)" );
	RadioButton *pRotate = new RadioButton( this, "action_rotate", "Rotate (C)" );

	pSelect->SetTabPosition( 1 );
	pSelect->SetSubTabPosition( 1 );
	pAdd->SetTabPosition( 1 );
	pAdd->SetSubTabPosition( 2 );
	pTranslate->SetTabPosition( 1 );
	pTranslate->SetSubTabPosition( 3 );
	pRotate->SetTabPosition( 1 );
	pRotate->SetSubTabPosition( 4 );

	pSelect->AddActionSignalTarget( this );
	pAdd->AddActionSignalTarget( this );
	pTranslate->AddActionSignalTarget( this );
	pRotate->AddActionSignalTarget( this );

	new Button( this, "button_save", "Save", this, "savelights" );
	new Button( this, "button_reload", "Reload", this, "reloadlights" );
	new Button( this, "button_props", "Properties", this, "toggleprops" );

	LoadControlSettings( "resource/deferred/lighteditor_controls.res" );

	SetCloseButtonVisible( false );
	SetSizeable( false );
	SetMinimizeButtonVisible( false );
	SetMaximizeButtonVisible( false );
	SetTitle( "Lights", false );
	SetProportional( false );

	pSelect->SetSelected( true );
	GetLightingEditor()->SetEditorInteractionMode( CLightingEditor::EDITORINTERACTION_SELECT );
}

void CVGUILightEditor_Controls::SelectModeButton( const char *pszName )
{
	RadioButton *pButton = assert_cast< RadioButton * >( FindChildByName( pszName, true ) );
	if ( pButton )
		pButton->SetSelected( true );
}

void CVGUILightEditor_Controls::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetMenuButtonVisible( false );
	LayoutControls();
}

void CVGUILightEditor_Controls::LayoutControls()
{
	int sw, sh;
	engine->GetScreenSize( sw, sh );
	SetSize( 180, 214 );
	SetPos( 8, sh / 2 - 107 );

	const char *pszNames[] = {
		"action_select",
		"action_add",
		"action_translate",
		"action_rotate",
		"button_save",
		"button_reload",
		"button_props",
	};

	int y = 28;
	for ( int i = 0; i < ARRAYSIZE( pszNames ); i++ )
	{
		Panel *pChild = FindChildByName( pszNames[i], true );
		if ( !pChild )
			continue;
		if ( i == 4 )
			y += 8;
		pChild->SetBounds( 8, y, 164, 22 );
		y += 24;
	}
}

void CVGUILightEditor_Controls::OnRadioButtonChecked( Panel *panel )
{
	if ( !panel )
		return;

	const char *pszName = panel->GetName();
	CLightingEditor::EDITORINTERACTION_MODE mode = CLightingEditor::EDITORINTERACTION_SELECT;
	if ( !Q_stricmp( pszName, "action_add" ) )
		mode = CLightingEditor::EDITORINTERACTION_ADD;
	else if ( !Q_stricmp( pszName, "action_translate" ) )
		mode = CLightingEditor::EDITORINTERACTION_TRANSLATE;
	else if ( !Q_stricmp( pszName, "action_rotate" ) )
		mode = CLightingEditor::EDITORINTERACTION_ROTATE;
	else if ( Q_stricmp( pszName, "action_select" ) )
		return;

	GetLightingEditor()->SetEditorInteractionMode( mode );
}

void CVGUILightEditor_Controls::OnCommand( const char *pszCommand )
{
	if ( !Q_stricmp( pszCommand, "savelights" ) )
		PostActionSignal( new KeyValues( "SaveEditorLights" ) );
	else if ( !Q_stricmp( pszCommand, "reloadlights" ) )
		PostActionSignal( new KeyValues( "ReloadEditorLights" ) );
	else if ( !Q_stricmp( pszCommand, "toggleprops" ) )
		PostActionSignal( new KeyValues( "ToggleEditorProperties" ) );
	else
		BaseClass::OnCommand( pszCommand );
}
