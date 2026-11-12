//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/ui/terror_spawnmenus.h"
#include "l4d2/gamerules/terror_transition_support.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/hud/hud_chat.h"
#include "hud.h"
#include "hud_macros.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "filesystem.h"
#include "iviewrender.h"
#include "viewpostprocess.h"
#include "ienginevgui.h"
#include "cdll_client_int.h"
#include "vgui_int.h"
#include "GameUI/IGameUI.h"
#include "tier1/interface.h"
#include "tier1/fmtstr.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"
#include "vgui/IScheme.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/ProgressBar.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/IMaterialVar.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include <float.h>
#include "tier0/memdbgon.h"

static bool s_bZombieIntroVisible;
static bool s_bSuppressTransitionPostProcessing;
static float s_flLastSpinnerAdvance = -1.0f; // Original106EE010 bits BF800000.
static bool s_bHighlightClockStarted;
static float s_flLastHighlightAdvance;
static ConVar stats_highlight_interval( "stats_highlight_interval", "10", 0x0000000A, "Interval between hightlight screens in the transition stats panel" );
static ConVar versus_stats_highlight_interval( "versus_stats_highlight_interval", "5", 0x0000000A, "Interval between hightlight screens in the transition stats panel for versus mode" );
static CPersonalStatsSummaryPanel *s_pPersonalStatsSummary; // Native107CA548[slot0].
bool IsZombieIntroPanelVisible() { return s_bZombieIntroVisible; }
bool IsTransitionStatsPostProcessingSuppressed() { return s_bSuppressTransitionPostProcessing; }

// Retail ImagePanel retains its explicit frame (3FC050/3FC070); SDK's
// IImage exposes only SetFrame. Keep that state in this game-owned control.
class CTerrorLoadingImagePanel : public vgui::ImagePanel
{
	DECLARE_CLASS_SIMPLE( CTerrorLoadingImagePanel, vgui::ImagePanel );
public:
	CTerrorLoadingImagePanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_nFrame( 0 ) {}
	void AdvanceFrame() { m_nFrame = (int)( (unsigned int)m_nFrame + 1u ); SetFrame( m_nFrame ); }
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE
	{
		BaseClass::ApplySettings( settings );
		m_nFrame = settings->GetInt( "frame", 0 ); SetFrame( m_nFrame );
	}
private:
	int m_nFrame;
};

KeyValues *TerrorClient_GetTransitionChapterInfo( bool next, KeyValues **mission, const char *levelName )
{
	// Main-approved match-framework adaptation. The SAME mounted dictionary
	// owns current/next chapter lookup; no filename/campaign heuristic/parser.
	if ( mission ) *mission = NULL;
	char map[MAX_PATH];
	V_FileBase( levelName ? levelName : engine->GetLevelName(), map, sizeof( map ) );
	KeyValues *settings = new KeyValues( "settings" );
	KeyValues::AutoDelete deleteSettings( settings );
	settings->SetString( "Game/mode", g_TerrorModes.GetName() );
	KeyValues *owner = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfoByBspName( settings, map, &owner );
	if ( !chapter ) return NULL;
	if ( mission ) *mission = owner;
	if ( !next ) return chapter;
	settings->SetString( "Game/campaign", owner->GetName() );
	settings->SetInt( "Game/chapter", chapter->GetInt( "chapter" ) + 1 );
	return g_TerrorModes.GetChapterInfo( settings, NULL );
}

static void SetTransitionLoadingBackground( bool state )
{
	// Native26F2C0 obtains/releases this actual module and GameUI011 each call.
	CSysModule *module = Sys_LoadModule( "gameui" );
	if ( !module ) return;
	CreateInterfaceFn factory = Sys_GetFactory( module );
	IGameUI *gameUI = factory ? static_cast< IGameUI * >( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
	if ( gameUI )
	{
		CTransitionStatsPanel *panel = state ? GetTransitionStatsPanel() : NULL;
		gameUI->SetLoadingBackgroundDialog( panel ? panel->GetVPanel() : 0 );
	}
	Sys_UnloadModule( module );
}

CTerrorViewportPanel::CTerrorViewportPanel( IViewPort *viewport, const char *name ) :
	BaseClass( NULL, name, true, true ), m_pViewport( viewport ), m_bChatActive( false )
{
	SetTitle( "", true ); SetScheme( "ClientScheme" );
	SetMoveable( false ); SetSizeable( false ); SetMenuButtonVisible( false );
	SetProportional( true ); SetPos( 0, 0 );
	SetMouseInputEnabled( true ); MakePopup( true, false );
}
void CTerrorViewportPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetBorder( NULL ); SetPaintBackgroundEnabled( false ); SetTitleBarVisible( false );
}
void CTerrorViewportPanel::ShowPanel( bool state )
{
	if ( state == IsVisible() ) return;
	if ( state ) engine->CheckPoint( GetName() ); // Native engine308 CheckPoint.
	if ( HasInputElements() )
	{
		if ( state ) { Activate(); SetMouseInputEnabled( true ); return; }
		SetVisible( false );
	}
	else { SetVisible( state ); SetKeyBoardInputEnabled( false ); }
	SetMouseInputEnabled( false );
}
void CTerrorViewportPanel::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "close" ) ) Close(); else BaseClass::OnCommand( command );
}
void CTerrorViewportPanel::OnThink()
{
	CBaseHudChat *chat = GET_HUDELEMENT( CHudChat );
	if ( !chat || !IsVisible() || !HasInputElements() ) return;
	bool active = chat->GetMessageMode() != 0;
	if ( active == m_bChatActive ) return;
	m_bChatActive = active;
	SetMouseInputEnabled( !active ); SetKeyBoardInputEnabled( !active );
	if ( active ) chat->RequestFocus(); else RequestFocus();
}

