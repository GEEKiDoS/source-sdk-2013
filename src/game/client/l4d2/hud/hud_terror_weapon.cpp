//========= Copyright Valve Corporation, All rights reserved. ============//
// Sole native weapon redraw and selection owners.
#include "cbase.h"
#include "hud.h"
#include "hud_crosshair.h"
#include "l4d2/hud/hud_terror_weapon_selection.h"
#include "iclientmode.h"
#include "iinput.h"
#include "in_buttons.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "game_controls/baseviewport.h"
#include <vgui/ISurface.h>
#include <vgui_controls/AnimationController.h>
#include "tier0/icommandline.h"
#include <stdlib.h>
#include <emmintrin.h>
#include "l4d2/terror_native_qsort.h"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// C12CD90/C12CFB0: this is redraw, NOT the ammo or inventory owner.
class CHudWeapon : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudWeapon, vgui::Panel );
public:
	CHudWeapon( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudWeapon" )
	{
		SetHiddenBits( HIDEHUD_WEAPONSELECTION );
		SetPaintBackgroundEnabled( false );
	}
protected:
	virtual void PerformLayout() OVERRIDE
	{
		BaseClass::PerformLayout();
		SetBounds( 0, 0, GetParent()->GetWide(), GetParent()->GetTall() );
	}
	virtual void Paint() OVERRIDE
	{
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player ) return;
		MDLCACHE_CRITICAL_SECTION();
		if ( player->GetActiveWeapon() ) player->GetActiveWeapon()->Redraw();
		else
		{
			CHudCrosshair *crosshair = GET_HUDELEMENT( CHudCrosshair );
			if ( crosshair ) crosshair->ResetCrosshair();
		}
	}
};
DECLARE_HUDELEMENT( CHudWeapon );
DECLARE_HUDELEMENT( CHudWeaponSelection );
static ConVar sc_draw_inv_slot_glyphs( "sc_draw_inv_slot_glyphs", "0", 0x00000088,
	"Whether or not to draw Steam Input glyphs for weapon slots." );

static bool TerrorGamepadUI()
{
	// C3D4F70 caches -nogamepadui / -gamepadui / SteamDeck exactly once.
	static int enabled = -1;
	if ( enabled == -1 )
	{
		const char *deck = getenv( "SteamDeck" );
		enabled = !CommandLine()->FindParm( "-nogamepadui" ) &&
			( CommandLine()->FindParm( "-gamepadui" ) || ( deck && *deck && atoi( deck ) != 0 ) );
	}
	return enabled != 0;
}


static void TerrorSelectionAnimation( const char *name )
{
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( name );
}

CHudWeaponSelection::CHudWeaponSelection( const char *name ) : CBaseHudWeaponSelection( name ),
	BaseClass( g_pClientMode->GetViewport(), "HudWeaponSelection" )
{
	// C2D0C00. The common native hidden mask is exactly 17.
	SetHiddenBits( HIDEHUD_WEAPONSELECTION | HIDEHUD_PLAYERDEAD );
	SetPaintBackgroundEnabled( false );
	m_bSelectionVisible = false;
	m_hSelectedWeapon = NULL;
	m_iPulseSlot = -1;
	m_flPulseDeadline = 0.0f;
	m_pWeaponBackgroundActive = m_pWeaponBackgroundInactive = NULL;
	m_pRoundedBackground = m_pRoundedGlow = m_pDPad = NULL;
	m_pFlashlight = m_pFlashlightActive = NULL;
	Q_memset( m_pEmptySlot, 0, sizeof( m_pEmptySlot ) );
	Q_memset( m_pUpgradeAmmo, 0, sizeof( m_pUpgradeAmmo ) );
	// The unread +884..1011 lastinv text/listener and unread 56 grow floats
	// are elided under Main's write-only ruling, not replaced with polling.
}

