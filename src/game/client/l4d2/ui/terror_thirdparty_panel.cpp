//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_legacy_controls.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "networkstringtabledefs.h"
#include "igameevents.h"
#include "vgui_controls/HTML.h"
#include "vgui_controls/MessageMap.h"
#include "tier0/memdbgon.h"
extern INetworkStringTable *g_pStringTableInfoPanel;
class CThirdPartyServerPanel : public vgui::EditablePanel, public IGameEventListener2
{
	DECLARE_CLASS_SIMPLE( CThirdPartyServerPanel, vgui::EditablePanel );
public:
	CThirdPartyServerPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_text( NULL ), m_hostnameLabel( NULL ), m_pending( false )
	{
		m_html = new vgui::HTML( this, "HTMLMessage", false ); m_html->SetContextMenuEnabled( true );
		m_hostname[0] = 0;
		gameeventmanager->AddListener( this, "server_spawn", false ); gameeventmanager->AddListener( this, "hostname_changed", false );
	}
	virtual ~CThirdPartyServerPanel() { gameeventmanager->RemoveListener( this ); }
	virtual bool IsVisible() OVERRIDE { return TerrorGameRules() ? TerrorGameRules()->IsNativeDedicatedServer() : BaseClass::IsVisible(); }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		if ( !Q_strcmp( event->GetName(), "server_spawn" ) || !Q_strcmp( event->GetName(), "hostname_changed" ) )
		{
			Q_strncpy( m_hostname, event->GetString( "hostname", "" ), sizeof( m_hostname ) ); m_pending = true; Refresh();
		}
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		KeyValues *conditions = NULL;
		if ( m_embedded ) { conditions = new KeyValues( "conditions" ); conditions->SetInt( "if_embedded", 1 ); }
		LoadControlSettings( "Resource/UI/ThirdPartyServerPanel.res", NULL, NULL, conditions ); if ( conditions ) conditions->deleteThis();
		BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundType( 0 ); SetPaintBorderEnabled( false );
		m_hostnameLabel = dynamic_cast<vgui::Label *>( FindChildByName( "HostnameLabel" ) );
		m_text = dynamic_cast<CTerrorRichText *>( FindChildByName( "TerrorTextMessage" ) );
		if ( m_text ) { m_text->SetUnusedScrollbarInvisible( false ); m_text->SetDrawTextOnly(); }
		m_html->SetScrollbarsEnabled( false ); Refresh();
	}
	virtual void OnThink() OVERRIDE { if ( m_pending && g_pStringTableInfoPanel ) Refresh(); BaseClass::OnThink(); }
private:
	void Refresh()
	{
		CTerrorGameRules *rules = TerrorGameRules();
		if ( rules )
		{
			char text[32]; Q_snprintf( text, sizeof( text ), " #%d", rules->GetNativeServerRank() ); SetDialogVariable( "rank", text );
			Q_snprintf( text, sizeof( text ), "%d", rules->GetNativeServerPlayerCount() ); SetDialogVariable( "playercount", text );
		}
		if ( !m_text || !m_html || !m_hostnameLabel ) return;
		const char *hostfile = NULL;
		if ( g_pStringTableInfoPanel )
		{
			m_pending = false;
			const int index = g_pStringTableInfoPanel->FindStringIndex( "hostfile" );
			if ( index != INVALID_STRING_INDEX ) { int length = 0; hostfile = static_cast<const char *>( g_pStringTableInfoPanel->GetStringUserData( index, &length ) ); }
		}
		if ( hostfile && *hostfile )
		{
			const bool url = !Q_strnicmp( hostfile, "http://", 7 );
			m_text->SetVisible( !url ); m_html->SetVisible( url );
			if ( url ) m_html->OpenURL( hostfile, "", false ); else { m_text->SetText( hostfile ); m_text->GotoTextStart(); }
			m_hostnameLabel->SetVisible( false );
		}
		else
		{
			SetDialogVariable( "hostname", *m_hostname ? m_hostname : "#L4D_Default_Hostname" );
			m_hostnameLabel->SetVisible( true ); m_text->SetVisible( false ); m_html->SetVisible( false );
		}
	}
	vgui::HTML *m_html;
	CTerrorRichText *m_text;
	vgui::Label *m_hostnameLabel;
	char m_hostname[128];
	bool m_pending;
	CPanelAnimationVar( bool, m_embedded, "embedded", "0" );
};
DECLARE_BUILD_FACTORY( CThirdPartyServerPanel );
