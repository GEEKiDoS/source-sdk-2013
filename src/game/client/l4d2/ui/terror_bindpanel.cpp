//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_bindpanel.h"
#include "hud.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "inputsystem/IInputSystem.h"
#include "steam/steam_api.h"
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

DECLARE_BUILD_FACTORY( CBindPanel );

// Main-approved, recorded capability gap. This is not a replacement decoder:
// native 2B0AB0 cannot be provided by this target's bitmap interfaces. Return
// the native decoder's failure result, not fabricated pixels or a texture.
static const unsigned char *TerrorClient_QuerySteamInputGlyphPNGDecodeCapability( const char *filename )
{
	return NULL; // PNG decode unavailable; native failed-decode branch.
}

CBindPanel::CBindPanel( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name ), m_iSlot( -1 ), m_iSteamInputOrigin( 0 ),
	m_flNextBindingUpdate( 0.0f ), m_flWidthScale( 1.0f ),
	m_bControllerButton( false ), m_bSteamInputGlyph( false ),
	m_bDrawKeyText( true ), m_pIcon( NULL )
{
	m_szBind[0] = m_szKey[0] = m_szLastKey[0] = m_szIcon[0] = 0;
}

void CBindPanel::SetBind( const char *binding, int slot )
{
	if ( !binding ) { m_szBind[0] = 0; return; }
	Q_strncpy( m_szBind, binding, sizeof( m_szBind ) );
	m_iSlot = slot;
	UpdateBinding();
}

void CBindPanel::ApplySettings( KeyValues *settings )
{
	const char *binding = settings->GetString( "bind", "" );
	if ( binding && binding[0] ) SetBind( binding, 0 ); // Supported VGUI slot 0.
	BaseClass::ApplySettings( settings ); // Native updates the bind first.
}

void CBindPanel::PerformLayout()
{
	BaseClass::PerformLayout();
	SetSize( (int)( ( m_flWidthScale * (float)m_iIconTall ) * m_flScale ),
		(int)( m_flScale * (float)m_iIconTall ) );
}

void CBindPanel::OnThink()
{
	if ( gpGlobals->curtime > m_flNextBindingUpdate ) UpdateBinding();
}

void CBindPanel::UpdateBinding()
{
	m_bSteamInputGlyph = false;
	ISteamInput *steamInput = steamapicontext ? steamapicontext->SteamInput() : NULL;
	InputHandle_t controller = 0;
	if ( steamInput )
	{
		// Native 1EBD0 fallback. The target has no preferred active-handle
		// query (native inputsystem196); do not invent a different slot owner.
		InputHandle_t controllers[STEAM_INPUT_MAX_COUNT];
		if ( steamInput->GetConnectedControllers( controllers ) > 0 ) controller = controllers[0];
	}
	if ( controller )
	{
		const char *actionName = m_szBind[0] == '+' ? m_szBind + 1 : m_szBind;
		InputDigitalActionHandle_t action = steamInput->GetDigitalActionHandle( actionName );
		if ( action && g_pInputSystem )
		{
			EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS];
			// Native action-set selector 1 is the real SDK FPSCONTROLS set.
			InputActionSetHandle_t actionSet = g_pInputSystem->GetActionSetHandle( GAME_ACTION_SET_FPSCONTROLS );
			if ( steamInput->GetDigitalActionOrigins( controller, actionSet, action, origins ) > 0 && origins[0] != k_EInputActionOrigin_None )
			{
				Q_strncpy( m_szKey, steamInput->GetStringForActionOrigin( origins[0] ), sizeof( m_szKey ) );
				if ( (int)origins[0] != m_iSteamInputOrigin )
				{
					m_iSteamInputOrigin = (int)origins[0];
					int wide, tall;
					vgui::surface()->GetScreenSize( wide, tall );
					const int pixels = (int)( (float)(int)( (float)wide * 0.03f ) * 0.85f );
					ESteamInputGlyphSize size = pixels > 128 ? k_ESteamInputGlyphSize_Large : pixels > 32 ? k_ESteamInputGlyphSize_Medium : k_ESteamInputGlyphSize_Small;
					const char *filename = steamInput->GetGlyphPNGForActionOrigin( origins[0], size, 0 );
					if ( filename )
					{
						// Decode is unavailable. Its native failure does NOT revert
						// to keyboard text, reset the origin, or suppress KeyChanged.
						TerrorClient_QuerySteamInputGlyphPNGDecodeCapability( filename );
					}
				}
				m_bControllerButton = m_bSteamInputGlyph = true;
			}
		}
	}
	if ( !m_bSteamInputGlyph )
	{
		m_iSteamInputOrigin = 0;
		const char *actionName = m_szBind[0] == '+' ? m_szBind + 1 : m_szBind;
		// Approved projection of Key_LookupBindingEx(action,slot,0,-1).
		// The SDK supplies no extended slot/filter request. m_iSlot is still
		// stored by SetBind exactly as native; only that engine query differs.
		const char *key = engine->Key_LookupBinding( actionName );
		Q_strncpy( m_szKey, key ? key : "?", sizeof( m_szKey ) );
	}
	if ( Q_stricmp( m_szKey, m_szLastKey ) )
	{
		Q_strncpy( m_szLastKey, m_szKey, sizeof( m_szLastKey ) );
		UpdateBackground();
		InvalidateLayout( false, false );
		PostActionSignal( new KeyValues( "Command", "Command", "KeyChanged" ) );
	}
	m_flNextBindingUpdate = gpGlobals->curtime + 0.5f;
}

