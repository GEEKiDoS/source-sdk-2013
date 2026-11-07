//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_TERROR_STATSCRAWL_H
#define HUD_TERROR_STATSCRAWL_H
#ifdef _WIN32
#pragma once
#endif
#include "hudelement.h"
#include "tier1/utlvector.h"
#include "l4d2/stats/terror_client_stats.h"
#include <vgui_controls/EditablePanel.h>

class VoteBarPanel;
class CStatsCrawl : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CStatsCrawl, vgui::EditablePanel );
public:
	CStatsCrawl( const char *name );
	virtual ~CStatsCrawl();
	virtual void Init() OVERRIDE;
	virtual void LevelInit() OVERRIDE;
	virtual void LevelShutdown() OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void Paint() OVERRIDE;
	virtual int KeyInput( int down, ButtonCode_t key, const char *binding );
	bool IsShowingCredits();
	void MsgFunc_CreditsMsg( bf_read &message );
	void MsgFunc_StatsCrawlMsg( bf_read &message );
	void MsgFunc_StatsSkipState( bf_read &message );
	void MsgFunc_JoinLateMsg( bf_read &message );
private:
	struct Font_t { vgui::HFont handle; char name[32]; };
	struct Line_t
	{
		wchar_t left[256], right[256];
		int leftWidth, rightWidth;
		Color color;
		float y;
		bool fixed, draw;
		Line_t();
	};
	struct Row_t
	{
		Line_t title;
		Line_t players[4]; // Native4; retain count, USER-adapted writes/reads never exceed4.
		int font, count;
		Row_t() : font( -1 ), count( 0 ) {}
	};
	COMPILE_TIME_ASSERT( sizeof( Row_t ) == 5228 );
	void BeginCrawl();
	void ReadParameters( KeyValues *parameters );
	void BuildCredits( KeyValues *entries );
	void BuildStats( KeyValues *entries );
	void BuildTitleAndCast();
	bool BuildStatLine( Row_t &row, KeyValues *entry );
	bool BuildStatBlock( Row_t &row, KeyValues *entry );
	int FindFont( const char *name );
	vgui::HFont GetFont( int index ) const;
	void MeasureLine( Line_t &line, vgui::HFont font );
	void ResetTitle();
	void PositionRows( bool preserveScroll );
	void DrawTitle();
	void DrawRow( Row_t &row, bool last );
	void DrawLine( Line_t &line, int alignment, int offset );
	void DrawSkipLegend();
	void PlayerName( const CTerrorPlayerStats::PlayerIdentity_t &player, wchar_t *text, int bytes, bool plainBotFallback = false ) const;
	CUtlVector< Font_t > m_fonts; // Native440.
	CUtlVector< Row_t > m_rows; //460, retained-capacity resets.
	CTerrorPlayerStats m_stats; // Native embedded collector480, ONE shared implementation.
	CTerrorPlayerStats::RankResult_t m_titleRoster; // Native684, failed rank retains prior output/count.
	int m_screenWidth, m_screenHeight;
	char m_image[64];
	int m_imageTexture;
	float m_scrollScreens;
	Color m_textColor, m_localTextColor;
	float m_scrollDelay, m_scrollStart, m_scrollTime, m_separation;
	float m_titleDeadline, m_backgroundDeadline;
	bool m_lastRowStopped, m_scrollStarted;
	int m_titleAlpha, m_textAlpha, m_backgroundAlpha;
	float m_titleStartDelay, m_titleFadeIn, m_titleVisible, m_titleFadeOut, m_maxTitleAlpha, m_backgroundFadeIn;
	int m_titlePhase, m_type; // Native676/680;0stats,1credits,2latejoin,-1inactive.
	VoteBarPanel *m_votes;
	float m_currentTitleVisible;
	bool m_skipVoted;
	float m_skippableAt;
	CPanelAnimationVar( vgui::HFont, m_hSkipLabelFont, "SkipLabelFont", "DefaultLarge" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFont, "ButtonFont", "GameUIButtons" );
	CPanelAnimationVar( vgui::HFont, m_hStatsCrawlFont, "StatsCrawlFont", "OuttroStatsCrawl" );
	CPanelAnimationVar( vgui::HFont, m_hStatsCrawlUnderlineFont, "StatsCrawlUnderlineFont", "OuttroStatsCrawlUnderline" );
	CPanelAnimationVar( vgui::HFont, m_hCreditsCrawlFont, "CreditsCrawlFont", "Credits" );
	CPanelAnimationVarAliasType( int, m_iVoteBoxInsetX, "vote_bot_inset_x", "100", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iVoteBoxInsetY, "vote_bot_inset_y", "100", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iSkipLegendInsetX, "skip_legend_inset_x", "10", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iSkipLegendInsetY, "skip_legend_inset_y", "10", "proportional_int" );
};
extern CStatsCrawl *g_pStatsCrawl; // Native107C9BA8; constructor/destructor producers.
#endif
