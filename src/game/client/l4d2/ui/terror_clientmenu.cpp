//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_clientmenu.h"
#include "menu.h"
#include "hud.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_team.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/player/c_transitioning_player.h"
#include "l4d2/ui/terror_client_panels.h"
#include "filesystem.h"
#include "tier1/utlvector.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"
static ConVar cl_menuduration( "cl_menuduration", "10", 0x00000088, "Client menus will hide after this many seconds (-1 disables)\n" );
static KeyValues *s_clientMenu;
static char s_menuName[128], s_menuFilename[MAX_PATH], s_commands[10][1024];
class ClientMenuOptions : public CHudMenu::CHudMenuOptions
{
public:
	virtual void OnBeforePaint() OVERRIDE {} // Actual native nullsub_571 at C2497C0.
	virtual void ProcessLine( int index, CHudMenu::ProcessedLine &line, const wchar_t *text, int selectedItem, unsigned char alpha ) OVERRIDE
	{
		if ( line.customColor ) { line.color[3] = alpha; return; }
		const wchar_t *start = text + line.startchar;
		if ( !wcsncmp( L">>", start, 2 ) ) { line.customColor=true; line.color=Color(255,255,255,alpha); line.startchar+=2; line.length-=2; }
		else if ( !wcsncmp( L"//", start, 2 ) ) { line.customColor=true; line.color=Color(127,127,127,alpha); line.startchar+=2; line.length-=2; }
	}
};
static const char *MenuCvarString( ConVar *variable ) { return variable->IsFlagSet( FCVAR_NEVER_AS_STRING ) ? "FCVAR_NEVER_AS_STRING" : variable->GetString(); }
static void ExpandMenuCvar( const char *source, char *output, int bytes )
{
	Q_strncpy( output, source, bytes );
	char *begin = Q_strstr( output, "<cvar " ); if ( !begin ) return;
	char *end = Q_strstr( begin, ">" ); if ( !end ) return;
	char expression[128]; const int length = int(end-(begin+6));
	if ( length < 0 ) return;
	Q_strncpy( expression, begin+6, MIN( int(sizeof(expression)), length+1 ) );
	CUtlVector<char *> words; V_SplitString( expression, " ", words );
	if ( words.Count() )
	{
		ConVar *variable = cvar->FindVar( words[0] );
		if ( variable )
		{
			const char *value = MenuCvarString( variable );
			if ( words.Count() > 2 ) for ( int i=1; i<words.Count()-1; i+=2 ) if ( !Q_stricmp( value,words[i] ) ) { value=words[i+1]; break; }
			char original[1024]; Q_strncpy( original,output,sizeof(original) ); const int suffix=int(end-output)+1, prefix=int(begin-output);
			Q_strncpy( output,original,MIN(bytes,prefix+1) ); Q_strncat(output,value,bytes); Q_strncat(output,original+suffix,bytes);
		}
	}
	words.PurgeAndDeleteElements();
}
static const char *MenuSurvivorName( int ordinal, bool includeTransitioning )
{
	int count=0;
	if ( g_PR ) for ( int i=1; i<=32; ++i ) if ( g_PR->GetTeam(i)==2 && g_PR->IsConnected(i) && ++count==ordinal ) return g_PR->GetPlayerName(i);
	if ( includeTransitioning )
	{
		const CUtlVector<C_TransitioningPlayer *> &players=GetTransitioningPlayers();
		FOR_EACH_VEC(players,i) { C_TransitioningPlayer *player=players[i]; if ( player && player->GetTeamNumber()==2 && !player->IsDormant() && ++count==ordinal ) return player->GetPlayerName(); }
	}
	return ""; // Native zero-initialized eight-byte process-global empty string107B94D8.
}
static void ExpandMenuText( const char *source, char *output, int bytes )
{
	const char *marker=Q_strstr(source,"*Survivor"), *tag="*Survivor"; bool transitioning=true;
	if ( !marker ) { tag="*ConnectedSurvivor"; marker=Q_strstr(source,tag); transitioning=false; }
	if ( !marker ) { ExpandMenuCvar(source,output,bytes); return; }
	const int prefix=int(marker-source), tagLength=Q_strlen(tag);
	Q_strncpy(output,source,MIN(bytes,prefix+1));
	const char digit=marker[tagLength];
	char number[2]={digit,0}; Q_strncat(output,MenuSurvivorName(atoi(number),transitioning),bytes);
	// Native consumes exactly one character, not a multi-digit ordinal.
	if ( digit ) Q_strncat(output,marker+tagLength+1,bytes);
}
static bool IsClientMenuEntryAllowed( KeyValues *entry )
{
	KeyValues *ignore=entry->FindKey("ignorevar");
	if ( ignore ) { ConVar *variable=cvar->FindVar(ignore->GetString("cvar","")); if ( variable && !Q_stricmp(MenuCvarString(variable),ignore->GetString("value","")) ) return false; }
	KeyValues *mission=NULL, *chapter=TerrorClient_GetTransitionChapterInfo(false,&mission);
	const char *missionName=mission ? mission->GetString("Name",NULL) : NULL;
	const char *ignoreMission=entry->GetString("ignoremission",NULL);
	if ( ignoreMission && missionName && !Q_stricmp(missionName,ignoreMission) ) return false;
	C_TerrorPlayerResource *resource=dynamic_cast<C_TerrorPlayerResource *>(g_PR);
	if ( entry->GetInt("MissionStartOnly") && ( !resource || (chapter && chapter->GetInt("chapter")>1) || resource->HasAnySurvivorLeftSafeArea() ) ) return false;
	return true;
}
static void ShowClientMenuCommand( const CCommand &args )
{
	if ( args.ArgC()<2 ) return;
	C_TerrorPlayer *local=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer()); CHudMenu *hud=GET_HUDELEMENT(CHudMenu);
	if ( !local || !hud ) return;
	if ( s_clientMenu ) { s_clientMenu->deleteThis(); s_clientMenu=NULL; }
	Q_strncpy(s_menuFilename,"scripts/ClientMenu.txt",sizeof(s_menuFilename));
	s_clientMenu=new KeyValues("ClientMenu");
	if ( !s_clientMenu->LoadFromFile(filesystem,s_menuFilename,NULL) ) return;
	const char *team=local->GetTeam() ? local->GetTeam()->Get_Name() : NULL;
	if ( !team ) return; // Native NULL team-name formatting is undefined.
	const char *life=local->IsAlive() ? "Alive" : "Dead";
	char section[1024]; Q_snprintf(section,sizeof(section),"%s,%s,%s",args[1],team,life); KeyValues *menu=s_clientMenu->FindKey(section);
	if ( !menu ) { Q_snprintf(section,sizeof(section),"%s,%s",args[1],team); menu=s_clientMenu->FindKey(section); }
	if ( !menu ) { Q_snprintf(section,sizeof(section),"%s,%s",args[1],life); menu=s_clientMenu->FindKey(section); }
	if ( !menu ) { Q_strncpy(section,args[1],sizeof(section)); menu=s_clientMenu->FindKey(section); }
	if ( !menu ) { s_clientMenu->deleteThis(); s_clientMenu=NULL; return; }
	Q_strncpy(s_menuName,section,sizeof(s_menuName));
	char expanded[1024], text[1024]; ExpandMenuCvar(menu->GetString("Title",""),expanded,sizeof(expanded)); Q_snprintf(text,sizeof(text),">>%s\n\n",expanded);
	for ( int i=0; i<10; ++i ) s_commands[i][0]=0;
	int slots=0; const char *single=NULL;
	for ( int index=1; index<=10; ++index )
	{
		const int slot=index>9 ? 0 : index; char key[8]; Q_snprintf(key,sizeof(key),"%d",slot); KeyValues *entry=menu->FindKey(key); if ( !entry ) continue;
		const bool allowed=IsClientMenuEntryAllowed(entry);
		if ( slot==0 ) Q_strncat(text,"\n\n",sizeof(text)); Q_strncat(text,"\n",sizeof(text));
		const int x=entry->GetInt("hotspotx",-1), y=entry->GetInt("hotspoty",-1);
		if ( x!=-1 && y!=-1 )
		{
			const float scale=float(ScreenWidth())*0.0015625f+10.0f; char position[64]; Q_snprintf(position,sizeof(position),"[%d,%d]",_mm_cvttss_si32(_mm_set_ss(float(x)*scale)),_mm_cvttss_si32(_mm_set_ss(-(float(y)*scale)))); Q_strncat(text,position,sizeof(text));
		}
		if ( !allowed ) Q_strncat(text,"//",sizeof(text));
		if ( menu->GetInt("Labels",1)==1 ) { char label[16]; Q_snprintf(label,sizeof(label),"%d. ",slot); Q_strncat(text,label,sizeof(text)); }
		const char *label=entry->GetString("label",""); if ( !allowed ) label=entry->GetString("invalidLabel",label);
		ExpandMenuText(label,expanded,sizeof(expanded)); Q_strncat(text,expanded,sizeof(text));
		if ( allowed ) ExpandMenuText(entry->GetString("command",""),s_commands[index-1],sizeof(s_commands[index-1]));
		if ( slot==0 ) Q_strncat(text,"\n",sizeof(text));
		// Original single-entry shortcut does NOT test admission. Do not repair it.
		if ( single || slots ) single=NULL; else single=entry->GetString("command","");
		slots|=1<<(index-1);
	}
	if ( single ) { engine->ClientCmd_Unrestricted(single); return; }
	if ( !slots ) return;
	float duration=menu->GetFloat("Duration",cl_menuduration.GetFloat());
	const char *durationVariable=menu->GetString("DurationConvar",""); if ( *durationVariable ) { ConVar *variable=cvar->FindVar(durationVariable); if ( variable ) duration=variable->GetFloat(); }
	ClientMenuOptions *options=new ClientMenuOptions; options->m_center=menu->GetInt("Center")!=0;
	hud->ShowMenu(text,slots,duration,options);
}
void TerrorClientMenuSelect( const CCommand &args )
{
	if ( args.ArgC()<2 ) return;
	if ( *s_menuName && *s_menuFilename )
	{
		const int slot=atoi(args[1]);
		if ( slot!=0 )
		{
			if ( !s_clientMenu || !s_clientMenu->FindKey(s_menuName) )
			{
				if ( s_clientMenu ) { s_clientMenu->deleteThis(); s_clientMenu=NULL; } s_menuName[0]=0;
				char command[1024]; Q_snprintf(command,sizeof(command),"menuselect %s",args[1]); engine->ServerCmd(command,true); return;
			}
			if ( slot!=INT_MIN ) // Skip only the native undefined signed subtraction/action read.
			{
				int index=slot-1;
				if ( index<0 ) index=9; // Native negative selection maps to the tenth cached command.
				if ( index<10 && *s_commands[index] ) engine->ClientCmd_Unrestricted(s_commands[index]);
			}
		}
		s_menuName[0]=0; return;
	}
	char command[1024]; Q_snprintf(command,sizeof(command),"menuselect %s",args[1]); engine->ServerCmd(command,true);
}
static ConCommand show_menu( "show_menu", ShowClientMenuCommand, "Opens a menu", 0x00000008 );
static ConCommand menu_select( "menuselect", TerrorClientMenuSelect, "menuselect", 0x40000008 );