class CTerrorLoadingProgressBar : public vgui::ContinuousProgressBar
{
	DECLARE_CLASS_SIMPLE( CTerrorLoadingProgressBar, vgui::ContinuousProgressBar );
public:
	CTerrorLoadingProgressBar( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
	{
		m_Background = vgui::surface()->CreateNewTextureID();
		m_Fill = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( m_Background, "vgui/loadingbar_bg", true, false );
		vgui::surface()->DrawSetTextureFile( m_Fill, "vgui/loadingbar", true, false );
	}
	virtual ~CTerrorLoadingProgressBar()
	{
		vgui::surface()->DestroyTextureID( m_Background );
		vgui::surface()->DestroyTextureID( m_Fill );
	}
	virtual void Paint() OVERRIDE
	{
		vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
		vgui::surface()->DrawSetTexture( m_Background );
		vgui::surface()->DrawTexturedRect( 0, 0, GetWide(), GetTall() );
		vgui::surface()->DrawSetTexture( m_Fill );
		const float fraction = clamp( GetProgress(), 0.0f, 1.0f );
		vgui::surface()->DrawTexturedSubRect( 0, 0, GetWide() * fraction, GetTall(), 0, 0, fraction, 1 );
	}
	virtual void PaintBackground() OVERRIDE {}
	virtual void PaintBorder() OVERRIDE {}
private:
	int m_Background, m_Fill;
};

class CTerrorLoadingScreenPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CTerrorLoadingScreenPanel, vgui::EditablePanel );
public:
	CTerrorLoadingScreenPanel() : BaseClass( NULL, "LoadingProgress" ), m_flLastSpinner( 0.0f )
	{
		SetProportional( true );
		m_pPoster = new vgui::ImagePanel( this, "Poster" );
		m_pTitle = new vgui::Label( this, "LocalizedCampaignName", "" );
		m_pTagline = new vgui::Label( this, "LocalizedCampaignTagline", "" );
		m_pLoadingText = new vgui::Label( this, "LoadingText", "#L4D360UI_Loading" );
		m_pSpinner = new CTerrorLoadingImagePanel( this, "WorkingAnim" );
		m_pProgress = new CTerrorLoadingProgressBar( this, "ProTotalProgress" );
		m_pMode = new vgui::Label( this, "GameModeLabel", "" );
		m_pStarring = new vgui::Label( this, "StarringLabel", "#L4D360UI_Loading_Starring" );
		m_pNames = new vgui::Label( this, "PlayerNames", "" );
		SetVisible( false ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false );
		vgui::ivgui()->AddTickSignal( GetVPanel(), 100 );
		m_bHasPoster = false;
	}
	virtual ~CTerrorLoadingScreenPanel() { vgui::ivgui()->RemoveTickSignal( GetVPanel() ); }
	virtual void OnTick() OVERRIDE
	{
		const float now = (float)Plat_FloatTime();
		if ( now - m_flLastSpinner < 0.1f ) return;
		m_flLastSpinner = now;
		m_pSpinner->AdvanceFrame();
	}
	void SetProgress( float fraction, const char *text )
	{
		if ( !m_bHasPoster ) PreparePoster();
		m_pProgress->SetProgress( fraction );
		if ( text && *text )
		{
			const wchar_t *localized = text[0] == '#' ? g_pVGuiLocalize->Find( text ) : NULL;
			if ( localized ) m_pLoadingText->SetText( localized );
			else m_pLoadingText->SetText( text );
		}
		Repaint();
	}
	void Prepare()
	{
		LoadControlSettings( "resource/ui/l4d360ui/loadingprogress.res" );
		PreparePoster();
		InvalidateLayout( true, true );
	}
	void PreparePoster()
	{
		KeyValues *mission = NULL;
		ConVarRef hostMap( "host_map" );
		TerrorClient_GetTransitionChapterInfo( false, &mission, hostMap.IsValid() ? hostMap.GetString() : NULL );
		KeyValues *poster = mission ? mission->FindKey( "poster" ) : NULL;
		m_bHasPoster = poster != NULL;
		int w, h; vgui::surface()->GetScreenSize( w, h );
		const bool widescreen = (float)w / (float)h > 1.5f;
		const char *image = poster ? poster->GetString( widescreen ? "posterImage_widescreen" : "posterImage", "LoadingScreen_Default" ) : "LoadingScreen_Default";
		m_pPoster->SetImage( image );
		m_pPoster->SetVisible( true );
		m_pTitle->SetText( poster ? poster->GetString( "posterTitle", "" ) : "" );
		m_pTagline->SetText( poster ? poster->GetString( "posterTagline", "" ) : "" );
		m_pMode->SetText( CFmtStr( "#L4D360UI_Loading_GameMode_%s", g_TerrorModes.GetName() ) );
		m_pMode->SetVisible( true ); m_pStarring->SetVisible( true ); m_pNames->SetVisible( true );
		char order[256], names[256] = "";
		Q_strncpy( order, poster ? poster->GetString( "character_order", "" ) : "", sizeof( order ) );
		for ( char *key = order; key && *key; )
		{
			char *next = Q_strstr( key, ";" );
			if ( next ) *next++ = 0;
			for ( int character = 0; character < 8; ++character )
			{
				if ( Q_stricmp( key, GetSurvivorCharacterName( character ) ) ) continue;
				if ( names[0] ) Q_strncat( names, ", ", sizeof( names ) );
				Q_strncat( names, GetSurvivorCharacterDisplayName( character ), sizeof( names ) );
				break;
			}
			key = next;
		}
		m_pNames->SetText( names );
		m_pSpinner->SetVisible( true );
		m_pProgress->SetProgress( 0.0f );
		InvalidateLayout( true, true );
	}
	virtual void PerformLayout() OVERRIDE
	{
		BaseClass::PerformLayout();
		int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
		m_pPoster->SetBounds( 0, 0, w, h );
		const int unit = vgui::scheme()->GetProportionalScaledValue( 20 );
		m_pSpinner->SetBounds( w - unit * 3, unit * 2, unit * 2, unit * 2 );
	}
	virtual void PaintBackground() OVERRIDE { vgui::surface()->DrawSetColor( 0, 0, 0, 255 ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() ); }
	vgui::ImagePanel *m_pPoster;
	vgui::Label *m_pTitle;
	vgui::Label *m_pTagline;
	vgui::Label *m_pLoadingText;
	vgui::Label *m_pMode, *m_pStarring, *m_pNames;
	CTerrorLoadingImagePanel *m_pSpinner;
	vgui::ContinuousProgressBar *m_pProgress;
	bool m_bHasPoster;
	float m_flLastSpinner;
};
static CTerrorLoadingScreenPanel *s_pTerrorLoadingScreen;
void SetTerrorLoadingScreenParent( vgui::Panel *parent )
{
	if ( !s_pTerrorLoadingScreen )
		s_pTerrorLoadingScreen = new CTerrorLoadingScreenPanel;
	if ( s_pTerrorLoadingScreen->GetParent() != parent )
		s_pTerrorLoadingScreen->SetParent( parent );
	s_pTerrorLoadingScreen->SetScheme( parent->GetScheme() );
}
void PrepareTerrorLoadingScreen()
{
	if ( !s_pTerrorLoadingScreen ) s_pTerrorLoadingScreen = new CTerrorLoadingScreenPanel;
	s_pTerrorLoadingScreen->Prepare();
}
void UpdateTerrorLoadingScreenProgress( float fraction, const char *statusText )
{
	if ( s_pTerrorLoadingScreen ) s_pTerrorLoadingScreen->SetProgress( fraction, statusText );
}
void SetTerrorLoadingScreenVisible( bool state )
{
	if ( state )
	{
		PrepareTerrorLoadingScreen();
		s_pTerrorLoadingScreen->SetVisible( true );
		s_pTerrorLoadingScreen->MoveToFront();
		return;
	}
	if ( s_pTerrorLoadingScreen )
	{
		s_pTerrorLoadingScreen->SetVisible( false );
		s_pTerrorLoadingScreen->MarkForDeletion();
		s_pTerrorLoadingScreen = NULL;
	}
}

