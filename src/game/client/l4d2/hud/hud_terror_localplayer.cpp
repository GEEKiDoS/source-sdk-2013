//========= Copyright Valve Corporation, All rights reserved. ============//
// Replace existing healthy-only owner. Native local/team/row/voice whole bodies.
#include "cbase.h"
#include "tier1/fmtstr.h"
#include "hud.h"
#include "iclientmode.h"
#include "l4d2/hud/hud_terror_localplayer.h"
#include "l4d2/hud/terror_health_panel.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/player/c_transitioning_player.h"
#include "l4d2/effects/terror_glow_player.h"
#include "l4d2/effects/terror_glow_entities.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "game_controls/IconPanel.h"
#include "game_controls/baseviewport.h"
#include "voice_status.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui/IVGui.h"
#include "vgui/IPanel.h"
#include "vgui/IScheme.h"
#include "in_buttons.h"
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/Label.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static ConVar teamwork_event_display_time( "teamwork_event_display_time", "8.0", 0x0000000A );
static ConVar teamwork_event_min_interval( "teamwork_event_min_interval", "3.0", 0x0000000A );
static CUtlVectorFixed< TeamDisplayPlayer *, 5 > s_terrorDisplayRows;
static unsigned int s_terrorDisplayRowCursor;

class CTeammateVoicePanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CTeammateVoicePanel, vgui::Panel );
public:
	CTeammateVoicePanel( vgui::Panel *parent ) : BaseClass( parent, "Voice" ), m_pIcon( NULL ) {}
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE
	{
		m_pIcon = gHUD.GetIcon( settings->GetString( "voice_icon", "" ) );
		BaseClass::ApplySettings( settings );
	}
	virtual void Paint() OVERRIDE
	{
		if ( m_pIcon ) m_pIcon->DrawSelf( 0, 0, GetWide(), GetTall(), Color(255,255,255,255) );
	}
private:
	CHudTexture *m_pIcon;
};

vgui::Panel *CreateTerrorTeammateVoicePanel( vgui::Panel *parent )
{
	return new CTeammateVoicePanel( parent );
}

class TeamDisplayPlayer : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( TeamDisplayPlayer, vgui::EditablePanel );
	friend class CHudTeamDisplay;
public:
	TeamDisplayPlayer( vgui::Panel *parent, const char *name, bool local ) : CHudElement( name ), BaseClass( parent, name ), m_bLocal( local )
	{
		m_nPlayer = 0; m_nTeam = 0; m_nBackground = 0; m_nCharacter = 0;
		m_iHealth = m_iTemporary = m_iMaximum = 0; m_bIncapacitated = m_bAlive = m_bDying = false;
		m_flHealthChangeTime = -1.0f;
		m_nSlots[0] = m_nSlots[1] = m_nSlots[2] = 0; m_name[0] = 0;
		m_pHealth = NULL; m_pHead = m_pDead = m_pBackground = NULL; m_pName = m_pHealthIcon = m_pHealthNumber = NULL;
		m_pVoice = NULL; m_pSlots[0] = m_pSlots[1] = m_pSlots[2] = NULL;
		m_hNameFont = vgui::INVALID_FONT; m_rewardTimer.Invalidate();
		m_bLayoutIncap = false; m_bDirty = true;
		Q_memset( &m_previous, 0, sizeof( m_previous ) );
		SetPaintBackgroundEnabled( false );
		s_terrorDisplayRows.AddToTail( this );
	}
	virtual ~TeamDisplayPlayer()
	{
		s_terrorDisplayRows.FindAndRemove( this );
		KeyValues *message = new KeyValues( "PlayerPanelDeleted" );
		message->SetPtr( "panel", static_cast<vgui::Panel *>( this ) );
		PostMessage( GetParent(), message );
	}
	int GetPlayerIndex() const { return m_nPlayer; }
	int GetTeam() const { return m_nTeam; }
	int GetCharacter() const { return m_nCharacter; }
	bool IsLocal() const { return m_bLocal; }
	void SetPlayer( int team, int index )
	{
		if ( m_nPlayer != index || m_nTeam != team ) Reset();
		m_nPlayer = index; m_nTeam = team;
	}
	virtual void Reset() OVERRIDE { m_rewardTimer.Invalidate(); m_bDirty = true; }
	void SetBackground( int background )
	{
		m_nBackground = ( background < 0 ? 0 : background ) % 4;
		if ( !m_pBackground ) return;
		if ( m_bIncapacitated ) m_pBackground->SetImage( GetTerrorSurvivorPortrait( m_nCharacter, true ) );
		else if ( !m_bLocal )
		{
			static const char *const names[4] = { "hud/healthbar_bg_1", "hud/healthbar_bg_2", "hud/healthbar_bg_3", "hud/healthbar_bg_1" };
			m_pBackground->SetImage( names[m_nBackground] );
		}
	}
	void AwardEarned( int award, C_BaseEntity *subject )
	{
		switch ( award ) { case 66: case 67: case 68: case 69: case 70: case 75: case 76: case 80: break; default: return; }
		const double now = (double)gpGlobals->curtime;
		if ( m_rewardTimer.m_timestamp <= 0.0f ||
			(double)teamwork_event_min_interval.GetFloat() <= now - (double)m_rewardTimer.m_timestamp + (double)m_rewardTimer.m_duration )
		{
			m_rewardTimer.Start( teamwork_event_display_time.GetFloat() );
			C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
			if ( local ) local->EmitSound( m_bLocal ? "Hint.BigReward" : "Hint.LittleReward" );
		}
	}
