//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_transition_support.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/terror_shareddefs.h"
#include "game_controls/IconPanel.h"
#include "hud.h"
#include "vgui/IImage.h"
#include "vgui/ISurface.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"
#include "vgui/IScheme.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ImagePanel.h"
#include "steam/steam_api.h"
#include "tier0/memdbgon.h"

static ConVar stats_show_empty( "stats_show_empty", "0", 0x00004008 );
DECLARE_BUILD_FACTORY( CVersusModeResults );

// Native1AE500..1AEA2D. Do not substitute the SDK's newer avatar cache,
// persona callbacks, one-second polling or different friend-frame sizing.
class CTerrorAvatarImage : public vgui::IImage
{
public:
	CTerrorAvatarImage() : m_nTexture( -1 ), m_nWide( 0 ), m_nTall( 0 ), m_nAvatarSize( 0 ), m_nX( 0 ), m_nY( 0 ),
		m_Color( 0, 0, 0, 0 ), m_bValid( false ), m_bFriend( false ), m_pFriendIcon( NULL ), m_flFriendAspect( 1.0f ) {}
	virtual void Paint() OVERRIDE
	{
		if ( !m_bValid ) return;
		int x = m_nX;
		if ( m_bFriend && m_pFriendIcon )
		{
			const int wide = (int)( (float)m_nTall * m_flFriendAspect );
			m_pFriendIcon->DrawSelf( x, m_nY, wide, m_nTall, m_Color );
			x += wide;
		}
		vgui::surface()->DrawSetColor( m_Color ); vgui::surface()->DrawSetTexture( m_nTexture );
		// Native well-defined sizing: square height, plus friend-icon offset,
		// even when that extends beyond the enclosing ImagePanel's clip.
		vgui::surface()->DrawTexturedRect( x, m_nY, x + m_nAvatarSize, m_nY + m_nAvatarSize );
	}
	virtual void SetPos( int x, int y ) OVERRIDE { m_nX = x; m_nY = y; }
	virtual void GetContentSize( int &wide, int &tall ) OVERRIDE { wide = m_nWide; tall = m_nTall; }
	virtual void GetSize( int &wide, int &tall ) OVERRIDE { GetContentSize( wide, tall ); }
	virtual void SetSize( int wide, int tall ) OVERRIDE { m_nWide = wide; m_nTall = tall; m_nAvatarSize = tall; }
	virtual void SetColor( Color color ) OVERRIDE { m_Color = color; }
	virtual bool Evict() OVERRIDE { return false; } // Native1AE6C0.
	virtual int GetNumFrames() OVERRIDE { return 0; } // Native1AE6D0.
	virtual void SetFrame( int frame ) OVERRIDE {} // Native nullsub428.
	virtual vgui::HTexture GetID() OVERRIDE { return m_nTexture; }
	virtual void SetRotation( int rotation ) OVERRIDE {} // Native nullsub429.
	void SetAccount( CSteamID identity, int avatarSize )
	{
		if ( m_Requested == identity && m_bValid ) return;
		m_bValid = m_bFriend = false; m_FriendIdentity.Clear(); m_Requested = identity;
		ISteamFriends *friends = steamapicontext ? steamapicontext->SteamFriends() : NULL;
		ISteamUtils *utils = steamapicontext ? steamapicontext->SteamUtils() : NULL;
		if ( !friends || !utils ) return;
		m_FriendIdentity = identity;
		const uint32 expected = avatarSize == 0 ? 32 : avatarSize == 1 ? 64 : 184;
		const int image = avatarSize == 0 ? friends->GetSmallFriendAvatar( identity ) : avatarSize == 1 ? friends->GetMediumFriendAvatar( identity ) : friends->GetLargeFriendAvatar( identity );
		uint32 wide, tall;
		if ( utils->GetImageSize( image, &wide, &tall ) )
		{
			const bool realImage = wide != 0 && tall != 0 && wide == expected && tall == expected;
			if ( !realImage ) wide = tall = expected; // Native explicit white-image branch.
			const int bytes = wide * tall * 4;
			unsigned char *rgba = new unsigned char[bytes];
			bool pixelsDefined = true;
			if ( realImage ) pixelsDefined = utils->GetImageRGBA( image, rgba, bytes );
			else Q_memset( rgba, 255, bytes );
			// Native1AE870 ignores GetImageRGBA's failure, then uploads
			// uninitialized bytes. Main's minimal-skip rule skips ONLY upload.
			if ( pixelsDefined ) Upload( rgba, wide, tall );
			delete[] rgba;
		}
		if ( m_FriendIdentity.IsValid() )
		{
			m_bFriend = friends->HasFriend( m_FriendIdentity, k_EFriendFlagImmediate );
			if ( m_bFriend && !m_pFriendIcon )
			{
				m_pFriendIcon = gHUD.GetIcon( "ico_friend_indicator_avatar" );
				m_flFriendAspect = m_pFriendIcon ? (float)m_pFriendIcon->Width() / (float)m_pFriendIcon->Height() : 1.0f;
			}
		}
	}
private:
	void Upload( const unsigned char *rgba, int wide, int tall )
	{
		if ( m_nTexture == -1 ) m_nTexture = vgui::surface()->CreateNewTextureID( true );
		g_pMatSystemSurface->DrawSetTextureRGBAEx2( m_nTexture, rgba, wide, tall, IMAGE_FORMAT_RGBA8888, false );
		int screenWide, screenTall; GetHudSize( screenWide, screenTall );
		m_nWide = (int)( (double)screenWide * 0.0015625 * (double)wide );
		m_nTall = (int)( (double)screenTall * 0.002083333333333333 * (double)tall );
		m_Color = Color( 255, 255, 255, 255 ); m_bValid = true;
	}
	CSteamID m_Requested, m_FriendIdentity;
	int m_nTexture, m_nWide, m_nTall, m_nAvatarSize, m_nX, m_nY;
	Color m_Color;
	bool m_bValid, m_bFriend;
	CHudTexture *m_pFriendIcon;
	float m_flFriendAspect;
};

