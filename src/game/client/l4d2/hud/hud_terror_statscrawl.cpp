//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "hud.h"
#include "hud_macros.h"
#include "l4d2/hud/hud_terror_statscrawl.h"
#include "hud_vote.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "iclientmode.h"
#include "cdll_util.h"
#include "filesystem.h"
#include "inputsystem/iinputsystem.h"
#include "inputsystem/ButtonCode.h"
#include "tier1/fmtstr.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include "tier0/memdbgon.h"


CStatsCrawl *g_pStatsCrawl = NULL;
DECLARE_HUDELEMENT( CStatsCrawl );
DECLARE_HUD_MESSAGE( CStatsCrawl, CreditsMsg );
DECLARE_HUD_MESSAGE( CStatsCrawl, StatsCrawlMsg );
DECLARE_HUD_MESSAGE( CStatsCrawl, StatsSkipState );
DECLARE_HUD_MESSAGE( CStatsCrawl, JoinLateMsg );

namespace
{
	float FAdd( float a, float b ) { return _mm_cvtss_f32( _mm_add_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }
	float FSub( float a, float b ) { return _mm_cvtss_f32( _mm_sub_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }
	float FMul( float a, float b ) { return _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }
	float FDiv( float a, float b ) { return _mm_cvtss_f32( _mm_div_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }
	int FInt( float value ) { return _mm_cvtt_ss2si( _mm_set_ss( value ) ); }
	Color CrawlColor( KeyValues *parameters, const char *name )
	{
		// Native3D5D90 accepts a caller default; SDK GetColor has no such
		// argument and initializes ALL channels black, changing typed values.
		Color result( 255, 0, 0, 255 );
		KeyValues *value = parameters->FindKey( name, false );
		if ( !value ) return result;
		switch ( value->GetDataType() )
		{
		case KeyValues::TYPE_COLOR: return value->GetColor();
		case KeyValues::TYPE_INT: result[0] = (unsigned char)value->GetInt(); break;
		case KeyValues::TYPE_FLOAT: result[0] = (unsigned char)FInt( value->GetFloat() ); break;
		case KeyValues::TYPE_STRING:
			{
				float channels[4];
				// Supported mounted scripts supply FOUR channels. Native
				// incomplete strings read argument/stack garbage; malformed
				// content is outside this port's accepted input domain, not
				// an emulated garbage value or a new guarded red fallback.
				int parsed = sscanf( value->GetString(), "%f %f %f %f", &channels[0], &channels[1], &channels[2], &channels[3] );
				Assert( parsed == 4 );
				for ( int i = 0; i < 4; ++i ) result[i] = (unsigned char)FInt( channels[i] );
				break;
			}
		default: break;
		}
		return result;
	}
	bool FNaN( float value ) { return ( FloatBits( value ) & 0x7fffffffu ) > 0x7f800000u; }
	unsigned int FOrder( float value )
	{
		unsigned int bits = FloatBits( value );
		if ( ( bits & 0x7fffffffu ) == 0u ) bits = 0u;
		return ( bits & 0x80000000u ) ? ~bits : bits | 0x80000000u;
	}
	bool FGe( float a, float b ) { return !FNaN( a ) && !FNaN( b ) && FOrder( a ) >= FOrder( b ); }
	bool FGt( float a, float b ) { return !FNaN( a ) && !FNaN( b ) && FOrder( a ) > FOrder( b ); }
	bool FLt( float a, float b ) { return FGt( b, a ); }
	bool FEq( float a, float b ) { return !FNaN( a ) && !FNaN( b ) && FOrder( a ) == FOrder( b ); }
	float ClampOrdered( float value ) { return FLt( value, 0.0f ) ? 0.0f : FGt( value, 1.0f ) ? 1.0f : value; }
	double CrawlRemap( float value, float a, float b, float c, float d )
	{
		// Native2CC30 uses SUBSS/DIVSS followed by independently widened x87
		// endpoint math, retaining unordered fraction rather than generic MIN/MAX.
		if ( FEq( a, b ) ) return ( FLt( value, b ) || FNaN( value ) || FNaN( b ) ) ? c : d;
		float fraction = ClampOrdered( FDiv( FSub( value, a ), FSub( b, a ) ) );
		return (double)c + ( (double)d - (double)c ) * (double)fraction;
	}
	void Localize( const char *source, wchar_t *destination, int bytes )
	{
		if ( source[0] == '#' )
		{
			const wchar_t *text = g_pVGuiLocalize->Find( source );
			if ( text ) g_pVGuiLocalize->ConstructString( destination, bytes, text, 0 );
		}
		else g_pVGuiLocalize->ConvertANSIToUnicode( source, destination, bytes );
	}
	struct StatType_t { int value; const char *name; };
	const StatType_t s_statTypes[40] =
	{
		{0,"STAT_LEAST_FRIENDLY_FIRE"},{2,"STAT_MOST_SPECIAL_INFECTED_KILLED"},{3,"STAT_MOST_SAVES"},
		{4,"STAT_LEAST_DAMAGE_TAKEN"},{5,"STAT_LEAST_INCAPS"},{6,"STAT_MOST_TANK_DAMAGE"},
		{7,"STAT_MOST_WITCH_DAMAGE"},{8,"STAT_MOST_HEADSHOTS"},{9,"STAT_MOST_INFECTED_KILLED"},
		{10,"STAT_MOST_PROTECTS"},{11,"STAT_MOST_ACCURATE"},{12,"STAT_MOST_DEATHS"},
		{13,"STAT_MOST_MEDKITS_USED"},{14,"STAT_MOST_PILLS_USED"},{15,"STAT_MOST_MOLOTOVS_USED"},
		{16,"STAT_MOST_PIPEBOMBS_USED"},{17,"STAT_MOST_BOOMER_BILES_USED"},{18,"STAT_MOST_ADRENALINES_USED"},
		{19,"STAT_MOST_DEFIBRILLATORS_USED"},{20,"STAT_MOST_HUNTERS_KILLED"},{21,"STAT_MOST_BOOMERS_KILLED"},
		{22,"STAT_MOST_SMOKERS_KILLED"},{23,"STAT_MOST_CHARGERS_KILLED"},{24,"STAT_MOST_JOCKEYS_KILLED"},
		{25,"STAT_MOST_SPITTERS_KILLED"},{26,"STAT_MOST_WITCHES_KILLED"},{27,"STAT_MOST_TANKS_KILLED"},
		{28,"STAT_MOST_COMMON_INFECTED_KILLED"},{29,"STAT_MOST_WITCH_DISTURBANCES"},{30,"STAT_MOST_HEAL_OTHER"},
		{31,"STAT_MOST_PILLS_SHARED"},{32,"STAT_MOST_HEADSHOT_PERCENT"},{57,"STAT_PLAYER_ZOEY"},
		{58,"STAT_PLAYER_BILL"},{59,"STAT_PLAYER_FRANCIS"},{60,"STAT_PLAYER_LOUIS"},
		{61,"STAT_DIFFICULTY"},{62,"STAT_MISSION_TIME"},{63,"STAT_RESTARTS"},{33,"STAT_MOST_MELEE_KILLS"}
	};
	int StatType( const char *name )
	{
		for ( int i = 0; i < ARRAYSIZE( s_statTypes ); ++i ) if ( !Q_stricmp( name, s_statTypes[i].name ) ) return s_statTypes[i].value;
		return 64;
	}
	C_TerrorPlayerResource *CrawlResource() { return dynamic_cast< C_TerrorPlayerResource * >( g_PR ); }
}

CStatsCrawl::Line_t::Line_t() : leftWidth( 0 ), rightWidth( 0 ), color( 0, 0, 0, 0 ), y( 0.0f ), fixed( false ), draw( false )
{
	left[0] = right[0] = L'\0';
}
CStatsCrawl::CStatsCrawl( const char *name ) : CHudElement( name ), BaseClass( NULL, "StatsCrawl" ),
	m_screenWidth( 0 ), m_screenHeight( 0 ), m_imageTexture( -1 ), m_scrollScreens( 0.0f ),
	m_textColor( 0,0,0,0 ), m_localTextColor( 0,0,0,0 ), m_scrollDelay( 0.0f ), m_scrollStart( 0.0f ),
	m_scrollTime( 0.0f ), m_separation( 0.0f ), m_titleDeadline( 0.0f ), m_backgroundDeadline( 0.0f ),
	m_lastRowStopped( false ), m_scrollStarted( false ), m_titleAlpha( 0 ), m_textAlpha( 0 ), m_backgroundAlpha( 0 ),
	m_titleStartDelay( 0.0f ), m_titleFadeIn( 0.0f ), m_titleVisible( 0.0f ), m_titleFadeOut( 0.0f ),
	m_maxTitleAlpha( 0.0f ), m_backgroundFadeIn( 0.0f ), m_titlePhase( 0 ), m_type( 0 ),
	m_currentTitleVisible( 0.0f ), m_skipVoted( false ), m_skippableAt( 0.0f )
{
	// Native2C3FD0 whole1456-byte allocation is zeroed before2C3D50; explicit
	// retained data initialization above represents that proven allocation.
	m_image[0] = '\0';
	m_titleRoster.count = 0; // Actual zeroed native embedded result, not a missing-resource fallback.
	SetParent( g_pClientMode->GetViewport() );
	g_pStatsCrawl = this;
	m_votes = new VoteBarPanel( this, "votes" );
	m_votes->SetVisible( false );
	SetProportional( true ); // Native3E9D60, not an invented background-paint writer.
}
CStatsCrawl::~CStatsCrawl()
{
	g_pStatsCrawl = NULL; // Native clears before texture/vector/base destruction.
	// Original does not delete the imageTexture here; do not repair that lifetime.
}
void CStatsCrawl::Init()
{
	HOOK_HUD_MESSAGE( CStatsCrawl, CreditsMsg ); HOOK_HUD_MESSAGE( CStatsCrawl, StatsCrawlMsg );
	HOOK_HUD_MESSAGE( CStatsCrawl, StatsSkipState ); HOOK_HUD_MESSAGE( CStatsCrawl, JoinLateMsg );
	SetActive( false );
}
void CStatsCrawl::LevelInit() { m_type = -1; } // Native2C0530 does not hide/reset here.
void CStatsCrawl::LevelShutdown()
{
	SetActive( false ); SetVisible( false ); m_rows.RemoveAll();
	m_lastRowStopped = m_scrollStarted = false;
	m_backgroundAlpha = m_textAlpha = 0;
	m_skipVoted = false; m_titlePhase = 4; m_type = -1;
}
bool CStatsCrawl::ShouldDraw() { return IsActive(); }
bool CStatsCrawl::IsShowingCredits() { return IsActive() && m_type == 1; }
void CStatsCrawl::MsgFunc_CreditsMsg( bf_read & )
{
	m_type = 1; BeginCrawl(); m_votes->SetVisible( false ); m_votes->SetVoteCounts( 0, 0, 0 );
}
void CStatsCrawl::MsgFunc_StatsCrawlMsg( bf_read & )
{
	if ( m_type == 0 ) return;
	m_type = 0; BeginCrawl(); m_votes->SetVisible( false ); m_votes->SetVoteCounts( 0, 0, 0 );
}
void CStatsCrawl::MsgFunc_StatsSkipState( bf_read &message )
{
	const int yes = message.ReadByte(), total = message.ReadByte();
	m_votes->SetVisible( yes > 0 ); m_votes->SetVoteCounts( yes, 0, total );
}
void CStatsCrawl::MsgFunc_JoinLateMsg( bf_read & )
{
	m_type = 2; m_backgroundFadeIn = 0.0f; BeginCrawl();
	SetActive( true ); SetVisible( true ); m_votes->SetVisible( false ); m_votes->SetVoteCounts( 0, 0, 0 );
}
int CStatsCrawl::KeyInput( int down, ButtonCode_t key, const char *binding )
{
	// Native caller26F560 supplies visible+keydown. No once-only local veto.
	if ( FGt( m_skippableAt, gpGlobals->curtime ) ) return 1;
	if ( key != KEY_SPACE && key != KEY_F1 && key != KEY_XBUTTON_A && key != STEAMCONTROLLER_A && key != STEAMCONTROLLER_START ) return 1;
	DevMsg( "Player voted to skip outtro stats\n" ); engine->ClientCmd( "SkipOuttro" ); m_skipVoted = true; return 0;
}
int CStatsCrawl::FindFont( const char *name )
{
	if ( !name || !*name ) return -1;
	FOR_EACH_VEC( m_fonts, i ) if ( !Q_stricmp( m_fonts[i].name, name ) ) return i;
	vgui::IScheme *scheme = vgui::scheme()->GetIScheme( vgui::scheme()->GetScheme( "ClientScheme" ) );
	if ( !scheme ) return -1;
	Font_t font; Q_strncpy( font.name, name, sizeof( font.name ) ); font.handle = scheme->GetFont( name, true );
	return m_fonts.AddToTail( font );
}
vgui::HFont CStatsCrawl::GetFont( int index ) const { return m_fonts.IsValidIndex( index ) ? m_fonts[index].handle : 0; }
void CStatsCrawl::MeasureLine( Line_t &line, vgui::HFont font )
{
	int height;
	vgui::surface()->GetTextSize( font, line.left, line.leftWidth, height );
	vgui::surface()->GetTextSize( font, line.right, line.rightWidth, height );
}
void CStatsCrawl::PlayerName( const CTerrorPlayerStats::PlayerIdentity_t &player, wchar_t *text, int bytes, bool plainBotFallback ) const
{
	if ( player.bot && !player.controlledBot )
	{
		static const char *const names[8] = { "#L4D360UI_Gambler", "#L4D360UI_Producer", "#L4D360UI_Coach", "#L4D360UI_Mechanic", "#L4D360UI_NamVet", "#L4D360UI_TeenGirl", "#L4D360UI_Biker", "#L4D360UI_Manager" };
		int character = GetTerrorSurvivorDisplayCharacter( player.character );
		const wchar_t *localized = g_pVGuiLocalize->Find( character >= 0 && character < 8 ? names[character] : "Survivor" );
		if ( localized ) { g_pVGuiLocalize->ConstructString( text, bytes, L"%s1", 1, localized ); return; }
		if ( plainBotFallback )
		{
			static const char *const plain[8] = { "Nick", "Rochelle", "Coach", "Ellis", "Bill", "Zoey", "Francis", "Louis" };
			g_pVGuiLocalize->ConvertANSIToUnicode( character >= 0 && character < 8 ? plain[character] : "Survivor", text, bytes );
			return;
		}
	}
	g_pVGuiLocalize->ConvertANSIToUnicode( player.name, text, bytes );
}
void CStatsCrawl::ReadParameters( KeyValues *params )
{
	if ( !params ) return;
	m_titleStartDelay = params->GetFloat( "titlefadestarttime", 0 ); m_titleFadeIn = params->GetFloat( "titlefadeintime", 3 );
	m_titleVisible = params->GetFloat( "titlevisibletime", 3 ); m_titleFadeOut = params->GetFloat( "titlefadeouttime", 8 );
	m_maxTitleAlpha = params->GetFloat( "maxtitlealpha", 255 ); m_backgroundFadeIn = params->GetFloat( "backgroundfadeintime", 3 );
	m_skippableAt = params->GetFloat( "skippabletime", 8 ); m_scrollDelay = params->GetFloat( "scrolldelay", 0 );
	m_scrollTime = params->GetFloat( "scrolltime", 57 ); m_separation = params->GetFloat( "separation", 5 );
	m_textColor = CrawlColor( params, "textcolor" ); m_localTextColor = CrawlColor( params, "localtextcolor" );
	Q_strncpy( m_image, params->GetString( "image", "" ), sizeof( m_image ) );
}
void CStatsCrawl::ResetTitle()
{
	m_currentTitleVisible = m_titleVisible; m_titleAlpha = 0;
	m_titleDeadline = FAdd( m_titleStartDelay, gpGlobals->curtime ); m_titlePhase = 0;
	m_skippableAt = FAdd( gpGlobals->curtime, m_skippableAt );
	if ( m_imageTexture == -1 ) m_imageTexture = vgui::surface()->CreateNewTextureID( false );
	if ( m_type == 0 || m_type == 2 )
	{
		KeyValues *details = TerrorBuildConsumedClientGameProjection();
		if ( details )
		{
			KeyValues *mission = NULL; g_TerrorModes.GetChapterInfo( details, &mission ); details->deleteThis();
			const char *image = mission ? mission->GetString( "OuttroImage", NULL ) : NULL;
			if ( image ) Q_strncpy( m_image, image, sizeof( m_image ) );
		}
	}
	vgui::surface()->DrawSetTextureFile( m_imageTexture, m_image, false, false );
}
void CStatsCrawl::BeginCrawl()
{
	SetActive( false ); SetVisible( false ); m_rows.RemoveAll();
	m_lastRowStopped = m_scrollStarted = false; m_backgroundAlpha = m_textAlpha = 0;
	m_titlePhase = 4; m_skipVoted = false;
	vgui::surface()->GetScreenSize( m_screenWidth, m_screenHeight );
	KeyValues *script = new KeyValues( "StatsCrawlScript" );
	if ( !script->LoadFromFile( filesystem, m_type != 0 ? "credits.txt" : "scripts/stats_crawl.txt", "GAME" ) ) { script->deleteThis(); return; }
	ReadParameters( script->FindKey( "Params" ) );
	KeyValues *entries = script->FindKey( "OuttroTemplate" );
	if ( m_type == 1 ) { BuildCredits( entries ); m_backgroundAlpha = m_textAlpha = 255; }
	else if ( m_type == 0 )
	{
		m_stats.CapturePlayers( 1, TEAM_SURVIVOR ); BuildTitleAndCast(); BuildStats( entries ); m_textAlpha = 0;
	}
	script->deleteThis(); ResetTitle();
	m_backgroundDeadline = FAdd( m_backgroundFadeIn, gpGlobals->curtime ); m_scrollStart = FAdd( gpGlobals->curtime, m_scrollDelay );
	if ( !m_rows.Count() ) return;
	vgui::surface()->GetFontTall( m_hSkipLabelFont ); vgui::surface()->GetFontTall( m_hButtonFont );
	if ( m_type == 1 ) vgui::surface()->GetFontTall( m_hCreditsCrawlFont );
	else { vgui::surface()->GetFontTall( m_hStatsCrawlUnderlineFont ); vgui::surface()->GetFontTall( m_hStatsCrawlFont ); }
	SetActive( true ); SetVisible( true ); PositionRows( false );
}
void CStatsCrawl::BuildCredits( KeyValues *entries )
{
	for ( KeyValues *entry = entries ? entries->GetFirstSubKey() : NULL; entry; entry = entry->GetNextKey() )
	{
		Row_t row; row.title.color = m_textColor; row.font = FindFont( entry->GetString() );
		const char *text = entry->GetName();
		if ( text[0] == '#' ) V_wcsncpy( row.title.left, TerrorFindLocalizedTextSafe( text ), sizeof( row.title.left ) );
		else V_UTF8ToUnicode( text, row.title.left, sizeof( row.title.left ) );
		MeasureLine( row.title, GetFont( row.font ) ); m_rows.AddToTail( row );
	}
}
bool CStatsCrawl::BuildStatLine( Row_t &row, KeyValues *entry )
{
	const int type = StatType( entry->GetString( "type", "" ) );
	char value[256]; value[0] = '\0';
	if ( type == 61 )
	{
		if ( g_TerrorModes.IsBaseMode( "versus" ) ) Q_strncpy( value, "#L4D_Versus", sizeof( value ) );
		else
		{
			ConVarRef difficulty( "z_difficulty" );
			static const char *const levels[4] = { "Easy", "Normal", "Hard", "Impossible" };
			static const char *const tokens[4] = { "#L4D_DifficultyEasy", "#L4D_DifficultyNormal", "#L4D_DifficultyHard", "#L4D_DifficultyImpossible" };
			Q_strncpy( value, "#L4D_DifficultyNormal", sizeof( value ) ); // Actual287110 invalid-string result1.
			for ( int i=0; i<4; ++i ) if ( !Q_stricmp( difficulty.GetString(), levels[i] ) ) { Q_strncpy( value, tokens[i], sizeof( value ) ); break; }
		}
	}
	else if ( type == 62 )
	{
		C_TerrorPlayerResource *resource = CrawlResource(); if ( !resource ) return false;
		int seconds = resource->GetMissionDuration(), hours = seconds / 60 / 60, minutes = seconds / 60 % 60;
		wchar_t text[32];
		_snwprintf( text, ARRAYSIZE(text), L"%d %ls, %d %ls", hours, TerrorFindLocalizedTextSafe( hours == 1 ? "#L4D_Hour" : "#L4D_Hours" ), minutes, TerrorFindLocalizedTextSafe( minutes == 1 ? "#L4D_Minute" : "#L4D_Minutes" ) ); // Native CRT truncation need not terminate.
		g_pVGuiLocalize->ConvertUnicodeToANSI( text, value, sizeof( value ) );
	}
	else if ( type == 63 ) { C_TerrorPlayerResource *resource = CrawlResource(); if ( !resource ) return false; Q_snprintf( value, sizeof(value), "%d", resource->GetMissionWipes() ); }
	else return false; // Original26D0B0 does NOT implement57..60, not a missing-actor substitute.
	Localize( value, row.title.right, sizeof( row.title.right ) ); MeasureLine( row.title, GetFont( row.font ) ); return true;
}
bool CStatsCrawl::BuildStatBlock( Row_t &row, KeyValues *entry )
{
	const int type = StatType( entry->GetString( "type", "" ) ); if ( type == 64 ) return false;
	CTerrorPlayerStats::RankResult_t players; if ( !m_stats.RankPlayers( type, players ) ) return false;
	const int requested = entry->GetInt( "numplayers", 0 );
	// USER DECISION: retain native min(numplayers,rank.count), including the
	// reported rank count; native Row5228 still has ONLY4 lines. Stock uses4.
	// Custom/malformed >4 requests truncate WRITES/READS to4, adapting native
	// out-of-allocation writes without extra storage, fatal errors or actors.
	row.count = requested < players.count ? requested : players.count;
	for ( int i = 0; i < MIN( row.count, 4 ); ++i )
	{
		Line_t &line = row.players[i];
		line.color = players.players[i].player.local ? m_localTextColor : m_textColor;
		PlayerName( players.players[i].player, line.left, sizeof(line.left) );
		CFmtStr value( entry->GetString("format","%d"), players.players[i].value ); // Native CFmtStrN<256>, not a growing string.
		g_pVGuiLocalize->ConvertANSIToUnicode( value.Access(), line.right, sizeof(line.right) ); MeasureLine( line, GetFont(row.font) );
	}
	return true;
}
void CStatsCrawl::BuildStats( KeyValues *entries )
{
	if ( !entries ) return; // Native34B0 guards the ENTIRE template/tagline body.
	for ( KeyValues *entry=entries?entries->GetFirstSubKey():NULL; entry; entry=entry->GetNextKey() )
	{
		Row_t row; Localize( entry->GetString("title",""), row.title.left, sizeof(row.title.left) );
		row.title.color=m_textColor; row.font=FindFont(entry->GetString("font","OuttroStatsCrawl")); MeasureLine(row.title,GetFont(row.font));
		if ( !Q_stricmp(entry->GetName(),"statline") ) { if(!BuildStatLine(row,entry))continue; }
		else if ( !Q_stricmp(entry->GetName(),"statblock") ) { if(!BuildStatBlock(row,entry))continue; }
		else { row.title.right[0]=L'\0'; row.title.rightWidth=0; row.count=0; }
		m_rows.AddToTail(row);
	}
	CTerrorPlayerStats::RankResult_t kills;
	// USER DECISION: this native LOCAL output is undefined on failed rank;
	// treat failure as empty/sum0, unlike the memorial's retained member output.
	kills.count=0;m_stats.RankPlayers(1,kills);unsigned int sum=0;
	// USER DECISION: rank keeps count; sum only min(count,8) written entries.
	for(int i=0;i<MIN(kills.count,8);++i)sum+=(unsigned int)kills.players[i].value;
	char digits[16],commas[24]; Q_snprintf(digits,sizeof(digits),"%d",(int)sum); int length=Q_strlen(digits),out=0;
	for(int i=0;i<length;++i){if(out>0&&(length-i)%3==0)commas[out++]=',';commas[out++]=digits[i];}commas[out]='\0';
	wchar_t number[16];g_pVGuiLocalize->ConvertANSIToUnicode(commas,number,sizeof(number));
	Row_t row;row.title.color=m_textColor;row.font=FindFont("OuttroStatsCrawl");
	g_pVGuiLocalize->ConstructString(row.title.left,sizeof(row.title.left),g_pVGuiLocalize->Find("#L4D_Stat_Tagline"),1,number);
	MeasureLine(row.title,GetFont(row.font));m_rows.AddToTail(row);
}
void CStatsCrawl::BuildTitleAndCast()
{
	// USER DECISION: this PERSISTENT native output remains unchanged on failed
	// rank; do not zero it as done for the two native uninitialized locals.
	m_stats.RankPlayers(0,m_titleRoster); int dead=0; const CTerrorPlayerStats::PlayerIdentity_t *firstOut=NULL;
	// USER DECISION: consume min(count,8), retaining the reported native count.
	for(int i=0;i<MIN(m_titleRoster.count,8);++i){if(!m_titleRoster.players[i].player.alive)++dead;else if(m_titleRoster.players[i].player.firstManOut&&!firstOut)firstOut=&m_titleRoster.players[i].player;}
	Row_t title;title.title.color=m_textColor;title.font=FindFont("InstructorTitle");
	if(firstOut)
	{
		wchar_t name[32];g_pVGuiLocalize->ConvertANSIToUnicode(firstOut->name,name,sizeof(name));
		if(firstOut->bot&&!firstOut->controlledBot)PlayerName(*firstOut,name,sizeof(name));
		g_pVGuiLocalize->ConstructString(title.title.left,sizeof(title.title.left),g_pVGuiLocalize->Find("#L4D360UI_FirstSurvivorOut_Fmt"),1,name);
	}
	else g_pVGuiLocalize->ConstructString(title.title.left,sizeof(title.title.left),dead?L"%s1:":L"%s1",1,g_pVGuiLocalize->Find(dead?"#L4D_InMemoryOf":"#L4D_Scenario_Finale_Won"));
	MeasureLine(title.title,GetFont(title.font));int height=vgui::surface()->GetFontTall(GetFont(title.font));int y=(m_screenHeight-height*(dead+1))/2;
	title.title.fixed=true;title.title.y=(float)y;m_rows.AddToTail(title);y=(int)((double)height*1.5+(double)y);
	// USER DECISION: same8-written-record bound for the memorial's names.
	if(!firstOut)for(int i=0;i<MIN(m_titleRoster.count,8);++i)if(!m_titleRoster.players[i].player.alive)
	{
		const CTerrorPlayerStats::PlayerIdentity_t &player=m_titleRoster.players[i].player;
		Row_t row;row.title.color=m_textColor;row.font=FindFont("InstructorTitle");
		if(!player.bot||player.controlledBot)g_pVGuiLocalize->ConstructString(row.title.left,sizeof(row.title.left),player.name,(KeyValues*)NULL);
		else PlayerName(player,row.title.left,sizeof(row.title.left));
		MeasureLine(row.title,GetFont(row.font));row.title.fixed=true;row.title.y=(float)y;y+=height;m_rows.AddToTail(row);
	}
	Row_t cast;cast.title.color=m_textColor;cast.font=FindFont("OuttroStatsCrawlUnderline");V_wcsncpy(cast.title.left,TerrorFindLocalizedTextSafe("#L4D360UI_Survivors"),sizeof(cast.title.left));MeasureLine(cast.title,GetFont(cast.font));m_rows.AddToTail(cast);
	Row_t space;space.title.color=Color(0,0,0,0);space.font=FindFont("OuttroStatsCrawl");V_wcsncpy(space.title.left,L" ",sizeof(space.title.left));space.title.leftWidth=10;m_rows.AddToTail(space);
	// Native2C2FE0 re-ranks and therefore consumes another randomized tie order.
	CTerrorPlayerStats::RankResult_t roster;
	// USER DECISION: failed native LOCAL rank output is empty, not arbitrary
	// stack count/identity; memorial member output above remains native-retained.
	roster.count=0;m_stats.RankPlayers(0,roster);
	// USER DECISION: consume min(count,8), never invented additional actors.
	for(int i=0;i<MIN(roster.count,8);++i)
	{
		const CTerrorPlayerStats::PlayerIdentity_t &player=roster.players[i].player;Row_t row;row.title.color=player.local?m_localTextColor:m_textColor;row.font=FindFont("OuttroStatsCrawl");PlayerName(player,row.title.left,sizeof(row.title.left),true);
		wchar_t role[64];
		if(player.bot&&!player.controlledBot)V_wcsncpy(role,TerrorFindLocalizedTextSafe(player.character==1?"#L4D360UI_Loading_As_Herself":"#L4D360UI_Loading_As_Himself"),sizeof(role));
		else
		{
			static const char *const names[8]={"L4D360UI_Loading_As_Nick","L4D360UI_Loading_As_Rochelle","L4D360UI_Loading_As_Coach","L4D360UI_Loading_As_Ellis","L4D360UI_Loading_As_Bill","L4D360UI_Loading_As_Zoey","L4D360UI_Loading_As_Francis","L4D360UI_Loading_As_Louis"};
			int character=GetTerrorSurvivorDisplayCharacter(player.character);V_wcsncpy(role,TerrorFindLocalizedTextSafe(character>=0&&character<8?names[character]:""),sizeof(role));
		}
		if(player.alive)V_wcsncpy(row.title.right,role,sizeof(row.title.right));else g_pVGuiLocalize->ConstructString(row.title.right,sizeof(row.title.right),L"%s1 (%s2)",2,role,g_pVGuiLocalize->Find("#L4D_Deceased"));
		MeasureLine(row.title,GetFont(row.font));m_rows.AddToTail(row);
	}
}
void CStatsCrawl::PositionRows( bool preserveScroll )
{
	int height=vgui::surface()->GetFontTall(m_type==1?m_hCreditsCrawlFont:m_hStatsCrawlFont),y=m_screenHeight;
	if(preserveScroll)y-=FInt(FMul(FDiv(FMul((float)m_screenHeight,m_scrollScreens),m_scrollTime),FSub(gpGlobals->curtime,m_scrollStart)));
	FOR_EACH_VEC(m_rows,i)if(!m_rows[i].title.fixed)
	{
		Row_t &row=m_rows[i];row.title.y=(float)y;row.title.draw=false;if(!row.count)y=FInt(FAdd(FAdd((float)height,m_separation),(float)y));
		// USER DECISION: preserve row.count but touch at most its4 actual lines.
		for(int j=0;j<MIN(row.count,4);++j){row.players[j].y=(float)y;row.players[j].draw=false;y=FInt(FAdd(FAdd((float)height,m_separation),(float)y));}
	}
	m_scrollScreens=(float)(y/m_screenHeight); // Original INTEGER division, no zero-height/scroll-time clamp.
}
void CStatsCrawl::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings(scheme);vgui::surface()->GetScreenSize(m_screenWidth,m_screenHeight);SetBounds(0,0,m_screenWidth,m_screenHeight);
	SetVisible(IsActive());SetTabPosition(0);m_votes->InvalidateLayout(true,true);m_votes->SetPos(m_screenWidth-m_iVoteBoxInsetX,m_screenHeight-m_iVoteBoxInsetY);
	FOR_EACH_VEC(m_fonts,i)m_fonts[i].handle=scheme->GetFont(m_fonts[i].name,true);
	FOR_EACH_VEC(m_rows,i){MeasureLine(m_rows[i].title,GetFont(m_rows[i].font));for(int j=0;j<4;++j)MeasureLine(m_rows[i].players[j],GetFont(m_rows[i].font));}PositionRows(true);
}
void CStatsCrawl::DrawTitle()
{
	if(m_titlePhase==4)return;
	float now=gpGlobals->curtime;
	switch(m_titlePhase)
	{
	case 0:if(FGe(now,m_titleDeadline)){m_titlePhase=1;m_titleDeadline=FAdd(m_titleFadeIn,now);}break;
	case 1:
		{
			float left=FSub(m_titleDeadline,now);float value=(float)CrawlRemap(left,m_titleFadeIn,0,-128,m_maxTitleAlpha);
			m_titleAlpha=FInt(FLt(value,0.0f)?0.0f:value); // FCOMIP/JBE retains unordered, after actual float spill.
			if(FGe(0.0f,left)){m_titlePhase=2;m_titleDeadline=FAdd(m_currentTitleVisible,now);}break;
		}
	case 2:if(FGe(now,m_titleDeadline)){m_titlePhase=3;m_titleDeadline=FAdd(m_titleFadeOut,now);}break;
	case 3:
		{
			float left=FSub(m_titleDeadline,now);
			// Native __ftol2_sse spills to double, then its SSE2 path converts
			// directly to signed32, including INT_MIN on invalid/overflow.
			m_titleAlpha=_mm_cvttsd_si32(_mm_set_sd(CrawlRemap(left,0,m_titleFadeOut,0,m_maxTitleAlpha)));
			if(FGe(0.0f,left))m_titlePhase=4;break;
		}
	}
	if(m_type==2)m_titleAlpha=255;
	vgui::surface()->DrawSetColor(255,255,255,m_titleAlpha);vgui::surface()->DrawSetTexture(m_imageTexture);
	int width,height;vgui::surface()->DrawGetTextureSize(m_imageTexture,width,height);float maximum=FMul((float)m_screenWidth,0.8f);
	if(FGt((float)width,maximum)){float ratio=FDiv(maximum,(float)width);width=FInt(FMul((float)width,ratio));height=FInt(FMul((float)height,ratio));}
	vgui::surface()->DrawTexturedRect((m_screenWidth-width)>>1,(m_screenHeight-height)>>1,(m_screenWidth+width)>>1,(m_screenHeight+height)>>1);
}
void CStatsCrawl::DrawLine( Line_t &line, int alignment, int offset )
{
	int x=0;switch(alignment){case 0:case 3:x=m_screenWidth/2-line.leftWidth-8;break;case 1:x=(m_screenWidth-line.leftWidth)/2;break;case 2:x=m_screenWidth/2+line.rightWidth+16;break;}
	line.color[3]=(unsigned char)m_textAlpha;vgui::surface()->DrawSetTextColor(line.color);vgui::surface()->DrawSetTextPos(offset+x,FInt(line.y));vgui::surface()->DrawPrintText(line.left,V_wcslen(line.left));
	if(line.right[0]){if(alignment==0||alignment==1)Warning("Unsupported stat alignment\n");else if(alignment==2||alignment==3)x=m_screenWidth/2+8;vgui::surface()->DrawSetTextPos(offset+x,FInt(line.y));vgui::surface()->DrawPrintText(line.right,V_wcslen(line.right));}
}
void CStatsCrawl::DrawRow( Row_t &row, bool last )
{
	vgui::HFont font=GetFont(row.font);int height=vgui::surface()->GetFontTall(font);
	row.title.draw=FGe(row.title.y,(float)-height)&&FGe((float)m_screenHeight,row.title.y);
	float elapsed=FSub(gpGlobals->curtime,m_scrollStart),speed=FDiv(FMul((float)m_screenHeight,m_scrollScreens),m_scrollTime);
	if(FLt(elapsed,2.0f))speed=FMul(ClampOrdered(FMul(elapsed,0.5f)),speed);
	if(last)
	{
		if(m_lastRowStopped)
		{
			if(FGe(gpGlobals->curtime,m_titleDeadline)&&m_textAlpha>0)
			{
				m_textAlpha=FInt(FSub((float)m_textAlpha,FMul(FMul(m_scrollTime,2.0f),gpGlobals->frametime)));if(m_textAlpha<0)m_textAlpha=0;row.title.color[3]=(unsigned char)m_textAlpha;
				if(m_textAlpha==0){row.title.draw=false;engine->ClientCmd("outtro_stats_done");return;}
			}
		}
		else
		{
			if(m_scrollStarted)row.title.y=FSub(row.title.y,FMul(gpGlobals->frametime,speed));
			if(FInt(row.title.y)+height/2<=m_screenHeight/2){m_lastRowStopped=true;m_titleDeadline=m_type==1?gpGlobals->curtime:FAdd(gpGlobals->curtime,5.0f);}
		}
	}
	else if(m_scrollStarted)
	{
		// USER DECISION: native count retained; at most4 real subrow reads/writes.
		float change=FMul(gpGlobals->frametime,speed);row.title.y=FSub(row.title.y,change);for(int i=0;i<MIN(row.count,4);++i)row.players[i].y=FSub(row.players[i].y,change);
	}
	if(!row.title.draw&&!row.count)return;
	vgui::surface()->DrawSetTextFont(font);DrawLine(row.title,row.title.right[0]?3:row.count?0:1,0);
	// USER DECISION: only the native Row5228's4 stored lines, never a count-driven OOB read.
	int maximum=0;for(int i=0;i<MIN(row.count,4);++i)if(maximum<=row.players[i].rightWidth)maximum=row.players[i].rightWidth;
	for(int i=0;i<MIN(row.count,4);++i)
	{
		Line_t &line=row.players[i];int x=maximum-line.rightWidth;line.color[3]=(unsigned char)m_textAlpha;vgui::surface()->DrawSetTextColor(line.color);
		vgui::surface()->DrawSetTextPos(x+m_screenWidth/2+line.rightWidth+16,FInt(line.y));vgui::surface()->DrawPrintText(line.left,V_wcslen(line.left));
		if(line.right[0]){vgui::surface()->DrawSetTextPos(x+m_screenWidth/2+8,FInt(line.y));vgui::surface()->DrawPrintText(line.right,V_wcslen(line.right));}
	}
}
void CStatsCrawl::DrawSkipLegend()
{
	if(!FGe(gpGlobals->curtime,m_skippableAt))return;
	int width,height;vgui::surface()->GetScreenSize(width,height);int x=width-m_iSkipLegendInsetX,y=height-m_iSkipLegendInsetY;
	int humans=0;C_TerrorPlayerResource *resource=CrawlResource();
	for(int i=1;i<=gpGlobals->maxClients;++i){C_TerrorPlayer *player=ToTerrorPlayer(UTIL_PlayerByIndex(i));if(player&&resource->IsConnected(i)&&!player->IsBot())++humans;}
	const bool controller=g_pInputSystem->IsSteamControllerActive();
	// Accepted capability boundary: ordinary PC/no-Steam-Input-controller
	// follows the native keyboard tokens and never queries an action origin.
	// Controller-present sized/styled GLYPH presentation is omitted; native
	// localized controller text and real SkipOuttro key commands remain. No
	// guessed action-name mapping, legacy-glyph adapter, texture or fake origin.
	const char *token;
	if(humans>1)token=controller?"#L4D_vote_skip_outtro":"#L4D_vote_skip_outtro_pc";
	else{if(m_skipVoted)return;token=controller?"#L4D360UI_ReturnToLobby":"#L4D_vote_skip_outtro_pc_singleplayer";}
	const wchar_t *text=TerrorFindLocalizedTextSafe(token);int textWidth,textHeight;vgui::surface()->GetTextSize(m_hSkipLabelFont,text,textWidth,textHeight);
	// Native sets DRAW color, NOT TEXT color; legend inherits the last row's tint/alpha.
	vgui::surface()->DrawSetColor(255,255,255,255);vgui::surface()->DrawSetTextFont(m_hSkipLabelFont);vgui::surface()->DrawSetTextPos(x-textWidth,y);vgui::surface()->DrawPrintText(text,V_wcslen(text));
}
void CStatsCrawl::Paint()
{
	if(m_backgroundAlpha<255)
	{
		const float left=FSub(m_backgroundDeadline,gpGlobals->curtime);
		float value;
		if(FEq(m_backgroundFadeIn,0.0f)) value=FGe(left,0.0f)?255.0f:0.0f;
		else
		{
			// Native1E30 is entirely SSE, NOT2CC30's x87 endpoint arithmetic.
			value=FSub(FMul(ClampOrdered(FDiv(FSub(left,m_backgroundFadeIn),-m_backgroundFadeIn)),383.0f),128.0f);
			if(FLt(value,0.0f))value=0.0f; // COMISS/JA does NOT clamp unordered.
		}
		m_backgroundAlpha=FInt(value);m_textAlpha=FInt(value);
	}
	vgui::surface()->DrawSetColor(0,0,0,m_backgroundAlpha);vgui::surface()->DrawFilledRect(0,0,m_screenWidth,m_screenHeight);DrawTitle();
	if(!m_scrollStarted&&FGt(gpGlobals->curtime,m_scrollStart))m_scrollStarted=true;
	FOR_EACH_VEC(m_rows,i)DrawRow(m_rows[i],i==m_rows.Count()-1);
	if(m_rows.Count()&&m_type==0)DrawSkipLegend();
}
