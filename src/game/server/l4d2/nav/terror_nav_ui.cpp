//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "NavUI/NavUI.h"
#include "NavUI/SelectionTool.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "tier0/memdbgon.h"

// S2E2A90 and S2E3700 are literal false-returning retail functions, with no
// reads/writes of fog parameters. All parameter-update branches are unreachable.
// Keep the real controls and commands, not an invented working fog editor.
class FogEditToolPanel : public CNavUIToolPanel
{
	DECLARE_CLASS_SIMPLE( FogEditToolPanel, CNavUIToolPanel );
public:
	FogEditToolPanel( vgui::Panel *parent, const char *toolName );
	virtual void Init() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
private:
	MESSAGE_FUNC_PARAMS( OnTextChanged, "TextChanged", data );
	void DisableApplyButton();
	vgui::ComboBox *m_fogCombo;
	vgui::CheckButton *m_enabled;
	vgui::CheckButton *m_blended;
	bool m_resetApplyPending;
};

FogEditToolPanel::FogEditToolPanel( vgui::Panel *parent, const char *toolName ) : BaseClass( parent, toolName )
{
	// S387970: the parent owns every child. Native field pointers to the six
	// entries are used only after the literal-false fog getter, so are elided.
	m_fogCombo = new CNavUIComboBox( this, "FogCombo", 10, false );
	m_enabled = new CNavUICheckButton( this, "FogEnabled", "Enabled" );
	m_blended = new CNavUICheckButton( this, "FogBlend", "Blended" );
	new CNavUITextEntry( this, "FogPrimaryColor" );
	new CNavUITextEntry( this, "FogSecondaryColor" );
	new CNavUITextEntry( this, "FogDirection" );
	new CNavUITextEntry( this, "FogStart" );
	new CNavUITextEntry( this, "FogEnd" );
	new CNavUITextEntry( this, "FogFarZ" );
	LoadControlSettings( "Resource/UI/NavTools/FogEditTool.res" );
	m_resetApplyPending = false;
}

void FogEditToolPanel::DisableApplyButton()
{
	vgui::Button *button = dynamic_cast< vgui::Button * >( FindChildByName( "ApplyChanges" ) );
	if ( button )
		button->SetEnabled( false );
}

void FogEditToolPanel::Init()
{
	// S3866D0. Do not clear previously inserted items: the native does not.
	engine->ServerCommand( "nav_edit 1\n" );
	engine->ServerCommand( "nav_fog_edit 1\n" );
	DisableApplyButton();
	TerrorNavMesh *mesh = TheTerrorNavMesh();
	const char *selected = mesh->GetPopulationNameForIndex( mesh->GetSelectedFogPopulation() );
	if ( !selected )
	{
		selected = mesh->GetPopulationNameForIndex( 1 );
		if ( selected )
			engine->ServerCommand( CFmtStr( "nav_fog_set %s\n", selected ) );
	}
	int activeItem = -1;
	for ( int i = 0; i < mesh->GetPopulationCount(); ++i )
	{
		// Native bug: this one-based lookup is fed [0,count), omitting the last.
		const char *name = mesh->GetPopulationNameForIndex( i );
		if ( name && *name )
		{
			KeyValues *data = new KeyValues( "UserData", "name", name );
			int item = m_fogCombo->AddItem( name, data );
			if ( selected && !Q_stricmp( name, selected ) )
				activeItem = item;
			data->deleteThis();
		}
	}
	m_fogCombo->AddActionSignalTarget( this );
	m_fogCombo->ActivateItem( activeItem );
	m_enabled->AddActionSignalTarget( this );
	m_blended->AddActionSignalTarget( this );
}

void FogEditToolPanel::Shutdown()
{
	// S3863F0.
	engine->ServerCommand( "nav_fog_edit 0\n" );
}

void FogEditToolPanel::OnThink()
{
	// S386910: Revert's Apply-button update is deferred until the next think.
	BaseClass::OnThink();
	if ( m_resetApplyPending )
	{
		m_resetApplyPending = false;
		DisableApplyButton();
	}
}

void FogEditToolPanel::PerformLayout()
{
	// S386410.
	vgui::Panel *parent = GetParent();
	if ( parent )
	{
		int wide, tall;
		parent->GetSize( wide, tall );
		SetBounds( 0, 0, wide, tall );
	}
	BaseClass::PerformLayout();
}

void FogEditToolPanel::OnCommand( const char *command )
{
	// S387030. Revert's native callee is itself a nullsub, and its refresh
	// returns at S2E2A90=false. Save still submits the real mission command.
	if ( !Q_stricmp( command, "ApplyChanges" ) )
		DisableApplyButton();
	else if ( !Q_stricmp( command, "RevertChanges" ) )
		m_resetApplyPending = true;
	else if ( !Q_stricmp( command, "SaveMission" ) )
	{
		engine->ServerCommand( "mission_save\n" );
		DisableApplyButton();
	}
	else
		BaseClass::OnCommand( command );
}

void FogEditToolPanel::OnTextChanged( KeyValues *data )
{
	// S386B00: the combo submission precedes all fog reads. Non-combo text
	// and CheckButtonChecked have no effect: their first fog getter is false.
	vgui::Panel *panel = static_cast< vgui::Panel * >( data->GetPtr( "panel" ) );
	if ( dynamic_cast< vgui::ComboBox * >( panel ) == m_fogCombo )
	{
		KeyValues *item = m_fogCombo->GetActiveItemUserData();
		if ( item )
			engine->ServerCommand( CFmtStr( "nav_fog_set %s\n", item->GetString( "name", "" ) ) );
	}
}