// Original CAvatarImagePanel remains an existing VGUI ImagePanel owner. The
// distinct name avoids replacing the SDK's unrelated same-named control.
class CTerrorAvatarImagePanel : public vgui::ImagePanel
{
	DECLARE_CLASS_SIMPLE( CTerrorAvatarImagePanel, vgui::ImagePanel );
public:
	CTerrorAvatarImagePanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_pAvatar( NULL ) {}
	void SetAccountID( unsigned int accountID )
	{
		if ( !accountID || !steamapicontext || !steamapicontext->SteamUtils() ) return;
		if ( !m_pAvatar ) { m_pAvatar = new CTerrorAvatarImage; SetImage( m_pAvatar ); }
		m_pAvatar->SetPos( 0, 0 );
		m_pAvatar->SetAccount( CSteamID( accountID, 1, steamapicontext->SteamUtils()->GetConnectedUniverse(), k_EAccountTypeIndividual ), GetWide() > 32 ? 1 : 0 );
		m_pAvatar->SetSize( GetWide(), GetTall() );
	}
	// Native1AED20->3FBB00 never deletes its IImage or texture: preserve leak.
protected:
	virtual void PaintBackground() OVERRIDE { if ( m_pAvatar ) { m_pAvatar->SetColor( GetDrawColor() ); m_pAvatar->Paint(); } }
private:
	CTerrorAvatarImage *m_pAvatar;
};

CTipPanel::CTipPanel( vgui::Panel *parent ) : BaseClass( parent, "tippanel" ), m_pIcon( NULL ), m_iSurvivorTip( 0 ), m_iInfectedTip( 0 ), m_nTeam( 0 )
{
	KeyValues *tips = new KeyValues( "Tips" );
	if ( tips->LoadFromFile( filesystem, "scripts/tips.txt", "GAME" ) )
	{
		LoadTips( tips->FindKey( "SurvivorTips" ), m_survivorTips );
		LoadTips( tips->FindKey( "InfectedTips" ), m_infectedTips );
	}
	tips->deleteThis();
}
void CTipPanel::LoadTips( KeyValues *root, CUtlVector< Tip_t > &tips )
{
	for ( KeyValues *entry = root ? root->GetFirstSubKey() : NULL; entry; entry = entry->GetNextKey() )
	{
		Tip_t tip;
		Q_strncpy( tip.text, entry->GetName(), sizeof( tip.text ) );
		Q_strncpy( tip.icon, entry->GetString(), sizeof( tip.icon ) );
		tips.AddToTail( tip );
	}
}
void CTipPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/TipPanel.res" );
	m_pIcon = dynamic_cast< CIconPanel * >( FindChildByName( "TipIcon" ) );
	AdvanceTip(); BaseClass::ApplySchemeSettings( scheme );
}
void CTipPanel::AdvanceTip()
{
	// Native2EAB10 requires BOTH arrays even when only one is selected. It
	// preincrements its index, so the first visible tip is entry1, not entry0.
	if ( !m_survivorTips.Count() || !m_infectedTips.Count() ) return;
	const Tip_t *tip;
	if ( m_nTeam == 0 ) { m_iSurvivorTip = ( m_iSurvivorTip + 1 ) % m_survivorTips.Count(); tip = &m_survivorTips[m_iSurvivorTip]; }
	else if ( m_nTeam == 1 ) { m_iInfectedTip = ( m_iInfectedTip + 1 ) % m_infectedTips.Count(); tip = &m_infectedTips[m_iInfectedTip]; }
	else return;
	if ( m_pIcon ) m_pIcon->SetIcon( tip->icon );
	SetControlString( "TipText", tip->text );
}

