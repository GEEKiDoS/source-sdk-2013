// Native CYardLinePanel2EF5A0..2EF844. New client TU to register in the
// existing client_l4d2 VPC list; no viewport alias or independent HUD timer.
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_vguiscreen.h"
#include "ienginevgui.h"
#include "panelmetaclassmgr.h"
#include "vgui/IVGui.h"
#include "vgui/IScheme.h"
#include "vgui_controls/Label.h"
#include "cdll_util.h"
#include "tier0/memdbgon.h"

class CYardLinePanel : public CVGuiScreenPanel
{
	DECLARE_CLASS_SIMPLE( CYardLinePanel, CVGuiScreenPanel );
public:
	CYardLinePanel( vgui::Panel *parent, const char *metaClassName );
	virtual bool Init( KeyValues *keyValues, VGuiScreenInitData_t *initData ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	vgui::Label *m_pYardLineLabel;
};

// Native2EF7E0 is this exact SDK CPanelFactory specialization: allocate the
// panel, Init with the borrowed VGuiScreenInitData_t, delete on Init failure.
// Native type and metaclass strings are BOTH "yard_line_panel".
DECLARE_VGUI_SCREEN_FACTORY( CYardLinePanel, "yard_line_panel" );

CYardLinePanel::CYardLinePanel( vgui::Panel *parent, const char *metaClassName )
	: BaseClass( parent, "CYardLinePanel", vgui::scheme()->LoadSchemeFromFileEx(
		enginevgui->GetPanel( PANEL_CLIENTDLL ), "resource/ClientScheme.res", "ClientScheme" ) )
{
	(void)metaClassName; // Native2EF620 uses its literal panel name.
	m_pYardLineLabel = new vgui::Label( this, "YardLineLabel", "" );
}

// Native2EF5E0 registers the tick BEFORE BaseClass::Init. Panel deletion
// removes the tick through the actual VGUI lifetime, not a second timer.
bool CYardLinePanel::Init( KeyValues *keyValues, VGuiScreenInitData_t *initData )
{
	vgui::ivgui()->AddTickSignal( GetVPanel() );
	return BaseClass::Init( keyValues, initData );
}

// Native2EF5A0 does not call the base ApplySchemeSettings.
void CYardLinePanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	if ( scheme )
		m_pYardLineLabel->SetFont( scheme->GetFont( "HudNumbers", true ) );
}

void CYardLinePanel::OnTick()
{
	BaseClass::OnTick();
	C_BaseEntity *entity = GetEntity();
	if ( !entity || !entity->IsPlayer() )
	{
		SetVisible( false );
		return;
	}
	SetVisible( true );
	SetPaintBackgroundEnabled( false );
	m_pYardLineLabel->SetPaintBackgroundEnabled( false );
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( entity );
	m_pYardLineLabel->SetFgColor( Color( 0, 255, 0, player->GetYardLineAlpha( this ) ) );
	//2EF7B3 consumes FULL EAX, despite9AD9's misleading char temporary.
	// LEA/ADD wrap in32 bits. Missing wrapper index-1 yields0, not2560.
	const int index = player->GetYardLineNumber( this );
	const uint32 yards = 2u * ( 5u * static_cast< uint32 >( index ) + 5u );
	m_pYardLineLabel->SetText( VarArgs( "%d >", static_cast< int32 >( yards ) ) );
}
