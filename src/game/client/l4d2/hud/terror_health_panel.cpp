//========= Copyright Valve Corporation, All rights reserved. ============//
// HealthPanel C295EF0..C296CE3: both native styles, temporary/incap health.
#include "cbase.h"
#include "l4d2/hud/terror_health_panel.h"
#include "vgui/ISurface.h"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static ConVar cl_temp_health_pulse_rate( "cl_temp_health_pulse_rate", "2.5", 0x0000000A, "temp health pulses this many times per second" );
static ConVar cl_temp_health_pulse_incap( "cl_temp_health_pulse_incap", "0", 0x0000000A, "incap health pulses in the same fashion as temp health" );
static ConVar cl_temp_health_fade( "cl_temp_health_fade", "0", 0x0000000A, "temp health fades top to bottom" );
DECLARE_BUILD_FACTORY( HealthPanel );

Color TerrorHealthPalette( int level )
{
	static const Color normal[4] = { Color(161,25,25,255), Color(161,25,25,255), Color(216,146,12,255), Color(10,177,50,255) };
	static const Color colorblind[4] = { Color(12,206,206,255), Color(12,206,206,255), Color(177,146,25,255), Color(12,177,100,255) };
	static ConVarRef cl_colorblind( "cl_colorblind" );
	return cl_colorblind.GetInt() == 2 ? colorblind[level] : normal[level];
}
static int TerrorHealthTexture( const char *name )
{
	if ( !name ) return -1;
	int texture = vgui::surface()->DrawGetTextureId( name );
	if ( texture == -1 )
	{
		texture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( texture, name, true, false );
	}
	return texture;
}
HealthPanel::HealthPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_flLastChange = 0.0f; m_bLocal = false;
	// Ordinary native factory296D90 does not zero-fill. Skip undefined pre-data
	// work only; all actual SetHealth and ApplySettings writes below are native.
	m_iHealth = -1; m_iTemporary = m_iMaximum = 0;
	m_bMaterialStyle = m_bDying = m_bIncapacitated = false;
	for ( int i = 0; i < 5; ++i ) m_iHealthTexture[i] = m_iTicksTexture[i] = -1;
	m_iWhite = m_iGrey = m_iOutline = -1;
}
void HealthPanel::SetHealth( int health, int temporary, int maximum, bool dying, bool incapacitated, bool local )
{
	if ( incapacitated ? m_iHealth != health : m_iTemporary != temporary ) m_flLastChange = gpGlobals->realtime;
	m_iHealth = health; m_iTemporary = temporary; m_iMaximum = maximum;
	m_bDying = dying; m_bIncapacitated = incapacitated; m_bLocal = local;
}
int HealthPanel::GetHealthLevel( int health ) const
{
	float fraction = (float)health / (float)m_iMaximum;
	if ( fraction > 1.0f ) fraction = 1.0f;
	else if ( fraction < 0.0f ) fraction = 0.0f;
	if ( m_bIncapacitated || ( m_bDying && health <= 1 ) ) return 0;
	if ( fraction < 0.25f ) return 1;
	static ConVarRef limp( "survivor_limp_health" );
	return !( limp.GetFloat() * 0.0099999998f > fraction ) ? 3 : 2;
}
Color HealthPanel::GetHealthColor() const
{
	return m_monochromeColor.a() ? m_monochromeColor : TerrorHealthPalette( GetHealthLevel( m_iHealth ) );
}
void HealthPanel::ApplySettings( KeyValues *settings )
{
	m_bMaterialStyle = settings->GetInt( "new_material_style", 0 ) != 0;
	static const char *const health[] = { "healthbar_image_low", "healthbar_image_low", "healthbar_image_medium", "healthbar_image_high", "healthbar_image_grey" };
	static const char *const ticks[] = { "healthbar_image_low_ticks", "healthbar_image_low_ticks", "healthbar_image_medium_ticks", "healthbar_image_high_ticks", "healthbar_image_grey_ticks" };
	for ( int i = 0; i < 5; ++i )
	{
		m_iHealthTexture[i] = TerrorHealthTexture( settings->GetString( health[i], NULL ) );
		m_iTicksTexture[i] = TerrorHealthTexture( settings->GetString( ticks[i], NULL ) );
	}
	m_iWhite = TerrorHealthTexture( "vgui/healthbar_white" );
	m_iGrey = TerrorHealthTexture( "vgui/healthbar_grey" );
	m_iOutline = TerrorHealthTexture( "vgui/hud/s_healthbar_outline" );
	BaseClass::ApplySettings( settings );
}
void HealthPanel::DrawTexture( int texture, int x, int y, int wide, int tall, Color color, float u )
{
	if ( texture == -1 ) return;
	vgui::surface()->DrawSetTexture( texture ); vgui::surface()->DrawSetColor( color );
	vgui::surface()->DrawTexturedSubRect( x, y, x + wide, y + tall, 0, 0, u, 1 );
}
void HealthPanel::DrawRange( int texture, float start, float end )
{
	// C296290 calculates both destination endpoints BEFORE clipping UVs.
	const int left = (int)( (float)GetWide() * start ), right = (int)( (float)GetWide() * end );
	if ( texture == -1 )
	{
		vgui::surface()->DrawSetColor( GetHealthColor() );
		vgui::surface()->DrawFilledRect( left, 0, right, GetTall() );
		return;
	}
	if ( start < 0.0f ) start = 0.0f;
	if ( end > 1.0f ) end = 1.0f;
	vgui::surface()->DrawSetTexture( texture );
	vgui::surface()->DrawSetColor( 255, 255, 255, GetAlpha() );
	vgui::surface()->DrawTexturedSubRect( left, 0, right, GetTall(), start, 0, end, 1 );
}
void HealthPanel::DrawTicks( int x, int y, int wide, int tall, int health, int temporary )
{
	Color color = TerrorHealthPalette( m_bIncapacitated ? 1 : GetHealthLevel( health ) );
	if ( m_bIncapacitated )
	{
		const float pulse = (float)sin( (double)gpGlobals->realtime + (double)gpGlobals->realtime );
		for ( int channel = 0; channel < 3; ++channel )
		{
			const float amount = ( (float)color[channel] * 64.0f ) * 0.0039215689f;
			color[channel] = (unsigned char)(int)( pulse * amount + ( (float)color[channel] - amount ) );
		}
	}
	if ( m_monochromeColor.a() ) color = m_monochromeColor;
	if ( m_bLocal && ( !m_bIncapacitated || cl_temp_health_pulse_incap.GetInt() != 0 ) )
	{
		const float weight = (float)( ( sin( (double)gpGlobals->realtime * 3.141592653589793 * (double)cl_temp_health_pulse_rate.GetFloat() ) + 1.0 ) * 0.5 );
		for ( int channel = 0; channel < 3; ++channel )
			color[channel] = (unsigned char)(int)( (float)( 255 - color[channel] ) * weight + (float)color[channel] );
	}
	vgui::surface()->DrawSetColor( color );
	const int step = (int)ceil( (double)wide * (double)0.0099999998f * 3.0 );
	// Native divides by step then by wide/step. Zero geometry is undefined;
	// minimal skip of only these tick rectangles, not the panel's other draws.
	if ( step == 0 || wide / step == 0 ) return;
	const float healthPerStep = 100.0f / (float)( wide / step );
	const int tickWide = (int)( (float)step * 0.5f );
	static ConVarRef decay( "pain_pills_decay_rate" );
	float decayFraction = ( m_bIncapacitated ? 1.0f : decay.GetFloat() ) * ( gpGlobals->realtime - m_flLastChange );
	if ( decayFraction > 1.0f ) decayFraction = 1.0f;
	else if ( decayFraction < 0.0f ) decayFraction = 0.0f;
	const float remaining = (float)temporary - decayFraction;
	float consumed = 0.0f;
	int left = x;
	if ( left >= wide ) return;
	while ( consumed + healthPerStep <= (float)( temporary + health ) )
	{
		if ( m_bIncapacitated || cl_temp_health_fade.GetInt() == 0 )
			vgui::surface()->DrawFilledRect( left, y, left + tickWide, y + tall );
		else vgui::surface()->DrawFilledRectFade( left, y, left + tickWide, y + tall, 0, 255, false );
		left += step; consumed += healthPerStep;
		if ( left >= wide ) return;
	}
	const float top = ( 1.0f - ( ( (float)health + remaining - consumed ) / healthPerStep ) ) * (float)tall + (float)y;
	if ( m_bIncapacitated || cl_temp_health_fade.GetInt() == 0 )
		vgui::surface()->DrawFilledRect( left, (int)top, left + tickWide, y + tall );
	else vgui::surface()->DrawFilledRectFade( left, (int)top, left + tickWide, y + tall, 0, 255, false );
}
void HealthPanel::Paint()
{
	if ( m_iHealth < 0 ) return;
	if ( m_bMaterialStyle )
	{
		if ( m_bIncapacitated )
		{
			DrawRange( m_iTicksTexture[m_monochromeColor.a() ? 4 : GetHealthLevel( m_iHealth )], 0, (float)m_iHealth * 0.0033333334f );
			return;
		}
		float fraction = (float)m_iHealth / (float)m_iMaximum;
		if ( fraction > 1.0f ) fraction = 1.0f;
		else if ( fraction < 0.0f ) fraction = 0.0f;
		if ( m_iHealth == 0 ) fraction = 0.0f;
		if ( m_iTemporary > 0 )
		{
			float temporary = (float)m_iTemporary / (float)m_iMaximum;
			if ( temporary > 1.0f ) temporary = 1.0f;
			else if ( temporary < 0.0f ) temporary = 0.0f;
			DrawRange( m_iTicksTexture[m_monochromeColor.a() ? 4 : GetHealthLevel( m_iHealth )], fraction, temporary + fraction );
		}
		if ( m_iHealth > 0 ) DrawRange( m_iHealthTexture[m_monochromeColor.a() ? 4 : GetHealthLevel( m_iHealth )], 0, fraction );
		return;
	}
	Color color = GetHealthColor(); color[3] = GetAlpha();
	if ( !m_monochromeColor.a() ) DrawTexture( m_iGrey, 0, 0, GetWide(), GetTall(), Color(255,255,255,GetAlpha()), 1 );
	if ( m_iHealth > 0 ) DrawTexture( m_iOutline, 0, 0, GetWide(), GetTall(), color, 1 );
	const int wide = GetWide() - 2 * m_inset, tall = GetTall() - 2 * m_inset;
	if ( m_bIncapacitated )
	{
		DrawTicks( m_inset, m_inset, wide, tall, 0, m_iHealth / 3 );
		return;
	}
	if ( wide == 0 ) return;
	const float inverse = 1.0f / (float)m_iMaximum;
	float fraction = (float)m_iHealth * inverse;
	if ( fraction > 1.0f ) fraction = 1.0f;
	else if ( fraction < 0.029999999f ) fraction = 0.029999999f;
	if ( m_iHealth == 0 ) fraction = 0.0f;
	if ( m_iTemporary > 0 ) DrawTicks( m_inset, m_inset, wide, tall, m_bDying ? 0 : m_iHealth, m_iTemporary + ( m_bDying ? 1 : 0 ) );
	if ( !m_bDying )
	{
		const int filled = (int)( (float)wide * fraction );
		DrawTexture( m_iWhite, m_inset, m_inset, filled, tall, color, (float)filled / (float)wide );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
