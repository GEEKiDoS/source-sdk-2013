// Copyright (c) 2006 Turtle Rock Studios, Inc.
// Public Alien Swarm SDK ancestry: swarm/vgui/radialmenu.cpp (SDK license).
// Mirror: ReactiveDrop/reactivedrop_public_src, commit
// 9231d40422194dfecca8b698698df4af7e0d99f1, src/game/client/swarm/vgui.
// Widget foundation only; L4D2 Frame/viewport/input ownership follows its binary.
// Pinned L4D2 C2B8D10..C2BBEC4; no ASW HUD/input/camera ownership.
#include "cbase.h"
#include "l4d2/ui/terror_radial_menu.h"
#include "l4d2/ui/terror_polygon_button.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_team.h"
#include "inputsystem/iinputsystem.h"
#include "steam/steam_api.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"
#include <emmintrin.h>
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#if _MSC_VER >= 1933
#pragma fp_contract(off)
#endif
#endif
ConVar cl_fastradial( "cl_fastradial", "1", 0xa, "If 1, releasing the button on a radial menu executes the highlighted button" );
ConVar RadialMenuDebug( "cl_rosetta_debug", "0", 0xa, "" );
ConVar cl_rosetta_line_inner_radius( "cl_rosetta_line_inner_radius", "25", 0xa, "" );
ConVar cl_rosetta_line_outer_radius( "cl_rosetta_line_outer_radius", "45", 0xa, "" );
static int NativeRadialInteger( float value ) { return _mm_cvttss_si32( _mm_set_ss( value ) ); }

static char s_radialMenuName[64];
static bool s_mouseMenuKeyHeld;
static CRadialMenu *s_pControllerInputOverride;
bool HasTerrorRadialInputOverride() { return s_pControllerInputOverride != NULL; }
#include "l4d2/ui/terror_radial_button.inc"

CRadialMenu::CRadialMenu(IViewPort *viewport):BaseClass(NULL,"radialmenu",true,true),m_viewport(viewport),m_armedButtonDir(CENTER),m_cursorX(-1),m_cursorY(-1),m_fading(false),m_fadeStart(0),m_lineColor(0,0,0,0),m_menuData(NULL),m_minButtonX(0),m_minButtonY(0),m_maxButtonX(0),m_maxButtonY(0)
{
 Q_memset(m_buttons,0,sizeof(m_buttons));m_resource=new KeyValues("RadialMenu");m_resource->LoadFromFile(filesystem,"resource/UI/RadialMenu.res");FlushClientMenus();SetScheme("ClientScheme");SetMoveable(false);SetSizeable(false);SetProportional(true);HandleControlSettings();SetCursor(vgui::dc_blank);
}
CRadialMenu::~CRadialMenu(){if(s_pControllerInputOverride==this)s_pControllerInputOverride=NULL;m_resource->deleteThis();if(m_menuData)m_menuData->deleteThis();}
const char *CRadialMenu::ButtonNameFromDir( ButtonDir dir )
{
	switch( dir )
	{
	case CENTER:
		return "Center";
	case NORTH:
		return "North";
	case NORTH_EAST:
		return "NorthEast";
	case EAST:
		return "East";
	case SOUTH_EAST:
		return "SouthEast";
	case SOUTH:
		return "South";
	case SOUTH_WEST:
		return "SouthWest";
	case WEST:
		return "West";
	case NORTH_WEST:
		return "NorthWest";
	}

	return "None";
}

CRadialMenu::ButtonDir CRadialMenu::DirFromButtonName( const char * name )
{
	if ( FStrEq( name, "Center" ) )
		return CENTER;
	if ( FStrEq( name, "North" ) )
		return NORTH;
	if ( FStrEq( name, "NorthEast" ) )
		return NORTH_EAST;
	if ( FStrEq( name, "East" ) )
		return EAST;
	if ( FStrEq( name, "SouthEast" ) )
		return SOUTH_EAST;
	if ( FStrEq( name, "South" ) )
		return SOUTH;
	if ( FStrEq( name, "SouthWest" ) )
		return SOUTH_WEST;
	if ( FStrEq( name, "West" ) )
		return WEST;
	if ( FStrEq( name, "NorthWest" ) )
		return NORTH_WEST;

	return CENTER;
}

