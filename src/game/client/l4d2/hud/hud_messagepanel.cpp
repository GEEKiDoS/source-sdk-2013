//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/hud_messagepanel.h"
#include "l4d2/terror_message_text.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/clientmode_terror.h"
#include "hud.h"
#include "hud_macros.h"
#include "l4d2/hud/hud_chat.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui_controls/RichText.h"
#include <ctype.h>
#include <stdio.h>
#include "tier0/memdbgon.h"

static bool s_bMessagePanelVisible; // Native107C9540, shared by the HUD owner.

class HudMessageLabel : public vgui::Label
{
	DECLARE_CLASS_SIMPLE( HudMessageLabel, vgui::Label );
public:
	HudMessageLabel( vgui::Panel *parent, Color color, const char *text ) :
		BaseClass( parent, "line", text ), m_bWhitespace( true )
	{
		SetFgColor( color );
		for ( const char *cursor = text; *cursor; ++cursor )
		{
			const int value = (signed char)*cursor;
			// Main-approved minimal skip at native2B3730/2B3801: 80..FE
			// become invalid negative ctype inputs. Skip ONLY that step.
			// ASCII and FF/EOF retain the native CRT classification.
			if ( value < 0 && value != EOF ) continue;
			if ( isgraph( value ) ) m_bWhitespace = false;
		}
	}
	bool IsWhitespace() const { return m_bWhitespace; }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		Color color = GetFgColor();
		BaseClass::ApplySchemeSettings( scheme );
		SetFgColor( color ); // Native preserves each message's supplied color.
		const char *setting = scheme->GetResourceString( "MessageTextProportional" );
		const bool proportional = setting && atoi( setting ) != 0;
		SetBgColor( Color( 0, 0, 0, 0 ) );
		SetFont( scheme->GetFont( "MessageText", proportional ) );
		SetContentAlignment( vgui::Label::a_northwest );
		SizeToContents();
		InvalidateLayout( false, false );
	}
private:
	bool m_bWhitespace;
};

class HudMessageCloseLabel : public vgui::RichText
{
	DECLARE_CLASS_SIMPLE( HudMessageCloseLabel, vgui::RichText );
public:
	explicit HudMessageCloseLabel( vgui::Panel *parent ) :
		BaseClass( parent, "MessageCloseLabel" ), m_Normal( 0, 0, 0, 0 ),
		m_Bound( 0, 0, 0, 0 ), m_Unbound( 0, 0, 0, 0 ), m_bChatActive( false ) {}
	void RefreshCloseHint() // 2B3250: rebuilding the hint, not SetVisible.
	{
		m_Text[0] = 0;
		wchar_t *output = m_Text;
		SetText( "" );
		if ( !m_bChatActive )
		{
			InsertColorChange( m_Normal );
			InsertString( L"Press " );
			output += V_snwprintf( output, 256, L"%s", L"Press " );
			const char *binding = engine->Key_LookupBinding( "hide_message_panel" );
			if ( binding )
			{
				InsertColorChange( m_Bound );
				InsertString( binding );
				output += V_snwprintf( output, 256, L"%hs", binding );
			}
			else
			{
				InsertColorChange( m_Unbound );
				InsertString( "< not bound >" );
				output += V_snwprintf( output, 256, L"%hs", "< not bound >" );
			}
			InsertColorChange( m_Normal );
			InsertString( L" to close" );
			output += V_snwprintf( output, 256, L"%s", L" to close" );
		}
		// Retail keeps the capacity256 for each append rather than reducing
		// it by output-m_Text. Engine key names are the real bounded names.
		InvalidateLayout( false, false );
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		// Native101137F0 is the genuine CBaseHudChat global; +504 is its
		// message mode, not an invented HUD/global hide flag.
		m_pChat = GET_HUDELEMENT( CHudChat );
		BaseClass::ApplySchemeSettings( scheme );
		m_Normal = Color( 255, 255, 255, 255 );
		m_Bound = Color( 255, 255, 0, 255 );
		m_Unbound = Color( 255, 0, 0, 255 );
		m_Font = scheme->GetFont( "HudHintText", true );
		SetFont( m_Font );
		SetPaintBorderEnabled( false );
		SetBgColor( Color( 0, 0, 0, 0 ) );
		SetVerticalScrollbar( false );
		SetDrawOffsets( 0, 0 );
		RefreshCloseHint();
	}
	virtual void OnThink() OVERRIDE
	{
		if ( !m_pChat ) return;
		bool active = m_pChat->GetMessageMode() != 0;
		if ( active == m_bChatActive ) return;
		m_bChatActive = active;
		RefreshCloseHint();
		InvalidateLayout( true, false );
	}
	virtual void PerformLayout() OVERRIDE
	{
		BaseClass::PerformLayout();
		vgui::Panel *parent = GetParent();
		if ( !parent ) return;
		int parentWide, parentTall, textWide, textTall;
		parent->GetSize( parentWide, parentTall );
		vgui::surface()->GetTextSize( m_Font, m_Text, textWide, textTall );
		int margin = vgui::scheme()->GetProportionalScaledValue( 5 );
		SetBounds( parentWide - margin - textWide, 2, margin + textWide, textTall + 2 );
	}
private:
	wchar_t m_Text[256];
	Color m_Normal, m_Bound, m_Unbound;
	CBaseHudChat *m_pChat; // Native caches this during ApplySchemeSettings.
	bool m_bChatActive;
	vgui::HFont m_Font;
};

