//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Fullscreen light-editor input surface.
//
//===========================================================================//

#include "cbase.h"
#include "deferred/vgui_light_editor_root.h"
#include "deferred/vgui_light_editor_controls.h"
#include "deferred/vgui_light_editor_props.h"
#include "deferred/deferred_light_editor.h"
#include "ienginevgui.h"
#include "collisionutils.h"
#include <vgui/IVGui.h>
#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IPanel.h>
#include "vgui/Cursor.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

CVGUILightEditor *CVGUILightEditor::s_pInstance = NULL;

static void NormalizeAngles( QAngle &angles )
{
	angles.x = AngleNormalize( angles.x );
	angles.y = AngleNormalize( angles.y );
	angles.z = AngleNormalize( angles.z );
}

void CVGUILightEditor::DestroyEditor()
{
	if ( !s_pInstance )
		return;

	CVGUILightEditor *pPanel = s_pInstance;
	s_pInstance = NULL;
	GetLightingEditor()->AbortEditorMovement( true );
	GetLightingEditor()->SetEditorActive( false );
	pPanel->DeletePanel();
}

void CVGUILightEditor::ToggleEditor()
{
	if ( !s_pInstance )
	{
		s_pInstance = new CVGUILightEditor( enginevgui->GetPanel( PANEL_CLIENTDLL ) );
		s_pInstance->MakeReadyForUse();
		s_pInstance->SetVisible( true );
		s_pInstance->MoveToFront();
	}
	else
	{
		s_pInstance->SetVisible( !s_pInstance->IsVisible() );
	}

	GetLightingEditor()->AbortEditorMovement( true );
	GetLightingEditor()->SetEditorActive( s_pInstance->IsVisible() );
	if ( s_pInstance->IsVisible() )
	{
		s_pInstance->m_pControls->MoveToFront();
		if ( s_pInstance->m_pProps->IsVisible() )
			s_pInstance->m_pProps->MoveToFront();
	}
	surface()->CalculateMouseVisible();
	s_pInstance->UpdateMapName();
}

bool CVGUILightEditor::IsEditorVisible()
{
	return s_pInstance && s_pInstance->IsVisible();
}

VPANEL CVGUILightEditor::GetEditorPanel()
{
	if ( !s_pInstance )
		return (VPANEL)0;
	return s_pInstance->GetVPanel();
}

bool CVGUILightEditor::IsTextEntryFocused()
{
	if ( !s_pInstance || !s_pInstance->IsVisible() )
		return false;

	VPANEL focus = input()->GetFocus();
	if ( !focus )
		return false;
	if ( focus != s_pInstance->GetVPanel() && !ipanel()->HasParent( focus, s_pInstance->GetVPanel() ) )
		return false;

	Panel *pPanel = ipanel()->GetPanel( focus, GetControlsModuleName() );
	if ( !pPanel )
		return false;
	return !Q_stricmp( pPanel->GetClassName(), "TextEntry" );
}

CVGUILightEditor::CVGUILightEditor( VPANEL pParent )
	: BaseClass( NULL, "LightEditorRoot" )
{
	SetParent( pParent );
	SetScheme( scheme()->LoadSchemeFromFile( "resource/SourceScheme.res", "SourceScheme" ) );
	SetProportional( false );
	SetConsoleStylePanel( true );
	SetZPos( 1000 );
	int w, h;
	engine->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );

	m_iDragMode = EDITORDRAG_NONE;
	m_bDragSelectArmed = false;
	m_iLastMouseX = 0;
	m_iLastMouseY = 0;
	m_iDragStart[0] = m_iDragStart[1] = 0;
	m_iLastInteractionMode = CLightingEditor::EDITORINTERACTION_SELECT;
	m_flLastInteractionTypeKeyPress = 0;
	m_bModeKeyHeld = false;
	m_vecLastDragWorldPos.Init();
	Q_memset( m_bMouseDown, 0, sizeof( m_bMouseDown ) );
	Q_memset( m_bKeyDown, 0, sizeof( m_bKeyDown ) );

	m_pMapName = new Label( this, "MapName", "" );
	m_pMapName->SetMouseInputEnabled( false );
	m_pMapName->SetKeyBoardInputEnabled( false );
	m_pMapName->SetPaintBackgroundEnabled( false );
	m_pMapName->SetFgColor( Color( 220, 220, 220, 255 ) );
	UpdateMapName();

	m_pControls = new CVGUILightEditor_Controls( this );
	m_pControls->Activate();
	m_pControls->AddActionSignalTarget( this );

	m_pProps = new CVGUILightEditor_Properties( this );
	m_pProps->AddActionSignalTarget( this );
	AddActionSignalTarget( m_pProps );
}

