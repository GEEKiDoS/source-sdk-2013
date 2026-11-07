//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_TERROR_WEAPON_SELECTION_H
#define HUD_TERROR_WEAPON_SELECTION_H
#ifdef _WIN32
#pragma once
#endif
#include "weapon_selection.h"
#include "tier1/utlvector.h"
#include <vgui_controls/Panel.h>
#include "l4d2/weapons/terror_weapon.h"
class CHudWeaponSelection : public CBaseHudWeaponSelection, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudWeaponSelection, vgui::Panel );
public:
	CHudWeaponSelection( const char *name );
	virtual void Init() OVERRIDE;
	virtual void VidInit() OVERRIDE;
	virtual void LevelInit() OVERRIDE;
	virtual void Reset() OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual void ProcessInput() OVERRIDE;
	virtual void OpenSelection() OVERRIDE;
	virtual void HideSelection() OVERRIDE;
	virtual void CancelWeaponSelection() OVERRIDE;
	virtual void CycleToNextWeapon() OVERRIDE;
	virtual void CycleToPrevWeapon() OVERRIDE;
	virtual void SwitchToLastWeapon() OVERRIDE;
	virtual C_BaseCombatWeapon *GetWeaponInSlot( int slot, int position ) OVERRIDE;
	virtual C_BaseCombatWeapon *GetFirstPos( int slot ) OVERRIDE;
	virtual C_BaseCombatWeapon *GetNextActivePos( int slot, int position ) OVERRIDE;
	virtual void SelectWeaponSlot( int slot ) OVERRIDE;
	virtual void SelectWeapon() OVERRIDE;
	virtual C_BaseCombatWeapon *GetSelectedWeapon() OVERRIDE { return m_hSelectedWeapon.Get(); }
	virtual void OnWeaponPickup( C_BaseCombatWeapon *weapon ) OVERRIDE;
	void PulsePickedUpWeaponSlot( int slot ); // Genuine C299DD0 item-pickup caller.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void Paint() OVERRIDE;
