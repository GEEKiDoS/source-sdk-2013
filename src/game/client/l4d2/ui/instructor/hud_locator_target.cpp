//========= Copyright (c) 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: See header file
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "l4d2/ui/instructor/hud_locator_target.h"
#include "iclientmode.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui/IVGUI.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Label.h>
#include <vgui/IInput.h>
#include <vgui/IScheme.h>
#include "iinput.h"
#include "view.h"
#include "hud.h"
#include "hudelement.h"
#include "vgui_int.h"
#include "inputsystem/IInputSystem.h"
#include "steam/steam_api.h"
#include "ienginevgui.h"
#include <emmintrin.h>

// Pinned L4D2 C2AE350..C2B4630. Published Valve locator algorithms are used
// only where the pinned binary agrees; no ASW-only UI-target features.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define ICON_SIZE			0.04f	// Icons are ScreenWidth() * ICON_SIZE wide.
#define ICON_GAP			5		// Number of pixels between the icon and the text

#define OFFSCREEN_ICON_POSITION_RADIUS 100

#define CAPTION_FONT_HANDLE m_hCaptionFont
#define BUTTON_FONT_HANDLE m_hButtonFont

#define ICON_DIST_TOO_FAR	(60.0f * 12.0f)

#define MIN_ICON_ALPHA 0.5
#define MAX_ICON_ALPHA 1

#define MIN_ICON_SCALE 1.0f
#define MAX_ICON_SCALE 1.75f

#define LOCATOR_OCCLUSION_TEST_RATE 0.25f

enum
{
	DRAW_ARROW_NO = 0,
	DRAW_ARROW_UP,
	DRAW_ARROW_DOWN,
	DRAW_ARROW_LEFT,
	DRAW_ARROW_RIGHT
};