vgui::Panel *CRadialMenu::CreateControlByName( const char *controlName )
{
	if( !Q_stricmp( "PolygonButton", controlName ) )
	{
		vgui::Button *newButton = new CRadialButton( this, NULL );
		return newButton;
	}
	else
	{
		return BaseClass::CreateControlByName( controlName );
	}
}

void CRadialMenu::HandleControlSettings()
{
	LoadControlSettings("Resource/UI/RadialMenu.res");

	for ( int i=0; i<NUM_BUTTON_DIRS; ++i )
	{
		ButtonDir dir = (ButtonDir)i;
		const char *buttonName = ButtonNameFromDir( dir );
		m_buttons[i] = dynamic_cast<CRadialButton *>(FindChildByName( buttonName ));
		if ( m_buttons[i] )
		{
			m_buttons[i]->SetMouseInputEnabled( true );
		}
	}

	m_armedButtonDir = CENTER;
}

static const char *ButtonDirString( CRadialMenu::ButtonDir dir )
{
	switch ( dir )
	{
	case CRadialMenu::CENTER:
		return "CENTER";
	case CRadialMenu::NORTH:
		return "NORTH";
	case CRadialMenu::NORTH_EAST:
		return "NORTH_EAST";
	case CRadialMenu::EAST:
		return "EAST";
	case CRadialMenu::SOUTH_EAST:
		return "SOUTH_EAST";
	case CRadialMenu::SOUTH:
		return "SOUTH";
	case CRadialMenu::SOUTH_WEST:
		return "SOUTH_WEST";
	case CRadialMenu::WEST:
		return "WEST";
	case CRadialMenu::NORTH_WEST:
		return "NORTH_WEST";
	default:
		return "UNKNOWN";
	}
}

void CRadialMenu::OnCursorEnteredButton( int x, int y, CRadialButton *button )
{
	for ( int i=0; i<NUM_BUTTON_DIRS; ++i )
	{
		if ( !m_buttons[i] ) continue; // Skip only a missing resource child.
		m_buttons[i]->InvalidateLayout();
		if ( m_buttons[i] == button )
		{
			m_cursorX = x;
			m_cursorY = y;
			m_armedButtonDir = (ButtonDir)i;
			if ( RadialMenuDebug.GetBool() )
			{
				Msg( "%f: Rosetta cursor entered %s at %d,%d\n", gpGlobals->curtime, ButtonDirString( m_armedButtonDir ), m_cursorX, m_cursorY );
				engine->Con_NPrintf( 20, "%d %d %s", m_cursorX, m_cursorY, ButtonDirString( m_armedButtonDir ) );
			}
		}
	}
}

void CRadialMenu::UpdateButtonBounds( void )
{
	// Save off the extents of our child buttons so we can clip the cursor to that later

	int wide, tall;
	GetSize( wide, tall );
	m_minButtonX = wide;
	m_minButtonY = tall;
	m_maxButtonX = 0;
	m_maxButtonY = 0;

	for ( int i=0; i<NUM_BUTTON_DIRS; ++i )
	{
		if ( !m_buttons[i] )
			continue;

		int hotspotMinX = 0;
		int hotspotMinY = 0;
		int hotspotMaxX = 0;
		int hotspotMaxY = 0;
		m_buttons[i]->GetHotspotBounds( &hotspotMinX, &hotspotMinY, &hotspotMaxX, &hotspotMaxY );

		int buttonX, buttonY;
		m_buttons[i]->GetPos( buttonX, buttonY );

		m_minButtonX = MIN( m_minButtonX, hotspotMinX + buttonX );
		m_minButtonY = MIN( m_minButtonY, hotspotMinY + buttonY );
		m_maxButtonX = MAX( m_maxButtonX, hotspotMaxX + buttonX );
		m_maxButtonY = MAX( m_maxButtonY, hotspotMaxY + buttonY );
	}

	// First frame, we won't have any hotspots set up, so we get inverted bounds from our initial setup.
	// Reverse these, so our button bounds covers our bounds.
	if ( m_minButtonX > m_maxButtonX )
	{
		V_swap( m_minButtonX, m_maxButtonX );
	}

	if ( m_minButtonY > m_maxButtonY )
	{
		V_swap( m_minButtonY, m_maxButtonY );
	}
}

