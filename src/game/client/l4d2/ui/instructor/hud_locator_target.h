//====== Copyright (c) 1996-2008, Valve Corporation, All rights reserved. =======
//
// Purpose: Add entities to this system, and the Locator will maintain an arrow
//			on the HUD that points to the entities when they are offscreen.
//
//=============================================================================

#ifndef L4D_HUD_LOCATOR_H
#define L4D_HUD_LOCATOR_H
#ifdef _WIN32
#pragma once
#endif


#include "vgui_controls/PHandle.h"
#include "tier1/utlvector.h"
class CHudTexture;


#define MAX_LOCATOR_BINDINGS_SHOWN	8
#define MAX_LOCATOR_TARGETS			10
#define LOCATOR_FLAGS_NONE			0x00000000

#define LOCATOR_ICON_FX_NONE			0x00000000
#define LOCATOR_ICON_FX_PULSE_SLOW		0x00000001
#define LOCATOR_ICON_FX_PULSE_FAST		0x00000002
#define LOCATOR_ICON_FX_PULSE_URGENT	0x00000004
#define LOCATOR_ICON_FX_ALPHA_SLOW		0x00000008
#define LOCATOR_ICON_FX_ALPHA_FAST		0x00000010
#define LOCATOR_ICON_FX_ALPHA_URGENT	0x00000020
#define LOCATOR_ICON_FX_SHAKE_NARROW	0x00000040
#define LOCATOR_ICON_FX_SHAKE_WIDE		0x00000080
#define LOCATOR_ICON_FX_STATIC			0x00000100	// This icon draws at a fixed location on the HUD.
#define LOCATOR_ICON_FX_NO_OFFSCREEN	0x00000200
#define LOCATOR_ICON_FX_FORCE_CAPTION	0x00000400	// Always draw the caption, even when the icon is occluded.
#define LOCATOR_ICON_FX_FADE_OUT		0x00000800	// Set when deactivated so it can smoothly vanish
#define LOCATOR_ICON_FX_FADE_IN			0x00001000	// Set when activated so it can smoothly appear
#define LOCATOR_ICON_FX_NO_PRESENT 0x00002000

#include "tier1/UtlSymbol.h"

// Native instructor-private CUtlSymbolTable: C4FA530 creates (0,16,false).
// Its sole definition/constructor/String bodies live in c_baselesson.cpp.
class CGameInstructorSymbol
{
public:
	CGameInstructorSymbol() {}
	CGameInstructorSymbol( const char *text );
	CGameInstructorSymbol( UtlSymId_t id ) : m_symbol( id ) {}
	bool IsValid() const { return m_symbol.IsValid(); }
	const char *String() const;
	operator UtlSymId_t() const { return m_symbol; }
	bool operator==( const CGameInstructorSymbol &other ) const { return m_symbol == other.m_symbol; }
	bool operator!=( const CGameInstructorSymbol &other ) const { return !( *this == other ); }
	bool operator<( const CGameInstructorSymbol &other ) const { return (UtlSymId_t)m_symbol < (UtlSymId_t)other.m_symbol; }
private:
	CUtlSymbol m_symbol;
	static CUtlSymbolTable s_Symbols;
};

//-----------------------------------------------------------------------------
// This class represents a single target to be tracked by the locator
//-----------------------------------------------------------------------------
class CLocatorTarget
{
public:
	Vector		m_vecOrigin;			// The location in the world to draw on the locator

	// ONLY the locator panel should fiddle with these fields.
	bool		m_isActive;		
	int			m_serialNumber;
	int			m_frameLastUpdated;
	bool		m_bOnscreen;
	bool		m_bOccluded;
	bool		m_bVisible;
	bool		m_bIsDrawing;
	float		m_distFromPlayer;
	CHudTexture	*m_pIcon_onscreen;
	CHudTexture	*m_pIcon_offscreen;
	int			m_iBindingTick;
	float		m_flNextBindingTick;
	float		m_flNextOcclusionTest;
	int			m_iBindingChoicesCount;
	const char	*(m_pchBindingChoices[ MAX_LOCATOR_BINDINGS_SHOWN ]);
	int			m_iBindChoicesOriginalToken[ MAX_LOCATOR_BINDINGS_SHOWN ];