protected:
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE
	{
		if ( !Q_stricmp( name, "TeamDisplayVoicePanel" ) ) return new CTeammateVoicePanel( this );
		return BaseClass::CreateControlByName( name );
	}
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( LayoutName(), "GAME" );
		BaseClass::ApplySchemeSettings( scheme );
		m_bLayoutIncap = m_bIncapacitated; m_bDirty = true;
		m_pBackground = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "BackgroundImage" ) );
		m_pHead = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "Head" ) );
		m_pDead = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "Dead" ) );
		m_pVoice = dynamic_cast<CTeammateVoicePanel *>( FindChildByName( "Voice" ) );
		m_pHealth = dynamic_cast<HealthPanel *>( FindChildByName( "Health" ) );
		m_pHealthIcon = dynamic_cast<vgui::Label *>( FindChildByName( "HealthIcon" ) );
		m_pHealthNumber = dynamic_cast<vgui::Label *>( FindChildByName( "HealthNumber" ) );
		m_pName = dynamic_cast<vgui::Label *>( FindChildByName( "Name" ) );
		if ( m_pName ) { m_pName->SetWrap( false ); m_hNameFont = m_pName->GetFont(); }
		m_aliveNameColor = scheme->GetColor( "FgColor", Color(255,0,0,255) );
		m_deadNameColor = scheme->GetColor( "MediumGray", Color(0,255,0,255) );
		m_pSlots[0] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_Pills" ) );
		m_pSlots[1] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_First_Aid" ) );
		m_pSlots[2] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_Grenade" ) );
		if ( m_pHealthNumber ) vgui::surface()->PrecacheFontCharacters( m_pHealthNumber->GetFont(), L"0123456789" );
		if ( m_pName ) vgui::surface()->PrecacheFontCharacters( m_pName->GetFont(), L"" );
		vgui::surface()->PrecacheFontCharacters( m_hPlayerNameFontSmall, L"" );
		static const char *const preload[17] = { "s_panel_dead", "s_panel_coach", "s_panel_mechanic", "s_panel_gambler", "s_panel_producer",
			"s_panel_gambler_incap", "s_panel_coach_incap", "s_panel_mechanic_incap", "s_panel_producer_incap", "s_panel_namvet_incap", "s_panel_biker_incap", "s_panel_manager_incap", "s_panel_teenangst_incap", "s_panel_namvet", "s_panel_biker", "s_panel_manager", "s_panel_teenangst" };
		for ( int i = 0; i < ARRAYSIZE( preload ); ++i ) vgui::scheme()->GetImage( preload[i], true );
		SetBackground( m_nBackground ); SetPaintBackgroundEnabled( false );
	}
public:
	void UpdateRow()
	{
		if ( m_nPlayer <= 0 || ( m_nTeam != TEAM_SURVIVOR && m_nTeam != TEAM_INFECTED ) ) { SetVisible( false ); return; }
		if ( !IsVisible() ) SetVisible( true );
		UpdatePlayer();
	}