CTransitionStatsPanel *GetTransitionStatsPanel()
{
	return gViewPortInterface ? dynamic_cast< CTransitionStatsPanel * >( gViewPortInterface->FindPanelByName( "transition_stats" ) ) : NULL;
}
bool IsTransitionStatsPanelVisible()
{
	CTransitionStatsPanel *panel = GetTransitionStatsPanel();
	return panel && panel->IsVisible();
}
void MoveTransitionStatsPanelToLoadingParent( vgui::Panel *parent )
{
	// Retail GameUI does this in ShowLoadingBackgroundDialog: while the
	// engine loads, only the GameUI panel tree is painted, so the stats
	// panel must live there or the default loading image replaces it.
	CTransitionStatsPanel *panel = GetTransitionStatsPanel();
	if ( panel && parent ) { panel->SetParent( parent->GetVPanel() ); panel->MoveToFront(); }
}
void HideTransitionStatsPanel()
{
	CTransitionStatsPanel *panel = GetTransitionStatsPanel();
	if ( panel ) { panel->RestoreLoadingParent(); panel->ShowPanel( false ); }
}
void CTransitionStatsPanel::RestoreLoadingParent()
{
	vgui::Panel *parent = m_PreviousParent.Get();
	if ( parent && GetParent() != parent ) SetParent( parent->GetVPanel() );
}