	// Fields for drawing
	int			m_iconX;				// screen X position (top)
	int			m_iconY;				// screen Y position (left)
	int			m_centerX;				// screen X position (center)
	int			m_centerY;				// screen Y position (center)
	int			m_wide;					// draw width of icon (may be different from frame to frame as the icon's size animates, for instance)
	int			m_tall;					// draw height of icon  ''			''
	float		m_widthScale_onscreen;	// for icons that are wider than standard
	int			m_alpha;				// 
	float		m_fadeStart;			// time stamp when fade out started
	float		m_lerpStart;			// time stamp when lerping started
	float		m_pulseStart;			// time stamp when pulsing started
	int			m_declutterIndex;		// sort order from the declutterer
	int			m_lastDeclutterIndex;	// last sort order from the declutterer
	int			m_drawArrowDirection;	// Whether to draw an arrow indicating this target is off-screen, also tells us which arrow to draw (left, up, etc.)
	int			m_captionWide;			// How wide (pixels) my caption is.
	bool		m_bDrawControllerButton;
	bool		m_bDrawControllerButtonOffscreen;

	// Fields for interpolating icon position
	float		m_flTimeLerpDone;		// How much time left before this icon arrives where it is supposed to be.
	int			m_lastXPos;				// screen X position last frame
	int			m_lastYPos;				// ''     Y

	CLocatorTarget( void );
	void Activate( int serialNumber );
	void Deactivate( bool bNoFade = false );
	~CLocatorTarget();
	void Update();

	int GetIconX( void );
	int GetIconY( void );
	int GetIconCenterX( void );
	int GetIconCenterY( void );
	int GetIconWidth( void );
	int GetIconHeight( void );

	void AddIconEffects( int add )			{ m_iEffectsFlags |= add; }
	void RemoveIconEffects( int remove )	{ m_iEffectsFlags &= ~remove; }
	int GetIconEffectsFlags()				{ return m_iEffectsFlags; }
	void SetCaptionColor( Color col )		{ m_captionColor = col; }
	void SetCaptionColor( const char *pszCaptionColor );
	bool IsStatic();
	bool IsPresenting();
	void StartTimedLerp();
	void StartPresent();
	void EndPresent();


	void SetOnscreenIconTextureName( const char *pszTexture );
	void SetOffscreenIconTextureName( const char *pszTexture );
	void SetBinding( const char *pszBinding );
	const char *UseBindingImage( char *pchIconTextureName, size_t bufSize, float *widthScale );

	const char *GetOnscreenIconTextureName()	{ return m_szOnscreenTexture.String(); }
	const char *GetOffscreenIconTextureName()	{ return m_szOffscreenTexture.String(); }
	const char *GetBinding()			{ return m_szBinding.String(); }

	void SetVisible( bool bVisible );
	bool IsVisible( void );

	void SetCaptionText( const char *pszText, const char *pszParam );
	const wchar_t *GetCaptionText( void )	{ return (const wchar_t *)m_wszCaption.Base(); }
	bool HasCaptionText( void )			{ return m_wszCaption.Count() > 1; }

	void DrawBindingName( const char *pchDrawName )		{ m_pchDrawBindingName = pchDrawName; }
	void DrawBindingNameOffscreen( const char *pchDrawName )	{ m_pchDrawBindingNameOffscreen = pchDrawName; }

	const char *DrawBindingName( void )				{ return m_pchDrawBindingName; }
	const char *DrawBindingNameOffscreen( void )	{ return m_pchDrawBindingNameOffscreen; }

	bool IsOnScreen()	{ return m_bOnscreen; }
	bool IsOccluded()	{ return m_bOccluded; }


private:
	friend class CLocatorPanel;

	CGameInstructorSymbol	m_szOnscreenTexture;
	CGameInstructorSymbol	m_szOffscreenTexture;
	CGameInstructorSymbol	m_szBinding;

	bool		m_bWasControllerLast;
	bool m_bSteamControllerLast;
	void AddSteamOrigins( const int *origins, int token );
	int m_iSteamTextures[MAX_LOCATOR_BINDINGS_SHOWN];
	int m_iSteamOrigins[MAX_LOCATOR_BINDINGS_SHOWN];
	const char	*m_pchDrawBindingName;
	const char	*m_pchDrawBindingNameOffscreen;
	int			m_iEffectsFlags;
	CUtlVector< wchar_t > m_wszCaption;

public:
	Color		m_captionColor;
};

extern int Locator_AddTarget();
extern void Locator_RemoveTarget( int hTarget );
CLocatorTarget *Locator_GetTargetFromHandle( int hTarget );
void Locator_ComputeTargetIconPositionFromHandle( int hTarget );


#endif // L4D_HUD_LOCATOR_H