CVGUILightEditor::~CVGUILightEditor()
{
}

void CVGUILightEditor::UpdateMapName()
{
	const char *pszLevel = engine->GetLevelName();
	if ( !pszLevel || !pszLevel[0] )
	{
		m_pMapName->SetText( "" );
		return;
	}

	char szName[MAX_PATH];
	Q_FileBase( pszLevel, szName, sizeof( szName ) );
	m_pMapName->SetText( szName );
}

void CVGUILightEditor::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetProportional( false );
	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	m_pMapName->SetProportional( false );
	m_pMapName->SetPaintBackgroundEnabled( false );
	m_pMapName->SetFgColor( Color( 220, 220, 220, 255 ) );
}

void CVGUILightEditor::PerformLayout()
{
	BaseClass::PerformLayout();

	int w, h;
	engine->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	m_pMapName->SetBounds( 12, h - 22, 240, 18 );
	m_pMapName->SetPaintBackgroundEnabled( false );
}

void CVGUILightEditor::OnToggleEditorProperties()
{
	if ( m_pProps->IsVisible() )
		m_pProps->Close();
	else
		m_pProps->Activate();
}

void CVGUILightEditor::OnSaveEditorLights()
{
	DeferredLights().Save();
}

void CVGUILightEditor::OnReloadEditorLights()
{
	DeferredLights().Load();
	GetLightingEditor()->ClearSelection();
	NotifySelectionChanged();
}

void CVGUILightEditor::NotifySelectionChanged()
{
	PostActionSignal( new KeyValues( "LightSelectionChanged" ) );
}

bool CVGUILightEditor::IsWorldTarget( VPANEL over ) const
{
	if ( !over || over == GetVPanel() )
		return true;
	if ( ipanel()->HasParent( over, GetVPanel() ) )
		return false;
	return over == enginevgui->GetPanel( PANEL_CLIENTDLL );
}

void CVGUILightEditor::OnMousePressed_Internal( MouseCode code )
{
	StopDrag();

	if ( code == MOUSE_RIGHT )
	{
		Panel *pFocused = ipanel()->GetPanel( input()->GetFocus(), GetControlsModuleName() );
		if ( pFocused && pFocused->GetParent() )
			pFocused->GetParent()->RequestFocus();

		StartDrag( EDITORDRAG_VIEW );
		m_iLastMouseX = GetWide() / 2;
		m_iLastMouseY = GetTall() / 2;
		input()->SetCursorPos( m_iLastMouseX, m_iLastMouseY );
		input()->SetCursorOveride( dc_none );
	}
	else if ( code == MOUSE_LEFT )
	{
		int mx, my;
		input()->GetCursorPos( mx, my );
		CLightingEditor *pEditor = GetLightingEditor();

		switch ( pEditor->GetEditorInteractionMode() )
		{
		case CLightingEditor::EDITORINTERACTION_SELECT:
			m_bDragSelectArmed = true;
			break;
		case CLightingEditor::EDITORINTERACTION_ADD:
			pEditor->AddLightFromScreen( mx, my );
			NotifySelectionChanged();
			break;
		case CLightingEditor::EDITORINTERACTION_TRANSLATE:
			if ( pEditor->GetCurrentSelectedAxis() != CLightingEditor::EDITORAXIS_NONE )
			{
				StartDrag( EDITORDRAG_TRANSLATE );
				m_vecLastDragWorldPos = GetDragWorldPos( mx, my );
			}
			break;
		case CLightingEditor::EDITORINTERACTION_ROTATE:
			if ( pEditor->GetCurrentSelectedAxis() != CLightingEditor::EDITORAXIS_NONE &&
				pEditor->GetCurrentSelectedAxis() < CLightingEditor::EDITORAXIS_FIRST_PLANE )
			{
				StartDrag( EDITORDRAG_ROTATE );
				m_vecLastDragWorldPos = GetDragWorldPos( mx, my );
				pEditor->SetSelectionCenterLocked( true );
			}
			break;
		default:
			break;
		}
	}
}