#if defined( L4D2 )
// The engine reloads all materials on every server spawn
// (IMaterialSystem::ReloadMaterials), which releases and recreates every
// render target, _rt_SmallFB0 included. Render targets have no persistence
// contract across that cycle, so the frozen backdrop is held in a
// procedural texture instead: its regenerator re-supplies the captured
// pixels whenever the texture is reconstructed.
class CTransitionFreezeRegenerator : public ITextureRegenerator
{
public:
	CTransitionFreezeRegenerator() : m_Width( 0 ), m_Height( 0 ) {}
	void SetPixels( int width, int height, const unsigned char *pixels )
	{
		m_Width = width; m_Height = height;
		m_Pixels.SetCount( width * height * 4 );
		memcpy( m_Pixels.Base(), pixels, width * height * 4 );
	}
	void ClearPixels() { m_Width = m_Height = 0; m_Pixels.RemoveAll(); }
	bool HasPixels() const { return m_Width > 0 && m_Pixels.Count() > 0; }
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect ) OVERRIDE
	{
		if ( !m_Pixels.Count() || !pVTFTexture ) return;
		unsigned char *dst = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !dst ) return;
		const int rowBytes = pVTFTexture->RowSizeInBytes( 0 );
		const int srcRow = m_Width * 4;
		for ( int y = 0; y < m_Height; ++y )
			memcpy( dst + y * rowBytes, m_Pixels.Base() + y * srcRow, MIN( rowBytes, srcRow ) );
	}
	virtual void Release() OVERRIDE {}
private:
	int m_Width, m_Height;
	CUtlVector< unsigned char > m_Pixels;
};
#endif
CTransitionStatsPanel::CTransitionStatsPanel( IViewPort *viewport ) :
	BaseClass( viewport, "transition_stats" ), m_flHideTime( 0.0f ), m_bFreezeFrame( false ),
#if defined( L4D2 )
	m_pFreezeRegenerator( NULL ),
