//========= Copyright Valve Corporation, All rights reserved. ============//
// Native C21E610..C21FB08. Original missions and resources remain mounted.
#include "cbase.h"
#include "l4d2/ui/terror_create_server.h"
#include "l4d2/terror_native_qsort.h"
#include "l4d2/clientmode_terror.h"
#include "filesystem.h"
#include "ienginevgui.h"
#include "vgui/IScheme.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

static CreateServerPanel *s_pCreateServer;
ClientGameUIPanel::ClientGameUIPanel( const char *name ) : BaseClass( NULL, name, true, true ) {}
void ClientGameUIPanel::Show()
{
	SetVisible( true );
	SetEnabled( true );
	MoveToFront();
	InvalidateLayout( false, false );
}
CSelectionOverlayPanel::CSelectionOverlayPanel( vgui::Panel *parent, CreateServerPanel *owner, int index ) : BaseClass( parent, NULL ), m_pOwner( owner ), m_iMission( index )
{
	SetPaintBorderEnabled( false );
	SetPaintBackgroundEnabled( false );
}
void CSelectionOverlayPanel::OnMousePressed( vgui::MouseCode code )
{
	if ( GetParent()->IsEnabled() ) m_pOwner->SelectMission( m_iMission );
}
void CSelectionOverlayPanel::OnMouseDoublePressed( vgui::MouseCode code )
{
	OnMousePressed( code );
	if ( GetParent()->IsEnabled() ) PostMessage( m_pOwner, new KeyValues( "Command", "command", "play" ) );
}
CMissionPanel::CMissionPanel( CreateServerPanel *parent, int index, KeyValues *mission ) : BaseClass( parent, NULL ),
	m_TextColor( 0, 0, 0, 0 ), m_DisabledColor( 0, 0, 0, 0 ), m_SelectionColor( 0, 0, 0, 0 ), m_FillColor( 0, 0, 0, 0 )
{
	m_pBorder = new vgui::ImagePanel( this, "LevelPicBorder" );
	m_pBorder->MakeReadyForUse();
	m_pPicture = new vgui::ImagePanel( this, "LevelPic" );
	m_pPicture->MakeReadyForUse();
	m_pTitle = new vgui::Label( this, "ChapterNameLabel", mission->GetString( "Title", "" ) );
	SetPaintBackgroundEnabled( false );
	char image[MAX_PATH]; Q_snprintf( image, sizeof( image ), "missions/%s", mission->GetString( "Screenshot", "" ) );
	char *extension = Q_strstr( image, "." ); if ( extension ) *extension = 0;
	m_pPicture->SetImage( image );
	LoadControlSettings( "Resource/MissionPanel.res" );
	int x, y; m_pBorder->GetPos( x, y );
	SetSize( m_pBorder->GetWide(), y + m_pBorder->GetTall() );
	CSelectionOverlayPanel *overlay = new CSelectionOverlayPanel( this, parent, index );
	overlay->SetBounds( 0, 0, GetWide(), GetTall() );
	overlay->MoveToFront();
	// Native stores screenshot/map/"Coming Soon" metadata but no recovered
	// method reads those stores. In particular it does NOT disable the mission.
}
void CMissionPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	m_TextColor = scheme->GetColor( "NewGame.TextColor", Color( 255, 255, 255, 255 ) );
	m_FillColor = scheme->GetColor( "NewGame.FillColor", Color( 255, 255, 255, 255 ) );
	m_DisabledColor = scheme->GetColor( "NewGame.DisabledColor", Color( 255, 255, 255, 255 ) );
	m_SelectionColor = scheme->GetColor( "NewGame.SelectionColor", Color( 255, 255, 255, 255 ) );
	BaseClass::ApplySchemeSettings( scheme );
}
void CMissionPanel::SetSelectionAppearance( bool selected )
{
	if ( IsEnabled() )
	{
		m_pTitle->SetFgColor( selected ? m_SelectionColor : m_TextColor );
		m_pBorder->SetFillColor( selected ? m_SelectionColor : m_FillColor );
		SetAlpha( 255 );
	}
	else
	{
		m_pTitle->SetFgColor( Color( 0, 0, 0, 0 ) );
		m_pBorder->SetFillColor( m_DisabledColor );
		SetAlpha( 128 );
	}
}
static int CompareMissions( const void *left, const void *right )
{
	KeyValues *a = *static_cast< KeyValues *const * >( left ), *b = *static_cast< KeyValues *const * >( right );
	const int first = a->GetInt( "SortPriority", 9999 ), second = b->GetInt( "SortPriority", 9999 );
	if ( first != second ) return first < second; // Actual broken C21E8E0, 0/1.
	return Q_strcmp( a->GetString( "title", "" ), b->GetString( "title", "" ) );
}
CreateServerPanel::CreateServerPanel() : BaseClass( "CreateServer" ), m_iPage( 0 ), m_iSelection( -1 )
{
	SetDeleteSelfOnClose( true );
	SetBounds( 0, 0, 372, 160 );
	SetSizeable( false );
	SetTitle( "#GameUI_NewGame", true );
	m_pNext = new vgui::Button( this, "Next", "#gameui_next" );
	m_pPrev = new vgui::Button( this, "Prev", "#gameui_prev" );
	// Original Next/Prev commands come from CreateServer.res, not constructor.
	m_pPlay = new vgui::Button( this, "Play", "#GameUI_Play" ); m_pPlay->SetCommand( "Play" );
	vgui::Button *cancel = new vgui::Button( this, "Cancel", "#GameUI_Cancel" ); cancel->SetCommand( "Close" );
	CUtlVector< KeyValues * > missions;
	FileFindHandle_t find;
	for ( const char *filename = filesystem->FindFirst( "missions/*.txt", &find ); filename; filename = filesystem->FindNext( find ) )
	{
		char path[128]; Q_snprintf( path, sizeof( path ), "missions/%s", filename );
		KeyValues *mission = new KeyValues( "mission" );
		if ( mission->LoadFromFile( filesystem, path, "MOD" ) ) { mission->SetString( "filename", path ); missions.AddToTail( mission ); }
		else mission->deleteThis();
	}
	filesystem->FindClose( find );
	TerrorNativeQSort( missions.Base(), missions.Count(), sizeof( KeyValues * ), CompareMissions );
	FOR_EACH_VEC( missions, i )
	{
		CMissionPanel *panel = new CMissionPanel( this, i, missions[i] );
		panel->MakeReadyForUse(); panel->SetVisible( false ); m_Missions.AddToTail( panel );
	}
	FOR_EACH_VEC( missions, i ) missions[i]->deleteThis();
	LoadControlSettings( "resource/CreateServer.res" );
	InvalidateLayout( false, false );
	SetPage( 0 ); SelectMission( 0 );
}
CreateServerPanel::~CreateServerPanel() { s_pCreateServer = NULL; }
void CreateServerPanel::SetPage( int page )
{
	m_iPage = page;
	FOR_EACH_VEC( m_Missions, i )
	{
		CMissionPanel *mission = m_Missions[i];
		if ( i < 3 * page || i >= 3 * page + 3 ) mission->SetVisible( false );
		else
		{
			mission->SetPos( i % 3 * ( mission->GetWide() + 16 ) + 24, 40 );
			mission->SetVisible( true ); mission->SetSelectionAppearance( false );
		}
	}
	m_pPrev->SetVisible( page != 0 ); m_pPrev->SetEnabled( page != 0 );
	bool next = 3 * page + 3 < m_Missions.Count();
	m_pNext->SetVisible( next ); m_pNext->SetEnabled( next && ( page != 0 || m_Missions.Count() >= 4 ) );
	m_pPlay->SetEnabled( false );
}
void CreateServerPanel::SelectMission( int index )
{
	m_iSelection = index;
	FOR_EACH_VEC( m_Missions, i ) m_Missions[i]->SetSelectionAppearance( i == index );
	m_pPlay->SetEnabled( true ); // Native also enables with an out-of-range selection.
}
void CreateServerPanel::Play()
{
	if ( m_iSelection < 0 || m_iSelection >= m_Missions.Count() ) return;
	cvar->FindVar( "sv_cheats" ); // Original does NOT revert cheat vars here.
	DevMsg( "FCVAR_CHEAT cvars reverted to defaults.\n" );
	// C21F470 passes its never-written stack char[1024] to ClientCmd. The
	// standing undefined-read rule omits only that command, NOT the close or
	// diagnostic. A map command here would repair an original native bug.
	Close();
}
void CreateServerPanel::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Play" ) ) Play();
	else if ( !Q_stricmp( command, "Next" ) ) SetPage( m_iPage + 1 );
	else if ( !Q_stricmp( command, "Prev" ) ) SetPage( m_iPage - 1 );
	else BaseClass::OnCommand( command );
}
static void OpenCreateServer()
{
	if ( !s_pCreateServer ) s_pCreateServer = new CreateServerPanel;
	RegisterClientGameUIPanel( s_pCreateServer ); // Actual C21E6B0 -> C21E860.
	s_pCreateServer->SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
	s_pCreateServer->Show();
}
static ConCommand cl_create_server( "cl_create_server", OpenCreateServer, "", FCVAR_CLIENTDLL );