void CHudWeaponSelection::Init()
{
	Reset(); // C1DBE80: native weapon-resource Init is itself empty.
	m_flSelectionTime = gpGlobals->curtime;
}
void CHudWeaponSelection::VidInit()
{
	CBaseHudWeaponSelection::VidInit(); // Real C1DD3E0 sprite refresh, null history gap, then Reset.
}
void CHudWeaponSelection::Reset()
{
	// C1DBEB0. No selected-handle clear, pulse reset or invented transition.
	m_bSelectionVisible = false;
	m_flSelectionTime = gpGlobals->curtime;
}
void CHudWeaponSelection::LevelInit()
{
	// C2CDCC0: no base Reset here. Binding formatting is write-only-elided.
	m_iMaxSlots = clamp( m_iMaxSlots, 0, 6 );
	m_flPulseDeadline = 0.0f;
}
void CHudWeaponSelection::OnThink()
{
	// C2CE7A0/C2CED70 only update the unread inventory grow array. Elided.
}

bool CHudWeaponSelection::ShouldDraw()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player )
	{
		if ( IsInSelectionMode() ) HideSelection();
		return false;
	}
	static ConVarRef menu( "gameui_ingame_menu_active" );
	if ( !CHudElement::ShouldDraw() || player->IsThirdPersonCameraActive() || menu.GetInt() != 0 ) return false;
	return ( player->GetTeamNumber() == TEAM_SURVIVOR && player->m_lifeState == LIFE_ALIVE ) || m_bSelectionVisible;
}

void CHudWeaponSelection::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );
	m_pWeaponBackgroundActive = gHUD.GetIcon( "WeaponBackgroundActive" );
	m_pWeaponBackgroundInactive = gHUD.GetIcon( "WeaponBackgroundInactive" );
	m_pDPad = gHUD.GetIcon( "icon_dpad" );
	m_pFlashlight = gHUD.GetIcon( "icon_equip_flashlight" );
	m_pFlashlightActive = gHUD.GetIcon( "icon_equip_flashlight_active" );
	m_pEmptySlot[0] = gHUD.GetIcon( "icon_equip_molotov" );
	m_pEmptySlot[1] = gHUD.GetIcon( "icon_equip_medkit" );
	m_pEmptySlot[2] = gHUD.GetIcon( "icon_equip_pills" );
	m_pUpgradeAmmo[0] = gHUD.GetIcon( "icon_incendiary_ammo" );
	m_pUpgradeAmmo[1] = gHUD.GetIcon( "icon_explosive_ammo" );
	m_pRoundedGlow = gHUD.GetIcon( "rounded_background_glow_green" );
	m_pRoundedBackground = gHUD.GetIcon( "rounded_background_noborder_green" );
	// Native C2CE080 precaches number/ABXY glyphs; the SDK's PrecacheFontCharacters
	// equivalent is surface->PrecacheFontCharacters, not DrawPrintText.
	vgui::surface()->PrecacheFontCharacters( m_hNumberFont, L"0123456789" );
	vgui::surface()->PrecacheFontCharacters( m_hAmmo1Font, L"0123456789" );
	vgui::surface()->PrecacheFontCharacters( m_hPrimaryAmmoFont, L"0123456789" );
	vgui::surface()->PrecacheFontCharacters( m_hBindingsFont, L"ABXY" );
}

// C2CDD30 deliberately returns ONLY0/1; the shared native CRT consumes it.
int CHudWeaponSelection::CompareWeapons( const void *first, const void *second )
{
	const WeaponEntry_t &a = *static_cast< const WeaponEntry_t * >( first );
	const WeaponEntry_t &b = *static_cast< const WeaponEntry_t * >( second );
	if ( a.weapon->GetSlot() != b.weapon->GetSlot() ) return a.weapon->GetSlot() < b.weapon->GetSlot();
	if ( a.weapon->GetWeaponID() != b.weapon->GetWeaponID() ) return a.weapon->GetWeaponID() < b.weapon->GetWeaponID();
	return a.inventoryIndex < b.inventoryIndex;
}
void CHudWeaponSelection::SortWeapons( WeaponList_t &entries )
{
	TerrorNativeQSort( entries.Base(), entries.Count(), sizeof( WeaponEntry_t ), CompareWeapons );
}

