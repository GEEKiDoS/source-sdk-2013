//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Shared graphics option definitions for the SDK and L4D2 interfaces.
//
//===========================================================================//

#ifndef VGUI_GRAPHICS_OPTIONS_H
#define VGUI_GRAPHICS_OPTIONS_H
#ifdef _WIN32
#pragma once
#endif

namespace GraphicsOptions
{

enum
{
	TAB_DISPLAY = 0,
	TAB_LIGHTING,
	TAB_POST,
	TAB_FRAMEGEN,
	TAB_COUNT
};

enum
{
	ROW_COMBO = 0,
	ROW_SLIDER,
	ROW_CHECK
};

enum
{
	ROWF_NONE = 0,
	ROWF_OFF_ENABLE_ONLY = 1,	// choice 0 matches and writes only the enable cvar, keeping the remembered quality cvars
	ROWF_LEGACY_ONLY = 2,	// native DX12 has no reduced-fillrate shader quality tier
	ROWF_DX12_ONLY = 4		// L4D2 hides renderer-specific options on legacy renderers
};

struct Choice_t
{
	const char *label;
	float v[5];
};

struct RowDef_t
{
	const char *label;
	int tab;
	int kind;
	const char *cvars[5];
	const Choice_t *choices;
	int nChoices;
	float sliderMin;
	float sliderMax;
	float sliderStep;
	const char *note;
	int flags;
};

const RowDef_t *GetRows( int &count );
int RowCvarCount( const RowDef_t &row );
bool IsRowAvailable( const RowDef_t &row );
// Matches current cvars; -1 means a custom value. Off choices match only the enable cvar when flagged.
int FindChoice( const RowDef_t &row );
bool IsDX12Renderer();

} // namespace GraphicsOptions

#endif // VGUI_GRAPHICS_OPTIONS_H
