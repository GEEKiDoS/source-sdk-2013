//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Live property editor for DeferredLight_t.
//
//===========================================================================//

#include "cbase.h"
#include "deferred/vgui_light_editor_props.h"
#include "deferred/deferred_light_editor.h"
#include "vgui_controls/textentry.h"
#include "vgui_controls/checkbutton.h"
#include "vgui_controls/label.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static bool ParseFloat( TextEntry *pEntry, float &out )
{
	char buf[64];
	pEntry->GetText( buf, sizeof( buf ) );
	return sscanf( buf, "%f", &out ) == 1;
}

static bool ParseVec3( TextEntry *pEntry, float out[3] )
{
	char buf[128];
	pEntry->GetText( buf, sizeof( buf ) );
	return sscanf( buf, "%f %f %f", &out[0], &out[1], &out[2] ) == 3;
}

static void FormatVec3( TextEntry *pEntry, const float *v, int nDecimals )
{
	char buf[128];
	if ( nDecimals <= 0 )
		Q_snprintf( buf, sizeof( buf ), "%.0f %.0f %.0f", v[0], v[1], v[2] );
	else
		Q_snprintf( buf, sizeof( buf ), "%.1f %.1f %.1f", v[0], v[1], v[2] );
	pEntry->SetText( buf );
}

static void FormatFloat( TextEntry *pEntry, float v )
{
	char buf[64];
	Q_snprintf( buf, sizeof( buf ), "%.2f", v );
	pEntry->SetText( buf );
}

CVGUILightEditor_Properties::CVGUILightEditor_Properties( Panel *pParent )
	: BaseClass( pParent, "LightEditorProperties", false, true )
{
	SetScheme( scheme()->LoadSchemeFromFile( "resource/SourceScheme.res", "SourceScheme" ) );
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	SetMenuButtonVisible( false );
	m_bRefreshing = false;
	BuildRows();
	LoadControlSettings( "resource/deferred/lighteditor_properties.res" );
	SetSizeable( false );
	SetMinimizeButtonVisible( false );
	SetMaximizeButtonVisible( false );
	SetTitle( "Properties", false );
	SetProportional( false );
	RefreshFromEditor();
}

vgui::TextEntry *CVGUILightEditor_Properties::AddTextRow( const char *pszName, const char *pszLabel )
{
	new Label( this, pszName, pszLabel );
	TextEntry *pEntry = new TextEntry( this, VarArgs( "%s_entry", pszName ) );
	pEntry->AddActionSignalTarget( this );
	pEntry->SetCatchEnterKey( false );
	return pEntry;
}

vgui::CheckButton *CVGUILightEditor_Properties::AddCheckRow( const char *pszName, const char *pszLabel )
{
	CheckButton *pCheck = new CheckButton( this, pszName, pszLabel );
	pCheck->AddActionSignalTarget( this );
	return pCheck;
}

void CVGUILightEditor_Properties::BuildRows()
{
	m_pOrigin = AddTextRow( "prop_origin", "Origin" );
	m_pLabelOrigin = assert_cast< Label * >( FindChildByName( "prop_origin", true ) );

	m_pAngles = AddTextRow( "prop_angles", "Angles" );
	m_pLabelAngles = assert_cast< Label * >( FindChildByName( "prop_angles", true ) );

	m_pColor = AddTextRow( "prop_color", "Color 0-255" );
	m_pLabelColor = assert_cast< Label * >( FindChildByName( "prop_color", true ) );

	m_pIntensity = AddTextRow( "prop_intensity", "Intensity" );
	m_pLabelIntensity = assert_cast< Label * >( FindChildByName( "prop_intensity", true ) );

	m_pRadius = AddTextRow( "prop_radius", "Radius" );
	m_pLabelRadius = assert_cast< Label * >( FindChildByName( "prop_radius", true ) );

	m_pInner = AddTextRow( "prop_inner", "Inner cone" );
	m_pLabelInner = assert_cast< Label * >( FindChildByName( "prop_inner", true ) );

	m_pOuter = AddTextRow( "prop_outer", "Outer cone" );
	m_pLabelOuter = assert_cast< Label * >( FindChildByName( "prop_outer", true ) );

	m_pSpot = AddCheckRow( "prop_spot", "Spot" );
	m_pShadow = AddCheckRow( "prop_shadow", "Shadow" );

	m_pCookie = AddTextRow( "prop_cookie", "Cookie" );
	m_pLabelCookie = assert_cast< Label * >( FindChildByName( "prop_cookie", true ) );
	m_pCookie->SetMaximumCharCount( 63 );
}

void CVGUILightEditor_Properties::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetMenuButtonVisible( false );
	LayoutRows();
}