void CRadialMenu::ChooseArmedButton( void )
{
	StartFade();

	CRadialButton *button = NULL;
	for ( int i=NUM_BUTTON_DIRS-1; i>=0; --i )
	{
		if ( !m_buttons[i] )
			continue;

		if ( m_buttons[i]->IsVisible() && m_buttons[i]->IsEnabled() && m_buttons[i]->IsArmed() && !m_buttons[i]->GetPassthru() )
		{
			if ( RadialMenuDebug.GetBool() )
			{
				Msg( "%f: Choosing armed button %s\n", gpGlobals->curtime, ButtonDirString( (ButtonDir)i ) );
			}
			button = m_buttons[i];
			break;
		}
	}

	if ( !button && m_armedButtonDir != NUM_BUTTON_DIRS )
	{
		if ( m_buttons[m_armedButtonDir] && m_buttons[m_armedButtonDir]->IsVisible() && m_buttons[m_armedButtonDir]->IsEnabled() )
		{
			if ( RadialMenuDebug.GetBool() )
			{
				Msg( "%f: Choosing saved armed button %s\n", gpGlobals->curtime, ButtonDirString( m_armedButtonDir ) );
			}
			button = m_buttons[m_armedButtonDir];
		}
	}

	if ( button )
	{
		KeyValues *command = button->GetCommand();
		if ( command )
		{
			const char *commandStr = command->GetString( "command", NULL );
			if ( commandStr )
			{
				button->SetChosen( true );
				SendCommand( commandStr );
			}
		}

		for ( int i=0; i<NUM_BUTTON_DIRS; ++i )
		{
			if ( !m_buttons[i] )
				continue;

			if ( m_buttons[i] == button )
				continue;

			m_buttons[i]->SetFakeArmed( false );
			m_buttons[i]->SetChosen( false );
		}
	}
}

void CRadialMenu::SetData( KeyValues *data )
{
	if ( !data )
		return;

	if ( RadialMenuDebug.GetBool() )
	{
		m_resource->deleteThis();
		m_resource = new KeyValues( "RadialMenu" );
		m_resource->LoadFromFile( filesystem, "resource/UI/RadialMenu.res" );
	}

	if ( m_menuData != data )
	{
		if ( m_menuData )
		{
			m_menuData->deleteThis();
		}

		m_menuData = data->MakeCopy();
	}

	for ( int i=0; i<NUM_BUTTON_DIRS; ++i )
	{
		if ( !m_buttons[i] )
			continue;

		ButtonDir dir = (ButtonDir)i;
		const char *buttonName = ButtonNameFromDir( dir );
		KeyValues *buttonData = data->FindKey( buttonName, false );
		if ( !buttonData )
		{
			m_buttons[i]->SetVisible( false );
			continue;
		}

		KeyValues *resourceControl = m_resource->FindKey( buttonName, false );
		if ( resourceControl )
		{
			m_buttons[i]->UpdateHotspots( resourceControl );
			m_buttons[i]->InvalidateLayout();
		}

		m_buttons[i]->SetVisible( true );
		m_buttons[i]->SetChosen( false );

		const char *text = buttonData->GetString( "text" );
		m_buttons[i]->SetText( text );

		const char *command = buttonData->GetString( "command" );
		m_buttons[i]->SetCommand( command );

		m_buttons[i]->ShowSubmenuIndicator( Q_strncasecmp( "radialmenu ", command, Q_strlen( "radialmenu " ) ) == 0 );

		const char *owner = buttonData->GetString( "owner", NULL );
		if ( owner )
		{
			ButtonDir dir = DirFromButtonName( owner );
			m_buttons[i]->SetPassthru( m_buttons[dir] );
		}
		else
		{
			m_buttons[i]->SetPassthru( NULL );
		}

		m_buttons[i]->SetArmed( false );
	}
}


