//========= Copyright Valve Corporation, All rights reserved. ============//
// Native C294DD0..C295E5E. New HUD element only; existing player/entity owners.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/c_survivor_death_model.h"
#include "l4d2/entities/c_point_script_use_target.h"
#include "l4d2/entities/c_buttons.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "game_controls/IconPanel.h"
#include "vgui/ILocalize.h"
#include <vgui/ISurface.h>
#include <vgui_controls/EditablePanel.h>
#include "l4d2/ui/terror_progress_bar.h"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static ConVar cl_drawprogressbar( "cl_drawprogressbar", "1", 0x0000000A, "Draw progress bar when healing" );


class CHudProgressBar : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudProgressBar, vgui::EditablePanel );
public:
	CHudProgressBar( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudProgressBar" )
	{
		SetHiddenBits( HIDEHUD_WEAPONSELECTION | HIDEHUD_PLAYERDEAD );
		m_pBar = new ProgressBarPanel( this, "Bar" );
		m_pAward = NULL;
	}
	virtual bool ShouldDraw() OVERRIDE
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		// C295410 deliberately does not call CHudElement::ShouldDraw.
		return player && player->IsProgressBarActive() && player->m_lifeState != LIFE_DYING && cl_drawprogressbar.GetInt() != 0;
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( "Resource/UI/HUD/ProgressBar.res", "GAME" );
		BaseClass::ApplySchemeSettings( scheme );
		m_pAward = dynamic_cast< CIconPanel * >( FindChildByName( "AwardIcon" ) );
	}
	virtual void OnThink() OVERRIDE
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		if ( player && player->m_flProgressBarDuration > 0.0f )
		{
			// Local player simulation clock, NOT global curtime or GetProgressBarProgress's double chain.
			float fraction = ( player->GetTimeBase() - player->m_flProgressBarStartTime ) / player->m_flProgressBarDuration;
			if ( fraction > 1.0f ) fraction = 1.0f;
			else if ( fraction < 0.0f ) fraction = 0.0f;
			m_pBar->SetProgress( fraction );
		}
	}
	virtual void Paint() OVERRIDE;
private:
	// Native 102954B0 writes the named controls, not dialog substitutions.
	void SetLabel( const char *text )
	{
		SetControlString( "BarLabel", text );
	}
	void ClearSubtext()
	{
		SetControlString( "Subtext", "" );
	}
	void SetIcon( const char *name ) { if ( m_pAward ) m_pAward->SetIcon( name ); }
	void SetPlayerSubtext( const char *token, C_TerrorPlayer *player, bool allowDormant = false )
	{
		if ( !player || ( !allowDormant && player->IsDormant() ) ) return;
		wchar_t name[32], message[128];
		GetTerrorHudPlayerName( player, name, sizeof( name ) );
		const wchar_t *format = g_pVGuiLocalize->Find( token );
		if ( !format ) return; // native missing localization is undefined; minimal skip of the write.
		g_pVGuiLocalize->ConstructString( message, sizeof( message ), format, 1, name );
		SetControlString( "Subtext", message );
	}
	ProgressBarPanel *m_pBar;
	CIconPanel *m_pAward;
};
DECLARE_HUDELEMENT( CHudProgressBar );