void CVGUILightEditor::OnMouseReleased( MouseCode code )
{
	BaseClass::OnMouseReleased( code );
}

void CVGUILightEditor::OnMouseReleased_Internal( MouseCode code )
{
	bool bDoLightUpdate = code == MOUSE_LEFT &&
		( m_iDragMode == EDITORDRAG_TRANSLATE || m_iDragMode == EDITORDRAG_ROTATE );
	bool bSelectBox = m_iDragMode == EDITORDRAG_SELECT;

	StopDrag();

	if ( code == MOUSE_LEFT &&
		GetLightingEditor()->GetEditorInteractionMode() == CLightingEditor::EDITORINTERACTION_SELECT )
	{
		int mx, my;
		input()->GetCursorPos( mx, my );
		bool bShift = input()->IsKeyDown( KEY_LSHIFT ) || input()->IsKeyDown( KEY_RSHIFT );
		bool bCtrl = input()->IsKeyDown( KEY_LCONTROL ) || input()->IsKeyDown( KEY_RCONTROL );

		if ( bSelectBox )
		{
			int dragMin[2], dragMax[2];
			GetDragBounds( dragMin, dragMax );
			if ( GetLightingEditor()->SelectLights( dragMin, dragMax, bShift, bCtrl ) )
				bDoLightUpdate = true;
		}
		else if ( GetLightingEditor()->SelectLight( mx, my, bShift, bCtrl ) )
		{
			bDoLightUpdate = true;
		}
	}

	if ( bDoLightUpdate )
		NotifySelectionChanged();
}

void CVGUILightEditor::OnMouseCaptureLost()
{
	StopDrag();
	BaseClass::OnMouseCaptureLost();
}

