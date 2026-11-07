//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/basemodui/terror_basemodui_graphics_dx12.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "GameUI/IGameUI.h"
#include "inputsystem/ButtonCode.h"
#include "tier1/fmtstr.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui/IInput.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ProgressBar.h"
#include "tier0/memdbgon.h"
namespace L4D360UI
{
static IGameUI *s_pOptionsGameUI;
void SetNativeOptionsGameUI( IGameUI *gameUI ) { s_pOptionsGameUI = gameUI; }
void OpenNativeSprayImport()
{
	// Approved adaptation: target GameUI owns image conversion. No import result
	// is reported by this screen and its actual cl_logofile remains authoritative.
	if ( s_pOptionsGameUI ) s_pOptionsGameUI->SendMainMenuCommand( "OpenOptionsDialog" );
}
CUIGameData::CUIGameData() : m_flLookSensitivity( 1.0f ) {} // G15DB0 +48
CUIGameData *CUIGameData::Get() { static CUIGameData data; return &data; }
void CUIGameData::SetLookSensitivity( float scale )
{
	// G11130: use each registered ConVar's default, not current values or a
	// synthetic profile. G11210 returns this UI-owned field even without a session.
	m_flLookSensitivity = scale;
	ConVar *yaw = cvar->FindVar( "joy_yawsensitivity" );
	ConVar *pitch = cvar->FindVar( "joy_pitchsensitivity" );
	if ( yaw ) yaw->SetValue( (float)atof( yaw->GetDefault() ) * scale );
	if ( pitch ) pitch->SetValue( (float)atof( pitch->GetDefault() ) * scale );
}
class CSliderButton : public L4D360HybridButton
{
	DECLARE_CLASS_SIMPLE( CSliderButton, L4D360HybridButton );
public:
	CSliderButton( vgui::Panel *parent ) : BaseClass( parent, "BtnDropButton" ) {}
	void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE
	{
		const vgui::KeyCode base=GetBaseButtonCode(code);
		if ( base==KEY_LEFT || base==KEY_XBUTTON_LEFT_SHOULDER || base==KEY_XBUTTON_LEFT || base==KEY_XSTICK1_LEFT || base==KEY_XSTICK2_LEFT || base==KEY_RIGHT || base==KEY_XBUTTON_RIGHT_SHOULDER || base==KEY_XBUTTON_RIGHT || base==KEY_XSTICK1_RIGHT || base==KEY_XSTICK2_RIGHT )
			GetParent()->OnKeyCodePressed( code );
		else BaseClass::OnKeyCodePressed( code );
	}
};
SliderControl::SliderControl( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ),
	m_flMin( 0 ), m_flMax( 100 ), m_flStep( 1 ), m_flValue( 0 ), m_BorderColor( 255,255,255,255 ), m_bInverse( false ), m_bDragging( false ), m_bDirty( false )
{
	SetProportional( true ); SetPaintBackgroundEnabled( false );
	if ( parent ) AddActionSignalTarget( parent );
	m_pButton = new CSliderButton( this );
	m_pLabel = new vgui::Label( this, "LblSliderText", "" );
	m_pProgress = new vgui::ContinuousProgressBar( this, "PrgValue" );
	m_pDefaultMark = new vgui::Panel( this, "PnlDefaultMark" );
	KeyValues::AutoDelete data( new KeyValues( "" ) );
	if ( LoadTerrorUiResource( data, "resource/ui/l4d360ui/slidercontrol.res" ) ) LoadControlSettings( "resource/ui/l4d360ui/slidercontrol.res", "GAME", data );
	m_pButton->SetMouseInputEnabled( false );
	m_pLabel->SetMouseInputEnabled( false ); m_pProgress->SetMouseInputEnabled( false ); m_pDefaultMark->SetMouseInputEnabled( false );
}
vgui::Panel *SliderControl::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "L4D360HybridButton" ) ) return new CSliderButton( this );
	return BaseClass::CreateControlByName( name );
}
void SliderControl::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings( data );
	m_flMin = data->GetFloat( "minValue", 0 ); m_flMax = data->GetFloat( "maxValue", 100 ); m_flStep = data->GetFloat( "stepSize", 1 );
	m_bInverse = data->GetInt( "inverseFill" ) == 1;
	m_ConCommand = data->GetString( "conCommand", "" ); m_DefaultCommand = data->GetString( "conCommandDefault", "" );
	m_pLabel->SetText( data->GetString( "labelText", "" ) );
	m_pButton->SetMouseInputEnabled( false );
	Reset();
}
void SliderControl::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_BorderColor = scheme->GetColor( "HybridButton.BorderColor", Color( 255,255,255,255 ) );
	m_pDefaultMark->SetBgColor( m_BorderColor );
}
void SliderControl::SetEnabled( bool enabled )
{
	BaseClass::SetEnabled( enabled ); m_pButton->SetEnabled( enabled ); m_pLabel->SetEnabled( enabled );
	m_pProgress->SetVisible( enabled ); m_pDefaultMark->SetVisible( enabled );
}
float SliderControl::GetDefaultValue() const
{
	ConVar *def = cvar->FindVar( m_DefaultCommand.String() );
	if ( def ) return def->GetFloat();
	ConVar *value = cvar->FindVar( m_ConCommand.String() );
	return value ? atof( value->GetDefault() ) : m_flMax;
}
void SliderControl::SetValue( float value, bool silent )
{
	value = clamp( value, m_flMin, m_flMax );
	if ( !silent )
	{
		if ( value==m_flValue ) { vgui::surface()->PlaySound("UI/menu_invalid.wav"); return; }
		vgui::surface()->PlaySound("UI/buttonclick.wav"); m_bDirty=true;
	}
	SetDisplayValue( value );
	ConVar *variable = cvar->FindVar( m_ConCommand.String() );
	if ( variable ) variable->SetValue( m_flValue );
	if ( !silent )
	{
		KeyValues *message = new KeyValues( "SliderMoved", "panel", GetName() );
		message->SetFloat( "value", m_flValue ); PostActionSignal( message );
	}
}
void SliderControl::SetDisplayValue( float value )
{
	m_flValue = clamp( value, m_flMin, m_flMax );
	const float fraction = ( m_flValue - m_flMin ) / ( m_flMax - m_flMin );
	m_pProgress->SetProgress( m_bInverse ? 1.0f - fraction : fraction );
}
void SliderControl::Reset()
{
	ConVar *variable = cvar->FindVar( m_ConCommand.String() );
	if ( variable ) SetDisplayValue( variable->GetFloat() );
	m_bDirty = false; InvalidateLayout();
}
void SliderControl::PerformLayout()
{
	BaseClass::PerformLayout();
	int x = GetWide() - m_pProgress->GetWide(), y = ( GetTall() - m_pProgress->GetTall() ) / 2;
	m_pProgress->SetPos( x, y );
	float fraction = ( GetDefaultValue() - m_flMin ) / ( m_flMax - m_flMin );
	if ( m_bInverse ) fraction = 1.0f - fraction;
	m_pDefaultMark->SetPos( x + (int)( ( m_pProgress->GetWide() - m_pDefaultMark->GetWide() ) * fraction ), y - 3 );
}
void SliderControl::PaintBackground() { BaseClass::PaintBackground(); }
void SliderControl::NavigateTo() { BaseClass::NavigateTo(); m_pButton->NavigateTo(); }
void SliderControl::NavigateFrom() { BaseClass::NavigateFrom(); m_pButton->NavigateFrom(); }
void SliderControl::OnKeyCodePressed( vgui::KeyCode code )
{
	const vgui::KeyCode base=GetBaseButtonCode(code);
	bool left=base==KEY_LEFT || base==KEY_XBUTTON_LEFT_SHOULDER || base==KEY_XBUTTON_LEFT || base==KEY_XSTICK1_LEFT || base==KEY_XSTICK2_LEFT;
	bool right=base==KEY_RIGHT || base==KEY_XBUTTON_RIGHT_SHOULDER || base==KEY_XBUTTON_RIGHT || base==KEY_XSTICK1_RIGHT || base==KEY_XSTICK2_RIGHT;
	if ( left || right )
	{
		if ( IsEnabled() ) SetValue(m_flValue+((right!=m_bInverse) ? m_flStep : -m_flStep),false);
		return;
	}
	BaseClass::OnKeyCodePressed( code );
}
void SliderControl::SetMouseValue( int x )
{
	int px,py; m_pProgress->GetPos( px,py );
	float fraction = clamp( (float)( x - px ) / m_pProgress->GetWide(), 0.0f, 1.0f );
	if ( m_bInverse ) fraction = 1.0f - fraction;
	SetValue( m_flMin + fraction * ( m_flMax - m_flMin ), false );
}
void SliderControl::OnMousePressed( vgui::MouseCode code )
{
	if ( code == MOUSE_LEFT && IsEnabled() )
	{
		int x,y; vgui::input()->GetCursorPos( x,y ); ScreenToLocal( x,y );
		int px,py; m_pProgress->GetPos( px,py );
		if ( x >= px && x <= px + m_pProgress->GetWide() && y >= py && y <= py + m_pProgress->GetTall() )
		{ m_bDragging = true; vgui::input()->SetMouseCapture( GetVPanel() ); SetMouseValue( x ); NavigateTo(); return; }
	}
	BaseClass::OnMousePressed( code );
}
void SliderControl::OnMouseReleased( vgui::MouseCode code )
{
	if ( code == MOUSE_LEFT && m_bDragging ) { m_bDragging = false; vgui::input()->SetMouseCapture( NULL ); }
	BaseClass::OnMouseReleased( code );
}
void SliderControl::OnCursorMoved( int x, int y ) { if ( m_bDragging ) SetMouseValue( x ); BaseClass::OnCursorMoved( x,y ); }
SpinnerControl::SpinnerControl( vgui::Panel *parent, const char *name ) : BaseClass( parent,name ), m_InactiveColor(64,64,64,255), m_ActiveColor(128,0,0,255), m_flArrowTime(0), m_nItem(0), m_nArrow(0)
{
	SetProportional(true); SetPaintBackgroundEnabled(false);
	m_pLeft = new vgui::Label(this,"LblLeftArrow","#GameUI_Icons_LEFT_ARROW");
	m_pCurrent = new vgui::Label(this,"LblCurrentText","");
	m_pRight = new vgui::Label(this,"LblRightArrow","#GameUI_Icons_RIGHT_ARROW");
	m_pLeft->SetMouseInputEnabled(false); m_pCurrent->SetMouseInputEnabled(false); m_pRight->SetMouseInputEnabled(false);
}
void SpinnerControl::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings(data);
	KeyValues *inactive=data->FindKey("inactiveColor",false), *active=data->FindKey("activeColor",false);
	m_InactiveColor=inactive ? inactive->GetColor() : Color(64,64,64,255);
	m_ActiveColor=active ? active->GetColor() : Color(128,0,0,255);
	m_pCurrent->SetFgColor(m_InactiveColor); m_pCurrent->SetPaintBorderEnabled(data->GetInt("paintTextBorder")==1);
	static const char *alignments[]={"north-west","north","north-east","west","center","east","south-west","south","south-east"};
	for ( int i=0;i<ARRAYSIZE(alignments);++i ) if ( !Q_stricmp(data->GetString("textAlignment",""),alignments[i]) ) m_pCurrent->SetContentAlignment((vgui::Label::Alignment)i);
}
void SpinnerControl::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings(scheme);
	m_pCurrent->SetBorder(scheme->GetBorder("ButtonBorder"));
	m_pLeft->SetFont(scheme->GetFont("GameUIButtons",false)); m_pRight->SetFont(scheme->GetFont("GameUIButtons",false));
}
void SpinnerControl::PerformLayout()
{
	BaseClass::PerformLayout(); int leftWide,leftTall,rightWide,rightTall;
	m_pLeft->GetContentSize(leftWide,leftTall); m_pRight->GetContentSize(rightWide,rightTall);
	m_pLeft->SetBounds(0,0,leftWide,leftTall); m_pRight->SetBounds(GetWide()-rightWide,0,rightWide,rightTall);
	m_pCurrent->SetBounds(4,0,MAX(0,GetWide()-8),GetTall());
}
void SpinnerControl::OnThink()
{
	BaseClass::OnThink(); bool expired=m_flArrowTime+0.25f<=Plat_FloatTime();
	m_pLeft->SetAlpha(!expired && m_nArrow==1 ? 255 : 64); m_pRight->SetAlpha(!expired && m_nArrow==2 ? 255 : 64);
}
void SpinnerControl::NavigateTo()
{
	BaseClass::NavigateTo(); RequestFocus(); m_pCurrent->SetFgColor(m_ActiveColor);
	PostActionSignal(new KeyValues("OnNavigateTo","panelName",GetName()));
}
void SpinnerControl::NavigateFrom() { BaseClass::NavigateFrom(); m_pCurrent->SetFgColor(m_InactiveColor); }
void SpinnerControl::AddItem( const char *text, int value ) { Item_t item; item.text=text; item.value=value; m_Items.AddToTail(item); }
int SpinnerControl::GetCurrentValue() const { return m_Items.IsValidIndex(m_nItem) ? m_Items[m_nItem].value : 0; }
void SpinnerControl::SetCurrentItem( int item )
{
	if ( !m_Items.IsValidIndex(item) ) return;
	m_nItem=item; m_pCurrent->SetText(m_Items[item].text.String());
	if ( HasFocus() ) PostActionSignal(new KeyValues("OnNavigateTo","panelName",GetName()));
	PostActionSignal(new KeyValues("OnSetCurrentItem","panelName",GetName()));
}
void SpinnerControl::OnKeyCodePressed( vgui::KeyCode code )
{
	const int previous=m_nItem;
	switch ( GetBaseButtonCode(code) )
	{
	case KEY_XBUTTON_RIGHT:
	case KEY_XSTICK1_RIGHT:
	case KEY_XSTICK2_RIGHT:
		++m_nItem; m_nArrow=2; m_flArrowTime=Plat_FloatTime(); break;
	case KEY_XBUTTON_LEFT:
	case KEY_XSTICK1_LEFT:
	case KEY_XSTICK2_LEFT:
		--m_nItem; m_nArrow=1; m_flArrowTime=Plat_FloatTime(); break;
	default:
		BaseClass::OnKeyCodePressed(code); break;
	}
	const int count=m_Items.Count();
	if ( m_nItem<0 && count>0 ) m_nItem=count-1;
	else if ( m_nItem>=count || !count ) m_nItem=0;
	if ( previous!=m_nItem ) SetCurrentItem(m_nItem);
}
NativeOptionsFrame::NativeOptionsFrame( vgui::Panel *parent, const char *name, const char *title ) : BaseClass( parent,name,true,true,true,false ), m_TitleToken( title ), m_bRefreshing( false ) { SetFooterEnabled( true ); }
void NativeOptionsFrame::OnOpen()
{
	BaseClass::OnOpen();
	if ( CL4DFooterPanel *footer = GetL4DFooterPanel( this ) )
	{ footer->SetButtons( 3,4,true ); footer->SetButtonText( 1,"#L4D360UI_Select" ); footer->SetButtonText( 2,"#L4D360UI_Controller_Done" ); }
}
void NativeOptionsFrame::PaintBackground() { DrawDialogBackground( m_TitleToken.String(),NULL,"#L4D360UI_AudioVideo_Desc",NULL,NULL ); }
vgui::Panel *NativeOptionsFrame::CreateControlByName( const char *name )
{
	vgui::Panel *control = CreateBaseModOptionsControl( this,name );
	return control ? control : BaseClass::CreateControlByName( name );
}
void NativeOptionsFrame::SaveConfig() { engine->ClientCmd_Unrestricted( "host_writeconfig\n" ); } // target is single slot; native host_writeconfig_ss 0
void NativeOptionsFrame::CloseWithoutSaving()
{
	BaseClass::OnClose();
}
void NativeOptionsFrame::OnClose() { SaveConfig(); CloseWithoutSaving(); }
void NativeOptionsFrame::OnCommand( const char *command )
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp( command,"Back" ) || !Q_stricmp( command,"Done" ) ) { NavigateBack(); return; }
	BaseClass::OnCommand( command );
}
void NativeOptionsFrame::Select( const char *control, const char *command )
{
	DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( control,true ) );
	if ( drop ) { const bool old = m_bRefreshing; m_bRefreshing = true; drop->SetCurrentSelection( command ); m_bRefreshing = old; }
}
void NativeOptionsFrame::RefreshChoices( const OptionChoice_t *choices, int count )
{
	for ( int i=0;i<count;++i )
	{
		ConVar *variable = cvar->FindVar( choices[i].cvar );
		SetControlEnabled( choices[i].control, variable != NULL, true );
		if ( variable && !Q_stricmp( variable->GetString(),choices[i].value ) ) Select( choices[i].control,choices[i].command );
	}
}
bool NativeOptionsFrame::ApplyChoice( const char *command, const OptionChoice_t *choices, int count )
{
	for ( int i=0;i<count;++i ) if ( !Q_stricmp( command,choices[i].command ) )
	{ if ( ConVar *variable = cvar->FindVar( choices[i].cvar ) ) variable->SetValue( choices[i].value ); return true; }
	return false;
}
void NativeOptionsFrame::ResetSliders( vgui::Panel *panel )
{
	if ( SliderControl *slider = dynamic_cast<SliderControl *>( panel ) ) slider->Reset();
	for ( int i=0;i<panel->GetChildCount();++i ) ResetSliders( panel->GetChild(i) );
}
void NativeOptionsFrame::Confirm( const char *title, const char *message, const char *command )
{
	GenericConfirmation *dialog = new GenericConfirmation( GetParent(),"GenericConfirmation" );
	GenericConfirmation::UsageData_t data; data.pWindowTitle = title; data.pMessageText = message; data.bOkButtonEnabled = data.bCancelButtonEnabled = true;
	dialog->SetUsageData( data ); dialog->SetNavBack( this ); dialog->SetOKCommand( new KeyValues( "Command","command",command ) ); dialog->AddActionSignalTarget( this );
	if ( !dialog->LoadLayout("resource/ui/l4d360ui/genericconfirmation.res") ) { delete dialog; return; }
	SetVisible( false ); dialog->OnOpen();
}
void NativeOptionsFrame::OpenChild( const char *name )
{
	CL4DFrame *child = CreateNativeOptionsWindow( GetParent(),name );
	if ( child ) { child->SetNavBack( this ); SetVisible( false ); child->OnOpen(); }
}
vgui::Panel *CreateBaseModOptionsControl( vgui::Panel *parent, const char *name )
{
	if ( !Q_stricmp( name,"SliderControl" ) ) return new SliderControl( parent,"" );
	if ( !Q_stricmp( name,"SpinnerControl" ) ) return new SpinnerControl( parent,"" );
	return NULL;
}
bool IsNativeOptionsCommand( const char *name )
{
	static const char *names[] = { "Audio","AudioVideo","CloudSettings","Controller","KeyboardMouse","MultiplayerSettings","Video","Game","GraphicsDX12" };
	for ( int i=0;i<ARRAYSIZE(names);++i ) if ( !Q_stricmp( name,names[i] ) ) return true;
	return false;
}
CL4DFrame *CreateNativeOptionsWindow( vgui::Panel *parent, const char *name )
{
	if ( !Q_stricmp(name,"Audio") ) return new Audio(parent,"Audio");
	if ( !Q_stricmp(name,"AudioVideo") ) return new AudioVideo(parent,"AudioVideo");
	if ( !Q_stricmp(name,"CloudSettings") || !Q_stricmp(name,"Cloud") ) return new Cloud(parent,"Cloud");
	if ( !Q_stricmp(name,"Controller") || !Q_stricmp(name,"ControllerOptions") ) return new ControllerOptions(parent,"ControllerOptions");
	if ( !Q_stricmp(name,"ControllerOptionsButtons") ) return new ControllerOptionsButtons(parent,"ControllerOptionsButtons");
	if ( !Q_stricmp(name,"ControllerOptionsSticks") ) return new ControllerOptionsSticks(parent,"ControllerOptionsSticks");
	if ( !Q_stricmp(name,"KeyboardMouse") ) return new KeyboardMouse(parent,"KeyboardMouse");
	if ( !Q_stricmp(name,"Multiplayer") || !Q_stricmp(name,"MultiplayerSettings") ) return new Multiplayer(parent,"Multiplayer");
	if ( !Q_stricmp(name,"Options") ) return new Options(parent,"Options");
	if ( !Q_stricmp(name,"Video") ) return new Video(parent,"Video");
	if ( !Q_stricmp(name,"GraphicsDX12") ) return new GraphicsDX12(parent,"GraphicsDX12");
	if ( !Q_stricmp(name,"Game") || !Q_stricmp(name,"GameOptions") ) return new GameOptions(parent,"GameOptions");
	return NULL;
}
}
