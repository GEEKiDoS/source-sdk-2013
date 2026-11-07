// Native L4D2 CBaseHudWeaponSelection branch for existing weapon_selection.cpp.
// Include after that file's ordinary includes, IN PLACE OF its SDK bodies for
// L4D2 only; retain the original bodies only under the non-L4D2 #else branch.
// One registry/singleton/command owner. Not a second HUD or direct input owner.
#include "history_resource.h"
#include "menu.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

ConVar hud_drawhistory_time( "hud_drawhistory_time", "5", 0x0000000A );
ConVar hud_fastswitch( "hud_fastswitch", "1", 3 ); // C508E60 input1 -> C3D3EC0 effective3 (unregistered+development).
extern ConVar spec_chasedistance; // Sole existing ObserverEF6F owner.
ConVar spec_chasedistancestep( "spec_chasedistancestep", "10", 0x00000088,
	"Amount to adjust the chase cam's distance on a zoom request" );

DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot1, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot2, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot3, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot4, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot5, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot6, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot7, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot8, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot9, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot0, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Slot10, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, Close, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, NextWeapon, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, PrevWeapon, "CHudWeaponSelection" );
DECLARE_HUD_COMMAND_NAME( CBaseHudWeaponSelection, LastWeapon, "CHudWeaponSelection" );
static ConCommand slot0( "slot0", __CmdFunc_Slot0, "", 0x10000008 );
static ConCommand slot1( "slot1", __CmdFunc_Slot1, "", 0x10000008 );
static ConCommand slot2( "slot2", __CmdFunc_Slot2, "", 0x10000008 );
static ConCommand slot3( "slot3", __CmdFunc_Slot3, "", 0x10000008 );
static ConCommand slot4( "slot4", __CmdFunc_Slot4, "", 0x10000008 );
static ConCommand slot5( "slot5", __CmdFunc_Slot5, "", 0x10000008 );
static ConCommand slot6( "slot6", __CmdFunc_Slot6, "", 0x10000008 );
static ConCommand slot7( "slot7", __CmdFunc_Slot7, "", 0x10000008 );
static ConCommand slot8( "slot8", __CmdFunc_Slot8, "", 0x10000008 );
static ConCommand slot9( "slot9", __CmdFunc_Slot9, "", 0x10000008 );
static ConCommand slot10( "slot10", __CmdFunc_Slot10, "", 0x10000008 );
static ConCommand cancelselect( "cancelselect", __CmdFunc_Close, "", 0x10000008 );
static ConCommand invnext( "invnext", __CmdFunc_NextWeapon, "", 0x10000008 );
static ConCommand invprev( "invprev", __CmdFunc_PrevWeapon, "", 0x10000008 );
static ConCommand lastinv( "lastinv", __CmdFunc_LastWeapon, "", 0x10000008 );