void CBindPanel::UpdateBackground()
{
	m_flWidthScale = 1.0f;
	m_bDrawKeyText = true;
	Q_strncpy( m_szIcon, "icon_blank", sizeof( m_szIcon ) );
	m_bControllerButton = false;
	if ( m_bSteamInputGlyph ) { m_bControllerButton = true; return; }
	static ConVarRef joystick( "joystick" );
	static const char *buttons[] = { "A_BUTTON", "B_BUTTON", "X_BUTTON", "Y_BUTTON", "L_SHOULDER", "R_SHOULDER", "BACK", "START", "STICK1", "STICK2", "UP", "DOWN", "LEFT", "RIGHT", "L_TRIGGER", "R_TRIGGER" };
	if ( m_szKey[0] && joystick.GetBool() )
	{
		for ( int i = 0; i < ARRAYSIZE( buttons ); ++i )
		{
			if ( Q_strcmp( m_szKey, buttons[i] ) ) continue;
			m_bControllerButton = true;
			if ( !Q_strcmp( m_szKey, "L_SHOULDER" ) || !Q_strcmp( m_szKey, "R_SHOULDER" ) ) m_flWidthScale = 2.0f;
			m_pIcon = gHUD.GetIcon( m_szIcon );
			return;
		}
	}
	static const struct { const char *key; const char *icon; } icons[] =
	{
		{ "MOUSE1", "icon_mouseLeft" }, { "MOUSE2", "icon_mouseRight" }, { "MOUSE3", "icon_mouseThree" },
		{ "MWHEELUP", "icon_mouseWheel_up" }, { "MWHEELDOWN", "icon_mouseWheel_down" },
		{ "UPARROW", "icon_key_up" }, { "LEFTARROW", "icon_key_left" },
		{ "DOWNARROW", "icon_key_down" }, { "RIGHTARROW", "icon_key_right" }
	};
	for ( int i = 0; i < ARRAYSIZE( icons ); ++i )
	{
		if ( Q_strcmp( m_szKey, icons[i].key ) ) continue;
		Q_strncpy( m_szIcon, icons[i].icon, sizeof( m_szIcon ) );
		m_bDrawKeyText = false;
		m_pIcon = gHUD.GetIcon( m_szIcon );
		return;
	}
	static const char *smallKeys[] = { "SEMICOLON", "INS", "DEL", "HOME", "END", "PGUP", "PGDN", "PAUSE", "F10", "F11", "F12" };
	bool small = !m_szKey[0] || Q_strlen( m_szKey ) <= 2;
	for ( int i = 0; i < ARRAYSIZE( smallKeys ) && !small; ++i ) small = !Q_strcmp( m_szKey, smallKeys[i] );
	Q_strncpy( m_szIcon, small ? "icon_key_generic" : "icon_key_wide", sizeof( m_szIcon ) );
	if ( !small ) m_flWidthScale = Q_strlen( m_szKey ) > 6 ? 3.0f : 2.0f;
	m_pIcon = gHUD.GetIcon( m_szIcon );
}

