//========= Copyright Valve Corporation, All rights reserved. ============//
// Complete C2B5300..C2B77A6 presentation family. C2B6320 owns the single
// direct C242F10 statistics call before HUD admission; no second message hook.
#include "cbase.h"
#include "tier1/fmtstr.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/stats/c_l4d_gamestats.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/AnimationController.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static ConVar hud_deathnotice_bots( "hud_deathnotice_bots", "1", 0x0000000A, "Show death notices for bots" );
static ConVar hud_deathnotice_threats( "hud_deathnotice_threats", "0", 0x0000000A, "Show death notices for Tanks and Threats" );
static ConVar hud_dmgrecord_persisttime_deaths( "hud_dmgrecord_persisttime_deaths", "4.0", 0x0000000A, "Time to show death related lines in the damage record." );
static ConVar hud_dmgrecord_persisttime_other( "hud_dmgrecord_persisttime_other", "2.0", 0x0000000A, "Time to show non-death related lines in the damage record." );

class CFloatingAchievementNumber : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CFloatingAchievementNumber, vgui::EditablePanel );
public:
	CFloatingAchievementNumber( int number, int x, int y, int direction, vgui::Panel *parent ) : BaseClass( parent, "FloatingAchievementNumber" ),
		m_nStartX( x ), m_nStartY( y ), m_nDirection( direction ), m_flStartTime( gpGlobals->curtime )
	{
		char text[64]; Q_snprintf( text, sizeof( text ), "+%d", number );
		m_pLabel = new vgui::Label( this, "FloatingNumberLabel", text );
	}
	virtual ~CFloatingAchievementNumber() { delete m_pLabel; }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme );
		LoadControlSettings( "resource/UI/HudAchievementFloatingNumber.res", "GAME" );
		const int height = vgui::surface()->GetFontTall( m_pLabel->GetFont() );
		SetPos( m_nStartX, (int)( (float)m_nStartY - (float)height * 0.5f ) );
		m_pLabel->SetAlpha( 0 );
		vgui::AnimationController *controller = vgui::GetAnimationController();
		controller->RunAnimationCommand( m_pLabel, "alpha", 255.0f, 0.0f, 0.30000001f, vgui::AnimationController::INTERPOLATOR_LINEAR );
		const bool horizontal = m_nDirection == 2 || m_nDirection == 3;
		const int target = horizontal ? m_nStartX + ( m_nDirection == 3 ? m_iScrollDistance : -m_iScrollDistance ) :
			m_nStartY + ( m_nDirection == 1 ? m_iScrollDistance : -m_iScrollDistance );
		controller->RunAnimationCommand( this, horizontal ? "xpos" : "ypos", (float)target, 0.0f, 2.0f, vgui::AnimationController::INTERPOLATOR_LINEAR );
	}
	virtual void OnThink() OVERRIDE
	{
		const float now = gpGlobals->curtime;
		if ( now <= m_flStartTime + 1.0f )
		{
			if ( m_flStartTime <= now ) return;
		}
		else
		{
			if ( m_pLabel->GetAlpha() >= 255 )
			{
				m_pLabel->SetAlpha( 254 );
				vgui::GetAnimationController()->RunAnimationCommand( m_pLabel, "alpha", 0.0f, 0.0f, 1.0f, vgui::AnimationController::INTERPOLATOR_LINEAR );
				return;
			}
			if ( m_pLabel->GetAlpha() > 0 ) return;
		}
		MarkForDeletion(); SetVisible( false );
	}
private:
	vgui::Label *m_pLabel;
	int m_nStartX, m_nStartY, m_nDirection;
	float m_flStartTime;
	CPanelAnimationVarAliasType( int, m_iScrollDistance, "ScrollDistance", "40", "proportional_int" );
};