Vector CVGUILightEditor::GetDragWorldPos( int x, int y )
{
	CLightingEditor *pEditor = GetLightingEditor();
	Vector center = pEditor->GetSelectionCenter();
	if ( !pEditor->IsAnythingSelected() )
		return center;

	Vector picker = pEditor->GetPickingRay( x, y );
	Vector viewOrigin = pEditor->GetViewOrigin();
	QAngle orientation = pEditor->GetSelectionOrientation();
	Vector viewFwd;
	AngleVectors( pEditor->GetViewAngles(), &viewFwd );

	Ray_t pickingRay;
	pickingRay.Init( viewOrigin, viewOrigin + picker * MAX_TRACE_LENGTH );

	CLightingEditor::EDITOR_SELECTEDAXIS axis = pEditor->GetCurrentSelectedAxis();
	Vector ret = center;

	if ( m_iDragMode == EDITORDRAG_TRANSLATE )
	{
		const bool bMoveScreen = axis == CLightingEditor::EDITORAXIS_SCREEN;
		const bool bMovePlane = axis >= CLightingEditor::EDITORAXIS_FIRST_PLANE;
		if ( bMovePlane || bMoveScreen )
		{
			Vector normals[3] = {
				Vector( 0, 0, 1 ),
				Vector( 0, 1, 0 ),
				Vector( 1, 0, 0 ),
			};
			cplane_t plane;
			plane.normal = bMoveScreen ? viewFwd : normals[ axis - CLightingEditor::EDITORAXIS_FIRST_PLANE ];
			plane.dist = DotProduct( plane.normal, center );
			float flDist = IntersectRayWithPlane( pickingRay, plane );
			ret = pickingRay.m_Start + pickingRay.m_Delta * flDist;
		}
		else
		{
			Vector direction( vec3_origin );
			direction[ axis - CLightingEditor::EDITORAXIS_FIRST ] = 1;
			Ray_t axisRay;
			axisRay.Init( center, center + direction );
			float t = 0, s = 0;
			IntersectRayWithRay( pickingRay, axisRay, t, s );
			ret = center + direction * s;
		}
	}
	else if ( m_iDragMode == EDITORDRAG_ROTATE )
	{
		Vector normals[4];
		normals[3] = viewOrigin - center;
		normals[3].NormalizeInPlace();
		AngleVectors( orientation, &normals[2], &normals[0], &normals[1] );

		int iAxis = axis - CLightingEditor::EDITORAXIS_FIRST;
		if ( iAxis < 0 )
			iAxis = 0;
		if ( iAxis > 3 )
			iAxis = 3;

		cplane_t plane;
		plane.normal = normals[iAxis];
		plane.dist = DotProduct( plane.normal, center );
		float t = IntersectRayWithPlane( pickingRay, plane );
		ret = pickingRay.m_Start + pickingRay.m_Delta * t;
	}

	return ret;
}

void CVGUILightEditor::GetDragBounds( int *pi2_min, int *pi2_max )
{
	int x, y;
	input()->GetCursorPos( x, y );
	pi2_min[0] = MIN( x, m_iDragStart[0] );
	pi2_min[1] = MIN( y, m_iDragStart[1] );
	pi2_max[0] = MAX( x, m_iDragStart[0] );
	pi2_max[1] = MAX( y, m_iDragStart[1] );
}

void CVGUILightEditor::StartDrag( int mode )
{
	m_iDragMode = mode;
	input()->SetMouseCapture( GetVPanel() );
	input()->GetCursorPos( m_iDragStart[0], m_iDragStart[1] );
}

void CVGUILightEditor::StopDrag()
{
	m_bDragSelectArmed = false;
	if ( m_iDragMode == EDITORDRAG_NONE )
		return;

	bool bView = m_iDragMode == EDITORDRAG_VIEW;
	if ( input()->GetMouseCapture() == GetVPanel() )
		input()->SetMouseCapture( (VPANEL)0 );

	m_iDragMode = EDITORDRAG_NONE;
	if ( bView )
		input()->SetCursorPos( m_iDragStart[0], m_iDragStart[1] );

	input()->SetCursorOveride( dc_user );
	GetLightingEditor()->SetSelectionCenterLocked( false );
}

