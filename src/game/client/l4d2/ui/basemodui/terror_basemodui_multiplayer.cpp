//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier1/fmtstr.h"
#include "vgui_controls/ImagePanel.h"
#include "tier0/memdbgon.h"
namespace L4D360UI
{
static const OptionChoice_t s_Multiplayer[] = {
	{ "DrpLanGamesDiscovery","MpLanGamesEnabled","net_allow_multicast","1" }, { "DrpLanGamesDiscovery","MpLanGamesDisabled","net_allow_multicast","0" },
	{ "DrpAllowCustomContent","#GameUI_DownloadFilter_all","cl_downloadfilter","all" }, { "DrpAllowCustomContent","#GameUI_DownloadFilter_nosounds","cl_downloadfilter","nosounds" }, { "DrpAllowCustomContent","#GameUI_DownloadFilter_none","cl_downloadfilter","none" },
	{ "DrpAllowPlayerSprays","MpPlayerSpraysEnabled","cl_playerspraydisable","0" }, { "DrpAllowPlayerSprays","MpPlayerSpraysDisabled","cl_playerspraydisable","1" },
	{ "DrpColorBlind","ColorBlind0","cl_colorblind","0" }, { "DrpColorBlind","ColorBlind1","cl_colorblind","1" },
	{ "DrpGameInstructor","GameInstructorEnabled","gameinstructor_enable","1" }, { "DrpGameInstructor","GameInstructorDisabled","gameinstructor_enable","0" },
	{ "DrpAllowFreeLook","AllowFreeLookEnabled","spec_allowroaming","1" }, { "DrpAllowFreeLook","AllowFreeLookDisabled","spec_allowroaming","0" },
	{ "DrpGore","#L4D360UI_Gore_High","z_wound_client_disabled","0" }, { "DrpGore","#L4D360UI_Gore_Low","z_wound_client_disabled","1" }
};
static void ShowSprayPreview(vgui::Panel *panel,const char *file)
{
	vgui::ImagePanel *image=dynamic_cast<vgui::ImagePanel *>(panel); if ( !image || !file || !*file ) return;
	char name[MAX_PATH]; V_FileBase(file,name,sizeof(name));
	const bool custom=Q_stristr(file,"/custom/")!=NULL || Q_stristr(file,"\\custom\\")!=NULL;
	// G7CF40 creates this ordinary native preview material on demand. It is not
	// an offline conversion of mounted original content or a spray-import result.
	char material[MAX_PATH]; Q_snprintf(material,sizeof(material),"materials/VGUI/logos/UI/%s.vmt",name);
	if ( !filesystem->FileExists(material) )
	{
		filesystem->CreateDirHierarchy("materials/VGUI/logos/UI","DEFAULT_WRITE_PATH");
		CUtlBuffer text(0,0,CUtlBuffer::TEXT_BUFFER);
		text.Printf("\"UnlitGeneric\"\n{\n\"$translucent\" 1\n\"$basetexture\" \"VGUI\\logos\\%s%s\"\n\"$vertexcolor\" 1\n\"$vertexalpha\" 1\n\"$no_fullbright\" 1\n\"$ignorez\" 1\n}\n",custom ? "custom/" : "",name);
		if ( !filesystem->WriteFile(material,"DEFAULT_WRITE_PATH",text) ) return;
	}
	image->SetImage(CFmtStr("logos/UI/%s",name));
}
Multiplayer::Multiplayer(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Multiplayer"), m_nLastFOV(-1) {}
void Multiplayer::PopulateSprays()
{
	DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpSpraypaint",true)); FlyoutMenu *flyout=drop ? drop->GetFlyout() : NULL; if ( !flyout ) return;
	m_Sprays.RemoveAll(); flyout->BeginDynamicRows();
	ConVar *logo=cvar->FindVar("cl_logofile"); const char *current=logo ? logo->GetString() : ""; int selected=0;
	m_CurrentLogo=current;
	KeyValues::AutoDelete manifest(new KeyValues("sprays_manifest"));
	if ( manifest->LoadFromFile(filesystem,"scripts/sprays_manifest.txt","GAME") )
	{
		for ( KeyValues *entry=manifest->GetFirstSubKey();entry && m_Sprays.Count()<16;entry=entry->GetNextKey() )
		{
			char texture[MAX_PATH],material[MAX_PATH]; Q_snprintf(texture,sizeof(texture),"materials/vgui/logos/%s",entry->GetString()); Q_strncpy(material,texture,sizeof(material)); V_SetExtension(material,".vmt",sizeof(material));
			if ( !filesystem->FileExists(texture,"GAME") || !filesystem->FileExists(material,"GAME") ) continue;
			int index=m_Sprays.AddToTail(CUtlString(texture)); if ( !Q_stricmp(texture,current) ) selected=index;
			flyout->AddDynamicRow(CFmtStr("BtnSpray%d",index),entry->GetName(),CFmtStr("_spraypaint%c",'0'+index));
		}
	}
	FileFindHandle_t find; const char *custom=filesystem->FindFirstEx("materials/vgui/logos/custom/*.vtf","GAME",&find);
	if ( custom )
	{
		char material[MAX_PATH]; Q_snprintf(material,sizeof(material),"materials/vgui/logos/custom/%s",custom); V_SetExtension(material,".vmt",sizeof(material));
		if ( filesystem->FileExists(material,"GAME") )
		{
			const bool selectedCustom=Q_stristr(current,"materials/vgui/logos/custom/")==current;
			int index=m_Sprays.AddToTail(CUtlString(selectedCustom ? current : "")); if ( selectedCustom ) selected=index;
			flyout->AddDynamicRow(CFmtStr("BtnSpray%d",index),"#L4D360UI_Multiplayer_Cutsom_Logo",CFmtStr("_spraypaint%c",'0'+index));
		}
	}
	filesystem->FindClose(find); flyout->EndDynamicRows(); flyout->SetBackgroundHeight(20*m_Sprays.Count()+5);
	if ( m_Sprays.Count() ) Select("DrpSpraypaint",CFmtStr("_spraypaint%c",'0'+selected));
	ShowSprayPreview(FindChildByName("LogoImage",true),current);
}
void Multiplayer::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/multiplayer.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); RefreshChoices(s_Multiplayer,ARRAYSIZE(s_Multiplayer)); ResetSliders(this); PopulateSprays();
	if ( ConVar *value=cvar->FindVar("cl_downloadfilter") ) Select("DrpAllowCustomContent",CFmtStr("#GameUI_DownloadFilter_%s",value->GetString()));
	if ( ConVar *value=cvar->FindVar("cl_colorblind") ) Select("DrpColorBlind",CFmtStr("ColorBlind%s",value->GetString()));
}
void Multiplayer::OnThink()
{
	BaseClass::OnThink(); if ( !IsVisible() ) return;
	if ( ConVar *logo=cvar->FindVar("cl_logofile") ) if ( Q_stricmp(logo->GetString(),m_CurrentLogo.String()) ) PopulateSprays();
	if ( ConVar *fov=cvar->FindVar("fov_desired") )
	{
		if ( m_nLastFOV!=fov->GetInt() ) { m_nLastFOV=fov->GetInt(); SetControlString("BtnFOVCurrentValue",CFmtStr("%d",m_nLastFOV)); }
	}
}
void Multiplayer::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( const char *value=StringAfterPrefix(command,"#GameUI_DownloadFilter_") ) { if ( ConVar *variable=cvar->FindVar("cl_downloadfilter") ) variable->SetValue(value); return; }
	if ( const char *value=StringAfterPrefix(command,"ColorBlind") ) { if ( ConVar *variable=cvar->FindVar("cl_colorblind") ) variable->SetValue(value); return; }
	if ( ApplyChoice(command,s_Multiplayer,ARRAYSIZE(s_Multiplayer)) ) return;
	if ( const char *suffix=StringAfterPrefix(command,"_spraypaint") )
	{
		int index=(unsigned char)*suffix-'0'; if ( !m_Sprays.IsValidIndex(index) ) return;
		if ( !m_Sprays[index].Length() || Q_stristr(m_Sprays[index].String(),"/custom/") ) { OpenNativeSprayImport(); return; }
		if ( ConVar *logo=cvar->FindVar("cl_logofile") ) { logo->SetValue(m_Sprays[index].String()); ShowSprayPreview(FindChildByName("LogoImage",true),logo->GetString()); }
		return;
	}
	if ( !Q_stricmp(command,"ImportSprayImage") ) { OpenNativeSprayImport(); return; }
	BaseClass::OnCommand(command);
}
Options::Options(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Options")
{
	// G6BF00 allocates these native buttons. The shipped PC mounts do not
	// contain Options.res; do not manufacture a replacement resource tree.
	new L4D360HybridButton(this,"BtnGame","#L4D360UI_Game",this,"Game");
	new L4D360HybridButton(this,"BtnAudioVideo","#L4D360UI_AudioVideo",this,"AudioVideo");
	new L4D360HybridButton(this,"BtnController","#L4D360UI_Controller",this,"Controller");
	new L4D360HybridButton(this,"BtnStorage","#L4D360UI_Storage",this,"Storage");
	new L4D360HybridButton(this,"BtnCredits","#L4D360UI_Credits",this,"Credits");
}
void Options::OnOpen()
{
	BaseClass::OnOpen();
	// Storage chooser has no target profile/storage service. Native PC entry
	// uses OptionsFlyout instead of this console-oriented window.
	SetControlEnabled("BtnStorage",false,true);
	if ( vgui::Panel *first=FindChildByName("BtnGame") ) first->NavigateTo();
}
void Options::OnCommand(const char *command)
{
	if ( !Q_stricmp(command,"Game") || !Q_stricmp(command,"AudioVideo") || !Q_stricmp(command,"Controller") ) { OpenChild(command); return; }
	BaseClass::OnCommand(command);
}
}