void CVGUILightEditor_Properties::LayoutRows()
{
	int sw, sh;
	engine->GetScreenSize( sw, sh );
	SetSize( 280, 292 );
	SetPos( sw - 292, 48 );

	Label *pLabels[] = {
		m_pLabelOrigin,
		m_pLabelAngles,
		m_pLabelColor,
		m_pLabelIntensity,
		m_pLabelRadius,
		m_pLabelInner,
		m_pLabelOuter,
		NULL,
		NULL,
		m_pLabelCookie,
	};
	Panel *pControls[] = {
		m_pOrigin,
		m_pAngles,
		m_pColor,
		m_pIntensity,
		m_pRadius,
		m_pInner,
		m_pOuter,
		m_pSpot,
		m_pShadow,
		m_pCookie,
	};

	int y = 28;
	for ( int i = 0; i < ARRAYSIZE( pControls ); i++ )
	{
		if ( pLabels[i] )
		{
			pLabels[i]->SetBounds( 8, y, 96, 20 );
			pControls[i]->SetBounds( 108, y, 160, 20 );
		}
		else
		{
			pControls[i]->SetBounds( 8, y, 260, 20 );
		}
		y += 24;
	}
}

void CVGUILightEditor_Properties::OnLightSelectionChanged()
{
	RefreshFromEditor();
}

void CVGUILightEditor_Properties::RefreshFromEditor()
{
	GetLightingEditor()->SyncTemplateFromSelection();
	const DeferredLight_t &light = GetLightingEditor()->AddTemplate();

	m_bRefreshing = true;

	float origin[3] = { light.origin.x, light.origin.y, light.origin.z };
	float angles[3] = { light.angles.x, light.angles.y, light.angles.z };
	float color[3] = { light.color.x * 255.0f, light.color.y * 255.0f, light.color.z * 255.0f };
	FormatVec3( m_pOrigin, origin, 1 );
	FormatVec3( m_pAngles, angles, 1 );
	FormatVec3( m_pColor, color, 0 );
	FormatFloat( m_pIntensity, light.intensity );
	FormatFloat( m_pRadius, light.radius );
	FormatFloat( m_pInner, light.innerCone );
	FormatFloat( m_pOuter, light.outerCone );
	m_pCookie->SetText( light.cookie );
	m_pSpot->SetSelected( light.spot );
	m_pShadow->SetSelected( light.shadow );

	m_bRefreshing = false;
}

void CVGUILightEditor_Properties::OnPropertyChanged( Panel *panel )
{
	if ( m_bRefreshing || !panel )
		return;

	DeferredLight_t value = GetLightingEditor()->AddTemplate();
	EditorLightField_t field;
	float v[3];

	if ( panel == m_pOrigin )
	{
		if ( !ParseVec3( m_pOrigin, v ) )
			return;
		value.origin.Init( v[0], v[1], v[2] );
		field = EDITORLIGHT_ORIGIN;
	}
	else if ( panel == m_pAngles )
	{
		if ( !ParseVec3( m_pAngles, v ) )
			return;
		value.angles.Init( v[0], v[1], v[2] );
		field = EDITORLIGHT_ANGLES;
	}
	else if ( panel == m_pColor )
	{
		if ( !ParseVec3( m_pColor, v ) )
			return;
		value.color.Init( clamp( v[0], 0.0f, 255.0f ) / 255.0f,
			clamp( v[1], 0.0f, 255.0f ) / 255.0f,
			clamp( v[2], 0.0f, 255.0f ) / 255.0f );
		field = EDITORLIGHT_COLOR;
	}
	else if ( panel == m_pIntensity )
	{
		if ( !ParseFloat( m_pIntensity, value.intensity ) )
			return;
		field = EDITORLIGHT_INTENSITY;
	}
	else if ( panel == m_pRadius )
	{
		if ( !ParseFloat( m_pRadius, value.radius ) )
			return;
		field = EDITORLIGHT_RADIUS;
	}
	else if ( panel == m_pInner )
	{
		if ( !ParseFloat( m_pInner, value.innerCone ) )
			return;
		field = EDITORLIGHT_INNERCONE;
	}
	else if ( panel == m_pOuter )
	{
		if ( !ParseFloat( m_pOuter, value.outerCone ) )
			return;
		field = EDITORLIGHT_OUTERCONE;
	}
	else if ( panel == m_pCookie )
	{
		m_pCookie->GetText( value.cookie, sizeof( value.cookie ) );
		field = EDITORLIGHT_COOKIE;
	}
	else
	{
		return;
	}

	GetLightingEditor()->CommitField( field, value );
}

void CVGUILightEditor_Properties::OnCheckButtonChecked( Panel *panel )
{
	if ( m_bRefreshing || !panel )
		return;

	DeferredLight_t value = GetLightingEditor()->AddTemplate();
	CheckButton *pCheck = assert_cast< CheckButton * >( panel );
	if ( panel == m_pSpot )
	{
		value.spot = pCheck->IsSelected();
		GetLightingEditor()->CommitField( EDITORLIGHT_SPOT, value );
	}
	else if ( panel == m_pShadow )
	{
		value.shadow = pCheck->IsSelected();
		GetLightingEditor()->CommitField( EDITORLIGHT_SHADOW, value );
	}
}