void CVGUILightEditor::OnCursorMoved_Internal( int &x, int &y )
{
	float deltaX = (float)( x - m_iLastMouseX );
	float deltaY = (float)( y - m_iLastMouseY );
	bool bReset = false;
	CLightingEditor *pEditor = GetLightingEditor();

	if ( m_bDragSelectArmed )
	{
		m_bDragSelectArmed = false;
		StartDrag( EDITORDRAG_SELECT );
	}


	if ( m_iDragMode == EDITORDRAG_NONE )
	{
		pEditor->UpdateCurrentSelectedAxis( x, y );
	}
	else if ( m_iDragMode == EDITORDRAG_VIEW )
	{
		Vector origin;
		QAngle angles;
		pEditor->GetEditorView( origin, angles );
		NormalizeAngles( angles );
		const float flTurnSpeed = 0.5f;
		angles.x += deltaY * flTurnSpeed;
		angles.y -= deltaX * flTurnSpeed;
		angles.x = clamp( angles.x, -89.0f, 89.0f );
		angles.y = AngleNormalize( angles.y );
		pEditor->SetEditorView( origin, angles );
		bReset = true;
	}
	else if ( m_iDragMode == EDITORDRAG_TRANSLATE || m_iDragMode == EDITORDRAG_ROTATE )
	{
		Vector vecCurrent = GetDragWorldPos( x, y );
		if ( m_iDragMode == EDITORDRAG_TRANSLATE )
		{
			pEditor->MoveSelectedLights( vecCurrent - m_vecLastDragWorldPos );
		}
		else
		{
			Vector center = pEditor->GetSelectionCenter();
			QAngle orientation = pEditor->GetSelectionOrientation();
			int iAxis = pEditor->GetCurrentSelectedAxis() - CLightingEditor::EDITORAXIS_FIRST;
			if ( iAxis < 0 )
				iAxis = 0;
			if ( iAxis > 3 )
				iAxis = 3;

			Vector vDirections[6];
			AngleVectors( orientation, &vDirections[0], &vDirections[1], &vDirections[2] );
			Vector vecView = center - pEditor->GetViewOrigin();
			vecView.NormalizeInPlace();
			VectorVectors( vecView, vDirections[3], vDirections[4] );
			vDirections[5] = vecView;

			int iDirIndices[4][3] = {
				{ 2, 0, 1 },
				{ 0, 1, 2 },
				{ 1, 2, 0 },
				{ 3, 4, 5 },
			};

			Vector vecDelta[2] = {
				m_vecLastDragWorldPos - center,
				vecCurrent - center,
			};
			vecDelta[0].NormalizeInPlace();
			vecDelta[1].NormalizeInPlace();

			float flAngle0 = atan2(
				DotProduct( vDirections[ iDirIndices[iAxis][0] ], vecDelta[0] ),
				DotProduct( vDirections[ iDirIndices[iAxis][1] ], vecDelta[0] ) );
			float flAngle1 = atan2(
				DotProduct( vDirections[ iDirIndices[iAxis][0] ], vecDelta[1] ),
				DotProduct( vDirections[ iDirIndices[iAxis][1] ], vecDelta[1] ) );

			VMatrix matRotate;
			matRotate.Identity();
			MatrixBuildRotationAboutAxis( matRotate,
				vDirections[ iDirIndices[iAxis][2] ],
				RAD2DEG( flAngle1 - flAngle0 ) );
			pEditor->RotateSelectedLights( matRotate );
		}
		m_vecLastDragWorldPos = vecCurrent;
	}

	m_iLastMouseX = x;
	m_iLastMouseY = y;

	if ( bReset )
	{
		int cx = GetWide() / 2;
		int cy = GetTall() / 2;
		m_iLastMouseX = cx;
		m_iLastMouseY = cy;
		x = cx;
		y = cy;
		input()->SetCursorPos( cx, cy );
	}
}

bool CVGUILightEditor::ActivateEditorInteractionMode( int mode )
{
	const char *pszName = "action_select";
	switch ( mode )
	{
	case CLightingEditor::EDITORINTERACTION_ADD:
		pszName = "action_add";
		break;
	case CLightingEditor::EDITORINTERACTION_TRANSLATE:
		pszName = "action_translate";
		break;
	case CLightingEditor::EDITORINTERACTION_ROTATE:
		pszName = "action_rotate";
		break;
	default:
		mode = CLightingEditor::EDITORINTERACTION_SELECT;
		break;
	}

	m_pControls->SelectModeButton( pszName );
	GetLightingEditor()->SetEditorInteractionMode( (CLightingEditor::EDITORINTERACTION_MODE)mode );
	return true;
}

