//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "vgui_controls/ScrollBar.h"
#include "vgui_controls/Label.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui/IPanel.h"
#include "vgui/ILocalize.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
class Panel_PassNavigationToParent : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( Panel_PassNavigationToParent, vgui::Panel );
public:
	Panel_PassNavigationToParent( GenericPanelList *parent ) : BaseClass( parent, "" ) { SetPaintBackgroundEnabled( false ); }
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE { GetParent()->OnKeyCodePressed( code ); }
	virtual void OnMouseWheeled( int delta ) OVERRIDE { GetParent()->OnMouseWheeled( delta ); }
	virtual void NavigateToChild( vgui::Panel *child ) OVERRIDE { GetParent()->NavigateToChild( child ); }
};
GenericPanelList::GenericPanelList( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ),
	m_Selected( -1 ), m_Spacing( 4 ), m_Offset( 0 ), m_bShowArrows( true ), m_bShowProgress( false ), m_bWrap( true )
{
	SetProportional( true ); SetPaintBackgroundEnabled( false );
	m_pClip = new Panel_PassNavigationToParent( this );
	m_pScroll = new vgui::ScrollBar( this, "ScrVerticalScroll", true );
	m_pScroll->AddActionSignalTarget( this ); m_pScroll->SetVisible( false );
	m_pUp = new vgui::Label( this, "LblUpArrow", "#GameUI_Icons_UP_ARROW" );
	m_pDown = new vgui::Label( this, "LblDownArrow", "#GameUI_Icons_DOWN_ARROW" );
	m_pProgress = new vgui::Label( this, "LblScrollProgress", "" );
	m_pProgress->SetVisible( false );
}
void GenericPanelList::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings( data );
	m_Spacing = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), data->GetInt( "panelBorder", 4 ) );
	SetScrollArrowsVisible( false ); // G57820 virtual+892(0), independent of the scrollbar.
	m_bWrap = !data->GetInt( "NoWrap", 0 );
	if ( data->GetInt( "NoDrawPanel", 0 ) ) SetPaintEnabled( false );
}
void GenericPanelList::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetBorder( scheme->GetBorder( "GenericPanelListBorder" ) );
	SetBgColor( scheme->GetColor( "GenericPanelList.BgColor", Color( 0, 0, 0, 0 ) ) );
	m_pUp->SetFont( scheme->GetFont( "GameUIButtons", false ) );
	m_pDown->SetFont( scheme->GetFont( "GameUIButtons", false ) );
	m_pUp->SetPaintBackgroundEnabled( false ); m_pDown->SetPaintBackgroundEnabled( false );
}
vgui::Panel *GenericPanelList::GetPanelItem( int index ) const
{
	return m_Items.IsValidIndex( index ) ? m_Items[index].Get() : NULL;
}
vgui::Panel *GenericPanelList::GetSelectedPanelItem() const { return GetPanelItem( m_Selected ); }
void GenericPanelList::AddPanelItem( vgui::Panel *item )
{
	item->SetParent( m_pClip );
	const int index = m_Items.AddToTail();
	m_Items[index] = item;
	item->InvalidateLayout( false, true ); InvalidateLayout();
	if ( GetParent() ) PostMessage( GetParent(), new KeyValues( "OnItemAdded", "panelName", GetName() ) );
}
void GenericPanelList::RemoveAllPanelItems()
{
	FOR_EACH_VEC( m_Items, i ) if ( m_Items[i].Get() ) delete m_Items[i].Get();
	m_Items.RemoveAll(); m_Selected = -1; m_Offset = 0; m_pScroll->SetValue( 0 ); InvalidateLayout();
}
void GenericPanelList::SetScrollBarVisible( bool visible ) { m_pScroll->SetVisible( visible ); InvalidateLayout(); }
void GenericPanelList::SetScrollArrowsVisible( bool visible ) { m_bShowArrows = visible; m_pUp->SetVisible( visible ); m_pDown->SetVisible( visible ); InvalidateLayout(); }
void GenericPanelList::SetScrollProgressVisible( bool visible ) { m_bShowProgress = visible; InvalidateLayout(); }
void GenericPanelList::SelectPanelItem( int index, bool focus )
{
	vgui::Panel *item = GetPanelItem( index );
	vgui::Panel *old = GetSelectedPanelItem();
	if ( !item || item == old || !item->IsEnabled() ) return;
	if ( old ) PostMessage( old, new KeyValues( "PanelUnSelected" ) );
	m_Selected = index;
	item->SetVisible( true );
	FOR_EACH_VEC( m_Items, i ) if ( i != index && m_Items[i].Get() ) m_Items[i]->NavigateFrom();

	// G0A280 preserves the root's existing navigation focus when requested.
	vgui::Panel *root = this;
	while ( vgui::Panel *parent = root->GetParent() ) root = parent;
	const vgui::VPANEL keyFocus = root->GetCurrentKeyFocus();
	vgui::Panel *previousFocus = keyFocus ? vgui::ipanel()->GetPanel( keyFocus, root->GetModuleName() ) : NULL;
	const bool restoreFocus = !focus && previousFocus && previousFocus != old;
	item->NavigateTo();
	if ( restoreFocus ) { item->NavigateFrom(); previousFocus->NavigateTo(); }

	ScrollToPanelItem( index );
	int x, clipY, itemY;
	m_pClip->GetPos( x, clipY ); item->GetPos( x, itemY );
	const int clipTall = m_pClip->GetTall(), itemTall = item->GetTall();
	if ( clipTall > 0 )
	{
		if ( itemY < clipY ) m_pScroll->SetValue( index * ( itemTall + m_Spacing ) );
		else if ( itemY + itemTall > clipY + clipTall )
			m_pScroll->SetValue( MAX( ( index + 1 ) * ( itemTall + m_Spacing ) - clipTall, 0 ) );
	}
	vgui::surface()->PlaySound( "UI/menu_focus.wav" );
	PostMessage( item, new KeyValues( "PanelSelected" ) );
	if ( GetParent() )
	{
		KeyValues *message = new KeyValues( "OnItemSelected", "panelName", GetName() );
		message->SetInt( "index", index ); PostMessage( GetParent(), message );
	}
	InvalidateLayout();
}
void GenericPanelList::NavigateTo()
{
	FOR_EACH_VEC( m_Items, i ) if ( m_Items[i].Get() )
	{
		if ( i == m_Selected ) m_Items[i]->NavigateTo();
		else m_Items[i]->NavigateFrom();
	}
}
void GenericPanelList::NavigateFrom()
{
	if ( GetSelectedPanelItem() ) GetSelectedPanelItem()->NavigateFrom();
}
void GenericPanelList::NavigateToChild( vgui::Panel *child )
{
	if ( GetParent() ) GetParent()->NavigateToChild( this );
	BaseClass::NavigateToChild( child );
}
bool GenericPanelList::IsPanelItemVisible( vgui::Panel *item, bool fully ) const
{
	if ( !item ) return false;
	int x, clipY, itemY;
	m_pClip->GetPos( x, clipY ); item->GetPos( x, itemY );
	const int clipTall = m_pClip->GetTall(), itemTall = item->GetTall();
	if ( fully ) return itemY >= clipY && itemY + itemTall <= clipY + clipTall;
	return itemY < clipY ? itemY + itemTall > clipY : itemY < clipY + clipTall;
}
int GenericPanelList::GetFirstVisiblePanelItem() const
{
	FOR_EACH_VEC( m_Items, i ) if ( IsPanelItemVisible( m_Items[i].Get(), false ) ) return i;
	return 0;
}
int GenericPanelList::GetLastVisiblePanelItem() const
{
	for ( int i = m_Items.Count() - 1; i >= 0; --i )
		if ( IsPanelItemVisible( m_Items[i].Get(), false ) ) return i;
	return 0;
}
void GenericPanelList::ScrollToPanelItem( int index )
{
	vgui::Panel *item = GetPanelItem( index );
	if ( !item ) return;
	int x, itemY; item->GetPos( x, itemY );
	const int itemTall = item->GetTall(), clipTall = m_pClip->GetTall();
	const int first = GetFirstVisiblePanelItem(), last = GetLastVisiblePanelItem();
	int shift;
	if ( itemY + itemTall > clipTall ) shift = itemY + itemTall - clipTall;
	else
	{
		if ( index > first && index < last ) return;
		if ( index == last && !IsPanelItemVisible( GetPanelItem( last ), true ) )
			shift = m_Spacing + GetPanelItem( first )->GetTall();
		else if ( index <= first ) shift = itemY - m_Spacing;
		else
		{
			shift = itemY - ( last - first ) * ( m_Spacing + itemTall ) - m_Spacing;
			int firstY; GetPanelItem( 0 )->GetPos( x, firstY );
			if ( firstY - shift > 0 ) shift = firstY - m_Spacing;
		}
	}
	m_pScroll->SetValue( m_pScroll->GetValue() + shift );
	FOR_EACH_VEC( m_Items, i ) if ( m_Items[i].Get() )
	{
		int y; m_Items[i]->GetPos( x, y ); m_Items[i]->SetPos( x, y - shift );
	}
	UpdateScrollIndicators();
}
void GenericPanelList::OnKeyCodePressed( vgui::KeyCode code )
{
	const vgui::KeyCode base = GetBaseButtonCode( code );
	if ( base == KEY_UP || base == KEY_DOWN || base == KEY_XBUTTON_UP || base == KEY_XBUTTON_DOWN || base == KEY_XSTICK1_UP || base == KEY_XSTICK1_DOWN || base == KEY_XSTICK2_UP || base == KEY_XSTICK2_DOWN || base == STEAMCONTROLLER_DPAD_UP || base == STEAMCONTROLLER_DPAD_DOWN )
	{
		static ConVarRef navLock( "vgui_nav_lock", true );
		if ( navLock.IsValid() && navLock.GetInt() ) return;
		const int step = base == KEY_UP || base == KEY_XBUTTON_UP || base == KEY_XSTICK1_UP || base == KEY_XSTICK2_UP || base == STEAMCONTROLLER_DPAD_UP ? -1 : 1;
		int i = m_Selected < 0 ? ( step < 0 ? m_Items.Count() : -1 ) : m_Selected;
		for ( int n = 0; n < m_Items.Count(); ++n )
		{
			i += step;
			// G589F0 wraps directly even though ApplySettings retains NoWrap.
			i = ( i + m_Items.Count() ) % m_Items.Count();
			if ( GetPanelItem( i )->IsEnabled() ) { if ( navLock.IsValid() ) navLock.SetValue( 1 ); SelectPanelItem( i ); return; }
		}
		return;
	}
	BaseClass::OnKeyCodePressed( code ); // Native GCECF0; no list-owned Select alias.
}
void GenericPanelList::OnMouseWheeled( int delta ) { m_pScroll->SetValue( m_pScroll->GetValue() - delta * 24 ); }
void GenericPanelList::OnScrollBarSliderMoved() { m_Offset = m_pScroll->GetValue(); InvalidateLayout(); }
void GenericPanelList::PerformLayout()
{
	BaseClass::PerformLayout();
	const int viewTall = MAX( 0, GetTall() - 2 * m_Spacing - ( m_bShowArrows ? m_pDown->GetTall() : 0 ) );
	m_pClip->SetBounds( 0, m_Spacing, GetWide(), viewTall );
	const bool scrollVisible = m_pScroll->IsVisible();
	const int scrollWide = scrollVisible ? m_pScroll->GetWide() : 0;
	const int wide = GetWide() - 2 * m_Spacing - scrollWide;
	m_Offset = scrollVisible ? m_pScroll->GetValue() : 0;
	if ( scrollVisible ) m_pClip->SetWide( MAX( 0, m_pClip->GetWide() - scrollWide ) );
	int y = m_Spacing - m_Offset, total = m_Spacing;
	FOR_EACH_VEC( m_Items, i ) if ( m_Items[i].Get() )
	{
		m_Items[i]->SetPos( m_Spacing, y ); m_Items[i]->SetWide( MAX( 0, wide ) );
		const int tall = m_Items[i]->GetTall();
		total += tall + m_Spacing; y += tall + m_Spacing;
	}
	m_pScroll->SetBounds( GetWide() - m_pScroll->GetWide(), 0, m_pScroll->GetWide(), GetTall() );
	m_pScroll->SetRange( 0, total ); m_pScroll->SetRangeWindow( MIN( total, GetTall() ) ); m_pScroll->SetButtonPressedScrollValue( 24 );
	m_pScroll->InvalidateLayout( false, false );
	m_pUp->SizeToContents(); m_pDown->SizeToContents();
	m_pUp->SetPos( GetWide() - m_Spacing - m_pDown->GetWide() - m_pUp->GetWide(), GetTall() - m_Spacing - m_pUp->GetTall() );
	m_pDown->SetPos( GetWide() - m_Spacing - m_pDown->GetWide(), GetTall() - m_Spacing - m_pUp->GetTall() );
	m_pUp->SetVisible( m_bShowArrows ); m_pDown->SetVisible( m_bShowArrows );
	m_pProgress->SetPos( 2 * m_Spacing, GetTall() - m_pUp->GetTall() ); m_pProgress->SetWide( 200 );
	m_pProgress->SetVisible( m_bShowProgress );
	UpdateScrollIndicators();
}
void GenericPanelList::UpdateScrollIndicators()
{
	const int first = GetFirstVisiblePanelItem(), last = GetLastVisiblePanelItem();
	m_pUp->SetText( first > 0 ? "#GameUI_Icons_UP_ARROW_HIGHLIGHT" : "#GameUI_Icons_UP_ARROW" );
	m_pDown->SetText( last < m_Items.Count() - 1 ? "#GameUI_Icons_DOWN_ARROW_HIGHLIGHT" : "#GameUI_Icons_DOWN_ARROW" );
	if ( m_bShowProgress && m_bShowArrows )
	{
		wchar_t firstText[64], lastText[64], countText[64], progress[128]; char number[64];
		Q_snprintf( number, sizeof( number ), "%d", first + 1 ); g_pVGuiLocalize->ConvertANSIToUnicode( number, firstText, sizeof( firstText ) );
		Q_snprintf( number, sizeof( number ), "%d", last + 1 ); g_pVGuiLocalize->ConvertANSIToUnicode( number, lastText, sizeof( lastText ) );
		Q_snprintf( number, sizeof( number ), "%d", m_Items.Count() ); g_pVGuiLocalize->ConvertANSIToUnicode( number, countText, sizeof( countText ) );
		g_pVGuiLocalize->ConstructString( progress, sizeof( progress ), g_pVGuiLocalize->Find( "#L4D360UI_Scroll_Progress" ), 3, firstText, lastText, countText );
		m_pProgress->SetText( progress );
	}
}
} // namespace L4D360UI