class CHudPZDamageRecordPanel : public vgui::EditablePanel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudPZDamageRecordPanel, vgui::EditablePanel );
public:
	CHudPZDamageRecordPanel( const char *name ) : BaseClass( g_pClientMode->GetViewport(), "HudPZDamageRecord" ), CHudElement( name )
	{
		// Both native fullscreen/current roots project onto the approved single viewport.
		for ( int i = 0; i < 5; ++i ) { m_records[i].label = NULL; m_records[i].type = m_records[i].count = 0; }
		m_pBackground = NULL; m_nActiveCount = 0; m_bCompactLayout = false;
		m_nTextAlignment = -1; m_nScrollDirection = 2;
		Reset(); vgui::ivgui()->AddTickSignal( GetVPanel(), 50 );
	}
	virtual void Init() OVERRIDE;
	virtual void Reset() OVERRIDE
	{
		for ( int i = 0; i < 5; ++i ) { m_records[i].attacker = NULL; m_records[i].victim = NULL; m_records[i].deadline = -1.0f; }
		m_nActiveCount = 0; // Native leaves label visibility/type/count until their ordinary tick.
	}
	virtual bool ShouldDraw() OVERRIDE
	{
		vgui::Panel *vote = g_pClientMode->GetViewport()->FindChildByName( "CHudVote" );
		return !( vote && vote->IsVisible() ) && m_nActiveCount > 0 && CHudElement::ShouldDraw();
	}
	void MsgFunc_PZDmgMsg( bf_read &message );
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE
	{
		BaseClass::ApplySettings( settings );
		static const char *const alignments[9] = { "north-west", "north", "north-east", "west", "center", "east", "south-west", "south", "south-east" };
		m_nTextAlignment = -1; m_nScrollDirection = 2;
		const char *alignment = settings->GetString( "label_textalign", "" );
		for ( int i = 0; i < 9; ++i ) if ( !Q_stricmp( alignment, alignments[i] ) )
		{ m_nTextAlignment = i; if ( i == 0 || i == 3 || i == 6 ) m_nScrollDirection = 3; break; }
	}
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( "Resource/UI/HUD/PZDamageRecordPanel.res", "GAME" );
		// Native vertical-split mutation is unreachable under the single-slot contract.
		for ( int i = 0; i < 5; ++i )
		{
			m_records[i].label = dynamic_cast<vgui::Label *>( FindChildByName( CFmtStr( "recordlabel%d", i ) ) );
			if ( m_records[i].label && m_nTextAlignment != -1 ) m_records[i].label->SetContentAlignment( (vgui::Label::Alignment)m_nTextAlignment );
		}
		m_pBackground = FindChildByName( "label4background" );
		BaseClass::ApplySchemeSettings( scheme );
		OnTick();
	}
	virtual void OnTick() OVERRIDE
	{
		CTerrorGameRules *rules = TerrorGameRules();
		if ( rules )
		{
			const bool compact = rules->IsSurvivalMode() || ( rules->IsScavengeMode() && !rules->IsInIntro() );
			if ( m_bCompactLayout != compact ) { m_bCompactLayout = compact; InvalidateLayout( false, true ); }
		}
		for ( int i = 0; i < 5; ++i )
		{
			Record_t &record = m_records[i];
			if ( !record.label || !record.label->IsVisible() ) continue;
			if ( !( record.deadline >= 0.0f ) )
			{
				record.label->SetVisible( false ); if ( i == 0 && m_pBackground ) m_pBackground->SetVisible( false ); continue;
			}
			const float remaining = record.deadline - gpGlobals->curtime;
			if ( !( remaining <= 0.0f ) ) continue;
			float fraction = remaining * -2.0f;
			if ( fraction < 0.0f ) fraction = 0.0f; else if ( fraction > 1.0f ) fraction = 1.0f;
			const int alpha = (int)( ( 1.0f - fraction ) * 255.0f );
			if ( alpha <= 0 )
			{
				record.label->SetVisible( false ); record.count = record.type = 0; record.deadline = -1.0f; record.attacker = NULL; record.victim = NULL;
				if ( i == 0 && m_pBackground ) m_pBackground->SetVisible( false );
				const int count = (int)( (unsigned int)m_nActiveCount - 1u ); m_nActiveCount = count <= 0 ? 0 : count;
			}
			else { record.label->SetAlpha( alpha ); if ( i == 0 && m_pBackground ) m_pBackground->SetAlpha( alpha ); }
		}
	}