struct HighlightDescription_t { int statistic; const char *title, *icon; };
static const HighlightDescription_t s_highlights[] =
{
	{ 6, "#L4D_Highlight_MostDamageToTank", "Stat_Most_Tank_Dmg" },
	{ 33, "#L4D_Highlight_MostMeleeKills", "Stat_Most_Melee_Kills" },
	{ 7, "#L4D_Highlight_MostDamageToWitch", "Stat_Most_Witch_Dmg" },
	{ 9, "#L4D_Highlight_MostInfectedKilled", "Stat_Most_Infected_Kills" },
	{ 2, "#L4D_Highlight_MostSpecialsKilled", "Stat_Most_Special_Kills" },
	{ 8, "#L4D_Highlight_MostHeadshots", "Stat_Most_Headshots" },
	{ 34, "#L4D_Highlight_MostDamage", "Stat_vs_Most_Damage_Done" },
	{ 36, "#L4D_Highlight_MostTankDamage", "Stat_vs_Most_Damage_As_Tank" },
	{ 52, "#L4D_Highlight_Best_Hunter_Pounce", "Stat_vs_Most_Hunter_Pounces" },
	{ 53, "#L4D_Highlight_Best_Smoker_Pull", "Stat_vs_Most_Smoker_Pulls" },
	{ 55, "#L4D_Highlight_Most_Boomer_Victims", "Stat_vs_Most_Vomit_Hit" },
	{ 54, "#L4D_Highlight_Most_Jockey_Rides", "Stat_vs_Most_Jockey_Rides" },
	{ 41, "#L4D_Highlight_MostSpitterDamage", "Stat_vs_Most_Spit_Dmg" },
	{ 56, "#L4D_Highlight_Most_Charges", "Stat_vs_Most_Damage_As_Charger" }
};
static const char *const s_survivorPortraits[8] = { "s_panel_gambler", "s_panel_producer", "s_panel_coach", "s_panel_mechanic", "s_panel_namvet", "s_panel_teenangst", "s_panel_biker", "s_panel_manager" };
static const char *const s_survivorNames[8] = { "#L4D360UI_Gambler", "#L4D360UI_Producer", "#L4D360UI_Coach", "#L4D360UI_Mechanic", "#L4D360UI_NamVet", "#L4D360UI_TeenGirl", "#L4D360UI_Biker", "#L4D360UI_Manager" };
CSurvivorHighlightStatsPanel::CSurvivorHighlightStatsPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_pLayout = new KeyValues( "resource/UI/TransitionStatsSurvivorHighlight.res" );
	m_pLayout->LoadFromFile( filesystem, "resource/UI/TransitionStatsSurvivorHighlight.res", NULL );
	for ( int i = 0; i < 4; ++i )
	{
		m_pAvatars[i] = new CTerrorAvatarImagePanel( this, CFmtStr( "Survivor%dSteamAvatar", i + 1 ) );
		m_pNames[i] = new vgui::Label( this, CFmtStr( "Survivor%dName", i + 1 ), "" );
		m_pNamesNoAvatar[i] = new vgui::Label( this, CFmtStr( "Survivor%dName_NoAvatar", i + 1 ), "" );
	}
}
CSurvivorHighlightStatsPanel::~CSurvivorHighlightStatsPanel() { m_pLayout->deleteThis(); }
void CSurvivorHighlightStatsPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	LoadControlSettings( "resource/UI/TransitionStatsSurvivorHighlight.res", NULL, m_pLayout );
	// Original preload list intentionally spells producer correctly here, even
	// though its loading order differs from the displayed-character order.
	static const char *const preload[8] = { "s_panel_coach", "s_panel_mechanic", "s_panel_gambler", "s_panel_producer", "s_panel_namvet", "s_panel_biker", "s_panel_manager", "s_panel_teenangst" };
	for ( int i = 0; i < 8; ++i ) vgui::scheme()->GetImage( preload[i], true );
}
void CSurvivorHighlightStatsPanel::CapturePlayers()
{
	m_stats.CapturePlayers( 0, g_TerrorModes.IsPlayerControlledZombies() ? -1 : TEAM_SURVIVOR );
	RebuildCategories( true );
}
int __cdecl CSurvivorHighlightStatsPanel::CompareCategories( const Category_t *a, const Category_t *b )
{
	return (int)( (unsigned int)b->total - (unsigned int)a->total ); // Native SUB wrapping.
}
void CSurvivorHighlightStatsPanel::RebuildCategories( bool positiveOnly )
{
	m_categories.RemoveAll();
	if ( stats_show_empty.GetBool() ) positiveOnly = false;
	const bool playerZombies = g_TerrorModes.IsPlayerControlledZombies();
	for ( int i = 0; i < ARRAYSIZE( s_highlights ); ++i )
	{
		const int stat = s_highlights[i].statistic;
		if ( i >= 6 && !playerZombies ) break;
		if ( stat == 9 ) continue;
		if ( stat == 8 && !stats_show_empty.GetBool() && UTIL_IsLowViolence() ) continue;
		if ( positiveOnly && !m_stats.HasAnyPositive( stat ) ) continue;
		Category_t category = { stat, m_stats.SumStatistic( stat ) };
		m_categories.AddToTail( category );
	}
	m_categories.Sort( CompareCategories );
	// Native inserts9 AFTER sorting. Its uninitialized priority is never read
	// before this entry is removed, so the discarded-undefined-read rule elides it.
	Category_t common = { 9, 0 }; m_categories.AddToHead( common );
}
void CSurvivorHighlightStatsPanel::AdvanceCategory()
{
	if ( !m_categories.Count() ) RebuildCategories( true );
	const int statistic = m_categories[0].statistic;
	m_categories.Remove( 0 );
	CTerrorPlayerStats::RankResult_t ranked;
	// Main-approved2E5510 adaptation: failed26E2F0 leaves native locals
	// uninitialized. Preserve category/title/icon and take native invalid rows.
	if ( !m_stats.RankPlayers( statistic, ranked ) ) ranked.count = 0;
	const HighlightDescription_t *description = NULL;
	for ( int i = 0; i < ARRAYSIZE( s_highlights ); ++i ) if ( s_highlights[i].statistic == statistic ) { description = &s_highlights[i]; break; }
	if ( !description || !description->title ) return;
	SetControlString( "SurvivorStatsTitle", description->title );
	SetControlString( "SurvivorStatsSubTitle", CFmtStr( "%s_Desc", description->title ) );
	CIconPanel *categoryIcon = dynamic_cast< CIconPanel * >( FindChildByName( "SurvivorStatsCategoryImage" ) );
	if ( categoryIcon && description->icon ) categoryIcon->SetIcon( description->icon );
	for ( int i = 0; i < 4; ++i )
	{
		// Test written-record count BEFORE native's otherwise undefined score read.
		if ( i >= ranked.count || ranked.players[i].value < 0 )
		{
			SetControlString( CFmtStr( "Survivor%dScore", i + 1 ), "" );
			SetControlVisible( CFmtStr( "Survivor%dBackground", i + 1 ), false );
			SetControlVisible( CFmtStr( "Survivor%dAvatar", i + 1 ), false );
			m_pAvatars[i]->SetVisible( false ); m_pNames[i]->SetVisible( false ); m_pNamesNoAvatar[i]->SetVisible( false );
			continue;
		}
		const CTerrorPlayerStats::RankedPlayer_t &row = ranked.players[i];
		SetControlVisible( CFmtStr( "Survivor%dBackground", i + 1 ), row.player.local );
		if ( statistic == 53 || statistic == 54 ) SetControlString( CFmtStr( "Survivor%dScore", i + 1 ), CFmtStr( "%d:%02d", row.value / 60, row.value % 60 ) );
		else SetControlString( CFmtStr( "Survivor%dScore", i + 1 ), CFmtStr( "%d", row.value ) );
		const bool steamAvatar = row.player.accountID != 0 && !row.player.bot;
		const int character = GetTerrorSurvivorDisplayCharacter( row.player.character );
		vgui::ImagePanel *portrait = dynamic_cast< vgui::ImagePanel * >( FindChildByName( CFmtStr( "Survivor%dAvatar", i + 1 ) ) );
		if ( portrait )
		{
			portrait->SetVisible( !steamAvatar );
			if ( !steamAvatar ) portrait->SetImage( character >= 0 && character < 8 ? s_survivorPortraits[character] : "" );
		}
		m_pAvatars[i]->SetVisible( steamAvatar );
		if ( steamAvatar ) m_pAvatars[i]->SetAccountID( row.player.accountID );
		m_pNames[i]->SetVisible( true ); m_pNamesNoAvatar[i]->SetVisible( false );
		if ( row.player.bot && !row.player.controlledBot )
		{
			const wchar_t *name = g_pVGuiLocalize->Find( character >= 0 && character < 8 ? s_survivorNames[character] : "Survivor" );
			if ( name ) m_pNames[i]->SetText( name ); else m_pNames[i]->SetText( row.player.name );
		}
		else m_pNames[i]->SetText( row.player.name );
	}
}

