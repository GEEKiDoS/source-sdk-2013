//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_TERROR_CROSSHAIR_H
#define HUD_TERROR_CROSSHAIR_H
#ifdef _WIN32
#pragma once
#endif

#include "hud_crosshair.h"

namespace vgui { class CircularProgressBar; }

// Sole existing CHudCrosshair named registration. Native primary105D4E8C,
// VGUI105D4B3C; source types/ownership replace PE32 offsets, never padding.
class CHudTerrorCrosshair : public CHudCrosshair
{
	DECLARE_CLASS_SIMPLE( CHudTerrorCrosshair, CHudCrosshair );
public:
	CHudTerrorCrosshair( const char *elementName ); //102CBD70, NOT102CB5D0
	virtual ~CHudTerrorCrosshair();              //102CB6D0, actual parent-owned child teardown
	virtual bool ShouldDraw() OVERRIDE;
	void SetCrosshairState( int state );         //102CB710, publishes real texture/color immediately

	void DrawGunCrosshair( float scatter, float aimError, float fov, bool shotgun );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void Paint() OVERRIDE;
	virtual void DrawCrosshair( int x, int y, int width, int height ) OVERRIDE;

private:

	CHudTexture *m_pInfectedCrosshair;            //448, borrowed gHUD icon
	CHudTexture *m_pWeaponCrosshair;              //452, borrowed gHUD icon
	vgui::CircularProgressBar *m_pAbilityProgress; //456, actual VGUI child owns its lifetime
	CPanelAnimationVar( int, m_abilitySize, "ability_size", "0" );
	CPanelAnimationVar( Color, m_abilitySurpressed, "ability_surpressed_color", "127 127 127 255" );
	CPanelAnimationVar( Color, m_abilityCharging, "ability_charging_color", "127 127 127 255" );
	CPanelAnimationVar( Color, m_abilityReady, "ability_ready_color", "255 255 255 255" );
	CPanelAnimationVar( Color, m_abilityShouldAttack, "ability_attack_color", "255 0 0 255" );
	CPanelAnimationVar( Color, m_abilityShouldAttack_ColorBlind, "ability_attack_color_colorblind", "12 206 206 255" );
};

//102CB5D0: actual current HUD registry lookup, supported local user0.
CHudTerrorCrosshair *GetTerrorCrosshair();

#endif // HUD_TERROR_CROSSHAIR_H