// kind0=Paint (2D0F80 false),1=cycle (2D0F80 true),2=lastinv(2D1040),
// kind3=numbered slot(2D1120). Real per-family virtuals, never gun-only RTTI.
void CHudWeaponSelection::CollectWeapons( WeaponList_t &entries, int kind, int slot )
{
	entries.RemoveAll();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( player->GetWeapon( i ) );
		if ( !weapon || !weapon->CanBeSelected() ) continue;
		if ( ( kind == 1 || kind == 2 ) && !weapon->IsAllowedToSwitch() ) continue;
		if ( kind == 2 && weapon->GetSlot() != 0 && weapon->GetSlot() != 1 ) continue;
		if ( kind == 3 && weapon->GetSlot() != slot ) continue;
		WeaponEntry_t item = { i, weapon };
		entries.AddToTail( item );
	}
	SortWeapons( entries );
}

C_BaseCombatWeapon *CHudWeaponSelection::GetWeaponInSlot( int slot, int position )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || player->m_lifeState == LIFE_DYING ) return NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && weapon->GetSlot() == slot && weapon->GetPosition() == position ) return weapon;
	}
	return NULL;
}
C_BaseCombatWeapon *CHudWeaponSelection::GetFirstInventoryWeapon( int slot )
{
	// C2CEB20 is intentionally NOT eligibility-filtered; first strictly lower
	// position wins, whereas C1DC8A0's <= tie handling is different.
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || player->m_lifeState == LIFE_DYING ) return NULL;
	int lowest = 999;
	C_BaseCombatWeapon *first = NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && weapon->GetSlot() == slot && weapon->GetPosition() < lowest )
		{ lowest = weapon->GetPosition(); first = weapon; }
	}
	return first;
}
C_BaseCombatWeapon *CHudWeaponSelection::GetFirstPos( int slot )
{
	// C1DC8A0, the inherited virtual, retains <= and visibility filtering.
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return NULL;
	int lowest = 20;
	C_BaseCombatWeapon *first = NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && weapon->GetSlot() == slot && weapon->VisibleInWeaponSelection() &&
			weapon->GetPosition() <= lowest )
		{ lowest = weapon->GetPosition(); first = weapon; }
	}
	return first;
}
C_BaseCombatWeapon *CHudWeaponSelection::GetNextActivePos( int slot, int position )
{
	if ( position >= 20 || slot >= 6 ) return NULL;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return NULL;
	int lowest = 20;
	C_BaseCombatWeapon *next = NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && CanBeSelectedInHUD( weapon ) && weapon->GetSlot() == slot &&
			weapon->GetPosition() <= lowest && weapon->GetPosition() >= position )
		{ lowest = weapon->GetPosition(); next = weapon; }
	}
	return next;
}

