//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "cdll_client_int.h"
#include "IGameUIFuncs.h"
#include "modes.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/materialsystem_config.h"
#include "tier1/fmtstr.h"
#include "tier0/platform.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Button.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "tier0/memdbgon.h"
namespace L4D360UI
{
static int VideoInt(const char *name) { ConVar *variable=cvar->FindVar(name); return variable ? variable->GetInt() : 0; }
static float VideoFloat(const char *name) { ConVar *variable=cvar->FindVar(name); return variable ? variable->GetFloat() : 0.0f; }
static void VideoSet(const char *name,int value) { if ( ConVar *variable=cvar->FindVar(name) ) variable->SetValue(value); }
static int VideoAspect(int width,int height)
{
	const double ratio=(double)width/height;
	static const struct { int aspect; float ratio; } ratios[]={{0,4.0f/3.0f},{1,16.0f/9.0f},{2,16.0f/10.0f},{2,1.0f}};
	int best=0; float closest=99999.0f;
	for ( int i=0;i<ARRAYSIZE(ratios);++i ) { double distance=fabs(ratios[i].ratio-ratio); if ( closest>distance ) { best=ratios[i].aspect; closest=(float)distance; } }
	return best;
}
static void PagedPoolOpened(DropDownMenu *drop,FlyoutMenu *)
{
	if ( drop && drop->GetParent() ) drop->GetParent()->OnCommand("PagedPoolWarning");
}
Video::Video(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#GameUI_Video"), m_nAspect(0),m_nWidth(640),m_nHeight(480),m_nGPU(0),m_nGPUMem(0),m_nCPU(0),m_nMem(0),m_nAA(0),m_nAniso(0),m_nQueue(0),m_flOldGamma(VideoFloat("mat_monitorgamma")),m_flOldGrain(VideoFloat("mat_grain_scale_override")),m_bWindowed(false),m_bBorderless(false),m_bVSync(false),m_bTriple(false),m_bDirty(false),m_bCancelled(false),m_bAdvanced(false) {}
void Video::LoadControlSettings( const char *resource, const char *pathID, KeyValues *preloaded, KeyValues *conditions )
{
	KeyValues::AutoDelete data( preloaded ? preloaded->MakeCopy() : new KeyValues( "" ) );
	if ( !preloaded && !LoadTerrorUiResource( data, resource ) ) return;
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_%language%.txt", NULL, false );
	if ( KeyValues *advanced = data->FindKey( "BtnAdvanced" ) )
	{
		KeyValues *graphics = advanced->MakeCopy();
		graphics->SetName( "BtnGraphicsDX12" );
		graphics->SetString( "fieldName", "BtnGraphicsDX12" );
		graphics->SetString( "labelText", "#GraphicsOptions_DX12Entry" );
		graphics->SetString( "tooltiptext", "#GraphicsOptions_Title" );
		graphics->SetString( "command", "GraphicsDX12" );
		graphics->SetInt( "ypos", advanced->GetInt( "ypos" ) + 20 );
		data->AddSubKey( graphics );
		// Leave the native advanced rows intact, with one additional row before
		// Recommended/Cancel/Done when that list is expanded.
		const char *lower[] = { "Video", "ImgBackground", "BtnUseRecommended", "BtnCancel", "BtnDone", "Btn3rdPartyCredits" };
		for ( int i = 0; i < ARRAYSIZE( lower ); ++i )
		{
			KeyValues *control = data->FindKey( lower[i] );
			if ( control ) control->SetInt( i < 2 ? "tall" : "ypos", control->GetInt( i < 2 ? "tall" : "ypos" ) + 20 );
		}
	}
	BaseClass::LoadControlSettings( resource, pathID, data, conditions );
}
void Video::OnOpen()
{
	if ( !LoadLayout( "resource/ui/l4d360ui/video.res" ) )
	{
		NavigateBack();
		return;
	}
	BaseClass::OnOpen();
	if ( m_hAspectRatio.Get() && !m_bDirty )
	{
		ReadSettings();
		Refresh();
		m_bDirty = false;
	}
	CenterDialogOnScreen();
}
void Video::OnThink()
{
	BaseClass::OnThink();

	// G73F70 initializes after binding the resource children. The SDK scheme
	// load replaces the whole tree, so a weak child handle re-arms that bind.
	if ( m_hAspectRatio.Get() )
		return;
	m_hAspectRatio = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpAspectRatio", true ) );
	if ( !m_hAspectRatio.Get() )
		return;

	ReadSettings();
	Refresh();
	if ( DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpPagedPoolMem", true ) ) )
	{
		drop->SetFlyoutOpenCallback( PagedPoolOpened );
	}

	// G74F60 selects the aspect control only for a newly bound control tree.
	if ( m_hLastFocus.Get() )
		m_hLastFocus->NavigateFrom();
	m_hAspectRatio->NavigateTo();
	m_hLastFocus = m_hAspectRatio.Get();
}
void Video::ReadSettings(bool recommended)
{
	int samples,quality;
	if ( recommended )
	{
		// Read actual target material-system recommendations, never a fabricated
		// hardware quality result or a copy of the x86 video-config ABI.
		KeyValues::AutoDelete data(new KeyValues("VideoConfig"));
		if ( !materials->GetRecommendedConfigurationInfo(0,data) ) return;
		m_nWidth=data->GetInt("setting.defaultres",640); m_nHeight=data->GetInt("setting.defaultresheight",480);
		if ( m_nWidth<640 || m_nHeight<480 ) { m_nWidth=640; m_nHeight=480; }
		m_bWindowed=!data->GetInt("setting.fullscreen",1); m_bBorderless=data->GetInt("setting.nowindowborder")!=0;
		m_nGPUMem=data->GetInt("setting.gpu_mem_level"); m_nMem=data->GetInt("setting.mem_level");
		m_nGPU=data->GetInt("setting.gpu_level"); m_nCPU=data->GetInt("setting.cpu_level"); m_nAniso=data->GetInt("setting.mat_forceaniso",1);
		m_bVSync=data->GetInt("setting.mat_vsync",1)!=0; m_bTriple=data->GetInt("setting.mat_triplebuffered")!=0; m_nQueue=data->GetInt("setting.mat_queue_mode",-1);
		samples=data->GetInt("setting.mat_antialias"); quality=data->GetInt("setting.mat_aaquality");
		if ( SliderControl *grain=dynamic_cast<SliderControl *>(FindChildByName("SldFilmGrain",true)) ) grain->SetValue(data->GetFloat("setting.mat_grain_scale_override",1.0f));
		m_bDirty=true;
	}
	else
	{
		const MaterialSystem_Config_t &config=materials->GetCurrentConfigForVideoCard();
		m_nWidth=config.m_VideoMode.m_Width; m_nHeight=config.m_VideoMode.m_Height; m_bWindowed=config.Windowed(); m_bBorderless=config.NoWindowBorder();
		m_nGPUMem=clamp(VideoInt("gpu_mem_level"),0,2); m_nMem=clamp(VideoInt("mem_level"),0,2); m_nGPU=clamp(VideoInt("gpu_level"),0,3); m_nCPU=clamp(VideoInt("cpu_level"),0,2);
		m_nAniso=VideoInt("mat_forceaniso"); m_bVSync=VideoInt("mat_vsync")!=0; m_bTriple=VideoInt("mat_triplebuffered")!=0; m_nQueue=VideoInt("mat_queue_mode");
		samples=VideoInt("mat_antialias"); quality=VideoInt("mat_aaquality");
	}
	m_nAspect=VideoAspect(m_nWidth,m_nHeight); PopulateAA(); m_nAA=0;
	for ( int i=0;i<m_AAModes.Count();++i ) if ( m_AAModes[i].samples==samples && m_AAModes[i].quality==quality ) { m_nAA=i; break; }
}
void Video::PopulateAA()
{
	DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpAntialias",true)); FlyoutMenu *flyout=drop ? drop->GetFlyout() : NULL;
	m_AAModes.RemoveAll(); if ( !flyout ) return; flyout->BeginDynamicRows();
	static const struct { int samples,quality; const char *label; } modes[]={
		{1,0,"#GameUI_None"},{2,0,"#GameUI_2X"},{4,0,"#GameUI_4X"},{6,0,"#GameUI_6X"},{4,2,"#GameUI_8X_CSAA"},{4,4,"#GameUI_16X_CSAA"},{8,0,"#GameUI_8X"},{8,2,"#GameUI_16XQ_CSAA"}
	};
	for ( int i=0;i<ARRAYSIZE(modes);++i )
	{
		if ( i && !(modes[i].quality ? materials->SupportsCSAAMode(modes[i].samples,modes[i].quality) : materials->SupportsMSAAMode(modes[i].samples)) ) continue;
		AAMode_t aa={modes[i].samples,modes[i].quality}; int index=m_AAModes.AddToTail(aa);
		flyout->AddDynamicRow(CFmtStr("BtnAA%d",index),modes[i].label,CFmtStr("_antialias%c",'0'+index));
	}
	flyout->EndDynamicRows(); flyout->SetBackgroundHeight(20*m_AAModes.Count()+5);
}
void Video::PopulateModes()
{
	DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpResolution",true)); FlyoutMenu *flyout=drop ? drop->GetFlyout() : NULL;
	m_Modes.RemoveAll(); if ( !flyout ) return;
	vmode_t *modes=NULL; int count=0,desktopWidth=0,desktopHeight=0;
	gameuifuncs->GetVideoModes(&modes,&count); gameuifuncs->GetDesktopResolution(desktopWidth,desktopHeight);
	CUtlVector<vmode_t> merged;
	if ( m_bWindowed )
	{
		// G75A60 merges the engine's ordered modes with these four real rows.
		// The following native 0x7f7fffff record is a sentinel, never a mode.
		static const vmode_t windowed[]={{640,480,32,60},{852,480,32,60},{1280,720,32,60},{1920,1080,32,60}};
		merged.EnsureCapacity(count+ARRAYSIZE(windowed));
		int next=0;
		for ( int i=0;i<ARRAYSIZE(windowed);++i )
		{
			while ( next<count && (modes[next].width<windowed[i].width || (modes[next].width==windowed[i].width && modes[next].height<=windowed[i].height)) )
			{
				if ( modes[next].width==windowed[i].width && modes[next].height==windowed[i].height ) { ++next; break; }
				merged.AddToTail(modes[next++]);
			}
			merged.AddToTail(windowed[i]);
		}
		while ( next<count ) merged.AddToTail(modes[next++]);
		modes=merged.Base(); count=merged.Count();
	}
	DropDownMenu *aspectDrop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpAspectRatio",true)); FlyoutMenu *aspects=aspectDrop ? aspectDrop->GetFlyout() : NULL;
	int retire=1;
	for ( int i=0;i<count;++i )
	{
		const vmode_t &mode=modes[i];
		if ( m_bWindowed && (mode.width>desktopWidth || mode.height>desktopHeight) ) continue;
		bool duplicate=false; for ( int j=0;j<m_Modes.Count();++j ) if ( m_Modes[j].width==mode.width && m_Modes[j].height==mode.height ) { duplicate=true; break; }
		if ( duplicate ) continue;
		const int aspect=VideoAspect(mode.width,mode.height);
		if ( aspect>0 && aspects ) if ( vgui::Button *button=aspects->FindChildButtonByCommand(aspect==1 ? "#GameUI_AspectWide16x9" : "#GameUI_AspectWide16x10") ) button->SetEnabled(true);
		if ( aspect!=m_nAspect ) continue;
		if ( m_Modes.Count()==15 ) { if ( retire>=15 ) retire=1; m_Modes.Remove(retire); retire+=2; }
		Mode_t entry={mode.width,mode.height}; m_Modes.AddToTail(entry);
	}
	flyout->BeginDynamicRows(); int selected=-1;
	for ( int i=0;i<m_Modes.Count();++i )
	{
		if ( m_Modes[i].width==m_nWidth && m_Modes[i].height==m_nHeight ) selected=i;
		const char *nativeSuffix=!m_bWindowed && m_Modes[i].width==desktopWidth && m_Modes[i].height==desktopHeight ? " (native)" : "";
		flyout->AddDynamicRow(CFmtStr("BtnResolution%d",i),CFmtStr("%i x %i%s",m_Modes[i].width,m_Modes[i].height,nativeSuffix),CFmtStr("_res%c",'0'+i));
	}
	flyout->EndDynamicRows(); flyout->SetBackgroundHeight(20*m_Modes.Count()+5);
	if ( selected<0 && m_Modes.Count() )
	{
		int next=0;
		for ( ;next<m_Modes.Count();++next ) if ( m_Modes[next].height>m_nHeight || (m_Modes[next].height==m_nHeight && m_Modes[next].width>m_nWidth) ) break;
		selected=MAX(next-1,0);
	}
	if ( selected>=0 ) { SelectOption("DrpResolution",CFmtStr("_res%c",'0'+selected)); m_nWidth=m_Modes[selected].width; m_nHeight=m_Modes[selected].height; }
}
void Video::SelectOption( const char *control, const char *command )
{
	DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName(control,true)); if ( !drop ) return;
	// G2D180 calls the parent for initial selections too. Suppress only its
	// reentrant UI callback, then perform that same native staging once.
	const bool refreshing=m_bRefreshing; m_bRefreshing=true;
	drop->SetCurrentSelection(command); m_bRefreshing=refreshing;
	StageCommand(command);
}
bool Video::StageCommand( const char *command )
{
	if ( !Q_stricmp(command,"#GameUI_AspectNormal") ) m_nAspect=0;
	else if ( !Q_stricmp(command,"#GameUI_AspectWide16x9") ) m_nAspect=1;
	else if ( !Q_stricmp(command,"#GameUI_AspectWide16x10") ) m_nAspect=2;
	else if ( const char *suffix=StringAfterPrefix(command,"_res") ) { int index=(unsigned char)*suffix-'0'; if ( !m_Modes.IsValidIndex(index) ) return false; m_nWidth=m_Modes[index].width; m_nHeight=m_Modes[index].height; }
	else if ( const char *suffix=StringAfterPrefix(command,"_antialias") ) { int index=(unsigned char)*suffix-'0'; if ( !m_AAModes.IsValidIndex(index) ) return false; m_nAA=index; }
	else if ( !Q_stricmp(command,"#GameUI_Windowed") ) { m_bWindowed=true; m_bBorderless=false; }
	else if ( !Q_stricmp(command,"#L4D360UI_VideoOptions_Windowed_NoBorder") ) { m_bWindowed=true; m_bBorderless=true; }
	else if ( !Q_strnicmp(command,"#GameUI_Fullscreen",18) ) { m_bWindowed=false; m_bBorderless=false; }
	else if ( !Q_stricmp(command,"ModelDetailHigh") ) m_nGPUMem=2;
	else if ( !Q_stricmp(command,"ModelDetailMedium") ) m_nGPUMem=1;
	else if ( !Q_stricmp(command,"ModelDetailLow") ) m_nGPUMem=0;
	else if ( !Q_stricmp(command,"PagedPoolMemHigh") ) m_nMem=2;
	else if ( !Q_stricmp(command,"PagedPoolMemMedium") ) m_nMem=1;
	else if ( !Q_stricmp(command,"PagedPoolMemLow") ) m_nMem=0;
	else if ( !Q_stricmp(command,"#GameUI_Bilinear") ) m_nAniso=0;
	else if ( !Q_stricmp(command,"#GameUI_Trilinear") ) m_nAniso=1;
	else if ( !Q_stricmp(command,"#GameUI_Anisotropic2X") ) m_nAniso=2;
	else if ( !Q_stricmp(command,"#GameUI_Anisotropic4X") ) m_nAniso=4;
	else if ( !Q_stricmp(command,"#GameUI_Anisotropic8X") ) m_nAniso=8;
	else if ( !Q_stricmp(command,"#GameUI_Anisotropic16X") ) m_nAniso=16;
	else if ( !Q_stricmp(command,"VSyncTripleBuffered") ) { m_bVSync=true; m_bTriple=true; }
	else if ( !Q_stricmp(command,"VSyncEnabled") ) { m_bVSync=true; m_bTriple=false; }
	else if ( !Q_stricmp(command,"VSyncDisabled") ) { m_bVSync=false; m_bTriple=false; }
	else if ( !Q_stricmp(command,"QueuedModeEnabled") ) m_nQueue=-1;
	else if ( !Q_stricmp(command,"QueuedModeDisabled") ) m_nQueue=0;
	else if ( !Q_stricmp(command,"ShaderDetailVeryHigh") ) m_nGPU=3;
	else if ( !Q_stricmp(command,"ShaderDetailHigh") ) m_nGPU=2;
	else if ( !Q_stricmp(command,"ShaderDetailMedium") ) m_nGPU=1;
	else if ( !Q_stricmp(command,"ShaderDetailLow") ) m_nGPU=0;
	else if ( !Q_stricmp(command,"CPUDetailHigh") ) m_nCPU=2;
	else if ( !Q_stricmp(command,"CPUDetailMedium") ) m_nCPU=1;
	else if ( !Q_stricmp(command,"CPUDetailLow") ) m_nCPU=0;
	else return false;
	m_bDirty=true; return true;
}
void Video::Refresh()
{
	SelectOption("DrpAspectRatio",m_nAspect==1 ? "#GameUI_AspectWide16x9" : m_nAspect==2 ? "#GameUI_AspectWide16x10" : "#GameUI_AspectNormal");
	if ( DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpDisplayMode",true)) ) if ( FlyoutMenu *flyout=drop->GetFlyout() )
	{
		flyout->SetBackgroundHeight(65);
		for ( int i=1;i<6;++i ) if ( vgui::Button *button=flyout->FindChildButtonByCommand(CFmtStr("#GameUI_Fullscreen%d",i)) ) button->SetVisible(false);
	}
	SelectOption("DrpDisplayMode",m_bWindowed ? (m_bBorderless ? "#L4D360UI_VideoOptions_Windowed_NoBorder" : "#GameUI_Windowed") : "#GameUI_Fullscreen");
	if ( m_nGPUMem>=0 && m_nGPUMem<=2 ) SelectOption("DrpModelDetail",m_nGPUMem==2 ? "ModelDetailHigh" : m_nGPUMem==1 ? "ModelDetailMedium" : "ModelDetailLow");
	if ( m_nMem>=0 && m_nMem<=2 ) SelectOption("DrpPagedPoolMem",m_nMem==2 ? "PagedPoolMemHigh" : m_nMem==1 ? "PagedPoolMemMedium" : "PagedPoolMemLow");
	SelectOption("DrpAntialias",CFmtStr("_antialias%c",'0'+m_nAA));
	const char *filter=m_nAniso==16 ? "#GameUI_Anisotropic16X" : m_nAniso==8 ? "#GameUI_Anisotropic8X" : m_nAniso==4 ? "#GameUI_Anisotropic4X" : m_nAniso==2 ? "#GameUI_Anisotropic2X" : m_nAniso==0 ? "#GameUI_Bilinear" : "#GameUI_Trilinear";
	SelectOption("DrpFiltering",filter); SelectOption("DrpVSync",!m_bVSync ? "VSyncDisabled" : m_bTriple ? "VSyncTripleBuffered" : "VSyncEnabled");
	const bool queued=GetCPUInformation()->m_nPhysicalProcessors>=2;
	SetControlEnabled("DrpQueuedMode",queued,true);
	if ( queued ) SelectOption("DrpQueuedMode",m_nQueue==0 ? "QueuedModeDisabled" : "QueuedModeEnabled");
	if ( m_nGPU>=0 && m_nGPU<=3 ) SelectOption("DrpShaderDetail",m_nGPU==3 ? "ShaderDetailVeryHigh" : m_nGPU==2 ? "ShaderDetailHigh" : m_nGPU==1 ? "ShaderDetailMedium" : "ShaderDetailLow");
	if ( m_nCPU>=0 && m_nCPU<=2 ) SelectOption("DrpCPUDetail",m_nCPU==2 ? "CPUDetailHigh" : m_nCPU==1 ? "CPUDetailMedium" : "CPUDetailLow");
	SetControlEnabled("SldBrightness",!m_bWindowed,true);
	static const char *advanced[]={"DrpModelDetail","DrpPagedPoolMem","DrpQueuedMode","DrpFiltering","DrpVSync","DrpShaderDetail","DrpCPUDetail","DrpAntialias"};
	SetControlVisible("BtnAdvanced",!m_bAdvanced,true); for ( int i=0;i<ARRAYSIZE(advanced);++i ) SetControlVisible(advanced[i],m_bAdvanced,true);
	if ( vgui::Panel *graphics = FindChildByName( "BtnGraphicsDX12", true ) )
	{
		int x, y; graphics->GetPos( x, y );
		graphics->SetPos( x, vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), m_bAdvanced ? 370 : 230 ) );
		graphics->SetNavUp( m_bAdvanced ? "DrpPagedPoolMem" : "BtnAdvanced" );
		graphics->SetNavDown( "BtnUseRecommended" );
	}
	if ( vgui::Panel *advancedButton = FindChildByName( "BtnAdvanced", true ) ) advancedButton->SetNavDown( "BtnGraphicsDX12" );
	if ( vgui::Panel *lastAdvanced = FindChildByName( "DrpPagedPoolMem", true ) ) lastAdvanced->SetNavDown( "BtnGraphicsDX12" );
	if ( vgui::Panel *firstAdvanced = FindChildByName( "DrpAntialias", true ) ) firstAdvanced->SetNavUp( "SldFilmGrain" );
	if ( vgui::Panel *recommended = FindChildByName( "BtnUseRecommended", true ) ) recommended->SetNavUp( "BtnGraphicsDX12" );
	PopulateModes();
}
void Video::ApplyVideoSettings()
{
	if ( !m_bDirty ) return;
	// G743C0: native cvar setter/callback order. Each real owner retains its
	// existing renderer-quality/cache callbacks and archive flags.
	VideoSet("gpu_mem_level",m_nGPUMem); VideoSet("mem_level",m_nMem);
	if ( m_AAModes.IsValidIndex(m_nAA) ) { VideoSet("mat_antialias",m_AAModes[m_nAA].samples); VideoSet("mat_aaquality",m_AAModes[m_nAA].quality); }
	VideoSet("mat_forceaniso",m_nAniso); VideoSet("mat_vsync",m_bVSync); VideoSet("mat_triplebuffered",m_bTriple); VideoSet("mat_queue_mode",m_nQueue); VideoSet("cpu_level",m_nCPU); VideoSet("gpu_level",m_nGPU);
	const MaterialSystem_Config_t &current=materials->GetCurrentConfigForVideoCard();
	if ( current.m_VideoMode.m_Width!=m_nWidth || current.m_VideoMode.m_Height!=m_nHeight || current.Windowed()!=m_bWindowed || current.NoWindowBorder()!=m_bBorderless ) engine->ClientCmd_Unrestricted(CFmtStr("mat_setvideomode %d %d %d %d\n",m_nWidth,m_nHeight,m_bWindowed ? 1 : 0,m_bBorderless ? 1 : 0));
	engine->ClientCmd_Unrestricted("mat_savechanges\n"); SaveConfig(); m_bDirty=false;
	m_flOldGamma = VideoFloat( "mat_monitorgamma" );
	m_flOldGrain = VideoFloat( "mat_grain_scale_override" );
}
void Video::OnClose()
{
	if ( m_bCancelled || !m_bDirty )
	{
		if ( ConVar *grain=cvar->FindVar("mat_grain_scale_override") ) if ( m_flOldGrain!=grain->GetFloat() ) grain->SetValue(m_flOldGrain);
		if ( ConVar *gamma=cvar->FindVar("mat_monitorgamma") ) if ( m_flOldGamma!=gamma->GetFloat() ) gamma->SetValue(m_flOldGamma);
	}
	else ApplyVideoSettings();
	CloseWithoutSaving();
}
void Video::OnSliderMoved(KeyValues *data) { m_bDirty=true; }
void Video::PaintBackground() { DrawDialogBackground(m_bAdvanced ? "#GameUI_VideoAdvanced_Title" : "#GameUI_Video",NULL,"#L4D360UI_AudioVideo_Desc",NULL,NULL); }
void Video::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp( command, "GraphicsDX12" ) )
	{
		ApplyVideoSettings();
		OpenChild( "GraphicsDX12" );
		return;
	}
	if ( !Q_stricmp(command,"3rdPartyCredits") ) { ShowNativeOptionsCredits(this,m_hCredits); return; }
	if ( !Q_stricmp(command,"PagedPoolWarning") )
	{
		GenericConfirmation *dialog=new GenericConfirmation(GetParent(),"GenericConfirmation");
		GenericConfirmation::UsageData_t data; data.pWindowTitle="#L4D360UI_VideoOptions_Paged_Pool_Mem"; data.pMessageText="#L4D360UI_VideoOptions_Paged_Pool_Mem_Info"; data.bOkButtonEnabled=true;
		if ( !dialog->LoadLayout("resource/ui/l4d360ui/genericconfirmation.res") ) { delete dialog; return; }
		dialog->SetUsageData(data); dialog->SetNavBack(this); dialog->SetOKCommand(new KeyValues("Command","command","PagedPoolAcknowledged")); dialog->AddActionSignalTarget(this); SetVisible(false); dialog->OnOpen(); return;
	}
	if ( !Q_stricmp(command,"PagedPoolAcknowledged") )
	{
		if ( DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpPagedPoolMem",true)) ) { drop->SetFlyoutOpenCallback(NULL); drop->OnCommand("FlmPagedPoolMem"); }
		return;
	}
	if ( !Q_stricmp(command,"Cancel") ) { m_bCancelled=true; m_bDirty=false; NavigateBack(); return; }
	if ( !Q_stricmp(command,"Back") ) { NavigateBack(); return; }
	if ( !Q_stricmp(command,"UseRecommended") ) { ReadSettings(true); Refresh(); return; }
	if ( !Q_stricmp(command,"ShowAdvanced") ) { m_bAdvanced=true; Refresh(); return; }
	if ( StageCommand(command) ) Refresh();
	else BaseClass::OnCommand(command);
}
}