void CVGUILightEditor::OnKeyCodePressed_Internal( KeyCode code )
{
	Vector &move = GetLightingEditor()->EditorMoveDir();

	if ( m_iDragMode == EDITORDRAG_VIEW )
	{
		switch ( code )
		{
		case KEY_LEFT:
		case KEY_A:
			move.y = -1;
			return;
		case KEY_RIGHT:
		case KEY_D:
			move.y = 1;
			return;
		case KEY_UP:
		case KEY_W:
			move.x = 1;
			return;
		case KEY_DOWN:
		case KEY_S:
			move.x = -1;
			return;
		case KEY_LSHIFT:
			move.z = 1;
			return;
		default:
			break;
		}
	}

	if ( m_iDragMode != EDITORDRAG_NONE )
		return;

	if ( IsTextEntryFocused() )
		return;

	CLightingEditor *pEditor = GetLightingEditor();
	int curMode = pEditor->GetEditorInteractionMode();
	int nextMode = curMode;

	switch ( code )
	{
	case KEY_DELETE:
		pEditor->DeleteSelectedLights();
		NotifySelectionChanged();
		return;
	case KEY_X:
		nextMode = CLightingEditor::EDITORINTERACTION_SELECT;
		break;
	case KEY_C:
		nextMode = CLightingEditor::EDITORINTERACTION_ROTATE;
		break;
	case KEY_V:
		nextMode = CLightingEditor::EDITORINTERACTION_TRANSLATE;
		break;
	case KEY_B:
		nextMode = CLightingEditor::EDITORINTERACTION_ADD;
		break;
	default:
		return;
	}

	if ( nextMode != curMode && ActivateEditorInteractionMode( nextMode ) )
	{
		m_iLastInteractionMode = curMode;
		m_flLastInteractionTypeKeyPress = gpGlobals->curtime;
		m_bModeKeyHeld = true;
	}
}

void CVGUILightEditor::OnKeyCodeReleased_Internal( KeyCode code )
{
	Vector &move = GetLightingEditor()->EditorMoveDir();
	switch ( code )
	{
	case KEY_LEFT:
	case KEY_A:
		if ( move.y < 0 )
			move.y = 0;
		break;
	case KEY_RIGHT:
	case KEY_D:
		if ( move.y > 0 )
			move.y = 0;
		break;
	case KEY_UP:
	case KEY_W:
		if ( move.x > 0 )
			move.x = 0;
		break;
	case KEY_DOWN:
	case KEY_S:
		if ( move.x < 0 )
			move.x = 0;
		break;
	case KEY_LSHIFT:
		move.z = 0;
		break;
	default:
		break;
	}

	if ( m_bModeKeyHeld && ( code == KEY_X || code == KEY_C || code == KEY_V || code == KEY_B ) )
	{
		m_bModeKeyHeld = false;
		if ( ( gpGlobals->curtime - m_flLastInteractionTypeKeyPress ) > 0.2f )
			ActivateEditorInteractionMode( m_iLastInteractionMode );
	}
}

void CVGUILightEditor::Paint()
{
	BaseClass::Paint();
	if ( m_iDragMode != EDITORDRAG_SELECT )
		return;

	int dragMin[2], dragMax[2];
	GetDragBounds( dragMin, dragMax );

	surface()->DrawSetColor( Color( 0, 96, 196, 48 ) );
	surface()->DrawFilledRect( dragMin[0], dragMin[1], dragMax[0], dragMax[1] );
	surface()->DrawSetColor( Color( 0, 64, 128, 196 ) );
	surface()->DrawFilledRect( dragMin[0], dragMin[1], dragMax[0], dragMin[1] + 1 );
	surface()->DrawFilledRect( dragMin[0], dragMax[1] - 1, dragMax[0], dragMax[1] );
	surface()->DrawFilledRect( dragMin[0], dragMin[1] + 1, dragMin[0] + 1, dragMax[1] - 1 );
	surface()->DrawFilledRect( dragMax[0] - 1, dragMin[1] + 1, dragMax[0], dragMax[1] - 1 );
}