void CHudWeaponSelection::OpenSelection()
{
	m_bSelectionVisible = true;
	TerrorSelectionAnimation( "OpenWeaponSelectionMenu" );
}
void CHudWeaponSelection::HideSelection()
{
	m_bSelectionVisible = false;
	TerrorSelectionAnimation( "CloseWeaponSelectionMenu" );
}
void CHudWeaponSelection::CancelWeaponSelection()
{
	if ( C_BasePlayer::GetLocalPlayer() && ShouldDraw() )
	{
		HideSelection();
		m_hSelectedWeapon = NULL;
	}
}
void CHudWeaponSelection::SelectWeapon()
{
	C_BaseCombatWeapon *selected = GetSelectedWeapon();
	if ( !selected ) { engine->ClientCmd( "cancelselect\n" ); return; }
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	if ( !selected->CanBeSelected() ) { player->EmitSound( "Player.DenyWeaponSelection" ); return; }
	if ( selected != player->GetActiveWeapon() ) player->EmitSound( "Player.WeaponSelected" );
	SetWeaponSelected();
	m_hSelectedWeapon = NULL;
	engine->ClientCmd( "cancelselect\n" );
}
void CHudWeaponSelection::ProcessInput()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	if ( player->IsInVGuiInputMode() && !player->IsInViewModelVGuiInputMode() )
	{
		if ( gHUD.m_iKeyBits & IN_ATTACK )
		{
			if ( hud_fastswitch.GetInt() != HUDTYPE_PLUS )
			{ gHUD.m_iKeyBits &= ~IN_ATTACK; ::input->ClearInputButton( IN_ATTACK ); }
			engine->ClientCmd( "cancelselect\n" );
		}
		return;
	}
	if ( ( gHUD.m_iKeyBits & ( IN_ATTACK | IN_ATTACK2 ) ) && IsWeaponSelectable() )
	{
		if ( hud_fastswitch.GetInt() != HUDTYPE_PLUS )
		{
			gHUD.m_iKeyBits &= ~( IN_ATTACK | IN_ATTACK2 );
			::input->ClearInputButton( IN_ATTACK ); ::input->ClearInputButton( IN_ATTACK2 );
		}
		SelectWeapon();
	}
}
void CHudWeaponSelection::CycleWeapon( bool next )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	// C2D20C0 lacks the LIFE_DYING check present in C2D21B0. Preserve asymmetry.
	if ( !player || ( !next && player->m_lifeState == LIFE_DYING ) ) return;
	C_BaseCombatWeapon *selected = IsInSelectionMode() ? GetSelectedWeapon() : player->GetActiveWeapon();
	if ( IsInSelectionMode() && !selected ) return;
	WeaponList_t entries;
	CollectWeapons( entries, 1 );
	if ( entries.Count() == 0 ) return;
	int index = next ? 1 : -1;
	if ( selected )
	{
		for ( int i = 0; i < entries.Count(); ++i )
			if ( entries[i].weapon == selected ) { index = i; break; }
		index += next ? -1 : 1;
		if ( index < 0 ) index = entries.Count() - 1;
		if ( index >= entries.Count() ) index = 0;
	}
	else index = 0;
	m_hSelectedWeapon = entries[index].weapon;
	if ( hud_fastswitch.GetInt() > 0 ) SelectWeapon();
	else if ( !IsInSelectionMode() ) OpenSelection();
	if ( m_bPlaySelectionSounds ) player->EmitSound( "Player.WeaponSelectionMoveSlot" );
}
void CHudWeaponSelection::CycleToNextWeapon() { CycleWeapon( true ); }
void CHudWeaponSelection::CycleToPrevWeapon() { CycleWeapon( false ); }
void CHudWeaponSelection::SelectPreviousEntry( const WeaponList_t &entries )
{
	if ( entries.Count() == 0 ) return;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	C_BaseCombatWeapon *active = player->GetActiveWeapon();
	int index = 0;
	if ( active )
	{
		for ( int i = 0; i < entries.Count(); ++i )
			if ( entries[i].weapon == active ) { index = i; break; }
		if ( --index < 0 ) index = entries.Count() - 1;
	}
	else index = entries.Count() - 1;
	::input->MakeWeaponSelection( entries[index].weapon );
}
void CHudWeaponSelection::SwitchToLastWeapon()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || player->m_lifeState == LIFE_DYING ) return;
	WeaponList_t entries;
	CollectWeapons( entries, 2 );
	SelectPreviousEntry( entries );
}
void CHudWeaponSelection::SelectWeaponSlot( int slot )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || player->m_lifeState == LIFE_DYING ) return;
	WeaponList_t entries;
	CollectWeapons( entries, 3, slot - 1 );
	SelectPreviousEntry( entries );
}
void CHudWeaponSelection::PulsePickedUpWeaponSlot( int slot )
{
	m_iPulseSlot = slot;
	m_flPulseDeadline = gpGlobals->curtime + 5.0f;
	TerrorSelectionAnimation( "PulsePickedUpWeaponSlot" );
}
void CHudWeaponSelection::OnWeaponPickup( C_BaseCombatWeapon *picked )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_pickup" );
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( picked );
	if ( !event ) return;
	if ( !weapon ) { gameeventmanager->FreeEvent( event ); return; }
	event->SetInt( "context", 0 );
	event->SetInt( "weaponid", weapon->GetWeaponID() );
	event->SetInt( "weaponslot", weapon->GetSlot() );
	event->SetInt( "dropped_by_infected", weapon->m_DroppedByInfectedGender );
	gameeventmanager->FireEventClientSide( event );
}

