// Whole-file composition into EXISTING client/l4d2/terror_flashlight.cpp.
// No new TU, second manager, CAutoGameSystem phase substitute or ready-bool
// projector surrogate. Existing real CFlashlightEffect is the sole projector.
// ResetTerrorFlashlight belongs at actual client shadow-manager LevelShutdown
// entry (nativeD0300) and native BasePlayer695A0 On[0] cleanup, not another hook.
#include "cbase.h"
#include "l4d2/effects/terror_flashlight.h"
#include "flashlighteffect.h"
#include "tier0/memdbgon.h"
#include "shadowmaps_dx12.h"


// Preserve original definitions/owners; input0 explicitly normalizes to2.
ConVar r_flashlightmuzzleflash( "r_flashlightmuzzleflash", "1", 0x0000000A );
ConVar r_flashlightmuzzleflashfov( "r_flashlightmuzzleflashfov", "120", 0x00004008 );
ConVar r_flashlight_attach_to_viewmodel( "r_flashlight_attach_to_viewmodel", "1", 0x00004008, "Attach the flashlight effect to the viewmodel" );
ConVar r_flashlightnearoffsetscale( "r_flashlightnearoffsetscale", "0.3", 0x00004008 );
ConVar r_flashlightbacktraceoffset( "r_flashlightbacktraceoffset", "0.4", 0x00004008 );
ConVar r_flashlighttracedistcutoff( "r_flashlighttracedistcutoff", "32", 0x00004008 );
ConVar r_flashlighttracedistwatercutoff( "r_flashlighttracedistwatercutoff", "80", 0x00004008 );
ConVar r_flashlightoffsetright( "r_flashlightoffsetright", "5.0", 0x00004008 );
ConVar r_flashlightoffsetup( "r_flashlightoffsetup", "-5.0", 0x00004008 );
ConVar r_flashlightoffsetforward( "r_flashlightoffsetforward", "0.0", 0x00004008 );
ConVar r_flashlightoffsetright_low( "r_flashlightoffsetright_low", "10.0", 0x00004008 );
ConVar r_flashlightoffsetup_low( "r_flashlightoffsetup_low", "-16.0", 0x00004008 );
ConVar r_flashlightoffsetforward_low( "r_flashlightoffsetforward_low", "0.0", 0x00004008 );

class CTerrorFlashlight
{
public:
	CTerrorFlashlight() : m_pEffect( NULL ), m_pTextureName( NULL ), m_nEntIndex( -1 ),
		m_flFOV( 0.0f ), m_flFarZ( 0.0f ), m_flLinear( 0.0f ), m_nMuzzleFrames( 0 ),
		m_flMuzzleBrightness( 1.0f ), m_bFlashlightOn( false ), m_nLastMuzzleFrame( -1 ), m_bOverride( false ) {}
	~CTerrorFlashlight() { delete m_pEffect; }
	void SetOwner( int entIndex ) { m_nEntIndex = entIndex; }