private:
	const char *LayoutName() const
	{
		return m_bLocal ? ( m_bIncapacitated ? "resource/UI/HUD/LocalPlayerPanel_Incap.res" : "resource/UI/HUD/LocalPlayerPanel.res" ) :
			( m_bIncapacitated ? "resource/UI/HUD/TeammatePanel_Incap.res" : "resource/UI/HUD/TeammatePanel.res" );
	}
	static void SanitizeName( const char *source, wchar_t *destination, int bytes )
	{
		// C0B4780: only leading '#', every '%', and escaped '&' are changed.
		char safe[2 * MAX_PLAYER_NAME_LENGTH + 1]; int count = 0;
		for ( const char *p = source; *p && count < ARRAYSIZE( safe ) - 1; ++p )
		{
			if ( ( p == source && *p == '#' ) || *p == '%' ) safe[count++] = '*';
			else if ( *p == '&' ) { if ( count + 2 < ARRAYSIZE( safe ) ) { safe[count++] = '&'; safe[count++] = '&'; } }
			else safe[count++] = *p;
		}
		safe[count] = 0; g_pVGuiLocalize->ConvertANSIToUnicode( safe, destination, bytes );
	}
	void UpdateHealth( int health, int temporary )
	{
		if ( m_iHealth != health ) m_flHealthChangeTime = gpGlobals->realtime;
		if ( health <= 1 && ( temporary + 1 < m_iTemporary || temporary > m_iTemporary ) ) m_flHealthChangeTime = gpGlobals->realtime;
		m_iHealth = health; m_iTemporary = temporary;
	}
	void UpdatePlayer();
	void UpdateControls( C_TerrorPlayer *player );
	int m_nPlayer, m_nTeam, m_nBackground, m_nCharacter;
	bool m_bLocal, m_bIncapacitated, m_bAlive, m_bDying, m_bLayoutIncap, m_bDirty;
	int m_iHealth, m_iTemporary, m_iMaximum, m_nSlots[3];
	float m_flHealthChangeTime;
	wchar_t m_name[32];
	CountdownTimer m_rewardTimer;
	HealthPanel *m_pHealth;
	vgui::ImagePanel *m_pHead, *m_pDead, *m_pBackground;
	CTeammateVoicePanel *m_pVoice;
	vgui::Label *m_pName, *m_pHealthIcon, *m_pHealthNumber;
	CIconPanel *m_pSlots[3];
	vgui::HFont m_hNameFont;
	Color m_aliveNameColor, m_deadNameColor;
	struct Previous_t { bool alive, incap, ducking; int totalHealth, slots[3]; wchar_t name[32]; bool idle; } m_previous;
	CPanelAnimationVar( vgui::HFont, m_hPlayerNameFontSmall, "player_name_font_small", "PlayerDisplayNameSmall" );
};