void CHudProgressBar::Paint()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return;
	// Revive handles have strict precedence over the action discriminator.
	if ( player->m_reviveTarget.Get() )
	{
		C_TerrorPlayer *target = ToTerrorPlayer( player->m_reviveTarget.Get() );
		SetLabel( target && target->m_isHangingFromLedge ? "#L4D_progress_helping_friend_up" : "#L4D_progress_reviving_friend" );
		ClearSubtext(); SetIcon( "icon_reviving" ); return;
	}
	if ( player->m_reviveOwner.Get() )
	{
		SetLabel( player->m_isHangingFromLedge ? "#L4D_progress_being_helped_up" : "#L4D_progress_being_revived" );
		C_TerrorPlayer *owner = ToTerrorPlayer( player->m_reviveOwner.Get() );
		if ( !owner || owner->IsDormant() ) ClearSubtext();
		else SetPlayerSubtext( "#L4D_progress_reviving_player", owner );
		SetIcon( "icon_reviving" ); return;
	}
	const int action = player->m_iCurrentUseAction;
	switch ( action )
	{
	case 1:
		if ( player->IsUseActionTarget() )
		{
			// Missing/dormant owner intentionally leaves previous subtext unchanged.
			SetPlayerSubtext( "#L4D_progress_healing_player", ToTerrorPlayer( player->m_useActionOwner.Get() ) );
			SetLabel( "#L4D_progress_being_healed" );
		}
		else if ( player->IsPerformingOtherUseAction() )
		{
			C_TerrorPlayer *target = ToTerrorPlayer( player->m_useActionTarget.Get() );
			if ( target && !target->IsDormant() ) SetPlayerSubtext( "#L4D_progress_healing_target", target );
			else ClearSubtext();
			SetLabel( "#L4D_progress_heal_friend" );
		}
		else { SetLabel( "#L4D_progress_heal" ); ClearSubtext(); }
		SetIcon( "icon_healing" ); return;
	case 2:
		if ( player->IsUseActionTarget() ) SetLabel( "#L4D_progress_being_given_ammo" );
		else if ( player->IsPerformingOtherUseAction() )
		{
			C_TerrorPlayer *target = ToTerrorPlayer( player->m_useActionTarget.Get() );
			if ( target && !target->IsDormant() ) SetPlayerSubtext( "#L4D_progress_giving_ammo_target", target );
			else ClearSubtext();
			// There is NO BarLabel assignment in this native branch.
		}
		else SetLabel( "#L4D_progress_giving_ammo_to_self" );
		ClearSubtext(); // Native unconditional final clear even after constructing target text.
		SetIcon( "icon_ammo" ); return;
	case 8: SetLabel( "#L4D_progress_using_gas_can" ); ClearSubtext(); SetIcon( "icon_gas_can" ); return;
	case 9: SetLabel( "#L4D_progress_using_cola_bottles_alt1" ); ClearSubtext(); SetIcon( "icon_cola_bottles" ); return;
	case 6: SetLabel( "#L4D_progress_open_upgrade_pack_incendiary" ); ClearSubtext(); SetIcon( "icon_incendiary_ammo" ); return;
	case 7: SetLabel( "#L4D_progress_open_upgrade_pack_explosive" ); ClearSubtext(); SetIcon( "icon_explosive_ammo" ); return;
	case 10:
		{
			C_ButtonTimed *button = dynamic_cast< C_ButtonTimed * >( player->m_useActionTarget.Get() );
			SetLabel( button ? button->GetUseString() : "" );
			SetControlString( "Subtext", button ? button->GetUseSubString() : "" );
			SetIcon( "icon_button" ); return;
		}
	case 11:
		{
			// Native weak self-handle re-read (293CC0/295390), no alternate target.
			C_PointScriptUseTarget *target = dynamic_cast< C_PointScriptUseTarget * >( player->m_useActionTarget.Get() );
			SetLabel( target ? target->GetUseString() : "" );
			SetControlString( "Subtext", target ? target->GetUseSubString() : "" );
			SetIcon( "icon_button" ); return;
		}
	case 4:
		if ( !player->IsPerformingOtherUseAction() ) return;
		{
			C_SurvivorDeathModel *body = dynamic_cast< C_SurvivorDeathModel * >( player->m_useActionTarget.Get() );
			if ( body )
			{
				C_TerrorPlayer *survivor = body->GetSurvivor();
				if ( !survivor ) return; // Native returns before label/icon too.
				SetPlayerSubtext( "#L4D_progress_defibrillator_target", survivor, true );
			}
			SetLabel( "#L4D_progress_defibrillator" ); SetIcon( "icon_defibrillator" ); return;
		}
	default:
		return; // Genuine native stale display for unspecified action values.
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