CVersusModeResults::CVersusModeResults( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_pYourHighlight( NULL ), m_pEnemyHighlight( NULL ), m_nLocalPlayerSlot( -1 ), m_nYourScore( 0 ), m_nEnemyScore( 0 ), m_bFinalChapter( false ) {}
void CVersusModeResults::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/VersusModeResults.res" ); BaseClass::ApplySchemeSettings( scheme );
	SetPaintEnabled( false ); SetPaintBackgroundEnabled( false );
	m_pYourHighlight = FindChildByName( "YourTeamHighlightImage" ); m_pEnemyHighlight = FindChildByName( "EnemyTeamHighlightImage" );
	UpdateResults();
}
void CVersusModeResults::UpdateResults()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); // Supported slot0, native request -1.
	if ( !player ) return;
	int team = player->m_iVersusTeam;
	if ( team == 0 ) team = 1;
	else if ( team < 0 || team > 2 ) return;
	m_bFinalChapter = TerrorClient_GetTransitionChapterInfo( true, NULL ) == NULL;
	m_nYourScore = TerrorGameRules()->GetVersusScore( team, true );
	m_nEnemyScore = TerrorGameRules()->GetVersusScore( team == 1 ? 2 : 1, true );
	SetDialogVariable( "YourSurvivor", m_nYourScore ); SetDialogVariable( "EnemySurvivor", m_nEnemyScore );
	bool yourHighlight = false, enemyHighlight = false;
	if ( m_nYourScore > m_nEnemyScore )
	{
		SetControlString( "TeamWinLabel", g_pVGuiLocalize->Find( m_bFinalChapter ? "#L4D_vs_YourTeamWon" : "#L4D_vs_YourTeamIsWinning" ) );
		if ( !m_bFinalChapter ) SetControlString( "TeamFlipExplanationLabel", "#L4D_vs_Survivors_First" );
		SetControlVisible( "TeamFlipExplanationLabel", !m_bFinalChapter ); yourHighlight = true;
	}
	else if ( m_nYourScore < m_nEnemyScore )
	{
		SetControlString( "TeamWinLabel", g_pVGuiLocalize->Find( m_bFinalChapter ? "#L4D_vs_EnemyTeamWon" : "#L4D_vs_EnemyTeamIsWinning" ) );
		if ( !m_bFinalChapter ) SetControlString( "TeamFlipExplanationLabel", "#L4D_vs_Infected_First" );
		SetControlVisible( "TeamFlipExplanationLabel", !m_bFinalChapter ); enemyHighlight = true;
	}
	else
	{
		SetControlVisible( "TeamFlipExplanationLabel", false ); SetControlString( "TeamWinLabel", "#L4D_vs_TeamsTied" );
		yourHighlight = enemyHighlight = true;
	}
	if ( m_pYourHighlight && m_pEnemyHighlight ) { m_pYourHighlight->SetVisible( yourHighlight ); m_pEnemyHighlight->SetVisible( enemyHighlight ); }
}