#endif
	m_pWorkingAnim( NULL ), m_pLoadingText( NULL ),
	m_pVersusResults( NULL ), m_bUpdatedVersusResults( false ), m_bLoadingPanel( false ), m_iNormalStatsY( -1 )
{
	// 2EBCC0 leaves560/564 unwritten in an ordinary624-byte allocation.
	// Main's minimal-skip UB rule: initially skip freeze-dependent work and
	// the undefined deadline; ShowPanel's genuine stores govern thereafter.
	SetScheme( "ClientScheme" ); SetProportional( true );
	m_pHighlightPanel = new CSurvivorHighlightStatsPanel( this, "SurvivorHighlightStatsPanel" );
	m_pHighlightPanel->MoveToFront();
	m_pTipPanel = new CTipPanel( this );
	new CTerrorLoadingImagePanel( this, "WorkingAnim" );
#if defined( L4D2 )
	if ( materials )
	{
		ITexture *small = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
		if ( small && !small->IsError() ) EnsureFreezeTexture( small->GetActualWidth(), small->GetActualHeight() );
	}
#endif
	KeyValues *material = new KeyValues( "UnlitGeneric" );
	material->SetString( "$basetexture", "TransitionFreezeFrame" );
	material->SetInt( "$nocull", 1 ); material->SetInt( "$nofog", 1 ); material->SetInt( "$ignorez", 1 );
	material->SetInt( "$linearwrite", 1 ); material->SetInt( "$gammacolorread", 1 );
	m_FreezeFrame.Init( "FreezeFrame_FullScreen", material );
	m_FreezeFrame->Refresh();
	LoadControlSettings( "Resource/UI/TransitionStatsSurvivor.res" );
	vgui::ivgui()->AddTickSignal( GetVPanel() );
}
CTransitionStatsPanel::~CTransitionStatsPanel()
{
	vgui::ivgui()->RemoveTickSignal( GetVPanel() ); SetPostProcessingSuppressed( false );
#if defined( L4D2 )
	ShutdownFreezeTexture();
	delete m_pFreezeRegenerator; m_pFreezeRegenerator = NULL;
#endif
}
#if defined( L4D2 )
bool CTransitionStatsPanel::EnsureFreezeTexture( int w, int h )
{
	if ( !materials || w <= 0 || h <= 0 ) return false;
	if ( !m_pFreezeRegenerator ) m_pFreezeRegenerator = new CTransitionFreezeRegenerator;
	if ( m_FreezeTexture.IsValid() && ( m_FreezeTexture->GetActualWidth() != w || m_FreezeTexture->GetActualHeight() != h ) )
		ShutdownFreezeTexture();
	if ( !m_FreezeTexture.IsValid() )
	{
		ITexture *texture = materials->CreateProceduralTexture( "TransitionFreezeFrame", TEXTURE_GROUP_VGUI, w, h,
			IMAGE_FORMAT_BGRA8888, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD );
		if ( !texture ) return false;
		m_FreezeTexture.Init( texture );
		texture->SetTextureRegenerator( m_pFreezeRegenerator );
	}
	return true;
}
void CTransitionStatsPanel::ShutdownFreezeTexture()
{
	if ( !m_FreezeTexture.IsValid() ) return;
	m_FreezeTexture->SetTextureRegenerator( NULL );
	m_FreezeTexture.Shutdown( true );
}
void CTransitionStatsPanel::CaptureFreezeFrame()
{
	if ( !materials ) return;
	ITexture *small = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
	if ( !small || small->IsError() ) return;
	const int w = small->GetActualWidth(), h = small->GetActualHeight();
	if ( w <= 0 || h <= 0 ) return;
	if ( !EnsureFreezeTexture( w, h ) ) return;
	if ( m_pFreezeRegenerator->HasPixels() ) return;
	CUtlVector< unsigned char > pixels;
	pixels.SetCount( w * h * 4 );
	{
		CMatRenderContextPtr context( materials );
		context->PushRenderTargetAndViewport();
		context->SetRenderTarget( small );
		context->Viewport( 0, 0, w, h );
		context->ReadPixels( 0, 0, w, h, pixels.Base(), IMAGE_FORMAT_BGRA8888 );
		context->PopRenderTargetAndViewport();
	}
	m_pFreezeRegenerator->SetPixels( w, h, pixels.Base() );
	m_FreezeTexture->Download();
	IMaterialVar *var = m_FreezeFrame->FindVar( "$basetexture", NULL, false );
	if ( var ) { var->SetTextureValue( m_FreezeTexture ); m_FreezeFrame->RecomputeStateSnapshots(); }
}
void CTransitionStatsPanel::ReleaseFreezeFrame()
{
	// The texture itself lives until panel shutdown so the backdrop
	// material's $basetexture name always resolves to it; only the captured
	// pixels are dropped, and the var is pointed back at _rt_SmallFB0.
	if ( m_FreezeTexture.IsValid() )
	{
		IMaterialVar *var = m_FreezeFrame->FindVar( "$basetexture", NULL, false );
		ITexture *small = materials ? materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET ) : NULL;
		if ( var && small ) { var->SetTextureValue( small ); m_FreezeFrame->RecomputeStateSnapshots(); }
	}
	if ( m_pFreezeRegenerator ) m_pFreezeRegenerator->ClearPixels();
}
#endif
void CTransitionStatsPanel::SetPostProcessingSuppressed( bool state )
{
	s_bSuppressTransitionPostProcessing = state;
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->SetTransitionStatsPanelOpen( state );
}
void CTransitionStatsPanel::RefreshNextMap()
{
	if ( !g_PR ) return;
	KeyValues *mission = NULL;
	KeyValues *chapter = TerrorClient_GetTransitionChapterInfo( !TerrorGameRules()->IsVersusVoteRestarting(), &mission );
	wchar_t missionBuffer[128], chapterBuffer[128], loading[128];
	const wchar_t *missionText = NULL, *chapterText = NULL;
	if ( mission )
	{
		const char *text = mission->GetString( "DisplayTitle", "" );
		missionText = g_pVGuiLocalize->Find( text );
		if ( !missionText ) { g_pVGuiLocalize->ConvertANSIToUnicode( text, missionBuffer, sizeof( missionBuffer ) ); missionText = missionBuffer; }
	}
	if ( chapter )
	{
		const char *text = chapter->GetString( "DisplayName", "" );
		chapterText = g_pVGuiLocalize->Find( text );
		if ( !chapterText ) { g_pVGuiLocalize->ConvertANSIToUnicode( text, chapterBuffer, sizeof( chapterBuffer ) ); chapterText = chapterBuffer; }
	}
	if ( missionText && chapterText )
	{
		g_pVGuiLocalize->ConstructString( loading, sizeof( loading ), g_pVGuiLocalize->Find( "#L4D_s_loading" ), 2, missionText, chapterText );
		SetControlString( "NextMap", loading );
	}
	else SetControlString( "NextMap", missionText ? missionText : L"" );
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( player ) m_pTipPanel->SetTeam( player->GetTeamNumber() == TEAM_INFECTED );
}
void CTransitionStatsPanel::PrepareForShow()
{
	RefreshNextMap();
	CHudChat *chat = GET_HUDELEMENT( CHudChat );
	if ( chat ) chat->PrepareForStats();
}
void CTransitionStatsPanel::ShowPanel( bool state )
{
	if ( state )
	{
		if ( !m_bFreezeFrame && materials && view )
		{
			// C2706D0: retain a final full blur before the loading panel owns
			// _rt_SmallFB0. The last EnginePost pass may have cleared its RGB.
			Terror_RenderTransitionBlur( 1.0f, 0.0f );
			view->FreezeFrame( 0.0f );
		}
#if defined( L4D2 )
		CaptureFreezeFrame();
#endif
		if ( GetParent() ) m_PreviousParent = GetParent();
		if ( m_bFreezeFrame ) SetPostProcessingSuppressed( true );
		PrepareForShow(); m_pHighlightPanel->CapturePlayers(); m_pHighlightPanel->AdvanceCategory();
		m_flHideTime = gpGlobals->curtime + 10.0f; m_bUpdatedVersusResults = false;
		MakePopup( true, false ); SetTransitionLoadingBackground( true );
		m_bFreezeFrame = true; view->FreezeFrame( 0.1f );
		SetPostProcessingSuppressed( true ); m_bLoadingPanel = true;
		BaseClass::ShowPanel( state );
	}
	else if ( !engine->IsDrawingLoadingImage() )
	{
		SetPostProcessingSuppressed( false ); m_bFreezeFrame = false;
#if defined( L4D2 )
		ReleaseFreezeFrame();
#endif
		SetTransitionLoadingBackground( false ); m_bLoadingPanel = false;
		BaseClass::ShowPanel( state );
	}
}
void CTransitionStatsPanel::SetData( KeyValues *data )
{
	if ( data->GetInt( "dontHide", 0 ) ) m_flHideTime = gpGlobals->curtime + FLT_MAX;
}
void CTransitionStatsPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	LoadControlSettings( "Resource/UI/TransitionStatsSurvivor.res" ); RefreshNextMap();
	m_pVersusResults = dynamic_cast< CVersusModeResults * >( FindChildByName( "VersusModeResults" ) );
	SetPaintBackgroundEnabled( true );
}
void CTransitionStatsPanel::PerformLayout()
{
	BaseClass::PerformLayout();
	m_pWorkingAnim = dynamic_cast< vgui::ImagePanel * >( FindChildByName( "WorkingAnim" ) );
	m_pLoadingText = dynamic_cast< vgui::Label * >( FindChildByName( "LoadingText" ) );
	if ( m_pWorkingAnim ) m_pWorkingAnim->SetVisible( false );
	if ( m_pLoadingText ) m_pLoadingText->SetVisible( false );
	int x, y; m_pHighlightPanel->GetPos( x, y );
	if ( m_iNormalStatsY == -1 ) m_iNormalStatsY = y;
	m_pHighlightPanel->SetPos( x, g_TerrorModes.IsBaseMode( "versus" ) ? (int)m_flStatsYInVersus : m_iNormalStatsY );
	if ( m_pVersusResults )
	{
		const bool versus = g_TerrorModes.IsBaseMode( "versus" );
		if ( versus && !m_bUpdatedVersusResults ) { m_pVersusResults->UpdateResults(); m_bUpdatedVersusResults = true; }
		m_pVersusResults->SetVisible( versus );
	}
}
void CTransitionStatsPanel::OnThink()
{
	const bool loading = engine->IsDrawingLoadingImage();
	if ( m_pWorkingAnim ) m_pWorkingAnim->SetVisible( loading );
	if ( m_pLoadingText ) m_pLoadingText->SetVisible( loading );
	if ( m_flHideTime <= gpGlobals->curtime && !loading && ( m_bFreezeFrame || !engine->IsConnected() ) ) ShowPanel( false );
}
void CTransitionStatsPanel::PaintBackground()
{
	if ( !m_bFreezeFrame ) return;
	CMatRenderContextPtr context( materials );
	int wide, tall; vgui::surface()->GetScreenSize( wide, tall );
	context->DrawScreenSpaceRectangle( m_FreezeFrame, 0, 0, wide, tall, 0.0f, 0.0f, (float)( wide - 1 ), (float)( tall - 1 ), wide, tall );
}
#pragma float_control( precise, on, push )
bool CTransitionStatsPanel::AdvanceLoadingAnimation()
{
	if ( !m_pWorkingAnim || !m_bLoadingPanel ) return false;
	const double time = Plat_FloatTime();
	if ( !( time - (double)s_flLastSpinnerAdvance > 0.1 ) ) return false;
	s_flLastSpinnerAdvance = (float)time;
	static_cast< CTerrorLoadingImagePanel * >( m_pWorkingAnim )->AdvanceFrame();
	return true;
}
void CTransitionStatsPanel::AdvanceLoadingStats() { m_pHighlightPanel->AdvanceCategory(); m_pTipPanel->AdvanceTip(); }
void CTransitionStatsPanel::OnTick()
{
	// The target has no CHLClient::UpdateProgressBar callback (B2AD0).
	// Main-approved loading-progress adaptation: VGUI supplies opportunities,
	// with the native strict wall-clock spinner gate and highlight periods.
	// Synchronous engine loads may not tick VGUI: timing is NOT parity.
	if ( !engine->IsDrawingLoadingImage() ) return;
	AdvanceLoadingAnimation();
#if defined( L4D2 )
	// A materials reload releases every texture's hardware copy; reconstruct
	// the procedural backdrop through its regenerator before it is painted.
	if ( m_FreezeTexture.IsValid() && m_pFreezeRegenerator && m_pFreezeRegenerator->HasPixels() ) m_FreezeTexture->Download();
#endif
	if ( !s_bHighlightClockStarted ) { s_bHighlightClockStarted = true; s_flLastHighlightAdvance = (float)Plat_FloatTime(); }
	bool humanInfected = false;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && !player->IsBot() && player->GetTeamNumber() == TEAM_INFECTED ) { humanInfected = true; break; }
	}
	const double now = Plat_FloatTime();
	const float elapsed = (float)( now - (double)s_flLastHighlightAdvance );
	if ( ( humanInfected && elapsed >= versus_stats_highlight_interval.GetFloat() ) || elapsed >= stats_highlight_interval.GetFloat() )
	{
		if ( IsLoadingPanel() ) AdvanceLoadingStats();
		s_flLastHighlightAdvance = (float)now;
	}
}
#pragma float_control( pop )
void CTransitionStatsPanel::OnDisconnectedFromGame() { ShowPanel( false ); }
void CTransitionStatsPanel::OnLoadedIntoGame() { ShowPanel( false ); }
void CTransitionStatsPanel::OnHideAsLoadingPanel()
{
	RestoreLoadingParent();
}

class SimpleMouseoverButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( SimpleMouseoverButton, vgui::Button );
public:
	SimpleMouseoverButton( vgui::Panel *parent, const char *name, const char *text ) : BaseClass( parent, name, text ) {}
protected:
	virtual void OnCursorEntered() OVERRIDE
	{
		BaseClass::OnCursorEntered();
		if ( IsEnabled() ) PostActionSignal( new KeyValues( "Mouseover", "panelName", GetName() ) );
	}
	virtual void PaintBorder() OVERRIDE { SetPaintBorderEnabled( false ); SetPaintBackgroundEnabled( false ); BaseClass::PaintBorder(); }
};
CZombieIntroPanel::CZombieIntroPanel( IViewPort *viewport ) : BaseClass( viewport, "zombie_intro" ), m_flHintTimestamp( -1.0f ), m_flHintDuration( 0.0f )
{
	static const char *const names[5] = { "Overview", "Hunter", "Smoker", "Boomer", "Tank" };
	for ( int i = 0; i < 5; ++i ) { SimpleMouseoverButton *button = new SimpleMouseoverButton( this, CFmtStr( "%sButton", names[i] ), names[i] ); button->AddActionSignalTarget( this ); }
}
CZombieIntroPanel::~CZombieIntroPanel()
{
	s_bZombieIntroVisible = false;
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->RemovePanelOpenFlag( 2 );
}
bool CZombieIntroPanel::NeedsUpdate() { return C_BasePlayer::GetLocalPlayer() != NULL; }
void CZombieIntroPanel::ShowPanel( bool state )
{
	if ( state == IsVisible() ) return;
	SetVisible( state ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( false );
	if ( state ) { LoadControlSettings( "Resource/UI/ZombieIntroPanel.res" ); MakePopup( false, true ); }
	ClientModeTerrorNormal *mode = static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() );
	if ( state ) mode->AddPanelOpenFlag( 2 ); else mode->RemovePanelOpenFlag( 2 );
	s_bZombieIntroVisible = state;
}
void CZombieIntroPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/ZombieIntroPanel.res" ); BaseClass::ApplySchemeSettings( scheme );
	OnMouseover( "OverviewButton" ); m_flHintTimestamp = gpGlobals->curtime; m_flHintDuration = 0.0f;
}
void CZombieIntroPanel::OnMouseover( const char *panelName )
{
	int selected = 9;
	const char *description = NULL;
	if ( !Q_stricmp( panelName, "OverviewButton" ) ) description = "#L4D_pz_spawn_overview";
	else if ( !Q_stricmp( panelName, "HunterButton" ) ) { selected = 3; description = "#L4D_pz_spawn_hunter"; }
	else if ( !Q_stricmp( panelName, "SmokerButton" ) ) { selected = 1; description = "#L4D_pz_spawn_boomer"; }
	else if ( !Q_stricmp( panelName, "BoomerButton" ) ) { selected = 2; description = "#L4D_pz_spawn_smoker"; }
	else if ( !Q_stricmp( panelName, "TankButton" ) ) { selected = 8; description = "#L4D_pz_spawn_tank"; }
	// Native well-defined bug: Smoker/Boomer descriptions are transposed.
	if ( description ) SetControlString( "Description", description );
	static const char *const names[5] = { "Overview", "Hunter", "Smoker", "Boomer", "Tank" };
	static const int classes[5] = { 9, 3, 1, 2, 8 };
	for ( int i = 0; i < 5; ++i )
	{
		SetControlVisible( CFmtStr( "%sBackgroundActive", names[i] ), selected == classes[i] );
		SetControlVisible( CFmtStr( "%sBackgroundInactive", names[i] ), selected != classes[i] );
	}
}
#pragma float_control( precise, on, push )
void CZombieIntroPanel::Update()
{
	SetKeyBoardInputEnabled( false );
	if ( !( (double)gpGlobals->curtime >= (double)m_flHintTimestamp ) ) return;
	m_flHintTimestamp = (float)( (double)gpGlobals->curtime + 10.0 ); m_flHintDuration = 10.0f;
	static const char *const hints[31] = {
		"#L4D_Hint_pz_falling", "#L4D_Hint_pz_glows", "#L4D_Hint_pz_ledge_hanger", "#L4D_Hint_pz_bash", "#L4D_Hint_pz_break_doors",
		"#L4D_Hint_hunter_leap", "#L4D_Hint_hunter_leap_aim", "#L4D_Hint_hunter_push", "#L4D_Hint_hunter_wallkick", "#L4D_Hint_hunter_pounce",
		"#L4D_Hint_hunter_pounce_committed", "#L4D_Hint_hunter_pounce_defense", "#L4D_Hint_hunter_pounce_incaps", "#L4D_Hint_smoker_choke",
		"#L4D_Hint_smoker_choke_committed", "#L4D_Hint_smoker_death_cloud", "#L4D_Hint_smoker_poison", "#L4D_Hint_boomer_slow", "#L4D_Hint_boomer_pop",
		"#L4D_Hint_boomer_bomb", "#L4D_Hint_boomer_vomit", "#L4D_Hint_boomer_vomit_blind", "#L4D_Hint_boomer_it", "#L4D_Hint_boomer_recharge",
		"#L4D_Hint_boomer_vomit_no_move", "#L4D_Hint_tank_health", "#L4D_Hint_tank_self_control", "#L4D_Hint_tank_throw", "#L4D_Hint_tank_attack_incaps",
		"#L4D_Hint_tank_break_walls", "#L4D_Hint_tank_punch_cars" };
	SetControlString( "HintText", hints[RandomInt( 0, 30 )] );
}
#pragma float_control( pop )