	// Whole63160. Preserve On=true even when actual new allocation fails.
	void TurnOn( int entIndex, const char *textureName, float fov, float farZ, float linear )
	{
		m_flFOV = fov;
		m_flFarZ = farZ;
		m_pTextureName = textureName;
		m_nEntIndex = entIndex;
		m_flLinear = linear;
		m_bFlashlightOn = true;
		if ( !m_bOverride )
		{
			if ( !m_pEffect ) m_pEffect = new CFlashlightEffect( m_nEntIndex, textureName,
				textureName ? fov : 0.0f, textureName ? farZ : 0.0f, textureName ? linear : 0.0f );
			if ( m_pEffect ) m_pEffect->TurnOn();
		}
	}
	void TurnOff() // Native67B00 inlined manager branch.
	{
		m_pTextureName = NULL;
		m_bFlashlightOn = false;
		if ( !m_bOverride && m_nMuzzleFrames == 0 &&
			static_cast< double >( gpGlobals->curtime ) >= m_muzzleTimer.m_timestamp.Get() )
		{
			delete m_pEffect;
			m_pEffect = NULL;
		}
	}
	void Reset() // NativeD0300 /695A0: do NOT reset brightness, frame, owner, params or duration.
	{
		m_pTextureName = NULL;
		m_nMuzzleFrames = 0;
		m_bFlashlightOn = false;
		m_bOverride = false;
		if ( m_muzzleTimer.m_timestamp.Get() != -1.0f ) m_muzzleTimer.Invalidate();
		delete m_pEffect;
		m_pEffect = NULL;
	}
	void MuzzleFlash() // Existing actual2F9110 caller remains sole.
	{
		if ( !r_flashlightmuzzleflash.GetBool() ) return;
		m_nMuzzleFrames = 2;
		m_muzzleTimer.Start( 0.066f );
		m_flMuzzleBrightness = RandomFloat( 0.4f, 2.0f );
	}
	void Update( const Vector &origin, const Vector &forward, const Vector &right,
		const Vector &up, float fov, bool shadows, float farZ, float linear, const char *textureName )
	{
		if ( m_bOverride ) return;
		const bool muzzle = m_nMuzzleFrames > 0 ||
			static_cast< double >( gpGlobals->curtime ) < m_muzzleTimer.m_timestamp.Get();
		if ( m_pEffect )
		{
			m_flFOV = fov;
			m_flFarZ = farZ;
			m_flLinear = linear;
			m_pEffect->UpdateLight( m_nEntIndex, origin, forward, right, up,
				fov, farZ, linear, shadows, textureName );
			// Native state changes AFTER physical submission: retain that latency.
			m_pEffect->SetMuzzleFlashState( muzzle, m_flMuzzleBrightness );
		}
		if ( muzzle )
		{
			if ( !m_bFlashlightOn && !m_pEffect )
			{
				m_pEffect = new CFlashlightEffect( m_nEntIndex, NULL, 0.0f, 0.0f, 0.0f );
				if ( m_pEffect ) m_pEffect->SetMuzzleFlashState( muzzle, m_flMuzzleBrightness );
				// No TurnOn/projection here: native first muzzle-only projection is next update.
			}
			if ( m_nLastMuzzleFrame != gpGlobals->framecount )
			{
				m_nMuzzleFrames = static_cast< int >( static_cast< unsigned int >( m_nMuzzleFrames ) - 1u );
				m_nLastMuzzleFrame = gpGlobals->framecount;
			}
		}
		else if ( !m_bFlashlightOn && m_pEffect )
		{
			delete m_pEffect;
			m_pEffect = NULL;
		}
	}
	void SetOverride( bool enabled ) // Actual mode2707E0 enable /26EF60 release semantics.
	{
		m_bOverride = enabled;
		if ( enabled ) return;
		if ( m_bFlashlightOn )
		{
			if ( !m_pEffect ) TurnOn( m_nEntIndex, m_pTextureName, m_flFOV, m_flFarZ, m_flLinear );
		}
		else if ( m_pEffect )
		{
			delete m_pEffect; // Native release ignores still-active muzzle timer in this branch.
			m_pEffect = NULL;
		}
	}
	void UpdateOverride( const Vector &origin, const Vector &forward, const Vector &right,
		const Vector &up, float fov, ITexture *texture, const Vector &color )
	{
		if ( !m_bOverride ) return;
		if ( !m_pEffect ) m_pEffect = new CFlashlightEffect( m_nEntIndex, NULL, 0.0f, 0.0f, 0.0f );
		if ( m_pEffect ) m_pEffect->UpdateLightOverride( m_nEntIndex, origin, forward, right, up,
			fov, true, texture, color, false ); // Exact mode2707E0 -> physicalEEB50 fixed booleans.
	}
private:
	CFlashlightEffect *m_pEffect;
	const char *m_pTextureName;
	int m_nEntIndex;
	float m_flFOV, m_flFarZ, m_flLinear;
	int m_nMuzzleFrames;
	CountdownTimer m_muzzleTimer;
	float m_flMuzzleBrightness;
	bool m_bFlashlightOn;
	int m_nLastMuzzleFrame;
	bool m_bOverride;
};

static ConVar r_flashlight_teammate_max( "r_flashlight_teammate_max", "3", FCVAR_ARCHIVE,
	"DX12: maximum projected flashlight spots for other survivors", true, 0, true, MAX_TERROR_TEAMMATE_FLASHLIGHTS );