DECLARE_HUDELEMENT( CHudMessagePanel );
DECLARE_HUD_MESSAGE( CHudMessagePanel, MessageText );

CON_COMMAND_F( hide_message_panel, "Hides the message panel" , 0x00000008)
{
	s_bMessagePanelVisible = false; // Native2B34E0: clear on next ShouldDraw.
}

CHudMessagePanel::CHudMessagePanel( const char *hudName ) :
	BaseClass( NULL, "HudMessagePanel", "" ), CHudElement( hudName )
{
	SetParent( GetClientModeNormal()->GetViewport() );
	s_bMessagePanelVisible = false;
	m_pCloseLabel = new HudMessageCloseLabel( this );
	SetHiddenBits( 0 );
}
void CHudMessagePanel::Init()
{
	HOOK_HUD_MESSAGE( CHudMessagePanel, MessageText );
	vgui::ivgui()->AddTickSignal( GetVPanel() ); // Native Init2B4660.
}
bool CHudMessagePanel::ShouldDraw()
{
	if ( !s_bMessagePanelVisible ) ClearMessages();
	return s_bMessagePanelVisible; // No added base HUD/alive/hidden-bit gate.
}
void CHudMessagePanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetFgColor( Color( 255, 255, 255, 255 ) );
	SetBgColor( scheme->GetColor( "TransparentLightBlack", Color( 0, 0, 0, 0 ) ) );
}
void CHudMessagePanel::AppendMessage( Color color, const char *text )
{
	m_Lines.AddToTail( new HudMessageLabel( this, color, text ) );
	s_bMessagePanelVisible = true;
	InvalidateLayout( false, false );
	m_pCloseLabel->InvalidateLayout( false, false );
}
void CHudMessagePanel::RefreshCloseHint() { m_pCloseLabel->RefreshCloseHint(); }
void CHudMessagePanel::ClearMessages()
{
	if ( !m_Lines.Count() ) return;
	FOR_EACH_VEC( m_Lines, i ) m_Lines[i]->MarkForDeletion();
	m_Lines.RemoveAll(); // Keep vector capacity, as native2B3BF0 does.
	InvalidateLayout( false, false );
}
void CHudMessagePanel::PerformLayout()
{
	BaseClass::PerformLayout();
	int wide, tall;
	GetSize( wide, tall );
	int y = m_offsetY;
	bool overflow = false;
	FOR_EACH_VEC( m_Lines, i )
	{
		HudMessageLabel *line = m_Lines[i];
		const int height = vgui::surface()->GetFontTall( line->GetFont() );
		line->SetBounds( m_offsetX, y, wide - 2 * m_offsetX, height );
		if ( height + y >= tall - m_offsetY ) overflow = true;
		y += height + m_spacing;
	}
	if ( !m_Lines.Count() ) return;
	if ( !overflow && !m_Lines[0]->IsWhitespace() ) return;
	m_Lines[0]->MarkForDeletion();
	m_Lines.Remove( 0 );
	InvalidateLayout( false, false ); // Exactly one oldest line per layout.
}
void CHudMessagePanel::MsgFunc_MessageText( bf_read &message )
{
	if ( message.GetNumBitsLeft() == 0 )
	{
		ClearMessages();
		s_bMessagePanelVisible = false;
		return;
	}
	// Read sequentially; argument evaluation order must not reorder RGB reads.
	const int red = message.ReadByte();
	const int green = message.ReadByte();
	const int blue = message.ReadByte();
	Color color( red, green, blue, 255 );
	char text[256];
	message.ReadString( text, sizeof( text ), false ); // Native ignores result.
	if ( red == 0 && green == 0 && blue == 0 ) color = GetFgColor();
	char *cursor = text;
	while ( *cursor )
	{
		char *end = cursor;
		while ( *end && *end != '\n' ) ++end;
		const char saved = *end;
		*end = 0;
		AppendMessage( color, cursor );
		Msg( "%s\n", cursor );
		*end = saved;
		cursor = saved ? end + 1 : end;
	}
	RefreshCloseHint();
}

void TerrorMessageText( CTerrorPlayer *player, const char *text, unsigned int rgb )
{
	if ( !player || !player->IsPlayer() || !player->IsLocalPlayer() ) return;
	CHudMessagePanel *panel = GET_HUDELEMENT( CHudMessagePanel );
	if ( !panel ) return;
	Color color( rgb & 0xff, ( rgb >> 8 ) & 0xff, ( rgb >> 16 ) & 0xff, 255 );
	if ( ( rgb & 0x00ffffff ) == 0 ) color = panel->GetFgColor();
	panel->AppendMessage( color, text ); // Native local entry does NOT split.
	Msg( "%s", text ); // Nor does it add a newline.
	panel->RefreshCloseHint();
}
