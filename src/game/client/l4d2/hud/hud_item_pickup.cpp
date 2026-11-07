//========= Copyright Valve Corporation, All rights reserved. ============//
// Whole C299AE0..C29A3AC. Not the neighboring IntensityGraph class/gate.
#include "cbase.h"
#include "tier1/fmtstr.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/hud/hud_terror_weapon_selection.h"
#include "game_controls/IconPanel.h"
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/AnimationController.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

class CItemPickupPanel : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CItemPickupPanel, vgui::EditablePanel );
public:
	CItemPickupPanel( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), name ), m_flNextAmmoPickup( 0.0f ), m_pAmmoIcon( NULL )
	{
		for ( int i = 0; i < 3; ++i )
		{
			m_pImages[i] = new CIconPanel( this, CFmtStr( "image%d", i + 1 ) );
			m_pBackgrounds[i] = new CIconPanel( this, CFmtStr( "bg%d", i + 1 ) );
			m_pImages[i]->SetVisible( false ); m_pBackgrounds[i]->SetVisible( false ); m_flLastPickup[i] = 0.0f;
		}
		SetHiddenBits( HIDEHUD_PLAYERDEAD );
	}
	virtual void Init() OVERRIDE
	{
		ListenForGameEvent( "weapon_pickup" ); ListenForGameEvent( "ammo_pickup" ); ListenForGameEvent( "ammo_pack_used" );
		ListenForGameEvent( "round_end" ); ListenForGameEvent( "round_start" ); ListenForGameEvent( "map_transition" ); ListenForGameEvent( "game_newmap" );
	}
	virtual void Reset() OVERRIDE
	{
		for ( int i = 0; i < 3; ++i ) { if ( m_pImages[i] ) m_pImages[i]->SetVisible( false ); if ( m_pBackgrounds[i] ) m_pBackgrounds[i]->SetVisible( false ); m_flLastPickup[i] = 0.0f; }
		m_flNextAmmoPickup = 0.0f;
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CleanupItemPickup" );
	}
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		const char *name = event->GetName();
		if ( !Q_strcmp( name, "weapon_pickup" ) )
		{
			if ( event->GetInt( "context" ) != 0 ) return; // Supported native split-screen slot0.
			CHudTexture *icon = IconForWeapon( event->GetInt( "weaponid" ) );
			if ( !icon ) return;
			ShowPickup( icon ); PulseSelection( event->GetInt( "weaponslot" ) ); return;
		}
		const bool ammo = !Q_strcmp( name, "ammo_pickup" ), pack = !Q_strcmp( name, "ammo_pack_used" );
		if ( ammo || pack )
		{
			C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
			C_BasePlayer *subject = UTIL_PlayerByIndex( engine->GetPlayerForUserID( event->GetInt( ammo ? "userid" : "subject" ) ) );
			if ( local && local == subject && gpGlobals->curtime > m_flNextAmmoPickup )
			{
				ShowPickup( m_pAmmoIcon ); PulseSelection( 0 ); m_flNextAmmoPickup = gpGlobals->curtime + 5.0f;
			}
			return;
		}
		if ( !Q_strcmp( name, "round_end" ) || !Q_strcmp( name, "round_start" ) || !Q_strcmp( name, "map_transition" ) || !Q_strcmp( name, "game_newmap" ) ) Reset();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme ); LoadControlSettings( "Resource/UI/HUD/ItemPickup.res", "GAME" ); m_pAmmoIcon = gHUD.GetIcon( "icon_ammo" );
	}
private:
	static CHudTexture *IconForWeapon( int id )
	{
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( id );
		if ( !info || info->iSlot == 0 ) return NULL;
		if ( info->iSlot == 1 && id != TERROR_WEAPON_PISTOL ) return NULL;
		if ( id == TERROR_WEAPON_PISTOL )
		{
			C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
			if ( !local ) return NULL;
			CTerrorWeapon *pistol = local->GetWeaponOfType( id ); // Sole actual26ACA0 index-cache provider.
			if ( !pistol || !pistol->IsDualWielding() ) return NULL;
		}
		return info->iconActive;
	}
	static void PulseSelection( int slot )
	{
		CHudWeaponSelection *selection = dynamic_cast<CHudWeaponSelection *>( GetHudWeaponSelection() );
		if ( selection ) selection->PulsePickedUpWeaponSlot( slot );
	}
	void ShowPickup( CHudTexture *icon )
	{
		const float now = gpGlobals->curtime;
		float earliest = now + 1.0f; int selected = -1;
		for ( int i = 0; i < 3; ++i )
		{
			if ( !( now <= m_flLastPickup[i] + 2.5f ) ) { selected = i; break; }
			if ( earliest > m_flLastPickup[i] ) { earliest = m_flLastPickup[i]; selected = i; }
		}
		// C299CE0 can retain-1 with entirely future timestamps. Omit only those
		// actual out-of-array accesses; event pulse/lockout behavior remains.
		if ( selected >= 0 )
		{
			if ( m_pImages[selected] ) { m_pImages[selected]->SetVisible( true ); m_pImages[selected]->SetIcon( icon ); }
			if ( m_pBackgrounds[selected] ) m_pBackgrounds[selected]->SetVisible( true );
		}
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, CFmtStr( "StartItemPickup%d", selected + 1 ) );
		if ( selected >= 0 ) m_flLastPickup[selected] = gpGlobals->curtime;
	}
	float m_flLastPickup[3], m_flNextAmmoPickup;
	CIconPanel *m_pImages[3], *m_pBackgrounds[3];
	CHudTexture *m_pAmmoIcon;
};
DECLARE_HUDELEMENT( CItemPickupPanel );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