void CHudWeaponSelection::DrawNumber( int number, vgui::HFont font, int right, int top, Color color )
{
	wchar_t text[16];
	V_snwprintf( text, ARRAYSIZE( text ), L"%d", number );
	int wide, tall;
	vgui::surface()->GetTextSize( font, text, wide, tall );
	vgui::surface()->DrawSetTextFont( font ); vgui::surface()->DrawSetTextColor( color );
	vgui::surface()->DrawSetTextPos( right - wide, top );
	vgui::surface()->DrawPrintText( text, V_wcslen( text ) );
}
void CHudWeaponSelection::DrawBackground( int x, int y, int wide, int tall, bool active )
{
	CHudTexture *texture = active ? m_pRoundedGlow : m_pRoundedBackground;
	if ( !texture ) return;
	const float scale = (float)ScreenWidth() * 0.0015625f;
	const float padding = scale * ( active ? 4.0f : 2.0f );
	const int corner = (int)( scale * ( active ? 8.0f : 4.0f ) );
	const int left = (int)( (float)x - padding ), top = (int)( (float)y - padding );
	const int w = (int)( (float)wide + padding * 2.0f ), h = (int)( (float)tall + padding * 2.0f );
	const int dx[4] = { left, left + corner, left + w - corner, left + w };
	const int dy[4] = { top, top + corner, top + h - corner, top + h };
	const int u[4] = { 0, 16, texture->Width() - 16, texture->Width() };
	const int v[4] = { 0, 16, texture->Height() - 16, texture->Height() };
	for ( int row = 0; row < 3; ++row ) for ( int column = 0; column < 3; ++column )
		texture->DrawSelfCropped( dx[column], dy[row], u[column], v[row], u[column+1]-u[column], v[row+1]-v[row],
			dx[column+1]-dx[column], dy[row+1]-dy[row], Color( 255, 255, 255, 180 ) );
}
Color CHudWeaponSelection::ItemColor( bool selected, bool reserve, int slot ) const
{
	Color color = reserve ? ( selected ? m_SelectedReserveAmmoColor : m_UnselectedReserveAmmoColor ) :
		( selected ? m_SelectedItemColor : m_UnselectedItemColor );
	if ( m_iPulseSlot == slot && m_flPulseDeadline > gpGlobals->curtime ) color[3] = (unsigned char)(int)m_flPulseAlpha;
	return color;
}