void TeamDisplayPlayer::UpdatePlayer()
{
	static ConVarRef menu( "gameui_ingame_menu_active" );
	if ( menu.GetInt() != 0 || m_nTeam == TEAM_INFECTED ) { SetVisible( false ); return; }
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR );
	if ( !resource ) return;
	if ( m_nPlayer > MAX_PLAYERS || !resource->IsConnected( m_nPlayer ) )
	{
		if ( m_nTeam == TEAM_SURVIVOR )
		{
			const CUtlVector<C_TransitioningPlayer *> &loading = GetTransitioningPlayers();
			for ( int i = 0; i < loading.Count(); ++i )
			{
				C_TransitioningPlayer *transition = loading[i];
				if ( transition->entindex() != m_nPlayer ) continue;
				if ( transition->IsDormant() ) { m_nPlayer = 0; SetVisible( false ); return; }
				SanitizeName( transition->GetPlayerName(), m_name, sizeof( m_name ) );
				SetControlString( "Status", "#L4D_Scoreboard_s_Loading" );
				UpdateHealth( transition->GetHealth(), 64 );
				m_bIncapacitated = transition->IsIncapacitated(); m_nCharacter = transition->GetCharacter();
				m_bAlive = transition->IsTransitionAlive(); m_iMaximum = 100; m_bDying = transition->IsTransitionDying();
				UpdateControls( NULL ); return;
			}
		}
		m_nPlayer = 0; SetVisible( false ); return;
	}
	if ( resource->GetTeam( m_nPlayer ) != m_nTeam ) { m_nPlayer = 0; SetVisible( false ); return; }
	C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( m_nPlayer ) );
	int temporary = 0;
	if ( player && !player->IsDormant() )
	{
		temporary = (int)player->GetHealthBuffer(); m_bDying = player->IsGoingToDie();
		if ( m_nTeam == TEAM_SURVIVOR ) m_nCharacter = player->GetSurvivorCharacter();
	}
	else m_bDying = false;
	SanitizeName( resource->GetPlayerName( m_nPlayer ), m_name, sizeof( m_name ) );
	m_nSlots[0] = resource->GetPillsSlot( m_nPlayer ); m_nSlots[1] = resource->GetFirstAidSlot( m_nPlayer ); m_nSlots[2] = resource->GetGrenade( m_nPlayer );
	const bool rescued = player && player->m_lifeState != LIFE_ALIVE && TerrorGlow_FindRescueModel( player );
	if ( !rescued )
	{
		m_bAlive = resource->IsAlive( m_nPlayer ) && !resource->IsGhost( m_nPlayer );
		m_bIncapacitated = resource->IsIncapacitated( m_nPlayer ); m_iMaximum = resource->GetMaxHealth( m_nPlayer );
		UpdateHealth( resource->GetHealth( m_nPlayer ), temporary );
	}
	else
	{
		m_bIncapacitated = m_bAlive = false; m_iMaximum = 100; m_iHealth = 50; m_iTemporary = 0;
	}
	UpdateControls( player );
}
void TeamDisplayPlayer::UpdateControls( C_TerrorPlayer *player )
{
	int voicePlayer = m_nPlayer;
	C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( player );
	if ( !m_bLocal && bot && bot->GetHumanSpectatorUserID() > 0 ) voicePlayer = bot->GetHumanSpectatorEntIndex();
	CVoiceStatus *voice = GetClientVoiceMgr();
	C_BasePlayer *speaker = UTIL_PlayerByIndex( voicePlayer );
	const bool localSpeaker = m_bLocal || ( speaker && speaker->IsLocalPlayer() );
	if ( m_pVoice ) m_pVoice->SetVisible( voice && ( localSpeaker ? voice->IsLocalPlayerSpeaking() : voicePlayer > 0 && voice->IsPlayerSpeaking( voicePlayer ) ) );
	if ( m_pHead )
	{
		m_pHead->SetVisible( !m_bIncapacitated );
		if ( !m_bIncapacitated ) m_pHead->SetImage( GetTerrorSurvivorPortrait( m_nCharacter ) );
		m_pHead->SetDrawColor( m_bAlive ? Color(255,255,255,255) : Color(255,0,0,200) );
	}
	if ( m_pDead ) m_pDead->SetVisible( !m_bAlive );
	if ( m_pHealth )
	{
		m_pHealth->SetVisible( m_bAlive );
		m_pHealth->SetAlpha( 255 );
		m_pHealth->SetHealth( m_iHealth, m_iTemporary, m_iMaximum, m_bDying, m_bIncapacitated, m_bLocal );
		const Color color = m_pHealth->GetHealthColor();
		if ( m_pHealthIcon ) { m_pHealthIcon->SetFgColor( color ); m_pHealthIcon->SetVisible( m_bAlive ); }
		if ( m_pHealthNumber ) { m_pHealthNumber->SetFgColor( color ); m_pHealthNumber->SetVisible( m_bAlive ); }
	}
	if ( m_bLayoutIncap != m_bIncapacitated ) InvalidateLayout( true, true );
	if ( m_bDirty || m_previous.alive != m_bAlive ) SetControlVisible( "HealthIcon", m_bAlive );
	if ( m_pName )
	{
		m_pName->SetVisible( true );
		if ( m_bDirty || V_wcscmp( m_name, m_previous.name ) )
		{
			m_pName->SetFont( m_hNameFont );
			const wchar_t *display = m_name;
			if ( bot && bot->GetHumanSpectatorUserID() <= 0 )
			{
				const wchar_t *localized = g_pVGuiLocalize->Find( GetTerrorSurvivorDisplayNameToken( bot->GetSurvivorCharacter() ) );
				if ( localized ) display = localized;
			}
			m_pName->SetText( display );
			int wide, tall; m_pName->GetContentSize( wide, tall );
			if ( wide > m_pName->GetWide() ) m_pName->SetFont( m_hPlayerNameFontSmall );
		}
		m_pName->SetFgColor( m_bAlive ? m_aliveNameColor : m_deadNameColor );
	}
	// C2C9A70 caches an incap-suppressed flag, but the actual visibility write
	// ignores incap and uses alive/alpha only. Preserve that native distinction.
	if ( m_bDirty || ( m_previous.alive && !m_previous.incap ) != ( m_bAlive && !m_bIncapacitated ) )
		SetControlVisible( "HealthNumber", m_bAlive );
	const int total = m_iHealth + m_iTemporary;
	if ( m_bDirty || m_previous.totalHealth != total ) SetDialogVariable( "HealthNumber", total );
	for ( int i = 0; i < 3; ++i )
	{
		if ( !m_pSlots[i] || ( !m_bDirty && m_previous.slots[i] == m_nSlots[i] ) ) continue;
		m_pSlots[i]->SetVisible( m_nSlots[i] != 0 );
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( m_nSlots[i] );
		m_pSlots[i]->SetIcon( info ? info->iconSmall : NULL );
	}
	const bool idle = bot && bot->GetHumanSpectatorUserID() > 0;
	if ( m_bDirty || m_previous.idle != idle ) SetControlString( "Status", idle ? "#L4D_Scoreboard_Idle" : "" );
	const bool ducking = m_bLocal && player && ( player->m_Local.m_bDucking || player->m_Local.m_bDucked ) &&
		( player->GetFlags() & FL_ONGROUND ) && !player->IsBeingPounced() && ( player->m_nButtons & IN_DUCK );
	if ( m_bDirty || m_previous.ducking != ducking ) SetControlVisible( "DuckingIcon", ducking );
	m_previous.alive = m_bAlive; m_previous.incap = m_bIncapacitated; m_previous.ducking = ducking;
	m_previous.totalHealth = total; m_previous.idle = idle;
	for ( int i = 0; i < 3; ++i ) m_previous.slots[i] = m_nSlots[i];
	V_wcsncpy( m_previous.name, m_name, sizeof( m_previous.name ) ); m_bDirty = false;
}