private:
	struct Record_t { EHANDLE attacker, victim; vgui::Label *label; int type, count; float deadline; };
	int FindRecord( C_BasePlayer *attacker, C_BasePlayer *victim );
	void AddRecord( int type, C_BasePlayer *attacker, C_BasePlayer *victim, C_BasePlayer *assistant = NULL, int amount = 0 );
	static C_TerrorPlayer *PlayerForUserID( int userid ) { return ToTerrorPlayer( UTIL_PlayerByIndex( engine->GetPlayerForUserID( userid ) ) ); }
	static const wchar_t *PlayerName( C_BasePlayer *player, wchar_t *buffer, int bytes );
	Record_t m_records[5];
	vgui::Panel *m_pBackground;
	int m_nActiveCount, m_nTextAlignment, m_nScrollDirection;
	bool m_bCompactLayout;
};
DECLARE_HUDELEMENT( CHudPZDamageRecordPanel );
DECLARE_HUD_MESSAGE( CHudPZDamageRecordPanel, PZDmgMsg );
void CHudPZDamageRecordPanel::Init()
{
	HOOK_HUD_MESSAGE( CHudPZDamageRecordPanel, PZDmgMsg );
	ListenForGameEvent( "player_death" ); ListenForGameEvent( "player_incapacitated" ); ListenForGameEvent( "survival_goal_reached" );
	ListenForGameEvent( "defibrillator_used" ); ListenForGameEvent( "versus_marker_reached" );
}
void CHudPZDamageRecordPanel::MsgFunc_PZDmgMsg( bf_read &message )
{
	const int type = message.ReadByte();
	C_TerrorPlayer *attacker = PlayerForUserID( message.ReadShort() );
	C_TerrorPlayer *victim = PlayerForUserID( message.ReadShort() );
	C_TerrorPlayer *assistant = PlayerForUserID( message.ReadShort() );
	const int amount = message.ReadShort();
	AddRecord( type, attacker, victim, assistant, amount );
}
int CHudPZDamageRecordPanel::FindRecord( C_BasePlayer *attacker, C_BasePlayer *victim )
{
	if ( !attacker ) return -1;
	for ( int i = 1; i < 5; ++i ) if ( m_records[i].attacker.Get() == attacker && m_records[i].victim.Get() == victim ) return i;
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	int selected = 1;
	for ( ; selected < 5; ++selected )
	{
		C_BaseEntity *actor = m_records[selected].attacker.Get();
		if ( !actor || !local || actor->GetTeamNumber() != local->GetTeamNumber() ) break;
	}
	if ( selected == 5 )
	{
		if ( !local || !local->IsPlayer() || local != attacker ) return -1;
		for ( selected = 1; selected < 5 && m_records[selected].attacker.Get() == local; ++selected ) {}
		if ( selected == 5 ) return -1;
	}
	m_records[selected].victim = victim; m_records[selected].attacker = attacker; m_records[selected].type = 0;
	return selected;
}
const wchar_t *CHudPZDamageRecordPanel::PlayerName( C_BasePlayer *player, wchar_t *buffer, int bytes )
{
	const wchar_t *localized = NULL;
	C_TerrorPlayer *terror = ToTerrorPlayer( player );
	if ( player->GetFlags() & FL_FAKECLIENT )
	{
		if ( player->GetTeamNumber() == TEAM_INFECTED && terror )
		{
			static const char *const classes[8] = { "#L4D_class_smoker_name", "#L4D_class_boomer_name", "#L4D_class_hunter_name", "#L4D_class_spitter_name", "#L4D_class_jockey_name", "#L4D_class_charger_name", "#L4D_class_witch_name", "#L4D_class_tank_name" };
			const int zombie = terror->GetZombieClass(); localized = g_pVGuiLocalize->Find( zombie >= 1 && zombie <= 8 ? classes[zombie - 1] : "NORMAL" );
		}
		else if ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == 4 )
		{
			C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( terror );
			if ( bot && bot->GetHumanSpectatorUserID() <= 0 ) localized = g_pVGuiLocalize->Find( GetTerrorSurvivorDisplayNameToken( terror->GetSurvivorCharacter() ) );
		}
	}
	if ( localized ) return localized;
	g_pVGuiLocalize->ConvertANSIToUnicode( player->GetPlayerName(), buffer, bytes ); return buffer;
}
void CHudPZDamageRecordPanel::AddRecord( int type, C_BasePlayer *attacker, C_BasePlayer *victim, C_BasePlayer *assistant, int amount )
{
	if ( !attacker || !victim ) return;
	TerrorRecordPZDamageStats( type, attacker, victim );
	const int index = type > 6 ? FindRecord( attacker, victim ) : 0;
	if ( index == -1 || type < 1 || type > 23 ) return;
	Record_t &record = m_records[index]; if ( !record.label ) return;
	CTerrorGameRules *rules = TerrorGameRules();
	const bool pz = rules && rules->IsPlayerControlledZombies();
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( local && local == attacker && pz ) local->EmitSound( "Hint.LittleReward" );
	if ( type < record.type && record.label->IsVisible() ) return;
	if ( record.type == type && record.victim.Get() == victim ) record.count = (int)( (unsigned int)record.count + (unsigned int)( amount ? amount : 1 ) );
	else { record.type = type; record.count = amount ? amount : 1; record.victim = victim; }
	static const char *const tokens[24] = { "", "#L4D_KILLED", "#L4D_INCAPACITATE", "#L4D_KILLED", "#L4D_DEFIBRILLATOR_USED", "#L4D_VS_REACHED_MARKER", "#L4D_SCAVENGE_DESTROY_GASCAN",
		"#L4D_HIT_ASSIST", "#L4D_HIT_ASSIST_ATTACK", "#L4D_HIT_ATTACK", "#L4D_HIT_PUSH_ASSIST_ATTACK", "#L4D_HIT_PUSH_ATTACK", "#L4D_HIT_POUNCE_ATTACK", "#L4D_INCAPACITATE_ASSIST", "#L4D_INCAPACITATE",
		"#L4D_OnAwardSaved", "#L4D_OnAwardSharing", "#L4D_OnAwardSharingAdrenaline", "#L4D_OnAwardProtector", "#L4D_OnAwardRescuer", "#L4D_OnAwardMedic", "#L4D_BronzeMedalEarned", "#L4D_SilverMedalEarned", "#L4D_GoldMedalEarned" };
	const wchar_t *format = g_pVGuiLocalize->Find( tokens[type] );
	if ( !format ) return; // Native missing-format UB: skip only dependent formatting/display.
	wchar_t attackerBuffer[128], victimBuffer[128], assistantBuffer[128], countText[128], text[256];
	const wchar_t *attackerName = PlayerName( attacker, attackerBuffer, sizeof( attackerBuffer ) );
	const wchar_t *victimName = PlayerName( victim, victimBuffer, sizeof( victimBuffer ) );
	char number[8];
	if ( pz ) { Q_snprintf( number, sizeof( number ), amount ? "%d" : " x%d", record.count ); g_pVGuiLocalize->ConvertANSIToUnicode( number, countText, sizeof( countText ) ); }
	else countText[0] = 0;
	if ( assistant ) g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 3, attackerName, victimName, PlayerName( assistant, assistantBuffer, sizeof( assistantBuffer ) ) );
	else if ( type == 5 ) g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, countText );
	else if ( amount ) g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 3, attackerName, victimName, countText );
	else g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 2, attackerName, victimName );
	if ( !amount && record.count > 1 && type > 6 )
	{
		const int length = V_wcslen( text ), extra = V_wcslen( countText );
		if ( length + extra < ARRAYSIZE( text ) ) V_wcsncpy( text + length, countText, sizeof( text ) - length * sizeof( wchar_t ) );
		// Native wcsncat bound511 can overflow text[256]; only that OOB append is absent.
	}
	record.label->SetText( text ); record.label->SetVisible( true ); record.label->SetAlpha( 255 );
	record.deadline = gpGlobals->curtime + ( index ? hud_dmgrecord_persisttime_other.GetFloat() : hud_dmgrecord_persisttime_deaths.GetFloat() );
	m_nActiveCount = (int)( (unsigned int)m_nActiveCount + 1u ); // Native counts updates, NOT visible rows.
	int contentWide, contentTall, x, y, wide, tall;
	record.label->GetContentSize( contentWide, contentTall ); record.label->GetBounds( x, y, wide, tall );
	if ( index == 0 && m_pBackground )
	{
		const int margin = (int)( (float)( (float)ScreenWidth() * 0.0015625f ) * 16.0f );
		m_pBackground->SetVisible( true ); m_pBackground->SetAlpha( 255 ); m_pBackground->SetWide( margin + contentWide );
		const int by = (int)( (double)y - (double)ScreenHeight() * 0.002083333333333333 * 5.0 );
		int bx;
		if ( m_nTextAlignment == 3 ) bx = (int)( (double)x - (double)margin * 0.5 );
		else if ( m_nTextAlignment == 5 ) bx = (int)( (double)( x - contentWide + record.label->GetWide() ) - (double)margin * 0.5 );
		else bx = (int)( (double)( record.label->GetWide() - margin - contentWide ) * 0.5 + (double)x );
		m_pBackground->SetPos( bx, by );
	}
	if ( pz && !amount && record.count > 1 )
	{
		int parentX, parentY; GetPos( parentX, parentY );
		switch ( m_nScrollDirection )
		{
		case 1: y += tall; x = (int)( (float)wide * 0.5f + (float)x ); break;
		case 2:
			y = (int)( (float)tall * 0.5f + (float)y );
			x = (int)( ( m_nTextAlignment == 4 ? (double)( wide - contentWide ) * 0.5 + (double)x : (double)( wide + x - contentWide ) ) - (double)ScreenWidth() * 0.0015625 * 10.0 ); break;
		case 3:
			x = m_nTextAlignment == 4 ? (int)( (double)contentWide * 0.5 + (double)x ) : x + contentWide;
			y = (int)( (float)tall * 0.5f + (float)y ); break;
		default: x = (int)( (float)wide * 0.5f + (float)x ); break;
		}
		new CFloatingAchievementNumber( 1, x + parentX, y + parentY, m_nScrollDirection, GetParent() );
	}
}
void CHudPZDamageRecordPanel::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "defibrillator_used" ) )
	{
		C_TerrorPlayer *actor = PlayerForUserID( event->GetInt( "userid" ) ), *subject = PlayerForUserID( event->GetInt( "subject" ) );
		if ( actor && subject ) AddRecord( 4, actor, subject ); return;
	}
	if ( !Q_stricmp( name, "versus_marker_reached" ) )
	{
		C_TerrorPlayer *player = PlayerForUserID( event->GetInt( "userid" ) ); AddRecord( 5, player, player, NULL, event->GetInt( "marker" ) );
		C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); if ( local ) local->EmitSound( "Survival.team_record" ); return;
	}
	const bool victimBot = event->GetBool( "victimisbot" );
	if ( victimBot && !hud_deathnotice_bots.GetBool() ) return;
	const char *victimName = event->GetString( "victimname", "" );
	if ( !hud_deathnotice_threats.GetBool() && ( !Q_strcmp( victimName, "Tank" ) || !Q_strcmp( victimName, "Witch" ) ) ) return;
	if ( event->GetBool( "abort" ) ) return;
	if ( !Q_stricmp( name, "survival_goal_reached" ) )
	{
		if ( Q_stricmp( event->GetString( "goalname", "" ), "medal" ) ) return;
		const int medal = event->GetInt( "id" ); C_TerrorPlayer *player = PlayerForUserID( event->GetInt( "userid" ) );
		if ( medal == 0 || medal == 1 || medal == 2 ) AddRecord( 21 + medal, player, player ); return;
	}
	C_TerrorPlayer *victim = PlayerForUserID( event->GetInt( "userid" ) ), *attacker = PlayerForUserID( event->GetInt( "attacker" ) );
	if ( !victim || !attacker || victim == attacker ) return;
	if ( victimBot && event->GetBool( "attackerisbot" ) && victim->GetTeamNumber() == TEAM_INFECTED && attacker->GetTeamNumber() == TEAM_INFECTED ) return;
	const bool survivor = victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == 4;
	const bool incapacitated = !Q_strcmp( name, "player_incapacitated" );
	if ( survivor ) AddRecord( incapacitated ? 2 : 3, attacker, victim );
	else if ( !incapacitated ) AddRecord( 1, attacker, victim );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