class TerrorSelectionToolPanel : public SelectionToolPanel
{
	DECLARE_CLASS_SIMPLE( TerrorSelectionToolPanel, SelectionToolPanel );
public:
	TerrorSelectionToolPanel( vgui::Panel *parent, const char *toolName );
	virtual bool IsFloodSelectable( CNavArea *area ) OVERRIDE;
private:
	bool MatchesSpawnAttribute( TerrorNavArea *area, const char *control, unsigned int attribute );
	vgui::TextEntry *m_minLight;
	vgui::TextEntry *m_maxLight;
};

TerrorSelectionToolPanel::TerrorSelectionToolPanel( vgui::Panel *parent, const char *toolName ) : BaseClass( parent, toolName )
{
	// S388330, after the base loaded the original SelectionTool resource.
	m_maxLight = dynamic_cast< vgui::TextEntry * >( FindChildByName( "MaxLight", true ) );
	m_minLight = dynamic_cast< vgui::TextEntry * >( FindChildByName( "MinLight", true ) );
	if ( m_maxLight ) m_maxLight->SetText( "1" );
	if ( m_minLight ) m_minLight->SetText( "0" );
}

bool TerrorSelectionToolPanel::MatchesSpawnAttribute( TerrorNavArea *area, const char *control, unsigned int attribute )
{
	// S388060. The real SelectionToolPanel flood owner sets m_floodStartArea.
	return !IsCheckButtonChecked( control ) ||
		ToTerrorNavArea( m_floodStartArea )->HasSpawnAttributes( attribute ) == area->HasSpawnAttributes( attribute );
}

#pragma float_control( precise, on, push )
bool TerrorSelectionToolPanel::IsFloodSelectable( CNavArea *base )
{
	// S3880B0: Terror filters first, all four corner lights, then base Place/Jump.
	TerrorNavArea *area = ToTerrorNavArea( base );
	if ( IsCheckButtonChecked( "Fog" ) &&
		ToTerrorNavArea( m_floodStartArea )->GetPopulationIndex() != area->GetPopulationIndex() ) return false;
	if ( !MatchesSpawnAttribute( area, "Finale", 0x40 ) ||
		 !MatchesSpawnAttribute( area, "Battlefield", 0x100 ) ||
		 !MatchesSpawnAttribute( area, "NoMobs", 0x2000 ) ||
		 !MatchesSpawnAttribute( area, "RescueVehicle", 0x8000 ) ||
		 !MatchesSpawnAttribute( area, "RescueCloset", 0x10000 ) ||
		 !MatchesSpawnAttribute( area, "Empty", 2 ) ||
		 !MatchesSpawnAttribute( area, "Obscured", 0x1000 ) ||
		 !MatchesSpawnAttribute( area, "NotClearable", 0x400 ) ||
		 !MatchesSpawnAttribute( area, "StopScan", 4 ) ||
		 !MatchesSpawnAttribute( area, "Checkpoint", 0x800 ) ||
		 !MatchesSpawnAttribute( area, "PlayerStart", 0x80 ) ) return false;
	float maxLight = m_maxLight ? m_maxLight->GetValueAsFloat() : 1.0f;
	float minLight = m_minLight ? m_minLight->GetValueAsFloat() : 0.0f;
	// S3882A9..3882D5 uses ordered JA branches: unordered values stay unchanged.
	if ( maxLight > 1.0f ) maxLight = 1.0f;
	else if ( 0.0f > maxLight ) maxLight = 0.0f;
	if ( minLight > 1.0f ) minLight = 1.0f;
	else if ( 0.0f > minLight ) minLight = 0.0f;
	for ( int corner = 0; corner < NUM_CORNERS; ++corner )
	{
		const double intensity = area->GetLightIntensity( area->GetCorner( static_cast< NavCornerType >( corner ) ) );
		const float roundedIntensity = static_cast< float >( intensity );
		if ( double( minLight ) > intensity || roundedIntensity > maxLight ) return false;
	}
	return BaseClass::IsFloodSelectable( base );
}
#pragma float_control( pop )

class CTerrorNavUIBasePanel : public CNavUIBasePanel
{
	DECLARE_CLASS_SIMPLE( CTerrorNavUIBasePanel, CNavUIBasePanel );
public:
	virtual void OnCommand( const char *command ) OVERRIDE;
protected:
	virtual CNavUIToolPanel *CreateTool( const char *toolName, vgui::Panel *parent ) OVERRIDE;
};

void CTerrorNavUIBasePanel::OnCommand( const char *command )
{
	// S388000.
	if ( !Q_stricmp( command, "FogTool" ) ) ActivateTool( "Fog Edit" );
	else BaseClass::OnCommand( command );
}

CNavUIToolPanel *CTerrorNavUIBasePanel::CreateTool( const char *toolName, vgui::Panel *parent )
{
	// S387F60. Every other tool remains the existing CNavUIBasePanel owner.
	if ( !Q_stricmp( toolName, "Fog Edit" ) ) return new FogEditToolPanel( parent, toolName );
	if ( !Q_stricmp( toolName, "Selection" ) ) return new TerrorSelectionToolPanel( parent, toolName );
	return BaseClass::CreateTool( toolName, parent );
}

CNavUIBasePanel *CreateNavUI()
{
	// S387F40: allocation failure follows the standing fatal-allocation rule.
	return new CTerrorNavUIBasePanel;
}