DECLARE_HUDELEMENT( CHudLocalPlayerDisplay );
DECLARE_HUDELEMENT( CHudTeamDisplay );
CHudLocalPlayerDisplay::CHudLocalPlayerDisplay( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), name )
{
	m_pLocalPlayer = new TeamDisplayPlayer( this, "LocalPlayer", true );
	SetHiddenBits( HIDEHUD_MISCSTATUS ); ListenForGameEvent( "award_earned" ); SetPaintBackgroundEnabled( false );
}
void CHudLocalPlayerDisplay::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme ); LoadControlSettings( "Resource/UI/HUD/LocalPlayerDisplay.res", "GAME" );
	SetMouseInputEnabled( false ); SetPaintBackgroundEnabled( false );
}
bool CHudLocalPlayerDisplay::ShouldDraw()
{
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	C_TerrorPlayer *view = CinematicViewPlayer();
	if ( !local || !view || !CHudElement::ShouldDraw() ) return false;
	int team = local->GetTeamNumber() == TEAM_SPECTATOR ? view->GetTeamNumber() : local->GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_INFECTED ) team = TEAM_SURVIVOR;
	if ( g_PR && g_PR->IsConnected( local->entindex() ) && m_pLocalPlayer ) m_pLocalPlayer->SetPlayer( team, local->entindex() );
	return m_pLocalPlayer && m_pLocalPlayer->GetPlayerIndex() > 0 &&
		( m_pLocalPlayer->GetTeam() == TEAM_SURVIVOR || m_pLocalPlayer->GetTeam() == TEAM_INFECTED );
}
void CHudLocalPlayerDisplay::LevelInit() { if ( m_pLocalPlayer ) m_pLocalPlayer->SetPlayer( 0, -1 ); }
void CHudLocalPlayerDisplay::LevelShutdown() { if ( m_pLocalPlayer ) m_pLocalPlayer->SetPlayer( 0, -1 ); }
void CHudLocalPlayerDisplay::Reset() { if ( m_pLocalPlayer ) m_pLocalPlayer->SetPlayer( 0, -1 ); }
void CHudLocalPlayerDisplay::OnPlayerPanelDeleted( vgui::Panel *panel ) { if ( m_pLocalPlayer.Get() == panel ) m_pLocalPlayer = NULL; }
void CHudLocalPlayerDisplay::FireGameEvent( IGameEvent *event )
{
	if ( Q_strcmp( event->GetName(), "award_earned" ) ) return;
	const int index = engine->GetPlayerForUserID( event->GetInt( "userid" ) );
	if ( m_pLocalPlayer && m_pLocalPlayer->GetPlayerIndex() == index )
		m_pLocalPlayer->AwardEarned( event->GetInt( "award" ), cl_entitylist->GetEnt( event->GetInt( "subjectentid" ) ) );
}
CHudTeamDisplay::CHudTeamDisplay( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), name )
{
	for ( int i = 0; i < 4; ++i )
	{
		m_pPlayers[i] = new TeamDisplayPlayer( this, CFmtStr( "TeamPlayer%d", i + 1 ), false );
		m_pPlayers[i]->SetBackground( i );
	}
	m_hScoreboard = vgui::INVALID_PANEL;
	SetHiddenBits( HIDEHUD_MISCSTATUS ); SetMouseInputEnabled( false ); SetPaintBackgroundEnabled( false ); ListenForGameEvent( "award_earned" );
	vgui::ivgui()->AddTickSignal( GetVPanel(), 0 );
}
CHudTeamDisplay::~CHudTeamDisplay() {}
void CHudTeamDisplay::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings ); LoadControlSettings( "Resource/UI/HUD/TeamDisplayHud.res", "GAME" );
}
void CHudTeamDisplay::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	// Single supported slot0: the vertical two-viewport branch is unreachable.
	LoadControlSettings( "Resource/UI/HUD/TeamDisplayHud.res", "GAME" );
	SetMouseInputEnabled( false ); SetPaintBackgroundEnabled( false );
}
void CHudTeamDisplay::ClearPlayers()
{
	for ( int i = 0; i < m_order.Count(); ++i ) if ( m_order[i] ) { m_order[i]->SetPlayer( 0, 0 ); m_order[i]->SetVisible( false ); }
	m_order.RemoveAll();
}
void CHudTeamDisplay::LevelInit() { for ( int i = 0; i < m_order.Count(); ++i ) if ( m_order[i] ) m_order[i]->SetPlayer( 0, -1 ); m_order.RemoveAll(); }
void CHudTeamDisplay::LevelShutdown() { LevelInit(); }
void CHudTeamDisplay::Reset() { LevelInit(); }
void CHudTeamDisplay::RefreshLayout() { InvalidateLayout( false, true ); }
bool CHudTeamDisplay::HasPlayer( int index ) const
{
	for ( int i = 0; i < m_order.Count(); ++i )
	{
		TeamDisplayPlayer *row = m_order[i];
		if ( !row || !row->IsVisible() ) continue;
		if ( row->GetPlayerIndex() == index ) return true;
		C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( UTIL_PlayerByIndex( row->GetPlayerIndex() ) );
		if ( bot && bot->GetTeamNumber() == TEAM_SURVIVOR && bot->GetHumanSpectatorUserID() > 0 && bot->GetHumanSpectatorEntIndex() == index ) return true;
	}
	return false;
}
void CHudTeamDisplay::AddPlayer( int index )
{
	for ( int i = 0; i < m_order.Count(); ++i ) if ( m_order[i] && m_order[i]->GetPlayerIndex() == index ) return;
	C_BaseEntity *entity = cl_entitylist->GetEnt( index );
	C_TerrorPlayer *player = ToTerrorPlayer( entity );
	// Native C_TransitioningPlayer v143 is C0196F0=>false. Retain the native
	// loading-list admission bug; UpdatePlayer's transition fallback remains.
	if ( !player ) return;
	const int character = player->GetSurvivorCharacter();
	for ( int i = 0; i < 4; ++i )
	{
		TeamDisplayPlayer *row = m_pPlayers[i];
		if ( !row ) break;
		vgui::DHANDLE< TeamDisplayPlayer > reference; reference = row;
		if ( m_order.Find( reference ) == m_order.InvalidIndex() )
		{ row->SetPlayer( TEAM_SURVIVOR, index ); row->UpdatePlayer(); m_order.AddToTail( reference ); return; }
		if ( row->GetCharacter() == character ) { row->SetPlayer( TEAM_SURVIVOR, index ); row->UpdatePlayer(); return; }
	}
}
void CHudTeamDisplay::OnTick()
{
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	C_TerrorPlayer *view = CinematicViewPlayer();
	if ( !local || !view ) return;
	const int before = m_order.Count();
	const int team = local->GetTeamNumber() == TEAM_SPECTATOR ? view->GetTeamNumber() : local->GetTeamNumber();
	for ( int i = m_order.Count() - 1; i >= 0; --i )
	{
		if ( !m_order[i] ) continue; // Minimal deleted-panel UB skip; no replacement row.
		if ( m_order[i]->GetPlayerIndex() <= 0 || ( m_order[i]->GetTeam() != TEAM_SURVIVOR && m_order[i]->GetTeam() != TEAM_INFECTED ) ) { ClearPlayers(); break; }
	}
	if ( team != TEAM_INFECTED )
	{
		if ( local->GetTeamNumber() == TEAM_SURVIVOR && local->m_lifeState == LIFE_ALIVE )
		{
			for ( int i = 0; i < m_order.Count(); ++i ) if ( m_order[i] && m_order[i]->GetPlayerIndex() == local->entindex() ) { ClearPlayers(); break; }
		}
		if ( g_PR ) for ( int i = 1; i <= MAX_PLAYERS; ++i )
		{
			if ( g_PR->IsConnected( i ) && g_PR->GetTeam( i ) == TEAM_SURVIVOR && i != local->entindex() ) AddPlayer( i );
		}
		const CUtlVector<C_TransitioningPlayer *> &loading = GetTransitioningPlayers();
		for ( int i = 0; i < loading.Count(); ++i ) if ( loading[i]->GetTeamNumber() == TEAM_SURVIVOR && !loading[i]->IsDormant() ) AddPlayer( loading[i]->entindex() );
	}
	if ( before != m_order.Count() ) RefreshLayout();
	// Native C2C8D70 ticks ONE shared row, including the local row,
	// even when this team panel is invisible.
	if ( s_terrorDisplayRows.Count() > 0 )
	{
		s_terrorDisplayRowCursor = ( s_terrorDisplayRowCursor + 1 ) % s_terrorDisplayRows.Count();
		if ( s_terrorDisplayRows[s_terrorDisplayRowCursor] ) s_terrorDisplayRows[s_terrorDisplayRowCursor]->UpdateRow();
	}
}
bool CHudTeamDisplay::ShouldDraw()
{
	if ( !CHudElement::ShouldDraw() ) return false;
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	static ConVarRef menu( "gameui_ingame_menu_active" );
	if ( !local || local->GetTeamNumber() != TEAM_SURVIVOR || menu.GetInt() ) return false;
	vgui::VPANEL scores = vgui::ivgui()->HandleToPanel( m_hScoreboard );
	if ( !scores )
	{
		IViewPortPanel *panel = gViewPortInterface->FindPanelByName( "scores" );
		if ( panel ) m_hScoreboard = vgui::ivgui()->PanelToHandle( panel->GetVPanel() );
		return m_order.Count() > 0; // Native does not test freshly found visibility this call.
	}
	return !vgui::ipanel()->IsVisible( scores ) && m_order.Count() > 0;
}
void CHudTeamDisplay::OnPlayerPanelDeleted( vgui::Panel *panel )
{
	TeamDisplayPlayer *row = dynamic_cast<TeamDisplayPlayer *>( panel );
	vgui::DHANDLE< TeamDisplayPlayer > reference; reference = row;
	m_order.FindAndRemove( reference );
}
void CHudTeamDisplay::FireGameEvent( IGameEvent *event )
{
	if ( Q_strcmp( event->GetName(), "award_earned" ) ) return;
	const int index = engine->GetPlayerForUserID( event->GetInt( "userid" ) );
	for ( int i = 0; i < m_order.Count(); ++i ) if ( m_order[i] && m_order[i]->GetPlayerIndex() == index )
	{
		m_order[i]->AwardEarned( event->GetInt( "award" ), cl_entitylist->GetEnt( event->GetInt( "subjectentid" ) ) ); return;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
#include "l4d2/hud/hud_terror_localplayer_console.inc"
#include "l4d2/hud/hud_terror_localplayer_console_registrations.inc"
#endif