struct TerrorTeammateFlashlight_t
{
	CFlashlightEffect *m_pEffect;
	int m_nEntIndex, m_nLastFrame;
	float m_flDistanceSqr;
	TerrorTeammateFlashlight_t() : m_pEffect( NULL ), m_nEntIndex( -1 ), m_nLastFrame( -1 ), m_flDistanceSqr( FLT_MAX ) {}
};
static TerrorTeammateFlashlight_t s_TeammateFlashlights[MAX_TERROR_TEAMMATE_FLASHLIGHTS];
static int s_nLastTeammateDisabledResetFrame = -1;
static void ResetTerrorTeammateFlashlights()
{
	for ( int i = 0; i < ARRAYSIZE( s_TeammateFlashlights ); ++i )
	{
		delete s_TeammateFlashlights[i].m_pEffect;
		s_TeammateFlashlights[i] = TerrorTeammateFlashlight_t();
	}
}
void UpdateTerrorTeammateFlashlight( int entIndex, const Vector &origin, const Vector &forward,
	const Vector &right, const Vector &up )
{
	if ( !ShadowMapsDX12_ProjectedLightsAvailable() || r_flashlight_teammate_max.GetInt() <= 0 )
	{
		if ( s_nLastTeammateDisabledResetFrame != gpGlobals->framecount )
		{
			ResetTerrorTeammateFlashlights();
			s_nLastTeammateDisabledResetFrame = gpGlobals->framecount;
		}
		return;
	}
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( !local || local->entindex() == entIndex ) return;
	s_nLastTeammateDisabledResetFrame = -1;
	const int maxLights = r_flashlight_teammate_max.GetInt() < ARRAYSIZE( s_TeammateFlashlights )
		? r_flashlight_teammate_max.GetInt() : ARRAYSIZE( s_TeammateFlashlights );
	for ( int i = maxLights; i < ARRAYSIZE( s_TeammateFlashlights ); ++i )
	{
		delete s_TeammateFlashlights[i].m_pEffect;
		s_TeammateFlashlights[i] = TerrorTeammateFlashlight_t();
	}
	const float distanceSqr = ( origin - local->EyePosition() ).LengthSqr();
	int slot = -1;
	for ( int i = 0; i < maxLights; ++i )
		if ( s_TeammateFlashlights[i].m_nEntIndex == entIndex || s_TeammateFlashlights[i].m_nLastFrame != gpGlobals->framecount ) { slot = i; break; }
	if ( slot < 0 )
	{
		slot = 0;
		for ( int i = 1; i < maxLights; ++i )
			if ( s_TeammateFlashlights[i].m_flDistanceSqr > s_TeammateFlashlights[slot].m_flDistanceSqr ) slot = i;
		if ( distanceSqr >= s_TeammateFlashlights[slot].m_flDistanceSqr ) return;
		delete s_TeammateFlashlights[slot].m_pEffect;
		s_TeammateFlashlights[slot] = TerrorTeammateFlashlight_t();
	}
	TerrorTeammateFlashlight_t &light = s_TeammateFlashlights[slot];
	if ( !light.m_pEffect )
	{
		light.m_pEffect = new CFlashlightEffect( entIndex, NULL, 53.0f, 750.0f, 100.0f );
		if ( light.m_pEffect ) light.m_pEffect->TurnOn();
	}
	if ( light.m_pEffect ) light.m_pEffect->UpdateLight( entIndex, origin, forward, right, up, 53.0f, 750.0f, 100.0f, true, NULL );
	light.m_nEntIndex = entIndex;
	light.m_nLastFrame = gpGlobals->framecount;
	light.m_flDistanceSqr = distanceSqr;
}

void RemoveTerrorTeammateFlashlight( int entIndex )
{
	for ( int i = 0; i < ARRAYSIZE( s_TeammateFlashlights ); ++i )
		if ( s_TeammateFlashlights[i].m_nEntIndex == entIndex )
		{
			delete s_TeammateFlashlights[i].m_pEffect;
			s_TeammateFlashlights[i] = TerrorTeammateFlashlight_t();
		}
}

static CTerrorFlashlight s_TerrorFlashlight;
void ResetTerrorTeammateFlashlightsOnLevelShutdown() { ResetTerrorTeammateFlashlights(); }
void SetTerrorFlashlightOwner( int entIndex ) { s_TerrorFlashlight.SetOwner( entIndex ); }
void TurnOnTerrorFlashlight( int entIndex, const char *texture, float fov, float farZ, float linear )
{
	s_TerrorFlashlight.TurnOn( entIndex, texture, fov, farZ, linear );
}
void TurnOffTerrorFlashlight() { s_TerrorFlashlight.TurnOff(); }
void ResetTerrorFlashlight() { s_TerrorFlashlight.Reset(); }
void UpdateTerrorFlashlight( const Vector &origin, const Vector &forward, const Vector &right,
	const Vector &up, float fov, bool shadows, float farZ, float linear, const char *texture )
{
	s_TerrorFlashlight.Update( origin, forward, right, up, fov, shadows, farZ, linear, texture );
}
void TerrorViewmodelMuzzleFlash() { s_TerrorFlashlight.MuzzleFlash(); }
void SetTerrorFlashlightOverride( bool enabled ) { s_TerrorFlashlight.SetOverride( enabled ); }
void UpdateTerrorFlashlightOverride( const Vector &origin, const Vector &forward, const Vector &right,
	const Vector &up, float fov, ITexture *texture, const Vector &color )
{
	s_TerrorFlashlight.UpdateOverride( origin, forward, right, up, fov, texture, color );
}
