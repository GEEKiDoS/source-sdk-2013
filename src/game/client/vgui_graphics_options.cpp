//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Advanced graphics dialog. One row table drives the controls.
//
//===========================================================================//

#include "cbase.h"
#include "ienginevgui.h"
#include "GameEventListener.h"
#include "vgui_controls/PropertyDialog.h"
#include "vgui_controls/PropertySheet.h"
#include "vgui_controls/PropertyPage.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/Slider.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Controls.h"
#include "vgui/IInput.h"
#include "vgui/IPanel.h"
#include "vgui/IVGui.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include <math.h>

#include "tier0/memdbgon.h"

using namespace vgui;

enum
{
	TAB_DISPLAY = 0,
	TAB_LIGHTING,
	TAB_POST,
	TAB_UPSCALE,
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
	ROWF_AA = 1,
	ROWF_GTAO = 2
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

static const Choice_t kModel[] =
{
	{ "#GraphicsOptions_High", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Medium", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Low", { 2, 0, 0, 0, 0 } },
};

static const Choice_t kTexture[] =
{
	{ "#GraphicsOptions_Ultra", { -1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_High", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Medium", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Low", { 2, 0, 0, 0, 0 } },
};

static const Choice_t kFilter[] =
{
	{ "#GraphicsOptions_Bilinear", { 0, 1, 0, 0, 0 } },
	{ "#GraphicsOptions_Trilinear", { 1, 1, 0, 0, 0 } },
	{ "#GraphicsOptions_Aniso2", { 0, 2, 0, 0, 0 } },
	{ "#GraphicsOptions_Aniso4", { 0, 4, 0, 0, 0 } },
	{ "#GraphicsOptions_Aniso8", { 0, 8, 0, 0, 0 } },
	{ "#GraphicsOptions_Aniso16", { 0, 16, 0, 0, 0 } },
};

static const Choice_t kAA[] =
{
	{ "#GraphicsOptions_None", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_MSAA2", { 2, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_MSAA4", { 4, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_MSAA8", { 8, 0, 0, 0, 0 } },
};

static const Choice_t kWater[] =
{
	{ "#GraphicsOptions_Simple", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_ReflectWorld", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_ReflectAll", { 1, 1, 0, 0, 0 } },
};

static const Choice_t kShader[] =
{
	{ "#GraphicsOptions_High", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Low", { 1, 0, 0, 0, 0 } },
};

static const Choice_t kMulticore[] =
{
	{ "#GraphicsOptions_Disabled", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Enabled", { -1, 0, 0, 0, 0 } },
};

static const Choice_t kShadowRes[] =
{
	{ "#GraphicsOptions_Low", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Medium", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_High", { 2, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Ultra", { 3, 0, 0, 0, 0 } },
};

static const Choice_t kShadowFilter[] =
{
	{ "#GraphicsOptions_Hard", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Soft", { 1, 0, 0, 0, 0 } },
};

static const Choice_t kSunDistance[] =
{
	{ "1024", { 1024, 0, 0, 0, 0 } },
	{ "2048", { 2048, 0, 0, 0, 0 } },
	{ "4096", { 4096, 0, 0, 0, 0 } },
	{ "8192", { 8192, 0, 0, 0, 0 } },
	{ "16384", { 16384, 0, 0, 0, 0 } },
};

static const Choice_t kRealtimeLights[] =
{
	{ "1", { 1, 0, 0, 0, 0 } },
	{ "2", { 2, 0, 0, 0, 0 } },
	{ "4", { 4, 0, 0, 0, 0 } },
	{ "8", { 8, 0, 0, 0, 0 } },
	{ "16", { 16, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Unlimited", { 0, 0, 0, 0, 0 } },
};

// Off matches r_gtao alone. High is the current default (resolution, slices, steps, denoise) = (1, 3, 3, 2).
static const Choice_t kGtao[] =
{
	{ "#GraphicsOptions_Off", { 0, 1, 3, 3, 2 } },
	{ "#GraphicsOptions_Low", { 1, 0.5f, 1, 2, 1 } },
	{ "#GraphicsOptions_Medium", { 1, 0.5f, 2, 3, 1 } },
	{ "#GraphicsOptions_High", { 1, 1, 3, 3, 2 } },
	{ "#GraphicsOptions_Ultra", { 1, 1, 6, 4, 2 } },
};

static const Choice_t kQuality3[] =
{
	{ "#GraphicsOptions_Low", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Medium", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_High", { 2, 0, 0, 0, 0 } },
};

static const Choice_t kUpscaler[] =
{
	{ "#GraphicsOptions_Off", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Auto", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_DLAA", { 2, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_FSRAA", { 3, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_XeSSAA", { 4, 0, 0, 0, 0 } },
};

static const Choice_t kFramegen[] =
{
	{ "#GraphicsOptions_Off", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Auto", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_DLSSG", { 2, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_FSR", { 3, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_XeSS", { 4, 0, 0, 0, 0 } },
};

static const Choice_t kMultiplier[] =
{
	{ "2x", { 2, 0, 0, 0, 0 } },
	{ "3x", { 3, 0, 0, 0, 0 } },
	{ "4x", { 4, 0, 0, 0, 0 } },
};

static const Choice_t kReflex[] =
{
	{ "#GraphicsOptions_Off", { 0, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_On", { 1, 0, 0, 0, 0 } },
	{ "#GraphicsOptions_Boost", { 2, 0, 0, 0, 0 } },
};

static const RowDef_t g_Rows[] =
{
	{ "#GraphicsOptions_Model", TAB_DISPLAY, ROW_COMBO, { "r_rootlod", NULL, NULL, NULL, NULL }, kModel, ARRAYSIZE( kModel ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Texture", TAB_DISPLAY, ROW_COMBO, { "mat_picmip", NULL, NULL, NULL, NULL }, kTexture, ARRAYSIZE( kTexture ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Filtering", TAB_DISPLAY, ROW_COMBO, { "mat_trilinear", "mat_forceaniso", NULL, NULL, NULL }, kFilter, ARRAYSIZE( kFilter ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_AA", TAB_DISPLAY, ROW_COMBO, { "mat_antialias", "mat_aaquality", NULL, NULL, NULL }, kAA, ARRAYSIZE( kAA ), 0, 0, 0, NULL, ROWF_AA },
	{ "#GraphicsOptions_Water", TAB_DISPLAY, ROW_COMBO, { "r_waterforceexpensive", "r_waterforcereflectentities", NULL, NULL, NULL }, kWater, ARRAYSIZE( kWater ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Shader", TAB_DISPLAY, ROW_COMBO, { "mat_reducefillrate", NULL, NULL, NULL, NULL }, kShader, ARRAYSIZE( kShader ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Color", TAB_DISPLAY, ROW_CHECK, { "mat_colorcorrection", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Multicore", TAB_DISPLAY, ROW_COMBO, { "mat_queue_mode", NULL, NULL, NULL, NULL }, kMulticore, ARRAYSIZE( kMulticore ), 0, 0, 0, "#GraphicsOptions_Restart", ROWF_NONE },
	{ "#GraphicsOptions_VSync", TAB_DISPLAY, ROW_CHECK, { "mat_vsync", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Fps", TAB_DISPLAY, ROW_SLIDER, { "fps_max", NULL, NULL, NULL, NULL }, NULL, 0, 0, 1000, 1, "#GraphicsOptions_FpsNote", ROWF_NONE },
	{ "#GraphicsOptions_PBR", TAB_DISPLAY, ROW_CHECK, { "mat_pbr_override", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Bicubic", TAB_DISPLAY, ROW_CHECK, { "r_lightmap_bicubic", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Detail", TAB_DISPLAY, ROW_SLIDER, { "cl_detaildist", NULL, NULL, NULL, NULL }, NULL, 0, 0, 4000, 50, NULL, ROWF_NONE },

	{ "#GraphicsOptions_Shadows", TAB_LIGHTING, ROW_CHECK, { "r_shadowmap_enable", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_ShadowRes", TAB_LIGHTING, ROW_COMBO, { "r_shadowmap_resolution", NULL, NULL, NULL, NULL }, kShadowRes, ARRAYSIZE( kShadowRes ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_ShadowFilter", TAB_LIGHTING, ROW_COMBO, { "r_shadowmap_filter", NULL, NULL, NULL, NULL }, kShadowFilter, ARRAYSIZE( kShadowFilter ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_SunDist", TAB_LIGHTING, ROW_COMBO, { "r_csm_distance", NULL, NULL, NULL, NULL }, kSunDistance, ARRAYSIZE( kSunDistance ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_MaxLights", TAB_LIGHTING, ROW_COMBO, { "r_shadowmap_max_realtime_lights", NULL, NULL, NULL, NULL }, kRealtimeLights, ARRAYSIZE( kRealtimeLights ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_EditorLights", TAB_LIGHTING, ROW_SLIDER, { "r_editor_lights_max_shadowed", NULL, NULL, NULL, NULL }, NULL, 0, 0, 32, 1, NULL, ROWF_NONE },
	{ "#GraphicsOptions_AO", TAB_LIGHTING, ROW_COMBO, { "r_gtao", "r_gtao_resolution", "r_gtao_slices", "r_gtao_steps", "r_gtao_denoise" }, kGtao, ARRAYSIZE( kGtao ), 0, 0, 0, NULL, ROWF_GTAO },

	{ "#GraphicsOptions_PostFx", TAB_POST, ROW_CHECK, { "mat_postfx_dx12", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_BloomQuality", TAB_POST, ROW_COMBO, { "mat_postfx_bloom_quality", NULL, NULL, NULL, NULL }, kQuality3, ARRAYSIZE( kQuality3 ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Bloom", TAB_POST, ROW_SLIDER, { "mat_postfx_bloom_intensity", NULL, NULL, NULL, NULL }, NULL, 0, 0, 400, 5, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Dirt", TAB_POST, ROW_CHECK, { "mat_postfx_lensdirt_enable", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Flare", TAB_POST, ROW_SLIDER, { "mat_postfx_lensflare", NULL, NULL, NULL, NULL }, NULL, 0, 0, 2, 0.01f, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Motion", TAB_POST, ROW_CHECK, { "mat_motion_blur_enabled", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_MotionQuality", TAB_POST, ROW_COMBO, { "mat_motion_blur_quality", NULL, NULL, NULL, NULL }, kQuality3, ARRAYSIZE( kQuality3 ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Exposure", TAB_POST, ROW_SLIDER, { "mat_postfx_exposure", NULL, NULL, NULL, NULL }, NULL, 0, -4, 4, 0.1f, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Sharpen", TAB_POST, ROW_SLIDER, { "mat_postfx_sharpen", NULL, NULL, NULL, NULL }, NULL, 0, 0, 1, 0.01f, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Hdr", TAB_POST, ROW_CHECK, { "mat_hdr_output", NULL, NULL, NULL, NULL }, NULL, 0, 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Nits", TAB_POST, ROW_SLIDER, { "mat_hdr_output_max_nits", NULL, NULL, NULL, NULL }, NULL, 0, 0, 2000, 10, "#GraphicsOptions_NitsNote", ROWF_NONE },

	{ "#GraphicsOptions_Upscaler", TAB_UPSCALE, ROW_COMBO, { "r_upscaler", NULL, NULL, NULL, NULL }, kUpscaler, ARRAYSIZE( kUpscaler ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Framegen", TAB_UPSCALE, ROW_COMBO, { "r_framegen", NULL, NULL, NULL, NULL }, kFramegen, ARRAYSIZE( kFramegen ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_FramegenRate", TAB_UPSCALE, ROW_COMBO, { "r_framegen_multiplier", NULL, NULL, NULL, NULL }, kMultiplier, ARRAYSIZE( kMultiplier ), 0, 0, 0, NULL, ROWF_NONE },
	{ "#GraphicsOptions_Reflex", TAB_UPSCALE, ROW_COMBO, { "r_reflex", NULL, NULL, NULL, NULL }, kReflex, ARRAYSIZE( kReflex ), 0, 0, 0, NULL, ROWF_NONE },
};

static int RowCvarCount( const RowDef_t &row )
{
	int n = 0;
	while ( n < 5 && row.cvars[n] )
		++n;
	return n;
}

static int SliderSteps( const RowDef_t &row )
{
	int steps = (int)( ( row.sliderMax - row.sliderMin ) / row.sliderStep + 0.5f );
	return steps < 1 ? 1 : steps;
}

static int FloatToSlider( const RowDef_t &row, float v )
{
	int steps = SliderSteps( row );
	float t = ( v - row.sliderMin ) / ( row.sliderMax - row.sliderMin );
	int s = (int)( t * steps + 0.5f );
	if ( s < 0 )
		s = 0;
	if ( s > steps )
		s = steps;
	return s;
}

static float SliderToFloat( const RowDef_t &row, int s )
{
	return row.sliderMin + row.sliderStep * s;
}

static bool Nearly( float a, float b )
{
	return fabsf( a - b ) <= 0.02f;
}

static float ReadCvar( const char *name )
{
	ConVarRef ref( name );
	return ref.IsValid() ? ref.GetFloat() : 0.f;
}

static void FormatNumber( char *buf, int n, float value )
{
	int i = (int)floorf( value + 0.5f );
	if ( fabsf( value - (float)i ) < 0.0001f )
		Q_snprintf( buf, n, "%d", i );
	else
		Q_snprintf( buf, n, "%.4g", value );
}

class CGraphicsOptionsDialog;

class CGraphicsOptionsPage : public PropertyPage
{
	DECLARE_CLASS_SIMPLE( CGraphicsOptionsPage, PropertyPage );
public:
	CGraphicsOptionsPage( Panel *parent, CGraphicsOptionsDialog *owner, const char *name )
		: BaseClass( parent, name ), m_pOwner( owner )
	{
		SetProportional( false );
	}

	virtual void OnSizeChanged( int wide, int tall )
	{
		Panel::OnSizeChanged( wide, tall );
		InvalidateLayout();
	}

	virtual void PerformLayout();

	CGraphicsOptionsDialog *m_pOwner;
	CUtlVector<int> m_Rows;
};

struct Widget_t
{
	const RowDef_t *row;
	Label *label;
	Label *note;
	ComboBox *combo;
	Slider *slider;
	Label *value;
	CheckButton *check;
	float custom[5];
};

class CGraphicsOptionsDialog : public PropertyDialog, public CGameEventListener
{
	DECLARE_CLASS_SIMPLE( CGraphicsOptionsDialog, PropertyDialog );
public:
	CGraphicsOptionsDialog( VPANEL parent );
	~CGraphicsOptionsDialog() {}

	void Open( int tab );
	void ApplyChanges();
	void SetPending( const char *cvar, float value );
	void LayoutPage( CGraphicsOptionsPage *page );

	virtual void ApplySchemeSettings( IScheme *pScheme );
	virtual void PerformLayout();
	virtual void OnCommand( const char *command );
	virtual void OnClose();
	virtual void OnKeyCodeTyped( KeyCode code );
	void FireGameEvent( IGameEvent *event );
	void DumpMetrics();

private:
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnSliderMoved, "SliderMoved", panel );

	void BuildRows();
	void ReadAll();
	void RefreshState();
	void UpdateValueLabel( Widget_t &w );
	int SelectedChoice( const Widget_t &w ) const;
	float ControlValue( const Widget_t &w, int slot ) const;
	bool UpscaleFromUI() const;
	Widget_t *FindWidget( const char *cvar );
	void ClearModal();
	void FitAndCenter();

	CGraphicsOptionsPage *m_pPages[TAB_COUNT];
	CUtlVector<Widget_t> m_Widgets;
};

static DHANDLE<CGraphicsOptionsDialog> s_hDialog;

void CGraphicsOptionsPage::PerformLayout()
{
	BaseClass::PerformLayout();
	if ( m_pOwner )
		m_pOwner->LayoutPage( this );
}

static void UseScheme( Panel *panel, HScheme hs )
{
	if ( !panel )
		return;
	panel->SetScheme( hs );
	for ( int i = 0; i < panel->GetChildCount(); ++i )
		UseScheme( panel->GetChild( i ), hs );
}

CGraphicsOptionsDialog::CGraphicsOptionsDialog( VPANEL parent )
	: BaseClass( NULL, "GraphicsOptions" )
{
	SetParent( parent );
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_%language%.txt" );
	HScheme hScheme = ipanel()->GetScheme( parent );
	if ( !hScheme )
		hScheme = scheme()->GetScheme( "SourceScheme" );
	SetScheme( hScheme );
	SetDeleteSelfOnClose( false );
	SetSizeable( false );
	SetMoveable( true );
	SetTitle( "#GraphicsOptions_Title", true );
	SetBounds( 0, 0, 512, 406 );

	static const char *s_Titles[TAB_COUNT] =
	{
		"#GraphicsOptions_TabDisplay",
		"#GraphicsOptions_TabLighting",
		"#GraphicsOptions_TabPost",
		"#GraphicsOptions_TabUpscale"
	};
	static const char *s_Names[TAB_COUNT] = { "DisplayPage", "LightingPage", "PostPage", "UpscalePage" };
	for ( int i = 0; i < TAB_COUNT; ++i )
	{
		m_pPages[i] = new CGraphicsOptionsPage( GetPropertySheet(), this, s_Names[i] );
		AddPage( m_pPages[i], s_Titles[i] );
	}

	m_Widgets.EnsureCapacity( ARRAYSIZE( g_Rows ) );
	BuildRows();

	SetApplyButtonVisible( true );
	EnableApplyButton( true );
	UseScheme( this, hScheme );
	InvalidateLayout( false, true );

	ListenForGameEvent( "gameui_hidden" );
	SetVisible( false );
}

void CGraphicsOptionsDialog::BuildRows()
{
	for ( int i = 0; i < ARRAYSIZE( g_Rows ); ++i )
	{
		const RowDef_t &row = g_Rows[i];
		CGraphicsOptionsPage *page = m_pPages[row.tab];
		int index = m_Widgets.AddToTail();
		Widget_t &w = m_Widgets[index];
		V_memset( &w, 0, sizeof( w ) );
		w.row = &row;

		w.label = new Label( page, "RowLabel", row.label );
		w.note = new Label( page, "RowNote", row.note ? row.note : "" );
		w.note->SetVisible( row.note != NULL );

		if ( row.kind == ROW_COMBO )
		{
			w.combo = new ComboBox( page, "RowCombo", row.nChoices > 0 ? row.nChoices : 1, false );
			w.combo->AddActionSignalTarget( this );
		}
		else if ( row.kind == ROW_SLIDER )
		{
			w.slider = new Slider( page, "RowSlider" );
			w.slider->SetRange( 0, SliderSteps( row ) );
			w.slider->AddActionSignalTarget( this );
			w.value = new Label( page, "RowValue", "" );
		}
		else
		{
			w.check = new CheckButton( page, "RowCheck", "" );
			w.check->AddActionSignalTarget( this );
		}

		page->m_Rows.AddToTail( index );
	}
}

void CGraphicsOptionsDialog::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetTitle( "#GraphicsOptions_Title", true );

	HFont font = pScheme->GetFont( "Default", IsProportional() );
	int fontTall = surface()->GetFontTall( font );
	if ( fontTall < 1 )
		fontTall = 16;
	int controlH = fontTall + 8;
	int noteH = fontTall;

	const char *titles[TAB_COUNT] =
	{
		"#GraphicsOptions_TabDisplay",
		"#GraphicsOptions_TabLighting",
		"#GraphicsOptions_TabPost",
		"#GraphicsOptions_TabUpscale"
	};
	int tabW = 84;
	for ( int i = 0; i < TAB_COUNT; ++i )
	{
		wchar_t converted[128];
		const wchar_t *text = g_pVGuiLocalize->Find( titles[i] );
		if ( !text )
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( titles[i], converted, sizeof( converted ) );
			text = converted;
		}
		int w, h;
		surface()->GetTextSize( font, text, w, h );
		if ( w + 16 > tabW )
			tabW = w + 16;
	}
	GetPropertySheet()->SetTabWidth( tabW );

	int rows[TAB_COUNT] = {};
	int notes[TAB_COUNT] = {};
	for ( int i = 0; i < ARRAYSIZE( g_Rows ); ++i )
	{
		rows[g_Rows[i].tab]++;
		if ( g_Rows[i].note )
			notes[g_Rows[i].tab]++;
	}
	int content = 0;
	for ( int i = 0; i < TAB_COUNT; ++i )
	{
		int h = rows[i] * ( controlH + 4 ) + notes[i] * ( noteH + 2 ) + 8;
		if ( h > content )
			content = h;
	}
	int frameW = tabW * TAB_COUNT + 28;
	if ( frameW < 512 )
		frameW = 512;
	SetSize( frameW, content + fontTall + 108 );

	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		w.label->SetFont( font );
		w.note->SetFont( font );
		if ( w.combo )
			w.combo->SetFont( font );
		if ( w.value )
			w.value->SetFont( font );
		if ( w.check )
			w.check->SetFont( font );
	}
	FitAndCenter();
}

void CGraphicsOptionsDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	for ( int i = 0; i < TAB_COUNT; ++i )
		LayoutPage( m_pPages[i] );
}

static VPANEL FindVPanel( VPANEL v, const char *name, int depth );

void CGraphicsOptionsDialog::FitAndCenter()
{
	MoveToCenterOfScreen();
}

void CGraphicsOptionsDialog::LayoutPage( CGraphicsOptionsPage *page )
{
	int wide = page->GetWide();
	if ( wide < 64 )
		wide = GetWide() - 40;
	HFont font = scheme()->GetIScheme( GetScheme() )->GetFont( "Default", IsProportional() );
	int fontTall = surface()->GetFontTall( font );
	if ( fontTall < 1 )
		fontTall = 16;
	int controlH = fontTall + 8;
	int noteH = fontTall;
	int y = 6;
	for ( int n = 0; n < page->m_Rows.Count(); ++n )
	{
		Widget_t &w = m_Widgets[page->m_Rows[n]];
		w.label->SetBounds( 8, y, 160, controlH );
		int x = 172;
		int cw = wide - x - ( controlH * 3 );
		if ( cw < 160 )
			cw = 160;
		if ( w.combo )
			w.combo->SetBounds( x, y, cw, controlH );
		if ( w.slider )
		{
			w.slider->SetBounds( x, y, cw, controlH );
			w.value->SetBounds( x + cw + 6, y, controlH * 3, controlH );
		}
		if ( w.check )
			w.check->SetBounds( x, y, controlH, controlH );
		y += controlH + 4;
		if ( w.note->IsVisible() )
		{
			w.note->SetBounds( x, y, wide - x - 8, noteH );
			y += noteH + 2;
		}
	}
}

int CGraphicsOptionsDialog::SelectedChoice( const Widget_t &w ) const
{
	if ( !w.combo )
		return 0;
	KeyValues *kv = w.combo->GetActiveItemUserData();
	return kv ? kv->GetInt( "i", 0 ) : 0;
}

float CGraphicsOptionsDialog::ControlValue( const Widget_t &w, int slot ) const
{
	if ( w.check )
		return w.check->IsSelected() ? 1.f : 0.f;
	if ( w.slider )
		return SliderToFloat( *w.row, w.slider->GetValue() );
	int choice = SelectedChoice( w );
	if ( choice < 0 )
		return w.custom[slot];
	return w.row->choices[choice].v[slot];
}

bool CGraphicsOptionsDialog::UpscaleFromUI() const
{
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		const Widget_t &w = m_Widgets[i];
		if ( !w.row->cvars[0] )
			continue;
		if ( Q_stricmp( w.row->cvars[0], "r_upscaler" ) && Q_stricmp( w.row->cvars[0], "r_framegen" ) )
			continue;
		if ( ControlValue( w, 0 ) > 0.5f )
			return true;
	}
	return false;
}

void CGraphicsOptionsDialog::UpdateValueLabel( Widget_t &w )
{
	if ( !w.value || !w.slider )
		return;
	float v = SliderToFloat( *w.row, w.slider->GetValue() );
	char buf[64];
	if ( !Q_stricmp( w.row->cvars[0], "fps_max" ) && v <= 0.f )
	{
		w.value->SetText( "#GraphicsOptions_Unlimited" );
		return;
	}
	if ( !Q_stricmp( w.row->cvars[0], "mat_hdr_output_max_nits" ) && v <= 0.f )
	{
		w.value->SetText( "#GraphicsOptions_Detect" );
		return;
	}
	else if ( w.row->sliderStep >= 1.f )
		Q_snprintf( buf, sizeof( buf ), "%.0f", v );
	else
		Q_snprintf( buf, sizeof( buf ), "%.2f", v );
	w.value->SetText( buf );
}

void CGraphicsOptionsDialog::ReadAll()
{
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		const RowDef_t &row = *w.row;
		int n = RowCvarCount( row );
		float cur[5];
		for ( int c = 0; c < 5; ++c )
			cur[c] = c < n ? ReadCvar( row.cvars[c] ) : 0.f;

		if ( w.combo )
		{
			w.combo->RemoveAll();
			for ( int choice = 0; choice < row.nChoices; ++choice )
			{
				KeyValues *kv = new KeyValues( "c" );
				kv->SetInt( "i", choice );
				w.combo->AddItem( row.choices[choice].label, kv );
				kv->deleteThis();
			}
			int match = -1;
			for ( int choice = 0; choice < row.nChoices; ++choice )
			{
				bool ok = true;
				int compare = ( ( row.flags & ROWF_GTAO ) && choice == 0 ) ? 1 : n;
				for ( int c = 0; c < compare; ++c )
				{
					if ( !Nearly( cur[c], row.choices[choice].v[c] ) )
					{
						ok = false;
						break;
					}
				}
				if ( ok )
				{
					match = choice;
					break;
				}
			}
			if ( match < 0 )
			{
				for ( int c = 0; c < 5; ++c )
					w.custom[c] = cur[c];
				KeyValues *kv = new KeyValues( "c" );
				kv->SetInt( "i", -1 );
				w.combo->AddItem( "#GraphicsOptions_Custom", kv );
				kv->deleteThis();
				w.combo->SilentActivateItemByRow( row.nChoices );
			}
			else
			{
				w.combo->SilentActivateItemByRow( match );
			}
		}
		else if ( w.slider )
		{
			w.slider->SetValue( FloatToSlider( row, cur[0] ), false );
			UpdateValueLabel( w );
		}
		else if ( w.check )
		{
			w.check->SetSelected( cur[0] != 0.f );
		}
	}
	RefreshState();
}

void CGraphicsOptionsDialog::RefreshState()
{
	bool up = UpscaleFromUI();
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		if ( w.row->flags & ROWF_AA )
		{
			if ( w.combo )
				w.combo->SetEnabled( !up );
			w.note->SetText( up ? "#GraphicsOptions_AABlocked" : "" );
			w.note->SetVisible( up );
		}
		else if ( w.row->note )
		{
			w.note->SetText( w.row->note );
			w.note->SetVisible( true );
		}
		else
		{
			w.note->SetVisible( false );
		}
	}
	InvalidateLayout();
	for ( int i = 0; i < TAB_COUNT; ++i )
		m_pPages[i]->InvalidateLayout();
}

static void AppendSet( char *cmd, int n, const char *name, float value )
{
	ConVarRef ref( name );
	if ( !ref.IsValid() || fabsf( ref.GetFloat() - value ) <= 0.0005f )
		return;
	char num[32];
	FormatNumber( num, sizeof( num ), value );
	int len = Q_strlen( cmd );
	if ( len )
		Q_snprintf( cmd + len, n - len, "; %s %s", name, num );
	else
		Q_snprintf( cmd, n, "%s %s", name, num );
}

void CGraphicsOptionsDialog::ApplyChanges()
{
	char cmd[4096];
	char aa[256];
	cmd[0] = 0;
	aa[0] = 0;
	bool up = UpscaleFromUI();

	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		const RowDef_t &row = *w.row;
		int n = RowCvarCount( row );
		if ( ( row.flags & ROWF_AA ) && up )
		{
			AppendSet( aa, sizeof( aa ), "mat_antialias", 0 );
			AppendSet( aa, sizeof( aa ), "mat_aaquality", 0 );
			continue;
		}
		if ( ( row.flags & ROWF_GTAO ) && w.combo && SelectedChoice( w ) == 0 )
			n = 1;
		for ( int c = 0; c < n; ++c )
		{
			const char *name = row.cvars[c];
			float value = ControlValue( w, c );
			bool aaCvar = !Q_stricmp( name, "mat_antialias" ) || !Q_stricmp( name, "mat_aaquality" );
			AppendSet( aaCvar ? aa : cmd, aaCvar ? sizeof( aa ) : sizeof( cmd ), name, value );
		}
	}

	if ( aa[0] )
	{
		int len = Q_strlen( cmd );
		if ( len )
			Q_snprintf( cmd + len, sizeof( cmd ) - len, "; %s", aa );
		else
			Q_snprintf( cmd, sizeof( cmd ), "%s", aa );
	}

	if ( !cmd[0] )
	{
		Msg( "graphics_options: no changes\n" );
		return;
	}

	int len = Q_strlen( cmd );
	Q_snprintf( cmd + len, sizeof( cmd ) - len, "; mat_savechanges; host_writeconfig" );
	Msg( "graphics_options: %s\n", cmd );
	engine->ClientCmd_Unrestricted( cmd );
}

Widget_t *CGraphicsOptionsDialog::FindWidget( const char *cvar )
{
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		for ( int c = 0; c < RowCvarCount( *w.row ); ++c )
		{
			if ( !Q_stricmp( w.row->cvars[c], cvar ) )
				return &w;
		}
	}
	return NULL;
}

void CGraphicsOptionsDialog::SetPending( const char *cvar, float value )
{
	Widget_t *w = FindWidget( cvar );
	if ( !w )
	{
		Warning( "graphics_options: unknown row %s\n", cvar );
		return;
	}
	if ( w->check )
	{
		w->check->SetSelected( value != 0.f );
	}
	else if ( w->slider )
	{
		w->slider->SetValue( FloatToSlider( *w->row, value ), false );
		UpdateValueLabel( *w );
	}
	else if ( w->combo )
	{
		int slot = 0;
		for ( int c = 0; c < RowCvarCount( *w->row ); ++c )
		{
			if ( !Q_stricmp( w->row->cvars[c], cvar ) )
			{
				slot = c;
				break;
			}
		}
		int match = -1;
		for ( int choice = 0; choice < w->row->nChoices; ++choice )
		{
			if ( Nearly( w->row->choices[choice].v[slot], value ) )
			{
				match = choice;
				break;
			}
		}
		if ( match >= 0 )
			w->combo->ActivateItemByRow( match );
		else
			Warning( "graphics_options: %s has no choice %g\n", cvar, value );
	}
	RefreshState();
}

void CGraphicsOptionsDialog::OnTextChanged( Panel *panel )
{
	if ( panel )
		RefreshState();
}

void CGraphicsOptionsDialog::OnSliderMoved( Panel *panel )
{
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		if ( m_Widgets[i].slider == panel )
			UpdateValueLabel( m_Widgets[i] );
	}
}

void CGraphicsOptionsDialog::ClearModal()
{
	if ( input() && input()->GetAppModalSurface() == GetVPanel() )
		input()->SetAppModalSurface( NULL );
}

void CGraphicsOptionsDialog::OnClose()
{
	VPANEL options = GetVParent();
	if ( options )
	{
		const char *name = ipanel()->GetName( options );
		if ( !name || Q_stricmp( name, "OptionsDialog" ) )
			options = 0;
	}
	ClearModal();
	BaseClass::OnClose();
	if ( options )
	{
		ipanel()->MoveToFront( options );
		ipanel()->SetKeyBoardInputEnabled( options, true );
		ipanel()->SetMouseInputEnabled( options, true );
	}
}

void CGraphicsOptionsDialog::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "OK" ) )
	{
		ApplyChanges();
		Close();
	}
	else if ( !Q_stricmp( command, "Apply" ) )
	{
		ApplyChanges();
	}
	else if ( !Q_stricmp( command, "Cancel" ) || !Q_stricmp( command, "Close" ) )
	{
		Close();
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

void CGraphicsOptionsDialog::OnKeyCodeTyped( KeyCode code )
{
	if ( code == KEY_ESCAPE )
	{
		Close();
		return;
	}
	BaseClass::OnKeyCodeTyped( code );
}

void CGraphicsOptionsDialog::FireGameEvent( IGameEvent *event )
{
	if ( event && !Q_strcmp( event->GetName(), "gameui_hidden" ) && IsVisible() )
		Close();
}

void CGraphicsOptionsDialog::Open( int tab )
{
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	VPANEL options = root ? FindVPanel( root, "OptionsDialog", 0 ) : 0;
	VPANEL parent = options ? options : root;
	if ( parent )
		SetParent( parent );
	if ( tab < 0 )
		tab = 0;
	if ( tab >= TAB_COUNT )
		tab = TAB_COUNT - 1;

	engine->ClientCmd_Unrestricted( "gameui_activate" );
	ReadAll();
	GetPropertySheet()->SetActivePage( m_pPages[tab] );
	Activate();
	FitAndCenter();
	MoveToFront();
	input()->SetAppModalSurface( GetVPanel() );
	Msg( "graphics_options: opened tab %d\n", tab );
}
static void DumpSchemeFonts( const char *tag, HScheme hs )
{
	IScheme *s = scheme()->GetIScheme( hs );
	int scale24 = scheme()->GetProportionalScaledValueEx( hs, 24 );
	Msg( "GFXMETRICS scheme %s handle=%d scale24=%d\n", tag, (int)hs, scale24 );
	if ( !s )
		return;
	const char *names[] = { "Default", "DefaultSmall", "DefaultVerySmall", "DefaultLarge" };
	for ( int i = 0; i < ARRAYSIZE( names ); ++i )
	{
		for ( int prop = 0; prop < 2; ++prop )
		{
			HFont font = s->GetFont( names[i], prop != 0 );
			Msg( "GFXMETRICS font %s %s prop=%d name=\"%s\" tall=%d\n",
				tag, names[i], prop, font ? surface()->GetFontName( font ) : "", font ? surface()->GetFontTall( font ) : 0 );
		}
	}
}

static VPANEL FindVPanel( VPANEL v, const char *name, int depth )
{
	if ( !v || depth > 14 )
		return 0;
	const char *got = ipanel()->GetName( v );
	if ( got && !Q_stricmp( got, name ) )
		return v;
	int count = ipanel()->GetChildCount( v );
	for ( int i = 0; i < count; ++i )
	{
		VPANEL found = FindVPanel( ipanel()->GetChild( v, i ), name, depth + 1 );
		if ( found )
			return found;
	}
	return 0;
}

static void DumpVPanelTree( VPANEL v, int depth )
{
	if ( !v || depth > 5 )
		return;
	int x, y, w, h;
	ipanel()->GetPos( v, x, y );
	ipanel()->GetSize( v, w, h );
	const char *cls = ipanel()->GetClassName( v );
	bool interesting = depth <= 2 || ( cls && ( !Q_stricmp( cls, "PageTab" ) || !Q_stricmp( cls, "CheckButton" ) || !Q_stricmp( cls, "Label" ) || !Q_stricmp( cls, "Button" ) || !Q_stricmp( cls, "ComboBox" ) || !Q_stricmp( cls, "Slider" ) ) );
	if ( interesting )
	{
		Msg( "GFXMETRICS panel d=%d name=\"%s\" class=%s scheme=%d prop=%d vis=%d %d,%d %dx%d\n",
			depth, ipanel()->GetName( v ), cls ? cls : "", (int)ipanel()->GetScheme( v ),
			ipanel()->IsProportional( v ) ? 1 : 0, ipanel()->IsVisible( v ) ? 1 : 0, x, y, w, h );
	}
	bool descend = depth < 3 || ( cls && ( !Q_stricmp( cls, "PropertySheet" ) || !Q_stricmp( cls, "PropertyPage" ) || !Q_stricmp( cls, "COptionsSubVideo" ) || !Q_stricmp( cls, "CGraphicsOptionsPage" ) ) );
	if ( !descend )
		return;
	int count = ipanel()->GetChildCount( v );
	for ( int i = 0; i < count; ++i )
		DumpVPanelTree( ipanel()->GetChild( v, i ), depth + 1 );
}

void CGraphicsOptionsDialog::DumpMetrics()
{
	Msg( "GFXMETRICS begin\n" );
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	if ( root )
	{
		int w, h;
		ipanel()->GetSize( root, w, h );
		Msg( "GFXMETRICS gameui name=\"%s\" class=%s scheme=%d prop=%d %dx%d\n",
			ipanel()->GetName( root ), ipanel()->GetClassName( root ), (int)ipanel()->GetScheme( root ),
			ipanel()->IsProportional( root ) ? 1 : 0, w, h );
		DumpSchemeFonts( "gameui", ipanel()->GetScheme( root ) );
		VPANEL options = FindVPanel( root, "OptionsDialog", 0 );
		if ( !options )
			Msg( "GFXMETRICS OptionsDialog not found\n" );
		else
		{
			DumpSchemeFonts( "stock", ipanel()->GetScheme( options ) );
			DumpVPanelTree( options, 0 );
		}
	}
	DumpSchemeFonts( "ours", GetScheme() );
	Msg( "GFXMETRICS ours prop=%d scheme=%d\n", IsProportional() ? 1 : 0, (int)GetScheme() );
	DumpVPanelTree( GetVPanel(), 0 );
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &w = m_Widgets[i];
		HFont font = w.label ? w.label->GetFont() : 0;
		int lw = 0, lh = 0, cw = 0, ch = 0;
		if ( w.label )
			w.label->GetSize( lw, lh );
		if ( w.check )
			w.check->GetSize( cw, ch );
		Msg( "GFXMETRICS row %s label=%dx%d font=\"%s\" tall=%d check=%dx%d\n",
			w.row->label, lw, lh, font ? surface()->GetFontName( font ) : "", font ? surface()->GetFontTall( font ) : 0, cw, ch );
	}
	Msg( "GFXMETRICS end\n" );
}


static CGraphicsOptionsDialog *GetDialog()
{
	if ( !s_hDialog.Get() )
	{
		VPANEL parent = enginevgui->GetPanel( PANEL_GAMEUIDLL );
		if ( !parent )
		{
			Warning( "graphics_options: gameui panel is not ready\n" );
			return NULL;
		}
		s_hDialog = new CGraphicsOptionsDialog( parent );
	}
	return s_hDialog;
}

static VPANEL FindVPanelClass( VPANEL v, const char *cls, int depth )
{
	if ( !v || depth > 14 )
		return 0;
	const char *got = ipanel()->GetClassName( v );
	if ( got && !Q_stricmp( got, cls ) )
		return v;
	int count = ipanel()->GetChildCount( v );
	for ( int i = 0; i < count; ++i )
	{
		VPANEL found = FindVPanelClass( ipanel()->GetChild( v, i ), cls, depth + 1 );
		if ( found )
			return found;
	}
	return 0;
}

static VPANEL FindChildName( VPANEL parent, const char *name )
{
	if ( !parent )
		return 0;
	int count = ipanel()->GetChildCount( parent );
	for ( int i = 0; i < count; ++i )
	{
		VPANEL child = ipanel()->GetChild( parent, i );
		const char *got = ipanel()->GetName( child );
		if ( got && !Q_stricmp( got, name ) )
			return child;
	}
	return 0;
}

class CGraphicsOptionsHook : public Panel
{
	DECLARE_CLASS_SIMPLE( CGraphicsOptionsHook, Panel );
public:
	CGraphicsOptionsHook( VPANEL parent ) : Panel( NULL, "GraphicsOptionsHook" )
	{
		SetParent( parent );
		SetVisible( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
		ivgui()->AddTickSignal( GetVPanel(), 500 );
	}
	virtual void OnTick() { AttachAdvancedButton(); }
	virtual void OnCommand( const char *command );
	void AttachAdvancedButton();
};

static DHANDLE<CGraphicsOptionsHook> s_hHook;
static DHANDLE<Button> s_hAdvanced;

void CGraphicsOptionsHook::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "OpenGraphicsOptions" ) )
	{
		CGraphicsOptionsDialog *dlg = GetDialog();
		if ( dlg )
			dlg->Open( 0 );
		return;
	}
	BaseClass::OnCommand( command );
}

void CGraphicsOptionsHook::AttachAdvancedButton()
{
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	if ( !root )
		return;
	VPANEL video = FindVPanelClass( root, "COptionsSubVideo", 0 );
	if ( !video )
		return;

	VPANEL existing = FindChildName( video, "GraphicsAdvancedButton" );
	if ( existing )
	{
		if ( !s_hAdvanced.Get() )
		{
			Panel *panel = ipanel()->GetPanel( existing, GetControlsModuleName() );
			s_hAdvanced = dynamic_cast<Button *>( panel );
		}
		return;
	}
	if ( s_hAdvanced.Get() )
	{
		s_hAdvanced->MarkForDeletion();
		s_hAdvanced = NULL;
	}

	int x = 254, y = 196, w = 182, h = 24;
	VPANEL stock = FindChildName( video, "AdvancedButton" );
	if ( stock )
	{
		int sw, sh;
		ipanel()->GetPos( stock, x, y );
		ipanel()->GetSize( stock, sw, sh );
		if ( sw > 0 && sh > 0 )
		{
			w = sw;
			h = sh;
		}
	}

	Button *button = new Button( NULL, "GraphicsAdvancedButton", "#GameUI_AdvancedEllipsis" );
	button->SetParent( video );
	button->SetScheme( ipanel()->GetScheme( video ) );
	button->SetBounds( x, y, w, h );
	button->SetContentAlignment( Label::a_west );
	button->SetCommand( "OpenGraphicsOptions" );
	button->AddActionSignalTarget( this );
	button->SetVisible( true );
	button->InvalidateLayout( false, true );
	s_hAdvanced = button;
	Msg( "graphics_options: attached advanced button %d,%d %dx%d\n", x, y, w, h );
}

static void ActivateVideoTab()
{
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	VPANEL options = root ? FindVPanel( root, "OptionsDialog", 0 ) : 0;
	if ( !options )
		return;
	VPANEL sheet = FindVPanel( options, "Sheet", 0 );
	VPANEL video = FindVPanelClass( options, "COptionsSubVideo", 0 );
	if ( !sheet || !video )
		return;

	CUtlVector<VPANEL> pages, tabs;
	int count = ipanel()->GetChildCount( sheet );
	for ( int i = 0; i < count; ++i )
	{
		VPANEL child = ipanel()->GetChild( sheet, i );
		const char *cls = ipanel()->GetClassName( child );
		if ( !cls )
			continue;
		if ( !Q_stricmp( cls, "PageTab" ) )
			tabs.AddToTail( child );
		else if ( !Q_strncmp( cls, "COptionsSub", 11 ) )
			pages.AddToTail( child );
	}
	int index = -1;
	for ( int i = 0; i < pages.Count(); ++i )
	{
		if ( pages[i] == video )
			index = i;
	}
	if ( index < 0 || index >= tabs.Count() )
	{
		Warning( "graphics_options: video tab index %d of %d\n", index, tabs.Count() );
		return;
	}

	const char *modules[] = { "GameUI", "BaseUI", "ClientDLL" };
	for ( int i = 0; i < ARRAYSIZE( modules ); ++i )
	{
		Panel *tab = ipanel()->GetPanel( tabs[index], modules[i] );
		if ( !tab )
			continue;
		KeyValues *kv = new KeyValues( "TabPressed" );
		kv->SetPtr( "panel", tab );
		ipanel()->SendMessage( sheet, kv, tabs[index] );
		kv->deleteThis();
		Msg( "graphics_options: video tab via %s\n", modules[i] );
		return;
	}
	Warning( "graphics_options: video tab panel is in another module\n" );
}

class CGraphicsOptionsSystem : public CAutoGameSystem
{
public:
	CGraphicsOptionsSystem() : CAutoGameSystem( "GraphicsOptions" ) {}
	void PostInit()
	{
		VPANEL parent = enginevgui->GetPanel( PANEL_CLIENTDLL );
		if ( parent && !s_hHook.Get() )
			s_hHook = new CGraphicsOptionsHook( parent );
	}
};
static CGraphicsOptionsSystem s_GraphicsOptionsSystem;

static void CC_GraphicsOptions( const CCommand &args )
{
	if ( args.ArgC() >= 2 && !Q_stricmp( args[1], "video" ) )
	{
		ActivateVideoTab();
		return;
	}
	if ( args.ArgC() >= 2 && !Q_stricmp( args[1], "click" ) )
	{
		if ( !s_hAdvanced.Get() )
			Warning( "graphics_options: advanced button is not attached\n" );
		else
			s_hAdvanced->DoClick();
		return;
	}
	if ( args.ArgC() >= 2 && !Q_stricmp( args[1], "metrics" ) )
	{
		CGraphicsOptionsDialog *dlg = GetDialog();
		if ( dlg )
			dlg->DumpMetrics();
		return;
	}
	if ( args.ArgC() >= 2 && !Q_stricmp( args[1], "apply" ) )
	{
		CGraphicsOptionsDialog *dlg = s_hDialog.Get();
		if ( !dlg || !dlg->IsVisible() )
			Warning( "graphics_options: dialog is not open\n" );
		else
			dlg->ApplyChanges();
		return;
	}
	if ( args.ArgC() >= 2 && !Q_stricmp( args[1], "cancel" ) )
	{
		if ( s_hDialog.Get() )
			s_hDialog->Close();
		return;
	}
	if ( args.ArgC() >= 4 && !Q_stricmp( args[1], "set" ) )
	{
		CGraphicsOptionsDialog *dlg = GetDialog();
		if ( !dlg )
			return;
		if ( !dlg->IsVisible() )
			dlg->Open( 0 );
		dlg->SetPending( args[2], (float)atof( args[3] ) );
		return;
	}

	CGraphicsOptionsDialog *dlg = GetDialog();
	if ( !dlg )
		return;
	int tab = args.ArgC() >= 2 ? atoi( args[1] ) : 0;
	dlg->Open( tab );
}

static ConCommand graphics_options( "graphics_options", CC_GraphicsOptions, "Open the advanced graphics dialog.", 0 );