void CRadialMenu::ApplySchemeSettings(vgui::IScheme *scheme){HandleControlSettings();BaseClass::ApplySchemeSettings(scheme);SetBgColor(Color(0,0,0,0));m_lineColor=scheme->GetColor("Rosetta.LineColor",Color(192,192,192,128));SetCursor(vgui::dc_blank);if(m_menuData)SetData(m_menuData);}
void CRadialMenu::ShowPanel(bool show)
{
 if(show)s_pControllerInputOverride=this;else if(s_pControllerInputOverride==this)s_pControllerInputOverride=NULL;
 m_viewport->ShowBackGround(show);
 if(show){for(int i=0;i<NUM_BUTTON_DIRS;++i)if(m_buttons[i]){m_buttons[i]->SetArmed(false);m_buttons[i]->SetFakeArmed(false);m_buttons[i]->SetChosen(false);}Activate();SetMouseInputEnabled(true);m_cursorX=m_cursorY=-1;}
 else{SetVisible(false);SetMouseInputEnabled(false);}SetKeyBoardInputEnabled(false);
}
void CRadialMenu::Update()
{
 m_fading=false;m_fadeStart=0;SetAlpha(255);CRadialButton *first=NULL;
 for(int i=0;i<NUM_BUTTON_DIRS;++i)if(m_buttons[i]&&m_buttons[i]->IsVisible()&&m_buttons[i]->IsEnabled()){if(first){SetMouseInputEnabled(true);return;}first=m_buttons[i];}
 m_viewport->ShowPanel(this,false);
 if(first){KeyValues *command=first->GetCommand();if(command){const char *text=command->GetString("command",NULL);if(text)engine->ClientCmd_Unrestricted(text);}}
}
void CRadialMenu::StartFade(){m_fading=true;m_fadeStart=gpGlobals->curtime;SetMouseInputEnabled(false);}
void CRadialMenu::Paint()
{
 if(m_fading){int alpha=NativeRadialInteger(((m_fadeStart+0.5f-gpGlobals->curtime)*255.0f)*2.0f);SetAlpha(alpha);if(alpha<=0){m_viewport->ShowPanel(this,false);return;}}
 int x,y,w,h;GetBounds(x,y,w,h);int cx=x+w/2,cy=y+h/2;
 float inner=cl_rosetta_line_inner_radius.GetFloat(),outer=cl_rosetta_line_outer_radius.GetFloat();
 inner=(float)((double)ScreenHeight()*(1.0/480.0)*(double)inner);
 outer=(float)((double)ScreenHeight()*(1.0/480.0)*(double)outer);
 vgui::surface()->DrawSetColor(m_lineColor);
 vgui::surface()->DrawLine(NativeRadialInteger((float)cx+inner),cy,NativeRadialInteger((float)cx+outer),cy);
 vgui::surface()->DrawLine(NativeRadialInteger((float)cx-inner),cy,NativeRadialInteger((float)cx-outer),cy);
 vgui::surface()->DrawLine(cx,NativeRadialInteger((float)cy+inner),cx,NativeRadialInteger((float)cy+outer));
 vgui::surface()->DrawLine(cx,NativeRadialInteger((float)cy-inner),cx,NativeRadialInteger((float)cy-outer));
 inner*=0.70700002f;outer*=0.70700002f;
 int innerRight=NativeRadialInteger((float)cx+inner),innerDown=NativeRadialInteger((float)cy+inner);
 int outerRight=NativeRadialInteger((float)cx+outer),outerDown=NativeRadialInteger((float)cy+outer);
 int innerLeft=NativeRadialInteger((float)cx-inner),innerUp=NativeRadialInteger((float)cy-inner);
 int outerLeft=NativeRadialInteger((float)cx-outer),outerUp=NativeRadialInteger((float)cy-outer);
 vgui::surface()->DrawLine(innerRight,innerDown,outerRight,outerDown);
 vgui::surface()->DrawLine(innerLeft,innerUp,outerLeft,outerUp);
 vgui::surface()->DrawLine(innerRight,innerUp,outerRight,outerUp);
 vgui::surface()->DrawLine(innerLeft,innerDown,outerLeft,outerDown);
 if(RadialMenuDebug.GetBool()){vgui::surface()->DrawOutlinedRect(x+m_minButtonX,y+m_minButtonY,x+m_maxButtonX,y+m_maxButtonY);vgui::surface()->DrawLine(m_cursorX-10,m_cursorY,m_cursorX+10,m_cursorY);vgui::surface()->DrawLine(m_cursorX,m_cursorY-10,m_cursorX,m_cursorY+10);}BaseClass::Paint();
}
void CRadialMenu::OnCommand(const char *command)
{
 if(RadialMenuDebug.GetBool())Msg("%f: Clicked on button with command '%s'\n",gpGlobals->curtime,command);
 if(!Q_strcmp(command,"done")){C_BasePlayer *p=C_BasePlayer::GetLocalPlayer();if(p)p->EmitSound("MouseMenu.abort");StartFade();}
 else if(!m_fading){StartFade();SendCommand(command);}BaseClass::OnCommand(command);
}
void CRadialMenu::SetArmedButtonDir(ButtonDir dir)
{
 if(dir!=NUM_BUTTON_DIRS){if((unsigned int)dir>=NUM_BUTTON_DIRS||!m_buttons[dir])return;CRadialButton *owner=m_buttons[dir]->GetPassthru();if(owner)for(int i=0;i<NUM_BUTTON_DIRS;++i)if(m_buttons[i]==owner){dir=(ButtonDir)i;break;}}
 m_armedButtonDir=dir;
 for(int i=0;i<NUM_BUTTON_DIRS;++i)if(m_buttons[i]){if(i!=dir){if(m_buttons[i]->IsFakeArmed()){m_buttons[i]->SetFakeArmed(false);m_buttons[i]->SetArmed(false);}m_buttons[i]->SetChosen(false);}}
 if(dir!=NUM_BUTTON_DIRS&&m_buttons[dir])m_buttons[dir]->SetFakeArmed(true);
}
void CRadialMenu::OnThink()
{
	if ( !IsMouseInputEnabled() ) return;
	bool armed = false;
	for ( int i = 0; i < NUM_BUTTON_DIRS; ++i )
	{
		if ( m_buttons[i] && m_buttons[i]->IsVisible() && m_buttons[i]->IsEnabled() && m_buttons[i]->IsArmed() )
		{
			SetArmedButtonDir( (ButtonDir)i );
			armed = true;
		}
	}
	int oldX = m_cursorX, oldY = m_cursorY;
	vgui::surface()->SurfaceGetCursorPos( m_cursorX, m_cursorY );
	ScreenToLocal( m_cursorX, m_cursorY );
	int width, height; GetSize( width, height );
	int centerX = width / 2, centerY = height / 2;
	float stickX = 0.0f, stickY = 0.0f;
	ISteamInput *steam = steamapicontext ? steamapicontext->SteamInput() : NULL;
	if ( steam && g_pInputSystem )
	{
		InputHandle_t controllers[STEAM_INPUT_MAX_COUNT];
		int count = steam->GetConnectedControllers( controllers );
		// Native deliberately sums the SAME slot query once per controller.
		for ( int i = 0; i < count; ++i )
		{
			float x = 0.0f, y = 0.0f;
			g_pInputSystem->GetRadialMenuStickValues( 0, x, y );
			stickX += x; stickY += y;
		}
	}
	if ( stickX != 0.0f || stickY != 0.0f )
	{
		m_cursorY = NativeRadialInteger( ( 1.0f - stickY ) * (float)centerY );
		m_cursorX = NativeRadialInteger( ( stickX + 1.0f ) * (float)centerX );
	}
	UpdateButtonBounds();
	float radius = (float)MAX( m_maxButtonX - centerX, m_maxButtonY - centerY );
	// Preserve native AND, not a repaired OR at an axis-aligned cursor.
	if ( m_cursorX != centerX && m_cursorY != centerY )
	{
		double x = (double)( m_cursorX - centerX ), y = (double)( m_cursorY - centerY );
		double distance = sqrt( x * x + y * y );
		float spilledDistance = (float)distance;
		if ( distance > (double)radius )
		{
			float ratio = radius / spilledDistance;
			m_cursorX = NativeRadialInteger( (float)x * ratio + (float)centerX );
			m_cursorY = NativeRadialInteger( (float)y * ratio + (float)centerY );
		}
		int xScreen = m_cursorX, yScreen = m_cursorY;
		LocalToScreen( xScreen, yScreen );
		if ( RadialMenuDebug.GetBool() ) Msg( "%f: Rosetta warping cursor to %d %d\n", gpGlobals->curtime, xScreen, yScreen );
		vgui::surface()->SurfaceSetCursorPos( xScreen, yScreen );
	}
	if ( m_cursorX == centerX && m_cursorY == centerY && oldX >= 0 && oldY >= 0 && ( m_cursorX != oldX || m_cursorY != oldY ) )
	{
		m_cursorX = oldX; m_cursorY = oldY; armed = false;
	}
	if ( armed )
	{
		if ( RadialMenuDebug.GetBool() && ( m_cursorX != oldX || m_cursorY != oldY ) )
		{
			const char *name = ButtonDirString( m_armedButtonDir );
			Msg( "%f: Rosetta cursor pos is %d,%d which is over %s\n", gpGlobals->curtime, m_cursorX, m_cursorY, name );
			engine->Con_NPrintf( 20, "%d %d %s", m_cursorX, m_cursorY, name );
		}
		return;
	}
	for ( int i = NUM_BUTTON_DIRS - 1; i >= 0; --i )
	{
		if ( m_buttons[i] && m_buttons[i]->IsVisible() && m_buttons[i]->IsEnabled() && m_buttons[i]->IsWithinTraverse( m_cursorX, m_cursorY, false ) )
		{
			SetArmedButtonDir( (ButtonDir)i );
			if ( RadialMenuDebug.GetBool() && ( m_cursorX != oldX || m_cursorY != oldY ) )
			{
				const char *name = ButtonDirString( m_armedButtonDir );
				Msg( "%f: Rosetta cursor pos is %d,%d which is over %s\n", gpGlobals->curtime, m_cursorX, m_cursorY, name );
				engine->Con_NPrintf( 20, "%d %d %s", m_cursorX, m_cursorY, name );
			}
			return;
		}
	}
	if ( RadialMenuDebug.GetBool() && ( m_cursorX != oldX || m_cursorY != oldY ) )
	{
		Msg( "%f: Rosetta cursor pos is %d,%d which defaults to CENTER\n", gpGlobals->curtime, m_cursorX, m_cursorY );
		engine->Con_NPrintf( 20, "%d %d CENTER", m_cursorX, m_cursorY );
	}
	m_armedButtonDir = CENTER;
}
void CRadialMenu::SendCommand(const char *command)const
{
 char buffer[256];if(Q_strstr(command,"*player")){C_TerrorPlayer *p=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer());C_BasePlayer *target=p?ToBasePlayer(p->m_hClientReviveEntity.Get()):NULL;if(!target){engine->ClientCmd_Unrestricted(command);return;}char user[32];Q_snprintf(user,sizeof(user),"%d",target->GetUserID());Q_StrSubst(command,"*player",user,buffer,sizeof(buffer),false);}
 else{int stamp=_mm_cvttss_si32(_mm_set_ss(gpGlobals->curtime*10.0f));Q_snprintf(buffer,sizeof(buffer),"%s #%d",command,stamp);}engine->ClientCmd_Unrestricted(buffer);
}
class ClientMenuManager
{
public:
 ClientMenuManager():m_menuKeys(NULL){}
 ~ClientMenuManager(){if(m_menuKeys)m_menuKeys->deleteThis();}
 void Flush(){if(m_menuKeys)m_menuKeys->deleteThis();m_menuKeys=NULL;AddMenuFile("scripts/RadialMenu.txt");}
 void AddMenuFile(const char *filename){if(!m_menuKeys)m_menuKeys=new KeyValues("ClientMenu");KeyValues *data=new KeyValues("ClientMenu");if(!data->LoadFromFile(filesystem,filename)){Warning("Could not load client menu %s\n",filename);data->deleteThis();return;}KeyValues *key=data->GetFirstTrueSubKey();while(key){if(!m_menuKeys->FindKey(key->GetName(),false)){data->RemoveSubKey(key);m_menuKeys->AddSubKey(key);}key=data->GetFirstTrueSubKey();}data->deleteThis();}
 KeyValues *FindMenu(const char *name){C_BasePlayer *player=C_BasePlayer::GetLocalPlayer();if(!player)return NULL;C_Team *team=player->GetTeam();if(!team)return NULL;const char *life=player->IsAlive()?"Alive":"Dead";const char *teamName=team->Get_Name();char full[256];Q_snprintf(full,sizeof(full),"%s,%s,%s",name,teamName,life);KeyValues *key=m_menuKeys->FindKey(full,false);if(!key){Q_snprintf(full,sizeof(full),"%s,%s",name,teamName);key=m_menuKeys->FindKey(full,false);}if(!key){Q_snprintf(full,sizeof(full),"%s,%s",name,life);key=m_menuKeys->FindKey(full,false);}return key?key:m_menuKeys->FindKey(name,false);}
private:KeyValues *m_menuKeys;
};
static ClientMenuManager TheClientMenuManager;
void FlushClientMenus(){TheClientMenuManager.Flush();}
static CRadialMenu *FindRadialMenu(){return gViewPortInterface?dynamic_cast<CRadialMenu*>(gViewPortInterface->FindPanelByName("radialmenu")):NULL;}
bool IsRadialMenuOpen(const char *name,bool includeFadingOut){CRadialMenu *menu=FindRadialMenu();return menu&&(!name||!Q_strcmp(s_radialMenuName,name))&&menu->IsVisible()&&(!menu->IsFading()||includeFadingOut);}
static void CenterRadialCursor(CRadialMenu *menu){int w,h;menu->GetSize(w,h);w/=2;h/=2;menu->LocalToScreen(w,h);vgui::surface()->SurfaceSetCursorPos(w,h);}
void OpenRadialMenu(const char *name)
{
 if(!name)return;ClientModeTerrorNormal *mode=static_cast<ClientModeTerrorNormal*>(GetClientMode());if(mode&&mode->AreEndOfRoundStatsRequested())return;CRadialMenu *menu=FindRadialMenu();if(!menu||IsRadialMenuOpen(name,false))return;if(RadialMenuDebug.GetBool())FlushClientMenus();KeyValues *data=TheClientMenuManager.FindMenu(name);if(!data){gViewPortInterface->ShowPanel(menu,false);return;}Q_snprintf(s_radialMenuName,sizeof(s_radialMenuName),"%s",name);menu->SetData(data);CenterRadialCursor(menu);gViewPortInterface->ShowPanel(menu,true);
}
void CloseRadialMenu(const char *name,bool sendCommand){if(Q_strcmp(s_radialMenuName,name))return;s_mouseMenuKeyHeld=false;CRadialMenu *menu=FindRadialMenu();if(!menu)return;if(menu->IsVisible()&&!menu->IsFading()){if(sendCommand)menu->ChooseArmedButton();else menu->StartFade();CenterRadialCursor(menu);}else if(!menu->IsVisible())gViewPortInterface->ShowPanel(menu,false);s_radialMenuName[0]=0;}
static void OpenRadialCommand(const CCommand &args){if(args.ArgC()>=2)OpenRadialMenu(args[1]);}
static void HoldRadialCommand(const CCommand &args){if(s_mouseMenuKeyHeld)return;s_mouseMenuKeyHeld=true;if(args.ArgC()>=2)OpenRadialMenu(args[1]);}
static void ReleaseRadialCommand(){s_mouseMenuKeyHeld=false;if(!cl_fastradial.GetBool())return;CRadialMenu *menu=FindRadialMenu();if(!menu)return;if(menu->IsVisible()&&!menu->IsFading()){menu->ChooseArmedButton();CenterRadialCursor(menu);}else if(!menu->IsVisible())gViewPortInterface->ShowPanel(menu,false);s_radialMenuName[0]=0;}
static ConCommand radialmenu( "radialmenu", OpenRadialCommand, "Opens a radial menu", 0x8 );
static ConCommand mouse_menu_open( "+mouse_menu", HoldRadialCommand, "Opens a menu while held", 0x8 );
static ConCommand mouse_menu_close( "-mouse_menu", ReleaseRadialCommand, "Executes the highlighted button on the radial menu (if cl_fastradial is 1)", 0x8 );
static ConCommand reload_rosetta( "cl_engine_reload_rosetta", FlushClientMenus, "", 0x8 );
#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