int CBindPanel::GetTextWidth( const wchar_t *text, vgui::HFont font ) const
{
	int width = 0;
	for ( ; *text; ++text ) width += vgui::surface()->GetCharacterWidth( font, *text );
	return width;
}

void CBindPanel::PaintBinding()
{
	const int centerX = (int)( ( m_flWidthScale * (float)m_iIconTall ) * m_flScale ) >> 1;
	const int centerY = (int)( ( m_flScale * (float)m_iIconTall ) * 0.5f );
	if ( m_bSteamInputGlyph )
	{
		// Native failed-decode path: texture ID remains -1; no image or text.
		return;
	}
	vgui::ISurface *surface = vgui::surface();
	const bool steamControllerActive = g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
	wchar_t text[64];
	if ( m_bControllerButton )
	{
		vgui::HFont font = steamControllerActive ? m_hButtonFontSC : m_hButtonFont;
		surface->DrawSetTextFont( font );
		const int height = surface->GetFontTall( font );
		char token[256];
		Q_snprintf( token, sizeof( token ), "#GameUI_Icons_%s", m_szKey );
		g_pVGuiLocalize->ConstructString( text, sizeof( text ), g_pVGuiLocalize->Find( token ), 0 );
		const int width = GetTextWidth( text, font );
		int screenWide, screenTall;
		surface->GetScreenSize( screenWide, screenTall );
		const float excess = (float)width - ( ( (float)screenWide * 0.04f + 5.0f ) + 5.0f );
		surface->DrawSetTextColor( 255, 255, 255, 255 );
		surface->DrawSetTextPos( centerX - ( width >> 1 ) - (int)MAX( 0.0f, excess ), centerY - ( height >> 1 ) );
	}
	else
	{
		char key[256];
		Q_strncpy( key, m_szKey, sizeof( key ) );
		if ( !Q_strcmp( key, "SEMICOLON" ) ) Q_strncpy( key, ";", sizeof( key ) );
		else if ( !Q_strcmp( key, "ESCAPE" ) ) Q_strncpy( key, "ESC", sizeof( key ) );
		else if ( Q_strlen( key ) == 1 && key[0] >= 'a' && key[0] <= 'z' ) key[0] -= 'a' - 'A';
		surface->DrawSetTextFont( m_hKeysFont );
		const int height = surface->GetFontTall( m_hKeysFont );
		g_pVGuiLocalize->ConvertANSIToUnicode( key, text, sizeof( text ) );
		int width = GetTextWidth( text, m_hKeysFont );
		if ( steamControllerActive ) surface->DrawSetTextColor( 255, 255, 255, 255 );
		else { surface->DrawSetTextColor( 0, 0, 0, 255 ); width >>= 1; }
		// Preserve native's asymmetric Steam Controller text centering.
		surface->DrawSetTextPos( centerX - width - 1, centerY - ( height >> 1 ) - 1 );
	}
	surface->DrawUnicodeString( text );
}

void CBindPanel::Paint()
{
	if ( !m_bControllerButton && m_pIcon )
		m_pIcon->DrawSelf( 0, 0, (int)( ( (float)m_iIconTall * m_flWidthScale ) * m_flScale ), (int)( (float)m_iIconTall * m_flScale ), GetFgColor() );
	if ( m_bDrawKeyText ) PaintBinding();
}
#pragma float_control( pop )