void CHudWeaponSelection::Paint()
{
	if ( !ShouldDraw() ) return;
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return;
	WeaponList_t entries;
	CollectWeapons( entries, 0 );
	int activeSlot = 0;
	for ( int i = 0; i < entries.Count(); ++i )
		if ( entries[i].weapon == player->GetActiveWeapon() ) { activeSlot = entries[i].weapon->GetSlot(); break; }
	int y = m_iPrimaryWeaponsYPos;
	float boxTall = (float)m_iPrimaryWeaponBoxTall;
	int x = (int)( (float)( GetWide() - m_iPrimaryWeaponBoxWide ) - m_flRightSideIndent );
	CTerrorWeapon *primary = dynamic_cast< CTerrorWeapon * >( GetFirstInventoryWeapon( 0 ) );
	if ( primary )
	{
		DrawBackground( x, y, m_iPrimaryWeaponBoxWide, m_iPrimaryWeaponBoxTall, activeSlot == 0 );
		Color color = ItemColor( activeSlot == 0, false, 0 );
		const Color reserveColor = ItemColor( activeSlot == 0, true, 0 );
		const CHudTexture *icon = primary->GetSpriteInactive();
		if ( primary->GetPrimaryAmmoType() < 0 )
		{
			if ( icon ) icon->DrawSelf( x + m_iMeleeWeaponX, y + m_iMeleeWeaponY, m_iMeleeWeaponWide, m_iMeleeWeaponTall, color );
		}
		else
		{
			// Native helper15E90 is ammoType>=0, NOT CTerrorGun RTTI.
			if ( icon && icon->Height() != 0 )
			{
				const float tall = (float)m_iPrimaryWeaponTall;
				const float wide = ( tall / (float)icon->Height() ) * (float)icon->Width();
				icon->DrawSelf( (int)( (float)GetWide() - wide - m_flRightSideIndent ), (int)( (float)y - tall * 0.5f ), (int)wide, (int)tall, color );
			}
			const int upgraded = (unsigned char)primary->m_nUpgradedPrimaryAmmoLoaded.Get();
			const bool incendiary = primary->HasUpgrade( 0 ), explosive = primary->HasUpgrade( 1 );
			int clip = primary->Clip1() - upgraded;
			if ( clip < 0 ) clip = 0;
			if ( incendiary || explosive ) clip = upgraded;
			const bool m60 = primary->GetWeaponID() == TERROR_WEAPON_RIFLE_M60;
			DrawNumber( clip, m_hPrimaryAmmoFont, x + ( m60 ? m_iM60AmmoXPos : m_iPrimaryAmmoXPos ),
				y + ( m60 ? m_iM60AmmoYPos : m_iPrimaryAmmoYPos ), color );
			const int reserve = player->GetAmmoCount( primary->GetPrimaryAmmoType() );
			if ( reserve > 0 && !incendiary && !explosive )
				DrawNumber( reserve, m_hReserveAmmoFont, x + m_iReserveAmmoXPos, y + m_iReserveAmmoYPos, reserveColor );
			if ( incendiary || explosive )
			{
				const CHudTexture *upgrade = m_pUpgradeAmmo[incendiary ? 0 : 1];
				if ( upgrade ) upgrade->DrawSelf( x + m_iSpecialAmmoXPos, y + m_iSpecialAmmoYPos, m_iAmmoIconSize, m_iAmmoIconSize, color );
			}
			else if ( primary->GetSpriteAmmo() )
				primary->GetSpriteAmmo()->DrawSelf( x + m_iAmmoIconXPos, y + m_iAmmoIconYPos, m_iAmmoIconSize, m_iAmmoIconSize, color );
		}
	}
	const double primaryGap = (double)ScreenWidth() * (double)0.0015625f;
	// C2D17A7 uses __ftol2_sse's double-spill/SSE2 signed32 conversion.
	y = _mm_cvttsd_si32( _mm_set_sd( primaryGap + primaryGap + (double)boxTall + (double)y ) );
	CTerrorWeapon *secondary = dynamic_cast< CTerrorWeapon * >( GetFirstInventoryWeapon( 1 ) );
	if ( secondary )
	{
		x = (int)( (float)( GetWide() - m_iPistolBoxWide ) - m_flRightSideIndent );
		boxTall = (float)m_iPistolBoxTall;
		DrawBackground( x, y, m_iPistolBoxWide, m_iPistolBoxTall, activeSlot == 1 );
		Color color = ItemColor( activeSlot == 1, false, 1 );
		const bool ammo = secondary->GetPrimaryAmmoType() >= 0;
		const bool chainsaw = secondary->GetWeaponID() == TERROR_WEAPON_CHAINSAW;
		const CHudTexture *icon = secondary->IsDualWielding() ? secondary->GetSpriteInactiveDual() : secondary->GetSpriteInactive();
		int iconX = x;
		if ( icon )
		{
			if ( chainsaw ) icon->DrawSelf( x + m_iChainsawX, y + m_iChainsawY, m_iChainsawWide, m_iChainsawTall, color );
			else if ( !ammo ) icon->DrawSelf( x + m_iMeleeWeaponX, y + m_iMeleeWeaponY, m_iMeleeWeaponWide, m_iMeleeWeaponTall, color );
			else
			{
				const float right = (float)GetWide() - m_flRightSideIndent - boxTall;
				iconX = (int)( right - (float)( (float)ScreenWidth() * 0.0015625f ) );
				icon->DrawSelf( iconX, y, (int)boxTall, (int)boxTall, color );
			}
		}
		if ( chainsaw )
		{
			const int bx = x + m_iChainsawBarX, by = y + m_iChainsawBarY;
			vgui::surface()->DrawSetColor( color );
			vgui::surface()->DrawOutlinedRect( bx, by, bx + m_iChainsawBarWide, by + m_iChainsawBarTall );
			// No clamp: native overfull/negative fuel geometry remains intact.
			const float maximum = (float)secondary->GetMaxClip1();
			if ( maximum != 0.0f ) // minimal skip: native invalid float->int at zero max.
			{
				const int fill = (int)( (float)( m_iChainsawBarTall - 4 ) * ( (float)secondary->Clip1() / maximum ) );
				vgui::surface()->DrawFilledRect( bx + 2, by + m_iChainsawBarTall - fill - 2, bx + m_iChainsawBarWide - 2, by + m_iChainsawBarTall - 2 );
			}
		}
		else if ( ammo )
		{
			wchar_t number[16]; V_snwprintf( number, ARRAYSIZE( number ), L"%d", secondary->Clip1() );
			int wide, tall; vgui::surface()->GetTextSize( m_hPistolAmmoFont, number, wide, tall );
			const double pistolGap = (double)ScreenWidth() * (double)0.0015625f;
			const int right = _mm_cvttsd_si32( _mm_set_sd( (double)iconX - ( pistolGap + pistolGap ) ) );
			DrawNumber( secondary->Clip1(), m_hPistolAmmoFont, right, (int)( ( boxTall - (float)tall ) * 0.5f + (float)y ), color );
		}
		const double secondaryGap = (double)ScreenWidth() * (double)0.0015625f;
		y = _mm_cvttsd_si32( _mm_set_sd( secondaryGap + secondaryGap + (double)boxTall + (double)y ) );
	}
	// Native2CDDF0/2CE980's Steam glyph bitmap decode is the same unavailable
	// 2B0AB0 service ruled out for CBindPanel. Omit ONLY that decode/glyph path;
	// the inventory icons and actual controller selection remain fully active.
	x = (int)( ( (float)GetWide() - m_flIconSize ) - m_flRightSideIndent );
	const int size = (int)m_flIconSize;
	if ( ::input->IsSteamControllerActive() && ( sc_draw_inv_slot_glyphs.GetBool() || TerrorGamepadUI() ) )
	{
		DrawBackground( x, y, size, size, false );
		CHudTexture *flashlight = player->IsEffectActive( EF_DIMLIGHT ) ? m_pFlashlightActive : m_pFlashlight;
		if ( flashlight ) flashlight->DrawSelf( x, y, size, size, Color( 255, 255, 255, 255 ) );
		y = (int)( (double)ScreenWidth() * 0.0015625 + (double)ScreenWidth() * 0.0015625 + m_flIconSize + (double)y );
	}
	for ( int slot = 2; slot <= 4; ++slot )
	{
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetFirstInventoryWeapon( slot ) );
		DrawBackground( x, y, size, size, activeSlot == slot );
		Color color = weapon ? ItemColor( activeSlot == slot, false, slot ) : m_InactiveItemSlotColor;
		const CHudTexture *icon = weapon ? weapon->GetSpriteInactive() : m_pEmptySlot[slot - 2];
		if ( icon ) icon->DrawSelf( x, y, size, size, color );
		if ( slot != 4 ) y = (int)( (double)ScreenWidth() * 0.0015625 + (double)ScreenWidth() * 0.0015625 + m_flIconSize + (double)y );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