CPersonalStatsSummaryPanel::CPersonalStatsSummaryPanel() : BaseClass( NULL, "PersonalStatsSummary", vgui::scheme()->LoadSchemeFromFile( "Resource/ClientScheme.res", "ClientScheme" ) )
{
	m_pLayout = new KeyValues( "Resource/UI/PersonalStatSummary.res" );
	m_pLayout->LoadFromFile( filesystem, "Resource/UI/PersonalStatSummary.res", NULL );
}
CPersonalStatsSummaryPanel::~CPersonalStatsSummaryPanel() { m_pLayout->deleteThis(); }
void CPersonalStatsSummaryPanel::SetBackground( const char *name )
{
	vgui::ImagePanel *background = dynamic_cast< vgui::ImagePanel * >( FindChildByName( "MainBackground" ) );
	if ( !background ) return;
	int wide, tall; vgui::surface()->GetScreenSize( wide, tall );
	background->SetImage( CFmtStr( (float)wide / (float)tall < 1.59 ? "../console/%s" : "../console/%s_widescreen", name ) );
}
void CPersonalStatsSummaryPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	SetProportional( true ); LoadControlSettings( "Resource/UI/PersonalStatSummary.res", NULL, m_pLayout );
	SetBackground( "background01" ); BaseClass::ApplySchemeSettings( scheme );
}
void CPersonalStatsSummaryPanel::OnThink() { if ( !engine->IsDrawingLoadingImage() && !engine->IsConnected() ) SetVisible( false ); }
void CPersonalStatsSummaryPanel::OnLoadedIntoGame() { SetVisible( false ); }
void CPersonalStatsSummaryPanel::OnDisconnectedFromGame() {} // Native nullsub638.
void CPersonalStatsSummaryPanel::OnShownByGameUI() {} // Native nullsub639.
void CPersonalStatsSummaryPanel::OnProgressLevelName( const char *levelName )
{
	if ( !levelName ) { SetBackground( "background01" ); return; } // 2D48D0: Main-approved skip of Q_strstr(NULL) after the native default write.
	if ( Q_strstr( levelName, "urban" ) && Q_strlen( levelName ) > 7 && levelName[7] == 'a' ) SetBackground( "RooftopRun" );
	else if ( Q_strstr( levelName, "urban" ) && Q_strlen( levelName ) > 7 && levelName[7] == 'b' ) SetBackground( "UrbanUnderground" );
	else if ( Q_strstr( levelName, "rural" ) && Q_strlen( levelName ) > 7 && levelName[7] == 'a' ) SetBackground( "TrainTracks" );
	else if ( Q_strstr( levelName, "rural" ) && Q_strlen( levelName ) > 7 && levelName[7] == 'b' ) SetBackground( "SmallTown" );
	else SetBackground( "background01" );
}
void ShutdownPersonalStatsSummary() { delete s_pPersonalStatsSummary; s_pPersonalStatsSummary = NULL; }
void ShowPersonalStatsSummary()
{
	if ( !s_pPersonalStatsSummary ) s_pPersonalStatsSummary = new CPersonalStatsSummaryPanel;
	s_pPersonalStatsSummary->SetParent( VGui_GetClientDLLRootPanel() );
	s_pPersonalStatsSummary->MakePopup( false, true ); s_pPersonalStatsSummary->SetVisible( true ); s_pPersonalStatsSummary->RequestFocus();
}
IViewPortPanel *CreateTerrorClientPanel( IViewPort *viewport, const char *name )
{
	if ( !Q_strcmp( name, "transition_stats" ) ) return new CTransitionStatsPanel( viewport );
	if ( !Q_strcmp( name, "zombie_intro" ) ) return new CZombieIntroPanel( viewport );
	return CreateTerrorSpawnPanel( viewport, name );
}
static void ShowStatsCommand( const CCommand & )
{
	gViewPortInterface->ShowPanel( "transition_stats", true );
	IViewPortPanel *panel = gViewPortInterface->FindPanelByName( "transition_stats" );
	if ( panel ) { panel->ShowPanel( true ); KeyValues *data = new KeyValues( "data", "dontHide", 1 ); panel->SetData( data ); data->deleteThis(); }
}
static void HideStatsCommand( const CCommand & ) { gViewPortInterface->ShowPanel( "transition_stats", false ); }
static void NextHighlightCommand( const CCommand & ) { CTransitionStatsPanel *panel = GetTransitionStatsPanel(); if ( panel ) panel->AdvanceLoadingStats(); }
static void PersonalStatsCommand( const CCommand & ) { ShowPersonalStatsSummary(); }
static ConCommand showstats( "+showstats", ShowStatsCommand, "", 0x0000000A );
static ConCommand hidestats( "-showstats", HideStatsCommand, "", 0x0000000A );
static ConCommand stats_nexthighlight( "stats_nexthighlight", NextHighlightCommand, 0, 0x00000008 );
static ConCommand personalstats( "personalstats", PersonalStatsCommand, "Shows the player stats dialog", 0x00000008 );
const wchar_t *TerrorFindLocalizedTextSafe( const char *token )
{
	// Original VGUI2 FindSafe10006D20, canonical3188733e5cb87a2c; exact
	// body/literal/DevMsg pinned in client_findsafe_original.txt. No new ABI.
	const wchar_t *text = g_pVGuiLocalize->Find( token );
	if ( text ) return text;
	DevMsg( "CLocalizedStringTable::FindSafe failed to localize: %s\n", token );
	return L"#FIXME_LOCALIZATION_FAIL_MISSING_STRING";
}