CBaseHudWeaponSelection *CBaseHudWeaponSelection::s_pInstance = NULL;
CBaseHudWeaponSelection *CBaseHudWeaponSelection::GetInstance() { return s_pInstance; }
CBaseHudWeaponSelection *GetHudWeaponSelection() { return CBaseHudWeaponSelection::GetInstance(); }
CBaseHudWeaponSelection::CBaseHudWeaponSelection( const char *name ) : CHudElement( name )
{
	s_pInstance = this; m_hSelectedWeapon = NULL;
	m_flLastInvActionTime = 0.0f;
	SetHiddenBits( 1073 ); // C1DC550; derived C2D0C00 deliberately replaces with17.
}
// C1DBE80/C1DBEB0 call actual weapon-resource nullsubs. Do not import
// SDK lifecycle work here; the native +68 timestamp is also write-only.
void CBaseHudWeaponSelection::Init() { Reset(); m_flSelectionTime = gpGlobals->curtime; }
void CBaseHudWeaponSelection::Reset() { m_bSelectionVisible = false; m_flSelectionTime = gpGlobals->curtime; }
void CBaseHudWeaponSelection::VidInit()
{
	gWR.LoadAllWeaponSprites();
	// C1DBEE0's history-gap callee is an actual nullsub, not SDK SetHistoryGap.
	Reset();
}
void CBaseHudWeaponSelection::UpdateSelectionTime() { m_flSelectionTime = gpGlobals->curtime; }
bool CBaseHudWeaponSelection::IsInSelectionMode() { return m_bSelectionVisible; }
void CBaseHudWeaponSelection::OpenSelection() { m_bSelectionVisible = true; }
void CBaseHudWeaponSelection::HideSelection() { m_bSelectionVisible = false; }
void CBaseHudWeaponSelection::OnThink()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return; // Native null dereference: minimal skip only.
	if ( ( player->GetFlags() & FL_FROZEN ) || player->m_lifeState == LIFE_DYING )
		if ( IsInSelectionMode() ) CancelWeaponSelection();
}
void CBaseHudWeaponSelection::ProcessInput()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( !player ) return;
	if ( player->IsInVGuiInputMode() && !player->IsInViewModelVGuiInputMode() )
	{
		if ( gHUD.m_iKeyBits & IN_ATTACK )
		{
			if ( hud_fastswitch.GetInt() != HUDTYPE_PLUS ) { gHUD.m_iKeyBits &= ~IN_ATTACK; input->ClearInputButton( IN_ATTACK ); }
			engine->ClientCmd( "cancelselect\n" );
		}
		return;
	}
	if ( ( gHUD.m_iKeyBits & ( IN_ATTACK | IN_ATTACK2 ) ) && IsWeaponSelectable() )
	{
		if ( hud_fastswitch.GetInt() != HUDTYPE_PLUS ) { gHUD.m_iKeyBits &= ~( IN_ATTACK | IN_ATTACK2 ); input->ClearInputButton( IN_ATTACK ); input->ClearInputButton( IN_ATTACK2 ); }
		SelectWeapon();
	}
}
bool CBaseHudWeaponSelection::CanBeSelectedInHUD( C_BaseCombatWeapon *weapon )
{
	return weapon->VisibleInWeaponSelection() && ( hud_fastswitch.GetInt() == HUDTYPE_PLUS || weapon->CanBeSelected() );
}
int CBaseHudWeaponSelection::KeyInput( int down, ButtonCode_t key, const char *binding )
{
	if ( IsInSelectionMode() && binding && !Q_stricmp( binding, "cancelselect" ) ) { HideSelection(); return 0; }
	if ( down >= 1 && key >= KEY_1 && key <= KEY_9 && HandleHudMenuInput( key - KEY_0 ) ) return 0;
	return 1;
}
void CBaseHudWeaponSelection::OnWeaponPickup( C_BaseCombatWeapon *weapon )
{
	CHudHistoryResource *history = GET_HUDELEMENT( CHudHistoryResource );
	if ( history ) history->AddToHistory( weapon ); // C1DBF50; derived native owner overrides.
}
bool CBaseHudWeaponSelection::IsHudMenuTakingInput()
{
	CHudMenu *menu = GET_HUDELEMENT( CHudMenu ); return menu && menu->IsMenuOpen();
}
bool CBaseHudWeaponSelection::HandleHudMenuInput( int slot )
{
	CHudMenu *menu = GET_HUDELEMENT( CHudMenu );
	if ( !menu || !menu->IsMenuOpen() ) return false;
	menu->SelectMenuItem( slot ); return true;
}
bool CBaseHudWeaponSelection::IsHudMenuPreventingWeaponSelection()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return false; // Native null dereference: minimal skip only.
	return ( player->GetFlags() & FL_FROZEN ) || player->m_lifeState == LIFE_DYING || IsHudMenuTakingInput();
}
void CBaseHudWeaponSelection::SelectSlot( int slot )
{
	if ( HandleHudMenuInput( slot ) || !CHudElement::ShouldDraw() ) return;
	UpdateSelectionTime(); SelectWeaponSlot( slot );
}
void CBaseHudWeaponSelection::UserCmd_Slot1()
{
	if ( hud_fastswitch.GetInt() != 3 ) SelectSlot( 1 );
	else if ( BaseClass::ShouldDraw() ) SwitchToLastWeapon();
}
void CBaseHudWeaponSelection::UserCmd_Slot2()
{
	if ( hud_fastswitch.GetInt() == 3 ) UserCmd_NextWeapon(); else SelectSlot( 2 );
}
void CBaseHudWeaponSelection::UserCmd_Slot3()
{
	if ( hud_fastswitch.GetInt() == 3 ) engine->ClientCmd( "phys_swap" ); else SelectSlot( 3 );
}
void CBaseHudWeaponSelection::UserCmd_Slot4()
{
	if ( hud_fastswitch.GetInt() == 3 ) UserCmd_PrevWeapon(); else SelectSlot( 4 );
}
void CBaseHudWeaponSelection::UserCmd_Slot5() { SelectSlot( 5 ); }
void CBaseHudWeaponSelection::UserCmd_Slot6() { SelectSlot( 6 ); }
void CBaseHudWeaponSelection::UserCmd_Slot7() { SelectSlot( 7 ); }
void CBaseHudWeaponSelection::UserCmd_Slot8() { SelectSlot( 8 ); }
void CBaseHudWeaponSelection::UserCmd_Slot9() { SelectSlot( 9 ); }
void CBaseHudWeaponSelection::UserCmd_Slot0() { SelectSlot( 0 ); }
void CBaseHudWeaponSelection::UserCmd_Slot10() { SelectSlot( 10 ); }
void CBaseHudWeaponSelection::UserCmd_Close() { CancelWeaponSelection(); }
void CBaseHudWeaponSelection::AdjustObserverWeaponCycle( bool next )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	ConVarRef mimic( "bot_mimic" );
	const int mode = player->GetObserverMode();
	const int mimicIndex = mimic.GetInt();
	if ( mode == OBS_MODE_CHASE && mimicIndex == 0 )
	{
		float bound;
		if ( next ? spec_chasedistance.GetMax( bound ) : spec_chasedistance.GetMin( bound ) )
		{
			float distance = spec_chasedistance.GetFloat() + ( next ? spec_chasedistancestep.GetFloat() : -spec_chasedistancestep.GetFloat() );
			if ( next ? bound <= distance : distance <= bound ) distance = bound;
			spec_chasedistance.SetValue( distance );
			player->m_flObserverChaseDistance = spec_chasedistance.GetFloat();
		}
	}
	else if ( ( mode == OBS_MODE_IN_EYE || mode == OBS_MODE_ROAMING || mode == OBS_MODE_CHASE ) &&
		( mimicIndex < 0 ? ( 0u - (unsigned int)mimicIndex ) : (unsigned int)mimicIndex ) == (unsigned int)player->entindex() )
		engine->ServerCmd( next ? "invnext" : "invprev", true );
}
void CBaseHudWeaponSelection::UserCmd_NextWeapon()
{
	AdjustObserverWeaponCycle( true );
	if ( !BaseClass::ShouldDraw() ) return;
	CycleToNextWeapon();
	if ( hud_fastswitch.GetInt() > 0 ) SelectWeapon();
	UpdateSelectionTime();
}
void CBaseHudWeaponSelection::UserCmd_PrevWeapon()
{
	AdjustObserverWeaponCycle( false );
	if ( !BaseClass::ShouldDraw() ) return;
	CycleToPrevWeapon();
	if ( hud_fastswitch.GetInt() > 0 ) SelectWeapon();
	UpdateSelectionTime();
}
void CBaseHudWeaponSelection::UserCmd_LastWeapon() { if ( CHudElement::ShouldDraw() ) SwitchToLastWeapon(); }
// C1DCA50: distinct native last-invaction time, not m_flSelectionTime.
void CBaseHudWeaponSelection::UserCmd_InvAction()
{
	ConVarRef timer( "next_weapon_timer" );
	const float interval = timer.IsValid() ? timer.GetFloat() : 1.0f;
	if ( interval + m_flLastInvActionTime > gpGlobals->curtime ) UserCmd_NextWeapon();
	else SwitchToLastWeapon();
	m_flLastInvActionTime = gpGlobals->curtime;
}
static void CC_InvActionDown()
{
	// C1DC0D0 writes only held6758 and resets675A to0. Held has no reader;
	// 675A has no other producer and is always0 at release. Ledger318 elides both.
}
static void CC_InvActionUp()
{
	GetHudWeaponSelection()->UserCmd_InvAction();
}
#include "l4d2/hud/weapon_selection_console_registrations.inc"
void CBaseHudWeaponSelection::SwitchToLastWeapon()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( player ) input->MakeWeaponSelection( player->GetLastWeapon() );
}
void CBaseHudWeaponSelection::SetWeaponSelected() { input->MakeWeaponSelection( GetSelectedWeapon() ); }
void CBaseHudWeaponSelection::SelectWeapon()
{
	C_BaseCombatWeapon *weapon = GetSelectedWeapon(); if ( !weapon ) { engine->ClientCmd( "cancelselect\n" ); return; }
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( !player ) return;
	if ( !weapon->CanBeSelected() ) { player->EmitSound( "Player.DenyWeaponSelection" ); return; }
	if ( weapon != player->GetActiveWeapon() ) player->EmitSound( "Player.WeaponSelected" );
	SetWeaponSelected(); m_hSelectedWeapon = NULL; engine->ClientCmd( "cancelselect\n" );
}
void CBaseHudWeaponSelection::CancelWeaponSelection()
{
	if ( C_BasePlayer::GetLocalPlayer() && ShouldDraw() ) { HideSelection(); m_hSelectedWeapon = NULL; }
}
C_BaseCombatWeapon *CBaseHudWeaponSelection::GetFirstPos( int slot )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( !player ) return NULL;
	int lowest = 20; C_BaseCombatWeapon *first = NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && weapon->GetSlot() == slot && weapon->VisibleInWeaponSelection() && weapon->GetPosition() <= lowest ) { lowest = weapon->GetPosition(); first = weapon; }
	}
	return first;
}
C_BaseCombatWeapon *CBaseHudWeaponSelection::GetNextActivePos( int slot, int position )
{
	if ( position >= 20 || slot >= 6 ) return NULL;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( !player ) return NULL;
	int lowest = 20; C_BaseCombatWeapon *next = NULL;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( weapon && CanBeSelectedInHUD( weapon ) && weapon->GetSlot() == slot && weapon->GetPosition() <= lowest && weapon->GetPosition() >= position ) { lowest = weapon->GetPosition(); next = weapon; }
	}
	return next;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