ConVar locator_fade_time( "locator_fade_time", "0.3", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Number of seconds it takes for a lesson to fully fade in/out." );
ConVar locator_lerp_speed( "locator_lerp_speed", "5.0f", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Speed that static lessons move along the Y axis." );
ConVar locator_lerp_rest( "locator_lerp_rest", "2.25f", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Number of seconds before moving from the center." );
ConVar locator_lerp_time( "locator_lerp_time", "1.75f", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Number of seconds to lerp before reaching final destination" );
ConVar locator_pulse_time( "locator_pulse_time", "1.0f", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Number of seconds to pulse after changing icon or position" );

ConVar locator_split_maxwide_percent( "locator_split_maxwide_percent", "0.80f", FCVAR_CHEAT | FCVAR_CLIENTDLL );
ConVar locator_split_len( "locator_split_len", "0.5f", FCVAR_CHEAT | FCVAR_CLIENTDLL );
// Native SSE conversions retain EAX32 and the invalid-result INT_MIN.
static int NativeLocatorInteger( float value ) { return _mm_cvttss_si32( _mm_set_ss( value ) ); }
static int NativeLocatorInteger( double value ) { return _mm_cvttsd_si32( _mm_set_sd( value ) ); }

//------------------------------------
CLocatorTarget::CLocatorTarget( void )
{
	Deactivate( true );
	// Native omits these pre-writer stores; inactive sentinels skip only
	// undefined first-read visibility/controller state.
	m_bVisible = m_bIsDrawing = m_bWasControllerLast = m_bSteamControllerLast = false;
	for ( int i = 0; i < MAX_LOCATOR_BINDINGS_SHOWN; ++i ) { m_iSteamTextures[i] = -1; m_iSteamOrigins[i] = 0; }
}
CLocatorTarget::~CLocatorTarget()
{
	for ( int i = 0; i < MAX_LOCATOR_BINDINGS_SHOWN; ++i )
		if ( m_iSteamTextures[i] > 0 ) vgui::surface()->DestroyTextureID( m_iSteamTextures[i] );
}

//------------------------------------
void CLocatorTarget::Activate( int serialNumber )
{ 
	m_serialNumber = serialNumber; 
	m_frameLastUpdated = gpGlobals->framecount;
	m_isActive = true;

	m_bOnscreen = true;
	m_alpha = 0;
	m_fadeStart = gpGlobals->curtime;


	int iStartX = ScreenWidth() / 2;
	int iStartY = ScreenHeight() / 4;


	m_lastXPos = iStartX;
	m_lastYPos = iStartY;

	m_drawArrowDirection = DRAW_ARROW_NO;
	m_lerpStart = gpGlobals->curtime;
	m_pulseStart = gpGlobals->curtime;
	m_declutterIndex = 0;
	m_lastDeclutterIndex = 0;
	AddIconEffects( LOCATOR_ICON_FX_FADE_IN );
}

//------------------------------------
void CLocatorTarget::Deactivate( bool bNoFade )						
{ 
	if ( ( engine && engine->IsPaused() ) || ( enginevgui && enginevgui->IsGameUIVisible() ) || bNoFade || m_alpha == 0 ||
		 ( m_bOccluded && !( m_iEffectsFlags & LOCATOR_ICON_FX_FORCE_CAPTION ) ) || 
		 ( !m_bOnscreen && ( m_iEffectsFlags & LOCATOR_ICON_FX_NO_OFFSCREEN ) ) )
	{

		m_serialNumber = -1;
		m_isActive = false;
		m_frameLastUpdated = 0;
		m_pIcon_onscreen = NULL;
		m_pIcon_offscreen = NULL;
		m_bDrawControllerButton = false;
		m_bDrawControllerButtonOffscreen = false;
		m_iEffectsFlags = LOCATOR_ICON_FX_NONE;
		m_captionWide = 0;

		m_pchDrawBindingName = NULL;
		m_pchDrawBindingNameOffscreen = NULL;
		m_widthScale_onscreen = 1.0f;
		m_bOccluded = false;
		m_alpha = 0;


		m_szBinding = "";
		m_iBindingTick = 0;
		m_flNextBindingTick = 0.0f;
		m_flNextOcclusionTest = 0.0f;
		m_iBindingChoicesCount = 0;

		m_wszCaption.RemoveAll();
		m_wszCaption.AddToTail( (wchar_t)0 );
	}
	else if ( !( m_iEffectsFlags & LOCATOR_ICON_FX_FADE_OUT ) )
	{
		// Determine home much time it would have spent fading to reach the current alpha
		float flAssumedFadeTime;
		flAssumedFadeTime = ( 1.0f - static_cast<float>( m_alpha ) * 0.0039215689f ) * locator_fade_time.GetFloat();

		// Set the fade
		m_fadeStart = gpGlobals->curtime - flAssumedFadeTime;
		AddIconEffects( LOCATOR_ICON_FX_FADE_OUT );
		RemoveIconEffects( LOCATOR_ICON_FX_FADE_IN );
	}
}

//------------------------------------
void CLocatorTarget::Update()							
{
	m_frameLastUpdated = gpGlobals->framecount;

	if ( m_bVisible && ( m_iEffectsFlags & LOCATOR_ICON_FX_FADE_OUT ) )
	{
		// Determine home much time it would have spent fading to reach the current alpha
		float flAssumedFadeTime;
		flAssumedFadeTime = ( 1.0f - static_cast<float>( m_alpha ) * 0.0039215689f ) * locator_fade_time.GetFloat();

		// Set the fade
		m_fadeStart = gpGlobals->curtime - flAssumedFadeTime;
		RemoveIconEffects( LOCATOR_ICON_FX_FADE_OUT );
	}
}

int CLocatorTarget::GetIconX( void )
{
	return m_iconX;
}

int CLocatorTarget::GetIconY( void )
{
	return m_iconY;
}

int CLocatorTarget::GetIconCenterX( void )
{
	return m_centerX;
}

int CLocatorTarget::GetIconCenterY( void )
{
	return m_centerY;
}

void CLocatorTarget::SetVisible( bool bVisible )
{
	if ( m_bVisible == bVisible )
	{
		// They are already the same
		return;
	}

	m_bVisible = bVisible;

	if ( bVisible )
	{
		// Determine home much time it would have spent fading to reach the current alpha
		float flAssumedFadeTime;
		flAssumedFadeTime = ( static_cast<float>( m_alpha ) * 0.0039215689f ) * locator_fade_time.GetFloat();

		// Set the fade
		m_fadeStart = gpGlobals->curtime - flAssumedFadeTime;
		AddIconEffects( LOCATOR_ICON_FX_FADE_IN );
		RemoveIconEffects( LOCATOR_ICON_FX_FADE_OUT );
	}
	else
	{
		// Determine home much time it would have spent fading to reach the current alpha
		float flAssumedFadeTime;
		flAssumedFadeTime = ( 1.0f - static_cast<float>( m_alpha ) * 0.0039215689f ) * locator_fade_time.GetFloat();

		// Set the fade
		m_fadeStart = gpGlobals->curtime - flAssumedFadeTime;
		AddIconEffects( LOCATOR_ICON_FX_FADE_OUT );
		RemoveIconEffects( LOCATOR_ICON_FX_FADE_IN );
	}
}
bool CLocatorTarget::IsVisible( void )
{
	return m_bVisible;
}

void CLocatorTarget::SetCaptionText( const char *text, const char *parameter )
{
	wchar_t result[256]; result[0] = 0;
	if ( parameter && parameter[0] )
	{
		wchar_t parameterBuffer[128];
		const wchar_t *localized = parameter[0] == '#' ? g_pVGuiLocalize->Find( parameter ) : NULL;
		if ( !localized ) { g_pVGuiLocalize->ConvertANSIToUnicode( parameter, parameterBuffer, sizeof( parameterBuffer ) ); localized = parameterBuffer; }
		const wchar_t *format = g_pVGuiLocalize->Find( text );
		if ( !format ) return; // Skip only the native undefined NULL-format write.
		g_pVGuiLocalize->ConstructString( result, sizeof( result ), format, 1, localized );
	}
	else g_pVGuiLocalize->ConstructString( result, sizeof( result ), text, (KeyValues *)NULL );
	int length = V_wcslen( result ) + 1;
	m_wszCaption.SetCount( length );
	V_wcsncpy( m_wszCaption.Base(), result, length * sizeof( wchar_t ) );
}

void CLocatorTarget::SetCaptionColor( const char *pszCaptionColor )
{
	int r,g,b;
	r = g = b = 0;

	CSplitString colorValues( pszCaptionColor, "," );

	if( colorValues.Count() == 3 )
	{
		r = atoi( colorValues[0] );
		g = atoi( colorValues[1] );
		b = atoi( colorValues[2] );

		m_captionColor.SetColor( r,g,b, 255 );
	}
	else
	{
		DevWarning( "caption_color format incorrect. RRR,GGG,BBB expected.\n");
	}
}

bool CLocatorTarget::IsStatic()
{
	return ( ( m_iEffectsFlags & LOCATOR_ICON_FX_STATIC ) || IsPresenting() );
}

bool CLocatorTarget::IsPresenting()
{
	return !( m_iEffectsFlags & LOCATOR_ICON_FX_NO_PRESENT ) && gpGlobals->curtime - m_lerpStart < locator_lerp_rest.GetFloat();
}

void CLocatorTarget::StartTimedLerp()
{
	if ( gpGlobals->curtime - m_lerpStart > locator_lerp_rest.GetFloat() )
	{
		m_lerpStart = gpGlobals->curtime - locator_lerp_rest.GetFloat();
	}
}

void CLocatorTarget::StartPresent()
{
	m_lerpStart = gpGlobals->curtime;
}


void CLocatorTarget::EndPresent()
{
	if ( gpGlobals->curtime - m_lerpStart < locator_lerp_rest.GetFloat() )
	{
		m_lerpStart = gpGlobals->curtime - locator_lerp_rest.GetFloat();
	}
}


//------------------------------------
void CLocatorTarget::SetOnscreenIconTextureName( const char *pszTexture )
{
	if ( Q_strcmp( m_szOnscreenTexture.String(), pszTexture ) == 0 )
		return;

	m_szOnscreenTexture = pszTexture;
	m_pIcon_onscreen = NULL; // Dirty the onscreen icon so that the Locator will look up the new icon by name.

	m_pulseStart = gpGlobals->curtime;
}

//------------------------------------
void CLocatorTarget::SetOffscreenIconTextureName( const char *pszTexture )
{
	if ( Q_strcmp( m_szOffscreenTexture.String(), pszTexture ) == 0 )
		return;

	m_szOffscreenTexture = pszTexture;
	m_pIcon_offscreen = NULL; // Ditto

	m_pulseStart = gpGlobals->curtime;
}

//------------------------------------
void CLocatorTarget::AddSteamOrigins( const int *origins, int token )
{
	ISteamInput *steam = steamapicontext ? steamapicontext->SteamInput() : NULL;
	if ( !steam ) return;
	for ( int i = 0; i < MAX_LOCATOR_BINDINGS_SHOWN && origins[i] != 0 && m_iBindingChoicesCount < MAX_LOCATOR_BINDINGS_SHOWN; ++i )
	{
		// Native C2B0B20 indexes the texture cache by origin-within-token,
		// NOT by accumulated binding choice. Preserve that native quirk.
		if ( m_iSteamOrigins[i] != origins[i] )
		{
			m_iSteamOrigins[i] = origins[i];
			if ( m_iSteamTextures[i] > 0 ) { vgui::surface()->DestroyTextureID( m_iSteamTextures[i] ); m_iSteamTextures[i] = -1; }
			int pixels = NativeLocatorInteger( (float)NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE ) * 0.85000002f );
			unsigned int power = (unsigned int)pixels;
			if ( power && ( power & ( power - 1 ) ) ) { --power; power |= power >> 1; power |= power >> 2; power |= power >> 4; power |= power >> 8; power |= power >> 16; ++power; }
			ESteamInputGlyphSize size = power > 128 ? k_ESteamInputGlyphSize_Large : power > 32 ? k_ESteamInputGlyphSize_Medium : k_ESteamInputGlyphSize_Small;
			steam->GetGlyphPNGForActionOrigin( (EInputActionOrigin)origins[i], size, 1 );
			// Main-approved C2B0AB0 PNG-decode capability gap: retain -1,
			// origin admission and binding text. No fabricated texture.
		}
		m_pchBindingChoices[m_iBindingChoicesCount] = steam->GetStringForActionOrigin( (EInputActionOrigin)origins[i] );
		m_iBindChoicesOriginalToken[m_iBindingChoicesCount++] = token;
	}
}
void CLocatorTarget::SetBinding( const char *binding )
{
	static ConVarRef joystick( "joystick" );
	bool steamActive = g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
	int filter = steamActive ? 2 : ( joystick.GetBool() ? 1 : 0 );
	if ( m_bWasControllerLast == ( filter != 0 ) && !Q_strcmp( m_szBinding.String(), binding ) ) return;
	m_bWasControllerLast = filter != 0;
	m_bSteamControllerLast = steamActive;
	m_szBinding = binding;
	m_pIcon_onscreen = m_pIcon_offscreen = NULL;
	m_flNextBindingTick = gpGlobals->curtime + 0.75f;
	m_iBindingChoicesCount = 0;
	ISteamInput *steam = steamapicontext ? steamapicontext->SteamInput() : NULL;
	InputHandle_t controller = 0;
	if ( steam ) { InputHandle_t controllers[STEAM_INPUT_MAX_COUNT]; if ( steam->GetConnectedControllers( controllers ) > 0 ) controller = controllers[0]; }
	int tokenIndex = 0;
	char token[128];
	const char *next = nexttoken( token, m_szBinding.String(), ';' );
	while ( next )
	{
		const char *action = token[0] == '+' ? token + 1 : token;
		if ( filter == 2 && controller && steam && g_pInputSystem )
		{
			InputActionSetHandle_t set = g_pInputSystem->GetActionSetHandle( GAME_ACTION_SET_FPSCONTROLS );
			EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS] = {};
			InputDigitalActionHandle_t digital = steam->GetDigitalActionHandle( action );
			if ( digital ) steam->GetDigitalActionOrigins( controller, set, digital, origins );
			else
			{
				if ( !Q_strcmp( action, "forward" ) ) action = "Move";
				InputAnalogActionHandle_t analog = steam->GetAnalogActionHandle( action );
				if ( analog ) steam->GetAnalogActionOrigins( controller, set, analog, origins );
			}
			AddSteamOrigins( (const int *)origins, tokenIndex );
			if ( !Q_strcmp( action, "invnext" ) || !Q_strcmp( action, "invprev" ) )
			{
				const char *extra[] = { !Q_strcmp( action, "invnext" ) ? "invprev" : "invnext", "slot1", "slot2" };
				for ( int i = 0; i < ARRAYSIZE( extra ) && m_iBindingChoicesCount < MAX_LOCATOR_BINDINGS_SHOWN; ++i )
				{
					Q_memset( origins, 0, sizeof( origins ) );
					digital = steam->GetDigitalActionHandle( extra[i] );
					steam->GetDigitalActionOrigins( controller, set, digital, origins );
					AddSteamOrigins( (const int *)origins, tokenIndex );
				}
			}
		}
		else if ( g_pInputSystem )
		{
			// Main-approved real target projection of absent Key_LookupBindingEx.
			for ( int i = KEY_FIRST; i <= BUTTON_CODE_LAST && m_iBindingChoicesCount < MAX_LOCATOR_BINDINGS_SHOWN; ++i )
			{
				ButtonCode_t code = (ButtonCode_t)i;
				bool controllerCode = IsJoystickCode( code );
				if ( ( filter == 0 && controllerCode ) || ( filter == 1 && !controllerCode ) ) continue;
				if ( controllerCode && GetJoystickForCode( code ) != 0 ) continue;
				const char *bound = engine->Key_BindingForKey( code );
				if ( !bound ) continue;
				if ( *bound == '+' ) ++bound;
				if ( Q_stricmp( bound, action ) ) continue;
				m_pchBindingChoices[m_iBindingChoicesCount] = g_pInputSystem->ButtonCodeToString( code );
				m_iBindChoicesOriginalToken[m_iBindingChoicesCount++] = tokenIndex;
			}
		}
		++tokenIndex;
		next = nexttoken( token, next, ';' );
	}
	if ( m_bSteamControllerLast && m_iBindingChoicesCount == 0 ) m_bSteamControllerLast = false;
	m_pulseStart = gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CLocatorTarget::UseBindingImage( char *image, size_t bytes, float *widthScale )
{
	bool steam = g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
	if ( m_iBindingChoicesCount <= 0 )
	{
		Q_strncpy( image, steam ? "icon_blank" : "icon_key_wide", bytes );
		if ( !steam ) *widthScale = 4.0f;
		return "#L4D_Zombie_UI_Not_Bound";
	}
	const char *key = m_pchBindingChoices[(unsigned int)m_iBindingTick % m_iBindingChoicesCount];
	if ( !key ) return NULL; // Native stale-pointer prerequisite, no invented key.
	static ConVarRef joystick( "joystick" );
	static const char *const buttons[] = { "A_BUTTON", "B_BUTTON", "X_BUTTON", "Y_BUTTON", "L_SHOULDER", "R_SHOULDER", "BACK", "START", "STICK1", "STICK2", "UP", "DOWN", "LEFT", "RIGHT" };
	bool button = false;
	if ( joystick.GetBool() ) for ( int i = 0; i < ARRAYSIZE( buttons ); ++i ) if ( !Q_strcmp( key, buttons[i] ) ) { button = true; break; }
	if ( steam || button ) { Q_strncpy( image, "icon_blank", bytes ); return key; }
	static const struct { const char *key, *image; } icons[] =
	{
		{ "MOUSE1", "icon_mouseLeft" }, { "MOUSE2", "icon_mouseRight" }, { "MOUSE3", "icon_mouseThree" },
		{ "MWHEELUP", "icon_mouseWheel_up" }, { "MWHEELDOWN", "icon_mouseWheel_down" },
		{ "UPARROW", "icon_key_up" }, { "LEFTARROW", "icon_key_left" }, { "DOWNARROW", "icon_key_down" }, { "RIGHTARROW", "icon_key_right" }
	};
	for ( int i = 0; i < ARRAYSIZE( icons ); ++i ) if ( !Q_strcmp( key, icons[i].key ) ) { Q_strncpy( image, icons[i].image, bytes ); return NULL; }
	static const char *const narrow[] = { "SEMICOLON", "INS", "DEL", "HOME", "END", "PGUP", "PGDN", "PAUSE", "F10", "F11", "F12" };
	int length = Q_strlen( key ); bool small = length <= 2;
	for ( int i = 0; i < ARRAYSIZE( narrow ) && !small; ++i ) small = !Q_strcmp( key, narrow[i] );
	Q_strncpy( image, small ? "icon_key_generic" : "icon_key_wide", bytes );
	if ( !small ) *widthScale = length <= 6 ? 2.0f : 3.0f;
	return key;
}

//-----------------------------------------------------------------------------
int CLocatorTarget::GetIconWidth( void )
{
	return m_wide;
}

//-----------------------------------------------------------------------------
int CLocatorTarget::GetIconHeight( void )
{
	return m_tall;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CLocatorPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CLocatorPanel, vgui::EditablePanel );
public:
	CLocatorPanel( vgui::Panel *parent, const char *name );
	~CLocatorPanel( void );

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void PerformLayout( void );
	virtual void OnTick( void );
	virtual void PaintBackground( void );
	virtual void Paint( void );
	void ValidateTexture( int *pTextureID, const char *pszTextureName );
	bool ValidateTargetTextures( CLocatorTarget *pTarget );
	bool IconsAreIntersecting( CLocatorTarget &first, CLocatorTarget &second, int iTolerance );
	virtual void PaintTarget( CLocatorTarget *pTarget );

	void DrawStaticIcon( CLocatorTarget *pTarget );
	void DrawDynamicIcon( CLocatorTarget *pTarget, bool bDrawCaption, bool bDrawSimpleArrow );
	void DrawIndicatorArrow( int x, int y, int iconWide, int iconTall, int textWidth, int direction );
	void DrawTargetCaption( CLocatorTarget *pTarget, int x, int y, bool bDrawMultiline );
	int  GetScreenWidthForCaption( const wchar_t *pString, vgui::HFont hFont );
	void DrawBindingName( CLocatorTarget *pTarget, const char *pchBindingName, int x, int y, bool bController );
	void ComputeTargetIconPosition( CLocatorTarget *pTarget, bool bSetPosition );
	void CalculateOcclusion( CLocatorTarget *pTarget );

	void DrawSimpleArrow( int x, int y, int iconWide, int iconTall );
	void GetIconPositionForOffscreenTarget( const Vector &vecDelta, float flDist, int *pXPos, int *pYPos );

	CLocatorTarget *GetPointerForHandle( int hTarget );
	int		AddTarget();
	void	RemoveTarget( int hTarget );

	void	GetTargetPosition( const Vector &vecDelta, float flRadius, float *xpos, float *ypos, float *flRotation );

	void	DeactivateAllTargets();
	void	CollectGarbage();

	// Animation
	void	AnimateIconSize( int flags, int *wide, int *tall, float fPulseStart );
	void	AnimateIconPosition( int flags, int *x, int *y );
	void	AnimateIconAlpha( int flags, int *alpha, float fadeStart );

private:
	CPanelAnimationVar( vgui::HFont, m_hCaptionFont, "font", "InstructorTitle" );
	CPanelAnimationVar( vgui::HFont, m_hCaptionFont_ss, "font", "InstructorTitle_ss" );
	CPanelAnimationVar( vgui::HFont, m_hKeysFont, "font", "MenuSubTitle" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFont, "font", "InstructorButtons" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFont_ss, "font", "GameUIButtons" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFontSC, "font", "InstructorButtonsSteamController" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFontSC_ss, "font", "InstructorButtonsSteamController_ss" );

	CPanelAnimationVar( int, m_iShouldWrapStaticLocators, "WrapStaticLocators", "0" );


	

	static	int		m_serializer;			// Used to issue unique serial numbers to targets, for use as handles
	int				m_textureID_ArrowRight;
	int				m_textureID_ArrowLeft;
	int				m_textureID_ArrowUp;
	int				m_textureID_ArrowDown;
	int				m_textureID_SimpleArrow;

	int				m_staticIconPosition;// Helps us stack static icons

	CLocatorTarget	m_targets[MAX_LOCATOR_TARGETS];
};

//-----------------------------------------------------------------------------
// Local variables
//-----------------------------------------------------------------------------
static CLocatorPanel *s_pLocatorPanel;
inline CLocatorPanel *GetPlayerLocatorPanel() { return s_pLocatorPanel; }


//-----------------------------------------------------------------------------
// Static variable initialization
//-----------------------------------------------------------------------------
int	CLocatorPanel::m_serializer = 1000;		// Serial numbers start at 1000


//-----------------------------------------------------------------------------
// This is the interface function that other systems use to send us targets
//-----------------------------------------------------------------------------
int Locator_AddTarget()
{
	if ( !s_pLocatorPanel )
	{
		CLocatorPanel *panel = new CLocatorPanel( g_pClientMode->GetViewport(), "LocatorPanel" );
		panel->SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
		panel->SetPos( 0, 0 );
		panel->SetVisible( true );
		vgui::ivgui()->AddTickSignal( panel->GetVPanel() );
	}
	return s_pLocatorPanel->AddTarget();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Locator_RemoveTarget( int hTarget )
{
	if ( CLocatorPanel *pPanel = GetPlayerLocatorPanel() )
	{
		pPanel->RemoveTarget( hTarget );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CLocatorTarget *Locator_GetTargetFromHandle( int hTarget )
{
	if ( CLocatorPanel *pPanel = GetPlayerLocatorPanel() )
		return pPanel->GetPointerForHandle( hTarget );
	else
		return NULL;
}

void Locator_ComputeTargetIconPositionFromHandle( int hTarget )
{
	if ( CLocatorPanel *pPanel = GetPlayerLocatorPanel() )
	{
		if ( CLocatorTarget *pTarget = pPanel->GetPointerForHandle( hTarget ) )
		{
			if( !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_STATIC ) )
			{
				// It's not presenting in the middle of the screen, so figure out it's position
				pPanel->ComputeTargetIconPosition( pTarget, !pTarget->IsPresenting() );
				pPanel->CalculateOcclusion( pTarget );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CLocatorPanel::CLocatorPanel( Panel *parent, const char *name ) : EditablePanel(parent,name)
{
	DeactivateAllTargets();
	s_pLocatorPanel = this;
	m_textureID_ArrowRight = -1;
	m_textureID_ArrowLeft = -1;
	m_textureID_ArrowUp = -1;
	m_textureID_ArrowDown = -1;
	m_textureID_SimpleArrow = -1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CLocatorPanel::~CLocatorPanel( void )
{
	s_pLocatorPanel = NULL;
	int *textures[] = { &m_textureID_ArrowRight, &m_textureID_ArrowLeft, &m_textureID_ArrowUp, &m_textureID_ArrowDown, &m_textureID_SimpleArrow };
	for ( int i = 0; i < ARRAYSIZE( textures ); ++i )
		if ( *textures[i] != -1 ) { vgui::surface()->DestroyTextureID( *textures[i] ); *textures[i] = -1; }
}

//-----------------------------------------------------------------------------
// Purpose: Applies scheme settings
//-----------------------------------------------------------------------------
void CLocatorPanel::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	LoadControlSettings( "resource/UI/Locator.res" );
	m_hCaptionFont = pScheme->GetFont( "InstructorTitle", true );
	m_hCaptionFont_ss = pScheme->GetFont( "InstructorTitle_ss", true );
	m_hKeysFont = pScheme->GetFont( "MenuSubTitle", true );
	m_hButtonFont = pScheme->GetFont( "InstructorButtons", true );
	m_hButtonFont_ss = pScheme->GetFont( "GameUIButtons", true );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLocatorPanel::PerformLayout( void )
{
	BaseClass::PerformLayout();

	vgui::Panel *pPanel = FindChildByName( "LocatorBG" );
	if ( pPanel )
	{
		pPanel->SetPos( NativeLocatorInteger( (double)( GetWide() - pPanel->GetWide() ) * 0.5 ), NativeLocatorInteger( (double)( GetTall() - pPanel->GetTall() ) * 0.5 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Given an offscreen target position, compute the 'compass position'
// so that we can draw an icon on the imaginary circle around the crosshair
// that indicates which way the player should turn to bring the target into view.
//-----------------------------------------------------------------------------
void CLocatorPanel::GetTargetPosition( const Vector &delta, float radius, float *x, float *y, float *rotation )
{
	Vector forward, right, up( 0, 0, 1 );
	AngleVectors( MainViewAngles(), &forward, NULL, NULL );
	forward.z = 0.0f; VectorNormalize( forward );
	CrossProduct( up, forward, right );
	*x = -DotProduct( delta, right ) * radius;
	*y = -DotProduct( delta, forward ) * radius;
	double angle = ( atan2( (double)*x, (double)*y ) + M_PI ) * 57.29577951308232;
	*rotation = (float)angle;
	float radians = (float)( M_PI * -angle * 0.005555555555555556 );
	*x = (float)NativeLocatorInteger( (double)( ScreenWidth() / 2 ) + sin( (double)radians ) * (double)radius );
	*y = (float)NativeLocatorInteger( (double)( ScreenHeight() / 2 ) - cos( (double)radians ) * (double)( radius * 0.60000002f ) );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CLocatorPanel::DeactivateAllTargets()
{
	for( int i = 0 ; i < MAX_LOCATOR_TARGETS ; i++ )
	{
		m_targets[ i ].Deactivate( true );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Deactivate any target that has not been updated within several frames
//-----------------------------------------------------------------------------
void CLocatorPanel::CollectGarbage()
{
	for( int i = 0 ; i < MAX_LOCATOR_TARGETS ; i++ )
	{
		if( m_targets[ i ].m_isActive )
		{
			if( gpGlobals->framecount - m_targets[ i ].m_frameLastUpdated > 20 )
			{
				m_targets[ i ].Deactivate();
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Provide simple animation by modifying the width and height of the
// icon before it is drawn.
//-----------------------------------------------------------------------------
void CLocatorPanel::AnimateIconSize( int flags, int *wide, int *tall, float pulseStart )
{
	float scale = 1.0f;
	float now = gpGlobals->curtime;
	if ( ( flags & LOCATOR_ICON_FX_PULSE_SLOW ) || now - pulseStart < locator_pulse_time.GetFloat() )
		scale = (float)( 0.75 * fabs( sin( ( (double)now - (double)pulseStart ) * M_PI ) ) + 1.0 );
	else if ( flags & LOCATOR_ICON_FX_PULSE_FAST ) scale = (float)( 0.75 * fabs( sin( (double)now * 2.0 * M_PI ) ) + 1.0 );
	else if ( flags & LOCATOR_ICON_FX_PULSE_URGENT ) scale = (float)( 0.75 * fabs( sin( (double)now * 4.0 * M_PI ) ) + 1.0 );
	float width = (float)*wide, height = (float)*tall;
	if ( !( width <= height ) )
	{
		int delta = NativeLocatorInteger( height * scale - height );
		scale = ( width + (float)delta ) / width;
	}
	*wide = NativeLocatorInteger( width * scale ); *tall = NativeLocatorInteger( height * scale );
}

//-----------------------------------------------------------------------------
// Purpose: Modify the alpha of the icon before it is drawn.
//-----------------------------------------------------------------------------
void CLocatorPanel::AnimateIconAlpha( int flags, int *alpha, float fadeStart )
{
	float scale = 1.0f;
	if ( flags & LOCATOR_ICON_FX_ALPHA_SLOW ) scale = (float)( ( fabs( sin( (double)gpGlobals->curtime * 3.0 ) ) + 1.0 ) * 0.5 );
	else if ( flags & LOCATOR_ICON_FX_ALPHA_FAST ) scale = (float)( ( fabs( sin( (double)gpGlobals->curtime * 7.0 ) ) + 1.0 ) * 0.5 );
	else if ( flags & LOCATOR_ICON_FX_ALPHA_URGENT ) scale = (float)( ( fabs( sin( (double)gpGlobals->curtime * 10.0 ) ) + 1.0 ) * 0.5 );
	if ( flags & ( LOCATOR_ICON_FX_FADE_IN | LOCATOR_ICON_FX_FADE_OUT ) )
	{
		float fade = locator_fade_time.GetFloat();
		float elapsed = gpGlobals->curtime - fadeStart;
		float remaining = ( fade - elapsed ) / fade;
		if ( 0.0f > remaining ) remaining = 0.0f;
		scale *= ( flags & LOCATOR_ICON_FX_FADE_OUT ) ? remaining : 1.0f - remaining;
	}
	*alpha = NativeLocatorInteger( scale * 255.0f );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLocatorPanel::AnimateIconPosition( int flags, int *x, int *y )
{
	int newX = *x;
	int newY = *y;

	if( flags & LOCATOR_ICON_FX_SHAKE_NARROW )
	{
		newX += RandomInt( -2, 2 );
		newY += RandomInt( -2, 2 );
	}
	else if( flags & LOCATOR_ICON_FX_SHAKE_WIDE )
	{
		newX += RandomInt( -5, 5 );
		newY += RandomInt( -5, 5 );
	}

	*x = newX;
	*y = newY;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLocatorPanel::OnTick( void )
{

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLocatorPanel::PaintBackground()
{
	static ConVarRef menu( "gameui_ingame_menu_active" );
	if ( !menu.GetBool() && C_BasePlayer::GetLocalPlayer() ) BaseClass::PaintBackground();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLocatorPanel::Paint( void )
{
	static ConVarRef menu( "gameui_ingame_menu_active" );
	if ( menu.GetBool() ) return;
	ValidateTexture( &m_textureID_ArrowLeft, "vgui/hud/icon_arrow_left" );
	ValidateTexture( &m_textureID_ArrowRight, "vgui/hud/icon_arrow_right" );
	ValidateTexture( &m_textureID_ArrowUp, "vgui/hud/icon_arrow_up" );
	ValidateTexture( &m_textureID_ArrowDown, "vgui/hud/icon_arrow_down" );
	ValidateTexture( &m_textureID_SimpleArrow, "vgui/hud/icon_arrow_plain" );

	// reset the static icon position. This is the y position at which the first
	// static icon will be drawn. This value will be incremented by the height of
	// each static icon drawn, which forces the next static icon to be drawn below
	// the previous one, creating a little fixed, vertical stack below the crosshair.
	m_staticIconPosition = ScreenHeight() / 6;

	// Time now to draw the 'dynamic' icons, the icons which help players locate things
	// in actual world space.

	//----------
	// Batch 1
	// Go through all of the active locator targets and compute where to draw the icons
	// that represent each of them. This builds a poor man's draw list by updating the 
	// m_iconX, m_iconY members of each locator target.
	CUtlVectorFixed< CLocatorTarget *, MAX_LOCATOR_TARGETS > vecValid;

	for( int i = 0 ; i < MAX_LOCATOR_TARGETS ; i++ )
	{
		CLocatorTarget *pLocatorTarget = &(m_targets[ i ]);

		// Reset drawing state for this frame... set back to true when it's finally draws
		pLocatorTarget->m_bIsDrawing = false;

		if ( ( !pLocatorTarget->m_bVisible && !pLocatorTarget->m_alpha ) || !pLocatorTarget->m_isActive )
		{
			// Don't want to be visible and have finished fading
			continue;
		}

		vecValid.AddToTail( pLocatorTarget );

		// This prevents an error that if a locator was fading as the map transitioned
		if ( gpGlobals->curtime <= pLocatorTarget->m_fadeStart ) pLocatorTarget->m_fadeStart = gpGlobals->curtime; 

		if( !( pLocatorTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_STATIC ) )
		{
			// It's not presenting in the middle of the screen, so figure out it's position
			ComputeTargetIconPosition( pLocatorTarget, !pLocatorTarget->IsPresenting() );
			CalculateOcclusion( pLocatorTarget );

			pLocatorTarget->m_lastDeclutterIndex = pLocatorTarget->m_declutterIndex;
			pLocatorTarget->m_declutterIndex = 0;
		}
	}

	//----------
	// Batch 2
	// Now that we know where each icon _wants_ to be drawn, we grovel through them and 
	// push apart any icons that are too close to one another. This helps to unclutter
	// the display and ensure the maximum number of legible icons and captions. Obviously
	// this process changes where some icons will be drawn. Bubble sort, but tiny data set.
	int iTolerance = NativeLocatorInteger( ( (float)ScreenWidth() * ICON_SIZE ) * 1.25f );
	int iterations = 0;// Count iterations, don't go infinite in the event of some weird case.
	bool bStillUncluttering = true;
	static int MAX_UNCLUTTER_ITERATIONS = 10;
	while( iterations < MAX_UNCLUTTER_ITERATIONS && bStillUncluttering )
	{
		iterations++;
		bStillUncluttering = false;

		for( int i = 0 ; i < vecValid.Count() ; ++i )
		{
			CLocatorTarget *pLocatorTarget1 = vecValid[ i ];

			for( int j = i + 1 ; j < vecValid.Count() ; ++j )
			{
				CLocatorTarget *pLocatorTarget2 = vecValid[ j ];

				// Don't attempt to declutter icons if one or both is attempting to fade out
				bool bLocatorsFullyActive = !((pLocatorTarget1->GetIconEffectsFlags()|pLocatorTarget2->GetIconEffectsFlags()) & LOCATOR_ICON_FX_FADE_OUT);

				if ( bLocatorsFullyActive && IconsAreIntersecting( *pLocatorTarget1, *pLocatorTarget2, iTolerance ) )
				{
					// Unclutter. Lift whichever icon is highest a bit higher
					if( pLocatorTarget1->m_iconY < pLocatorTarget2->m_iconY )
					{
						pLocatorTarget1->m_iconY = pLocatorTarget2->m_iconY - iTolerance;
						pLocatorTarget1->m_centerY = pLocatorTarget2->m_centerY - iTolerance;
						pLocatorTarget1->m_declutterIndex -= 1;
					}
					else
					{
						pLocatorTarget2->m_iconY = pLocatorTarget1->m_iconY - iTolerance;
						pLocatorTarget2->m_centerY = pLocatorTarget1->m_centerY - iTolerance;
						pLocatorTarget2->m_declutterIndex -= 1;
					}

					bStillUncluttering = true;
				}
			}
		}
	}

	if( iterations == MAX_UNCLUTTER_ITERATIONS )
	{
		DevWarning( "Game instructor hit MAX_UNCLUTTER_ITERATIONS!\n");
	}

	float flLocatorLerpRest = locator_lerp_rest.GetFloat();
	float flLocatorLerpTime = locator_lerp_time.GetFloat();

	//----------
	// Batch 3
	// Draw each of the icons.
	for( int i = 0 ; i < vecValid.Count() ; i++ )
	{
		CLocatorTarget *pLocatorTarget = vecValid[ i ];
		// Back to lerping for these guys
		if ( pLocatorTarget->m_lastDeclutterIndex != pLocatorTarget->m_declutterIndex )
		{
			// It wants to be popped to another position... do it smoothly
			pLocatorTarget->StartTimedLerp();
		}

		// Lerp to the desired position
		float flLerpTime = gpGlobals->curtime - pLocatorTarget->m_lerpStart;

		if ( flLerpTime >= flLocatorLerpRest && flLerpTime < flLocatorLerpRest + flLocatorLerpTime )
		{
			// Lerp slow to fast
			float fInterp = 1.0f - ( ( flLocatorLerpTime - ( flLerpTime - flLocatorLerpRest ) ) / flLocatorLerpTime );

			// Get our desired position
			float iconX = pLocatorTarget->m_iconX;
			float iconY = pLocatorTarget->m_iconY;

			// Get the distance we need to go to reach it
			float diffX = (float)fabs( (double)( pLocatorTarget->m_iconX - pLocatorTarget->m_lastXPos ) );
			float diffY = fabsf( pLocatorTarget->m_iconY - pLocatorTarget->m_lastYPos );

			// Go from our current position toward the desired position as quick as the interp allows
			pLocatorTarget->m_iconX = NativeLocatorInteger( Approach( iconX, pLocatorTarget->m_lastXPos, diffX * fInterp ) );
			pLocatorTarget->m_iconY = NativeLocatorInteger( Approach( iconY, pLocatorTarget->m_lastYPos, diffY * fInterp ) );

			// Get how much our position changed and apply it to the center values
			int iOffsetX = NativeLocatorInteger( (float)pLocatorTarget->m_iconX - iconX );
			int iOffsetY = NativeLocatorInteger( (float)pLocatorTarget->m_iconY - iconY );

			pLocatorTarget->m_centerX += iOffsetX;
			pLocatorTarget->m_centerY += iOffsetY;

		}

		PaintTarget( pLocatorTarget );
	}

	CollectGarbage();
}

//-----------------------------------------------------------------------------
// Purpose: A helper function to save on typing. Make sure our texture ID's 
//			stay valid.
//-----------------------------------------------------------------------------
void CLocatorPanel::ValidateTexture( int *pTextureID, const char *pszTextureName )
{
	if( *pTextureID == -1 )
	{
		*pTextureID = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( *pTextureID, pszTextureName, true, false );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called every frame before painting the targets. Ensures that the
//			target's textures are properly cached.
//-----------------------------------------------------------------------------
bool CLocatorPanel::ValidateTargetTextures( CLocatorTarget *target )
{
	bool bindingTick = false;
	if ( gpGlobals->curtime >= target->m_flNextBindingTick )
	{
		if ( target->m_iBindingChoicesCount > 1 ) { target->m_iBindingTick = (int)( (unsigned int)target->m_iBindingTick + 1u ); bindingTick = true; }
		target->m_flNextBindingTick = gpGlobals->curtime + 0.75f;
	}
	bool binding = !Q_stricmp( target->GetOnscreenIconTextureName(), "use_binding" );
	if ( target->m_pIcon_onscreen && target->m_pIcon_offscreen && ( !binding || !bindingTick ) ) return false;
	char image[256]; float scale = 1.0f;
	if ( binding )
	{
		target->DrawBindingName( target->UseBindingImage( image, sizeof( image ), &scale ) );
		target->m_bDrawControllerButton = !Q_strcmp( image, "icon_blank" );
	}
	else { target->m_bDrawControllerButton = false; Q_strncpy( image, target->GetOnscreenIconTextureName(), sizeof( image ) ); target->DrawBindingName( NULL ); }
	if ( !image[0] ) { DevWarning( "Locator Target has no onscreen texture name!\n" ); return false; }
	target->m_pIcon_onscreen = gHUD.GetIcon( image );
	target->m_widthScale_onscreen = scale;
	if ( !Q_stricmp( target->GetOffscreenIconTextureName(), "use_binding" ) )
	{
		target->DrawBindingNameOffscreen( target->UseBindingImage( image, sizeof( image ), &scale ) );
		target->m_bDrawControllerButtonOffscreen = !Q_strcmp( image, "icon_blank" );
	}
	else { target->m_bDrawControllerButtonOffscreen = false; Q_strncpy( image, target->GetOffscreenIconTextureName(), sizeof( image ) ); target->DrawBindingNameOffscreen( NULL ); }
	if ( image[0] ) target->m_pIcon_offscreen = gHUD.GetIcon( image );
	else if ( target->m_pIcon_onscreen ) { target->m_pIcon_offscreen = target->m_pIcon_onscreen; target->DrawBindingNameOffscreen( target->DrawBindingName() ); }
	else DevWarning( "Locator Target has no offscreen texture name and can't fall back!\n" );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Compute where on the screen to draw the icon for this target.
//-----------------------------------------------------------------------------
void CLocatorPanel::ComputeTargetIconPosition( CLocatorTarget *pTarget, bool bSetPosition )
{

	int iconX;
	int iconY;

	// Measure the delta and the dist from this player to this target.
	Vector vecTarget = pTarget->m_vecOrigin;
	Vector vecDelta = vecTarget - MainViewOrigin();
	pTarget->m_distFromPlayer = VectorNormalize( vecDelta );
	GetVectorInScreenSpace( vecTarget, iconX, iconY );

	pTarget->m_drawArrowDirection = DRAW_ARROW_NO;

	float fTitleSafeInset = ScreenWidth() * 0.075f;

	if( iconX < fTitleSafeInset || iconX > ScreenWidth() - fTitleSafeInset )
	{
		// It's off the screen left or right.
		if ( pTarget->m_bOnscreen && !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_NO_OFFSCREEN ) )
		{
			// Back to lerping
			pTarget->StartTimedLerp();
			pTarget->m_pulseStart = gpGlobals->curtime;
		}

		if ( bSetPosition )
		{
			pTarget->m_bOnscreen = false;
		}

		GetIconPositionForOffscreenTarget( vecDelta, pTarget->m_distFromPlayer, &iconX, &iconY );

		Vector vCenter = pTarget->m_vecOrigin;
		if( MainViewRight().Dot( vCenter - MainViewOrigin() ) <= 0 )
			pTarget->m_drawArrowDirection = DRAW_ARROW_LEFT;
		else
			pTarget->m_drawArrowDirection = DRAW_ARROW_RIGHT;
	}
	else if( iconY < fTitleSafeInset || iconY > ScreenHeight() - fTitleSafeInset )
	{
		// It's off the screen up or down.
		if ( pTarget->m_bOnscreen && !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_NO_OFFSCREEN ) )
		{
			// Back to lerping
			pTarget->StartTimedLerp();
			pTarget->m_pulseStart = gpGlobals->curtime;
		}

		if ( bSetPosition )
		{
			pTarget->m_bOnscreen = false;
		}

		GetIconPositionForOffscreenTarget( vecDelta, pTarget->m_distFromPlayer, &iconX, &iconY );

		Vector vCenter = pTarget->m_vecOrigin;
		if( MainViewUp().Dot( vCenter - MainViewOrigin() ) <= 0 )
			pTarget->m_drawArrowDirection = DRAW_ARROW_DOWN;
		else
			pTarget->m_drawArrowDirection = DRAW_ARROW_UP;
	}
	else
	{
		if ( !pTarget->m_bOnscreen && !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_NO_OFFSCREEN ) )
		{
			// Back to lerping
			pTarget->StartTimedLerp();
			pTarget->m_pulseStart = gpGlobals->curtime;
		}

		pTarget->m_bOnscreen = true;
	}

	if ( bSetPosition )
	{
		int tall = NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE );
		int wide = NativeLocatorInteger( (float)tall * pTarget->m_widthScale_onscreen );

		// Animate the icon
		AnimateIconSize( pTarget->GetIconEffectsFlags(), &wide, &tall, pTarget->m_pulseStart );
		AnimateIconPosition( pTarget->GetIconEffectsFlags(), &iconX, &iconY );
		AnimateIconAlpha( pTarget->GetIconEffectsFlags(), &pTarget->m_alpha, pTarget->m_fadeStart );

		if( pTarget->m_distFromPlayer > ICON_DIST_TOO_FAR )
		{
			// Make the icon smaller
			wide = wide >> 1;
			tall = tall >> 1;
		}

		pTarget->m_centerX = iconX;
		pTarget->m_centerY = iconY;

		pTarget->m_iconX = pTarget->m_centerX - ( wide >> 1 );
		pTarget->m_iconY = pTarget->m_centerY - ( tall >> 1 );
		pTarget->m_wide = wide;
		pTarget->m_tall = tall;
	}
}

void CLocatorPanel::CalculateOcclusion( CLocatorTarget *pTarget )
{

	if ( gpGlobals->curtime >= pTarget->m_flNextOcclusionTest )
	{
		// Assume the target is not occluded.
		pTarget->m_bOccluded = false;


		trace_t	tr;
		UTIL_TraceLine( pTarget->m_vecOrigin, MainViewOrigin(), (CONTENTS_SOLID|CONTENTS_MOVEABLE), NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 1.0f )
		{
			pTarget->m_bOccluded = true;
		}
		pTarget->m_flNextOcclusionTest = gpGlobals->curtime + LOCATOR_OCCLUSION_TEST_RATE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: This is not valid until after you have computed the onscreen
//			icon position for each target!
//-----------------------------------------------------------------------------
bool CLocatorPanel::IconsAreIntersecting( CLocatorTarget &first, CLocatorTarget &second, int iTolerance )
{
	if( first.m_bOnscreen != second.m_bOnscreen )
	{
		// We only declutter onscreen icons against other onscreen icons and vice-versa.
		return false;
	}

	if( first.IsStatic() || second.IsStatic() )
	{
		// Static icons don't count.
		return false;
	}

	if( abs(first.GetIconY() - second.GetIconY()) < iTolerance )
	{
		// OK, we need the Y-check first. Now we have to see if these icons and their captions overlap.
		int firstWide = iTolerance + first.m_captionWide;
		int secondWide = iTolerance + second.m_captionWide;
		
		if( abs(first.GetIconX() - second.GetIconX()) < (firstWide + secondWide) / 2 )
		{
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Draw this target on the locator.
//
// IF onscreen and visible, draw no icon, draw no arrows
// IF onscreen and occluded, draw icon transparently, draw no arrows
// IF offscreen, draw icon, draw an arrow indicating the direction to the target
//-----------------------------------------------------------------------------
void CLocatorPanel::PaintTarget( CLocatorTarget *pTarget )
{
	bool bNewTexture = ValidateTargetTextures( pTarget );

	if ( bNewTexture )
	{
		// Refigure the width/height for the new texture
		int tall = NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE );
		int wide = NativeLocatorInteger( (float)tall * pTarget->m_widthScale_onscreen );

		AnimateIconSize( pTarget->GetIconEffectsFlags(), &wide, &tall, pTarget->m_pulseStart );

		pTarget->m_wide = wide;
		pTarget->m_tall = tall;
	}

	// A static icon just draws with other static icons in a stack under the crosshair. 
	// Once displayed, they do not move. The are often used for notifiers.
	if( ( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_STATIC ) ||
		( !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_NO_PRESENT ) &&
		  !( locator_lerp_rest.GetFloat() <= gpGlobals->curtime - pTarget->m_lerpStart ) ) )
	{
		DrawStaticIcon( pTarget );
		return;
	}

	if ( !pTarget->m_bOnscreen && ( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_NO_OFFSCREEN ) )
	{
		// Doesn't draw when offscreen... reset it's alpha so it has to fade in again
		pTarget->m_fadeStart = gpGlobals->curtime;
		pTarget->m_alpha = 0;
	}
	else
	{
		// Save these coordinates for later lerping
		pTarget->m_lastXPos = pTarget->m_iconX;
		pTarget->m_lastYPos = pTarget->m_iconY;

		// Draw when it's on screen or allowed to draw offscreen
		DrawDynamicIcon( pTarget, pTarget->HasCaptionText(), pTarget->m_bOnscreen );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws the caption-like background with word-bubble style pointer
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Draw an icon with the group of static icons. 
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawStaticIcon( CLocatorTarget *pTarget )
{
	int centerX = ScreenWidth() / 2;
	int centerY = ScreenHeight() / 2;
	centerY += m_staticIconPosition;

	int iconTall = NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE );
	int iconWide = NativeLocatorInteger( (float)iconTall * pTarget->m_widthScale_onscreen );

	pTarget->m_centerX = centerX;
	pTarget->m_centerY = centerY;

	// Animate the icon
	AnimateIconSize( pTarget->GetIconEffectsFlags(), &iconWide, &iconTall, pTarget->m_pulseStart );
	AnimateIconPosition( pTarget->GetIconEffectsFlags(), &centerX, &centerY );
	AnimateIconAlpha( pTarget->GetIconEffectsFlags(), &pTarget->m_alpha, pTarget->m_fadeStart );

	// Figure out the caption width
	pTarget->m_captionWide = GetScreenWidthForCaption( pTarget->GetCaptionText(), CAPTION_FONT_HANDLE );

	bool bDrawMultilineCaption = false;

	if ( m_iShouldWrapStaticLocators > 0 )	// conditionalized in locator.res
	{
		if ( pTarget->m_captionWide > (  ScreenWidth() * locator_split_maxwide_percent.GetFloat() ) )
		{
			// we will double-line this
			pTarget->m_captionWide = NativeLocatorInteger( (float)pTarget->m_captionWide * locator_split_len.GetFloat() );
			bDrawMultilineCaption = true;
		}
	}
	int totalWide = iconWide + ICON_GAP + pTarget->m_captionWide;
	pTarget->m_iconX = NativeLocatorInteger( (float)centerX - (float)totalWide * 0.5f );
	pTarget->m_iconY = centerY - ( iconTall >> 1 );

	// Lerp by speed on the Y axis
	float iconY = pTarget->m_iconY;

	float diffY = fabsf( pTarget->m_iconY - pTarget->m_lastYPos );

	float flLerpSpeed = gpGlobals->frametime * locator_lerp_speed.GetFloat();
	pTarget->m_iconY = NativeLocatorInteger( Approach( iconY, pTarget->m_lastYPos, flLerpSpeed * diffY >= 3.0f ? flLerpSpeed * diffY : 3.0f ) );
	pTarget->m_centerY = NativeLocatorInteger( (float)pTarget->m_centerY + ( (float)pTarget->m_iconY - iconY ) );

	pTarget->m_lastXPos = pTarget->m_iconX;
	pTarget->m_lastYPos = pTarget->m_iconY;

	pTarget->m_bIsDrawing = true;


	if ( pTarget->m_pIcon_onscreen )
	{
		if ( !pTarget->m_bDrawControllerButton )
		{
			// Don't draw the icon if we're on 360 and have a binding to draw
			pTarget->m_pIcon_onscreen->DrawSelf( pTarget->GetIconX(), pTarget->GetIconY(), iconWide, iconTall, Color( 255, 255, 255, pTarget->m_alpha ) );
		}
	}

	DrawTargetCaption( pTarget, pTarget->GetIconX() + iconWide + ICON_GAP, pTarget->GetIconCenterY(), bDrawMultilineCaption );
	if ( pTarget->DrawBindingName() )
	{
		DrawBindingName( pTarget, pTarget->DrawBindingName(), pTarget->GetIconX() + (iconWide>>1), pTarget->GetIconY() + (iconTall>>1), pTarget->m_bDrawControllerButton );
	}

	// Draw the arrow.
	int iArrowSize = NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE );	// Always square width
	DrawIndicatorArrow( pTarget->GetIconX(), pTarget->GetIconY(), iArrowSize, iArrowSize, pTarget->m_captionWide + ICON_GAP, pTarget->m_drawArrowDirection );

	pTarget->m_bOnscreen = true;

	// Move the static icon position so the next static icon drawn this frame below this one.
	m_staticIconPosition += iconTall + (iconTall>>2);
	// Move down a little more if this one was multi-line
	if ( bDrawMultilineCaption )
	{
		m_staticIconPosition += (iconTall>>2);
	}
	return;
}

//-----------------------------------------------------------------------------
// Purpose: Position and animate this target's icon on the screen. Based on
//			options, draw the indicator arrows (arrows that point to the 
//			direction the player should turn to see the icon), text caption, 
//			and the 'simple' arrow which just points down to indicate the 
//			item the icon represents.
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawDynamicIcon( CLocatorTarget *pTarget, bool bDrawCaption, bool bDrawSimpleArrow )
{
	int alpha = pTarget->m_alpha;

	if( pTarget->m_bOccluded && !( pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_FORCE_CAPTION ) )
	{
		return;
	}

	// Draw the icon!
	vgui::surface()->DrawSetColor( 255, 255, 255, alpha );

	int iWide = pTarget->m_wide;

	if ( !pTarget->m_bOnscreen )
	{
		// Width is always square for offscreen icons
		iWide = NativeLocatorInteger( (float)iWide / pTarget->m_widthScale_onscreen );
	}

	bool bShouldDrawCaption = ( (pTarget->GetIconEffectsFlags() & LOCATOR_ICON_FX_FORCE_CAPTION) || (!pTarget->m_bOccluded && pTarget->m_distFromPlayer <= ICON_DIST_TOO_FAR) );

	pTarget->m_bIsDrawing = true;


	if( pTarget->m_bOnscreen && pTarget->m_pIcon_onscreen )
	{
		if ( !pTarget->m_bDrawControllerButton )
		{
			pTarget->m_pIcon_onscreen->DrawSelf( pTarget->GetIconX(), pTarget->GetIconY(), iWide, pTarget->m_tall, Color( 255, 255, 255, alpha ) );
		}
	}
	else if ( pTarget->m_pIcon_offscreen )
	{
		if ( !pTarget->m_bDrawControllerButtonOffscreen )
		{
			pTarget->m_pIcon_offscreen->DrawSelf( pTarget->GetIconX(), pTarget->GetIconY(), iWide, pTarget->m_tall, Color( 255, 255, 255, alpha ) );
		}
	}

	if( !pTarget->m_bOnscreen )
	{
		if ( pTarget->DrawBindingNameOffscreen() )
		{
			DrawBindingName( pTarget, pTarget->DrawBindingName(), pTarget->GetIconX() + (iWide>>1), pTarget->GetIconY() + (pTarget->m_tall>>1), pTarget->m_bDrawControllerButtonOffscreen );
		}

			// Draw the arrow.
			DrawIndicatorArrow( pTarget->GetIconX(), pTarget->GetIconY(), iWide, pTarget->m_tall, 0, pTarget->m_drawArrowDirection );
	}
	else if( bShouldDrawCaption )
	{
		if( bDrawCaption )
		{
			//ScreenWidth() *  * pTarget->m_widthScale_onscreen
			DrawTargetCaption( pTarget, NativeLocatorInteger( (double)ScreenWidth() * (double)ICON_SIZE * (double)pTarget->m_widthScale_onscreen * 0.5 + (double)( pTarget->GetIconCenterX() + ICON_GAP ) ), pTarget->GetIconCenterY(), false );
		}
		if ( pTarget->DrawBindingName() )
		{
			DrawBindingName( pTarget, pTarget->DrawBindingName(), pTarget->GetIconX() + (iWide>>1), pTarget->GetIconY() + (pTarget->m_tall>>1), pTarget->m_bDrawControllerButton );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Some targets have text captions. Draw the text.
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawTargetCaption( CLocatorTarget *pTarget, int x, int y, bool bDrawMultiline )
{
	// Draw the caption
	vgui::surface()->DrawSetTextFont( CAPTION_FONT_HANDLE );
	int fontTall = vgui::surface()->GetFontTall( CAPTION_FONT_HANDLE );
	int iCaptionWidth = GetScreenWidthForCaption( pTarget->GetCaptionText(), CAPTION_FONT_HANDLE );

	if ( bDrawMultiline )
	{
		iCaptionWidth = NativeLocatorInteger( (float)iCaptionWidth * locator_split_len.GetFloat() );
	}


	// Only draw drop shadow on PC because it looks crappy on a TV
	if ( !IsConsole() )
	{
		// Draw black text (drop shadow)
		vgui::surface()->DrawSetTextColor( 0,0,0, pTarget->m_alpha );
		vgui::surface()->DrawSetTextPos( x, y - (fontTall >>1) );
		vgui::surface()->DrawUnicodeString( pTarget->GetCaptionText() );
	}

	// Draw text
	vgui::surface()->DrawSetTextColor( pTarget->m_captionColor.r(),pTarget->m_captionColor.g(),pTarget->m_captionColor.b(), pTarget->m_alpha );

	if ( !bDrawMultiline )
	{
		vgui::surface()->DrawSetTextPos( x - 1, y - (fontTall >>1) - 1 );
		vgui::surface()->DrawUnicodeString( pTarget->GetCaptionText() );
	}
	else
	{
		int charX = x-1;
		int charY = y - ( fontTall >> 1 ) - 1;

		int iWidth = 0;

		const wchar_t *pString = pTarget->GetCaptionText();
		int len = Q_wcslen( pString );

		for ( int iChar = 0; iChar < len; ++ iChar )
		{
			int charW = vgui::surface()->GetCharacterWidth( CAPTION_FONT_HANDLE, pString[ iChar ] );
			iWidth += charW;

			if ( iWidth > pTarget->m_captionWide && pString[iChar] == L' ' )
			{
				charY += fontTall;
				charX = x-1;
				iWidth = 0;
			}

			vgui::surface()->DrawSetTextPos( charX, charY );
			vgui::surface()->DrawUnicodeChar( pString[iChar] );
			charX += charW;
		}		
	}
}

//-----------------------------------------------------------------------------
// Purpose: Figure out how wide (pixels) a string will be if rendered with this font
// 
//-----------------------------------------------------------------------------
int CLocatorPanel::GetScreenWidthForCaption( const wchar_t *pString, vgui::HFont hFont )
{
	int iWidth = 0;

	for ( int iChar = 0; iChar < Q_wcslen( pString ); ++ iChar )
	{
		iWidth += vgui::surface()->GetCharacterWidth( hFont, pString[ iChar ] );
	}

	return iWidth;
}

//-----------------------------------------------------------------------------
// Purpose: Some targets' captions contain information about key bindings that
//			should be displayed to the player. Do so.
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawBindingName( CLocatorTarget *target, const char *binding, int x, int y, bool controller )
{
	bool steam = g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
	if ( steam && target->m_iBindingChoicesCount )
	{
		int texture = target->m_iSteamTextures[(unsigned int)target->m_iBindingTick % target->m_iBindingChoicesCount];
		if ( texture > 0 )
		{
			int pixels = NativeLocatorInteger( (float)NativeLocatorInteger( (float)ScreenWidth() * ICON_SIZE ) * target->m_widthScale_onscreen * 0.85000002f );
			vgui::surface()->DrawSetColor( 255, 255, 255, target->m_alpha );
			vgui::surface()->DrawSetTexture( texture );
			vgui::surface()->DrawTexturedRect( x - pixels / 2, y - pixels / 2, x + pixels / 2, y + pixels / 2 );
			return;
		}
	}
	char key[256]; Q_strncpy( key, binding ? binding : "", sizeof( key ) );
	wchar_t text[64];
	if ( controller )
	{
		vgui::HFont font = steam ? m_hButtonFontSC : m_hButtonFont;
		char token[256]; Q_snprintf( token, sizeof( token ), "#GameUI_Icons_%s", key );
		const wchar_t *format = g_pVGuiLocalize->Find( token );
		if ( !format ) return; // Native missing-format prerequisite only.
		g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 0 );
		int width = GetScreenWidthForCaption( text, font );
		float excess = (float)width - ( (float)ScreenWidth() * ICON_SIZE + 5.0f + 5.0f );
		int shift = NativeLocatorInteger( excess >= 0.0f ? excess : 0.0f );
		vgui::surface()->DrawSetTextFont( font );
		vgui::surface()->DrawSetTextColor( 255, 255, 255, target->m_alpha );
		vgui::surface()->DrawSetTextPos( x - ( width >> 1 ) - shift, y - ( vgui::surface()->GetFontTall( font ) >> 1 ) );
	}
	else
	{
		if ( !Q_strcmp( key, "SEMICOLON" ) ) Q_strncpy( key, ";", sizeof( key ) );
		else if ( !Q_strcmp( key, "ESCAPE" ) ) Q_strncpy( key, "ESC", sizeof( key ) );
		else if ( Q_strlen( key ) == 1 && key[0] >= 'a' && key[0] <= 'z' ) key[0] -= 'a' - 'A';
		g_pVGuiLocalize->ConstructString( text, sizeof( text ), key, (KeyValues *)NULL );
		int width = GetScreenWidthForCaption( text, m_hKeysFont );
		vgui::surface()->DrawSetTextFont( m_hKeysFont );
		vgui::surface()->DrawSetTextColor( steam ? 255 : 0, steam ? 255 : 0, steam ? 255 : 0, target->m_alpha );
		vgui::surface()->DrawSetTextPos( x - ( steam ? width : width >> 1 ) - 1, y - ( vgui::surface()->GetFontTall( m_hKeysFont ) >> 1 ) - 1 );
	}
	vgui::surface()->DrawUnicodeString( text );
}

//-----------------------------------------------------------------------------
// Purpose: Draw an arrow to indicate that a target is offscreen
//
// iconWide is sent to this function so that the arrow knows how to straddle
// the icon that it is being drawn near.
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawIndicatorArrow( int x, int y, int iconWide, int iconTall, int textWidth, int direction )
{
	int wide = iconWide;
	int tall = iconTall;

	//tall = wide = ScreenWidth() * ICON_SIZE;

	switch( direction )
	{
	case DRAW_ARROW_LEFT:
		vgui::surface()->DrawSetTexture( m_textureID_ArrowLeft );
		x -= wide;
		y += iconTall / 2 - tall / 2;
		vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
		break;

	case DRAW_ARROW_RIGHT:
		vgui::surface()->DrawSetTexture( m_textureID_ArrowRight );
		x += iconWide + textWidth;
		y += iconTall / 2 - tall / 2;
		vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
		break;

	case DRAW_ARROW_UP:
		vgui::surface()->DrawSetTexture( m_textureID_ArrowUp );
		x += iconWide / 2 - wide / 2;
		y -= tall;
		vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
		break;

	case DRAW_ARROW_DOWN:
		vgui::surface()->DrawSetTexture( m_textureID_ArrowDown );
		x += iconWide / 2 - wide / 2;
		y += iconTall;
		vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
		break;

	default:
		// Do not draw.
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws a very simple arrow that points down.
//-----------------------------------------------------------------------------
void CLocatorPanel::DrawSimpleArrow( int x, int y, int iconWide, int iconTall )
{
	vgui::surface()->DrawSetTexture( m_textureID_SimpleArrow );

	y += iconTall;

	vgui::surface()->DrawTexturedRect( x, y, x + iconWide, y + iconTall );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CLocatorPanel::GetIconPositionForOffscreenTarget( const Vector &vecDelta, float flDist, int *pXPos, int *pYPos )
{
	float xpos, ypos;
	float flRotation;
	float flRadius = (float)( (double)ScreenHeight() * ( 1.0 / 480.0 ) * (double)OFFSCREEN_ICON_POSITION_RADIUS );


	GetTargetPosition( vecDelta, flRadius, &xpos, &ypos, &flRotation );

	*pXPos = NativeLocatorInteger( xpos );
	*pYPos = NativeLocatorInteger( ypos );
}

//-----------------------------------------------------------------------------
// Purpose: Given a handle, return the pointer to the proper locator target.
//-----------------------------------------------------------------------------
CLocatorTarget *CLocatorPanel::GetPointerForHandle( int hTarget )
{
	for( int i = 0 ; i < MAX_LOCATOR_TARGETS ; i++ )
	{
		if( m_targets[ i ].m_isActive && m_targets[ i ].m_serialNumber == hTarget )
		{
			return &m_targets[ i ];
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CLocatorPanel::AddTarget()
{
	for( int i = 0 ; i < MAX_LOCATOR_TARGETS ; i++ )
	{
		if( !m_targets[ i ].m_isActive )
		{
			m_targets[ i ].Activate( m_serializer );
			m_serializer = (int)( (unsigned int)m_serializer + 1u );

			return m_targets[ i ].m_serialNumber;
		}
	}

	DevWarning( "Locator Panel has no free targets!\n" );
	return -1;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CLocatorPanel::RemoveTarget( int hTarget )
{
	CLocatorTarget *pTarget = GetPointerForHandle( hTarget );

	if( pTarget )
	{
		pTarget->Deactivate();
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
