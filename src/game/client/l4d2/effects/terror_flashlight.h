//========= Copyright Valve Corporation, All rights reserved. ============//
// Whole replacement of EXISTING client/l4d2/terror_flashlight.h, not a new API owner.
#ifndef TERROR_FLASHLIGHT_H
#define TERROR_FLASHLIGHT_H
#ifdef _WIN32
#pragma once
#endif

class Vector;
class ITexture;

// One actual local-user0 manager. Old UpdateTerrorFlashlight(player*) is removed
// with whole Simulate; SDK m_pFlashlight remains only on stock other-game paths.
void SetTerrorFlashlightOwner( int entIndex );
void TurnOnTerrorFlashlight( int entIndex, const char *texture, float fov, float farZ, float linear );
void TurnOffTerrorFlashlight();
void ResetTerrorFlashlight();
void UpdateTerrorFlashlight( const Vector &origin, const Vector &forward, const Vector &right,
	const Vector &up, float fov, bool shadows, float farZ, float linear, const char *texture );
void TerrorViewmodelMuzzleFlash();

// Frozen with sole native TerrorClientMode lifecycle owner. Backend owns actual
// effect creation/default-state builder/handle/update/restoration, not the mode.
void SetTerrorFlashlightOverride( bool enabled );
void UpdateTerrorFlashlightOverride( const Vector &origin, const Vector &forward, const Vector &right,
	const Vector &up, float fov, ITexture *texture, const Vector &color );

#endif // TERROR_FLASHLIGHT_H