void CVGUILightEditor::OnThink()
{
	if ( !IsVisible() )
	{
		BaseClass::OnThink();
		return;
	}

	const bool bCapturing = input()->GetMouseCapture() == GetVPanel();
	VPANEL over = input()->GetMouseOver();
	const bool bWorld = bCapturing || IsWorldTarget( over );
	if ( !bWorld )
		GetLightingEditor()->AbortEditorMovement( false );

	const KeyCode keyMap[] = {
		KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
		KEY_A, KEY_W, KEY_S, KEY_D, KEY_LSHIFT,
		KEY_DELETE, KEY_X, KEY_C, KEY_V, KEY_B,
	};
	const int nFlyKeys = 9;
	bool keyCur[ ARRAYSIZE( keyMap ) ];
	Q_memset( keyCur, 0, sizeof( keyCur ) );
	for ( int i = 0; i < ARRAYSIZE( keyMap ); i++ )
	{
		if ( i < nFlyKeys && m_iDragMode != EDITORDRAG_VIEW )
			continue;
		keyCur[i] = input()->IsKeyDown( keyMap[i] );
	}
	for ( int i = 0; i < ARRAYSIZE( keyMap ); i++ )
	{
		if ( keyCur[i] == m_bKeyDown[i] )
			continue;
		if ( keyCur[i] )
			OnKeyCodePressed_Internal( keyMap[i] );
		else
			OnKeyCodeReleased_Internal( keyMap[i] );
	}
	COMPILE_TIME_ASSERT( ARRAYSIZE( keyMap ) <= ARRAYSIZE( m_bKeyDown ) );
	Q_memcpy( m_bKeyDown, keyCur, sizeof( keyCur ) );

	const MouseCode mouseMap[2] = { MOUSE_LEFT, MOUSE_RIGHT };
	bool mouseCur[2];
	for ( int i = 0; i < 2; i++ )
		mouseCur[i] = input()->IsMouseDown( mouseMap[i] );

	if ( bWorld || m_iDragMode != EDITORDRAG_NONE )
	{
		for ( int i = 0; i < 2; i++ )
		{
			if ( mouseCur[i] == m_bMouseDown[i] )
				continue;
			if ( mouseCur[i] )
				OnMousePressed_Internal( mouseMap[i] );
			else
				OnMouseReleased_Internal( mouseMap[i] );
		}

		int mx, my;
		input()->GetCursorPos( mx, my );
		if ( mx != m_iLastMouseX || my != m_iLastMouseY )
			OnCursorMoved_Internal( mx, my );
	}
	Q_memcpy( m_bMouseDown, mouseCur, sizeof( m_bMouseDown ) );

	BaseClass::OnThink();
}

CON_COMMAND( deferred_lighteditor_toggle, "Toggle the deferred light editor" )
{
	CVGUILightEditor::ToggleEditor();
}

class CLightEditorHotkey : public CAutoGameSystemPerFrame
{
public:
	CLightEditorHotkey() : CAutoGameSystemPerFrame( "LightEditorHotkey" )
	{
		m_bTabDown = false;
	}

	void LevelShutdownPostEntity()
	{
		CVGUILightEditor::DestroyEditor();
	}

	void Update( float ft )
	{
		(void)ft;
		if ( !engine->IsInGame() )
		{
			if ( CVGUILightEditor::IsEditorVisible() )
				CVGUILightEditor::ToggleEditor();
			m_bTabDown = input()->IsKeyDown( KEY_TAB );
			return;
		}

		bool bTab = input()->IsKeyDown( KEY_TAB );
		if ( bTab != m_bTabDown )
		{
			bool bToggle = !CVGUILightEditor::IsTextEntryFocused();
			VPANEL focus = input()->GetFocus();
			VPANEL editor = CVGUILightEditor::GetEditorPanel();
			if ( bToggle && focus && editor && focus != editor && !ipanel()->HasParent( focus, editor ) )
				bToggle = false;
			if ( bTab && bToggle )
				CVGUILightEditor::ToggleEditor();
			m_bTabDown = bTab;
		}
	}

private:
	bool m_bTabDown;
};

static CLightEditorHotkey g_LightEditorHotkey;