private:
	struct WeaponEntry_t { int inventoryIndex; CTerrorWeapon *weapon; };
	typedef CUtlVectorFixed< WeaponEntry_t, MAX_WEAPONS > WeaponList_t;
	static int CompareWeapons( const void *first, const void *second );
	static void SortWeapons( WeaponList_t &weapons );
	static void CollectWeapons( WeaponList_t &weapons, int kind, int slot = -1 );
	static void SelectPreviousEntry( const WeaponList_t &weapons );
	C_BaseCombatWeapon *GetFirstInventoryWeapon( int slot );
	void CycleWeapon( bool next );
	void DrawBackground( int x, int y, int wide, int tall, bool active );
	void DrawNumber( int number, vgui::HFont font, int right, int top, Color color );
	Color ItemColor( bool selected, bool reserve, int slot ) const;
	CHudTexture *m_pWeaponBackgroundActive;
	CHudTexture *m_pWeaponBackgroundInactive;
	CHudTexture *m_pRoundedBackground;
	CHudTexture *m_pDPad;
	CHudTexture *m_pFlashlight;
	CHudTexture *m_pFlashlightActive;
	CHudTexture *m_pEmptySlot[3];
	CHudTexture *m_pUpgradeAmmo[2];
	CHudTexture *m_pRoundedGlow;
	int m_iPulseSlot;
	float m_flPulseDeadline;
	CPanelAnimationVar( vgui::HFont, m_hNumberFont, "NumberFont", "HudSelectionNumbers" );
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "HudSelectionText" );
	CPanelAnimationVarAliasType( float, m_flLargeBoxWide, "LargeBoxWide", "108", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flLargeBoxTall, "LargeBoxTall", "72", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flSmallBoxWide, "SmallBoxWide", "108", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flSmallBoxTall, "SmallBoxTall", "32", "proportional_float" );
	CPanelAnimationVar( int, m_iBoxDirection, "BoxDirection", "0" );
	CPanelAnimationVarAliasType( float, m_flBoxGap, "BoxGap", "12", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flSelectionNumberXPos, "SelectionNumberXPos", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flSelectionNumberYPos, "SelectionNumberYPos", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconXPos, "IconXPos", "16", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconYPos, "IconYPos", "8", "proportional_float" );
	CPanelAnimationVar( vgui::HFont, m_hAmmo1Font, "Ammo1Font", "HudAmmo" );
	CPanelAnimationVar( Color, m_Ammo1Color, "Ammo1Color", "255 255 255 255" );
	CPanelAnimationVarAliasType( float, m_flAmmo1XPos, "Ammo1XPos", "16", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flAmmo1YPos, "Ammo1YPos", "8", "proportional_float" );
	CPanelAnimationVar( vgui::HFont, m_hAmmo2Font, "Ammo2Font", "HudSelectionText" );
	CPanelAnimationVar( Color, m_Ammo2Color, "Ammo2Color", "128 128 128 255" );
	CPanelAnimationVarAliasType( float, m_flAmmo2XPos, "Ammo2XPos", "16", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flAmmo2YPos, "Ammo2YPos", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flTextYPos, "TextYPos", "54", "proportional_float" );
	CPanelAnimationVar( float, m_flAlphaOverride, "Alpha", "255" );
	CPanelAnimationVar( float, m_flSelectionAlphaOverride, "SelectionAlpha", "255" );
	CPanelAnimationVar( Color, m_TextColor, "TextColor", "SelectionTextFg" );
	CPanelAnimationVar( Color, m_NumberColor, "NumberColor", "SelectionNumberFg" );
	CPanelAnimationVar( Color, m_EmptyBoxColor, "EmptyBoxColor", "SelectionEmptyBoxBg" );
	CPanelAnimationVar( Color, m_BoxColor, "BoxColor", "SelectionBoxBg" );
	CPanelAnimationVar( Color, m_SelectedBoxColor, "SelectedBoxClor", "SelectionSelectedBoxBg" );
	CPanelAnimationVar( float, m_flWeaponPickupGrowTime, "SelectionGrowTime", "0.1" );
	CPanelAnimationVar( float, m_flTextScan, "TextScan", "1.0" );
	CPanelAnimationVar( int, m_iMaxSlots, "MaxSlots", "6" );
	CPanelAnimationVar( bool, m_bPlaySelectionSounds, "PlaySelectSounds", "1" );
	CPanelAnimationVar( float, m_flPulseAlpha, "pulse_alpha", "1.0" );
	CPanelAnimationVarAliasType( int, m_iPrimaryWeaponsYPos, "PrimaryWeaponsYPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPrimaryWeaponBoxWide, "PrimaryWeaponBoxWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPrimaryWeaponBoxTall, "PrimaryWeaponBoxTall", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPrimaryWeaponWide, "PrimaryWeaponWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPrimaryWeaponTall, "PrimaryWeaponTall", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iMeleeWeaponX, "MeleeWeaponX", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iMeleeWeaponY, "MeleeWeaponY", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iMeleeWeaponWide, "MeleeWeaponWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iMeleeWeaponTall, "MeleeWeaponTall", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawX, "ChainsawX", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawY, "ChainsawY", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawWide, "ChainsawWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawTall, "ChainsawTall", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawBarX, "ChainsawBarX", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawBarY, "ChainsawBarY", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawBarWide, "ChainsawBarWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iChainsawBarTall, "ChainsawBarTall", "0", "proportional_int" );
	CPanelAnimationVar( vgui::HFont, m_hPrimaryAmmoFont, "PrimaryAmmoFont", "HudAmmo" );
	CPanelAnimationVar( vgui::HFont, m_hReserveAmmoFont, "ReserveAmmoFont", "HudAmmoSmall" );
	CPanelAnimationVar( vgui::HFont, m_hPistolAmmoFont, "PistolAmmoFont", "HudAmmo" );
	CPanelAnimationVarAliasType( int, m_iPrimaryAmmoXPos, "PrimaryAmmoXPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPrimaryAmmoYPos, "PrimaryAmmoYPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iReserveAmmoXPos, "ReserveAmmoXPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iReserveAmmoYPos, "ReserveAmmoYPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iM60AmmoXPos, "M60AmmoXPos", "26", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iM60AmmoYPos, "M60AmmoYPos", "7", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iAmmoIconXPos, "AmmoIconXPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iAmmoIconYPos, "AmmoIconYPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iAmmoIconSize, "AmmoIconSize", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iSpecialAmmoXPos, "SpecialAmmoXPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iSpecialAmmoYPos, "SpecialAmmoYPos", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPistolBoxWide, "PistolBoxWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPistolBoxTall, "PistolBoxTall", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_iPistolWide, "PistolWide", "0", "proportional_int" );
	CPanelAnimationVarAliasType( float, m_flPrimaryBindingXPos, "PrimaryBindingXPos", "0", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flPrimaryBindingYPos, "PrimaryBindingYPos", "0", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flDPadXPos, "DPadXPos", "0", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flDPadYPos, "DPadYPos", "0", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flRightSideIndent, "RightSideIndent", "10", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconSize, "IconSize", "32", "proportional_float" );
	CPanelAnimationVar( vgui::HFont, m_hBindingsFont, "BindingsFont", "GameUIButtons" );
	CPanelAnimationVar( Color, m_SelectedItemColor, "SelectedItemColor", "255 0 0 255" );
	CPanelAnimationVar( Color, m_UnselectedItemColor, "ActiveItemColor", "255 255 255 255" );
	CPanelAnimationVar( Color, m_SelectedReserveAmmoColor, "SelectedReserveAmmoColor", "128 128 128 255" );
	CPanelAnimationVar( Color, m_UnselectedReserveAmmoColor, "UnselectedReserveAmmoColor", "128 128 128 255" );
	CPanelAnimationVar( Color, m_InactiveItemSlotColor, "InactiveItemColor", "100 100 100 255" );
};
#endif
