//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef FLASHLIGHTEFFECT_H
#define FLASHLIGHTEFFECT_H
#ifdef _WIN32
#pragma once
#endif

struct dlight_t;
#if defined( L4D2 )
struct FlashlightState_t;
#endif


class CFlashlightEffect
{
public:

#if defined( L4D2 )
	CFlashlightEffect( int entIndex = 0, const char *textureName = NULL,
		float fov = 0.0f, float farZ = 0.0f, float linear = 0.0f );
#else
	CFlashlightEffect(int nEntIndex = 0);
#endif
	virtual ~CFlashlightEffect();

#if defined( L4D2 )
	void UpdateLight( int entIndex, const Vector &origin, const Vector &forward,
		const Vector &right, const Vector &up, float fov, float farZ, float linear,
		bool shadows, const char *textureName );
	void UpdateLightOverride( int entIndex, const Vector &origin, const Vector &forward,
		const Vector &right, const Vector &up, float fov, bool shadows,
		ITexture *texture, const Vector &color, bool tracePlayers );
	void SetMuzzleFlashState( bool active, float brightness );
#else
	virtual void UpdateLight(const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance);
#endif
	void TurnOn();
	void TurnOff();
	bool IsOn( void ) { return m_bIsOn;	}

	ClientShadowHandle_t GetFlashlightHandle( void ) { return m_FlashlightHandle; }
	void SetFlashlightHandle( ClientShadowHandle_t Handle ) { m_FlashlightHandle = Handle;	}
	
protected:

	void LightOff();
#if !defined( L4D2 )
	void LightOffOld();
#endif
	void LightOffNew();

#if defined( L4D2 )
	void UpdateTextureName( const char *textureName );
	bool ComputeLightPosAndOrientation( const Vector &origin, const Vector &forward,
		const Vector &right, const Vector &up, Vector &lightOrigin, Quaternion &orientation,
		bool tracePlayers );
	bool UpdateDefaultFlashlightState( FlashlightState_t &state, const Vector &origin,
		const Vector &forward, const Vector &right, const Vector &up, bool shadows, bool tracePlayers );
	void UpdateLightTopDown( const Vector &origin, const Vector &forward, const Vector &right, const Vector &up );
	void SubmitFlashlightState( FlashlightState_t &state );
#else
	void UpdateLightNew(const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp);
	void UpdateLightOld(const Vector &vecPos, const Vector &vecDir, int nDistance);
#endif

	bool m_bIsOn;
	int m_nEntIndex;
	ClientShadowHandle_t m_FlashlightHandle;

#if !defined( L4D2 )
	// Vehicle headlight dynamic light pointer used by the stock old projector.
	dlight_t *m_pPointLight;
#endif
	float m_flDistMod;

	// Texture for flashlight
	CTextureReference m_FlashlightTexture;
#if defined( L4D2 )
	CTextureReference m_MuzzleFlashTexture;
	bool m_bMuzzleFlash;
	float m_flMuzzleFlashBrightness;
	float m_flFlashlightFOV, m_flFlashlightFarZ, m_flFlashlightLinear;
	bool m_bFlashlightShadows;
	char m_szFlashlightTextureName[64];
#endif
};

class CHeadlightEffect : public CFlashlightEffect
{
public:
	
	CHeadlightEffect();
	~CHeadlightEffect();

	virtual void UpdateLight(const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance);
};



#endif // FLASHLIGHTEFFECT_H
