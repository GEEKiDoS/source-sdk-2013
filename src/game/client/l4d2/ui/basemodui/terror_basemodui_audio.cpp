//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "cdll_client_int.h"
#include "steam/steam_api.h"
#include "language.h"
#include "tier1/utlbuffer.h"
#include "tier1/fmtstr.h"
#include "vgui/ISystem.h"
#include "vgui_controls/ProgressBar.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"
namespace L4D360UI
{
static int GetOptionInt( const char *name ) { ConVar *v=cvar->FindVar(name); return v ? v->GetInt() : 0; }
static void SetOption( const char *name, int value ) { if ( ConVar *v=cvar->FindVar(name) ) v->SetValue(value); }
static bool SetCaption( const char *command )
{
	int caption,subtitles;
	if ( !Q_stricmp(command,"#L4D360UI_AudioOptions_CaptionOff") ) { caption=0; subtitles=0; }
	else if ( !Q_stricmp(command,"#L4D360UI_AudioOptions_CaptionSubtitles") ) { caption=1; subtitles=1; }
	else if ( !Q_stricmp(command,"#L4D360UI_AudioOptions_CaptionOn") ) { caption=1; subtitles=0; }
	else return false;
	SetOption("closecaption",caption); SetOption("cc_subtitles",subtitles); return true;
}
static const char *CaptionSelection()
{
	return !GetOptionInt("closecaption") ? "#L4D360UI_AudioOptions_CaptionOff" : GetOptionInt("cc_subtitles") ? "#L4D360UI_AudioOptions_CaptionSubtitles" : "#L4D360UI_AudioOptions_CaptionOn";
}
static const OptionChoice_t s_Speakers[] = {
	{ "DrpSpeakerConfiguration","#GameUI_Headphones","snd_surround_speakers","0" },
	{ "DrpSpeakerConfiguration","#GameUI_2Speakers","snd_surround_speakers","2" },
	{ "DrpSpeakerConfiguration","#GameUI_4Speakers","snd_surround_speakers","4" },
	{ "DrpSpeakerConfiguration","#GameUI_5Speakers","snd_surround_speakers","5" },
	{ "DrpSpeakerConfiguration","#GameUI_7Speakers","snd_surround_speakers","7" }
};
Audio::Audio( vgui::Panel *parent, const char *name ) : BaseClass(parent,name,"#GameUI_Audio"), m_pVoice(engine->GetVoiceTweakAPI()), m_bTesting(false), m_bLanguageStateInitialized(false) {}
Audio::~Audio() { EndMicrophoneTest(); if ( CL4DFooterPanel *footer=GetL4DFooterPanel(this) ) footer->SetCloudEnabled(false); }
void Audio::PopulateLanguages()
{
	m_Languages.RemoveAll();
	DropDownMenu *drop=dynamic_cast<DropDownMenu *>(FindChildByName("DrpLanguage",true));
	FlyoutMenu *flyout=drop ? drop->GetFlyout() : NULL;
	ISteamApps *apps=steamapicontext ? steamapicontext->SteamApps() : NULL;
	const bool initialize=!m_bLanguageStateInitialized;
	if ( initialize )
	{
		char uiLanguage[50]; engine->GetUILanguage(uiLanguage,sizeof(uiLanguage));
		m_OriginalLanguage=apps ? apps->GetCurrentGameLanguage() : uiLanguage;
		m_SelectedLanguage=m_OriginalLanguage;
		m_bLanguageStateInitialized=true;
	}
	// G232B0 snapshots the staged language before G236E0 opens confirmation.
	// H correctly restores/reopens its caller before posting the choice; a
	// modal reopen must rebind real rows without replacing that staged choice.
	const char *available=apps ? apps->GetAvailableGameLanguages() : NULL;
	if ( !flyout || !available || !*available ) { SetControlEnabled("DrpLanguage",false,true); return; }
	CUtlVector<char *> languages; V_SplitString(available,",",languages);
	flyout->BeginDynamicRows();
	int selected=-1;
	for ( int i=0;i<languages.Count() && i<15;++i )
	{
		CUtlString language(languages[i]); m_Languages.AddToTail(language);
		if ( !Q_stricmp(language.String(),m_SelectedLanguage.String()) ) selected=i;
		flyout->AddDynamicRow(CFmtStr("BtnLanguage%d",i),GetLanguageVGUILocalization(PchLanguageToELanguage(language.String())),CFmtStr("_language%c",'0'+i));
	}
	languages.PurgeAndDeleteElements(); flyout->EndDynamicRows(); flyout->SetBackgroundHeight(20*m_Languages.Count()+5);
	if ( selected<0 && initialize && m_Languages.Count() ) { selected=0; m_SelectedLanguage=m_Languages[0]; }
	SetControlEnabled("DrpLanguage",m_Languages.Count()>1,true);
	if ( selected>=0 ) Select("DrpLanguage",CFmtStr("_language%c",'0'+selected));
}
void Audio::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/audio.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen();
	PopulateLanguages(); Refresh();
	if ( CL4DFooterPanel *footer=GetL4DFooterPanel(this) ) { footer->SetButtons(3,4,false); footer->SetCloudEnabled(true); }
}
void Audio::Refresh()
{
	ResetSliders(this); RefreshChoices(s_Speakers,ARRAYSIZE(s_Speakers));
	Select("DrpSoundQuality",GetOptionInt("snd_pitchquality") ? "#GameUI_High" : GetOptionInt("dsp_slow_cpu") ? "#GameUI_Low" : "#GameUI_Medium");
	Select("DrpCaptioning",CaptionSelection()); RefreshVoice();
	if ( m_pVoice ) if ( SliderControl *slider=dynamic_cast<SliderControl *>(FindChildByName("SldVoiceTransmitVolume",true)) ) slider->SetValue(m_pVoice->GetControlFloat(MicrophoneVolume)*100.0f);
}
void Audio::RefreshVoice()
{
	bool enabled=GetOptionInt("voice_modenable")!=0;
	Select("DrpVoiceCommunication",enabled ? "VoiceCommunicationEnabled" : "VoiceCommunicationDisabled");
	Select("DrpVoiceCommunicationStyle",GetOptionInt("voice_vox") ? "VoiceCommunicationOpenMic" : "VoiceCommunicationPushToTalk");
	Select("DrpBoostMicrophone",m_pVoice && m_pVoice->GetControlFloat(MicBoost)>0.0f ? "BoostMicrophoneEnabled" : "BoostMicrophoneDisabled");
	SetControlEnabled("SldVoiceTransmitVolume",enabled && m_pVoice && !m_bTesting,true);
	SetControlEnabled("SldVoiceReceiveVolume",enabled && !m_bTesting,true);
	SetControlEnabled("DrpBoostMicrophone",enabled && m_pVoice,true);
	SetControlEnabled("DrpVoiceCommunicationStyle",enabled,true);
	SetControlEnabled("SldVoiceVoxThreshold",enabled && GetOptionInt("voice_vox"),true);
	SetControlEnabled("BtnTestMicrophone",enabled && m_pVoice,true);
	SetControlVisible("MicMeter",enabled,true); SetControlVisible("MicMeter2",false,true);
}
void Audio::EndMicrophoneTest() { if ( m_bTesting && m_pVoice ) m_pVoice->EndVoiceTweakMode(); m_bTesting=false; }
void Audio::OnThink()
{
	BaseClass::OnThink();
	if ( m_bTesting && m_pVoice )
	{
		if ( !m_pVoice->IsStillTweaking() ) { m_bTesting=false; RefreshVoice(); }
		if ( vgui::ProgressBar *meter=dynamic_cast<vgui::ProgressBar *>(FindChildByName("MicMeter",true)) ) meter->SetProgress(m_pVoice->GetControlFloat(SpeakingVolume));
	}
}
void Audio::OnClose()
{
	EndMicrophoneTest();
	if ( m_pVoice ) if ( SliderControl *slider=dynamic_cast<SliderControl *>(FindChildByName("SldVoiceTransmitVolume",true)) ) m_pVoice->SetControlFloat(MicrophoneVolume,(int)slider->GetValue()*0.01f);
	m_bLanguageStateInitialized=false;
	BaseClass::OnClose();
}
vgui::Panel *Audio::NavigateBack()
{
	if ( Q_stricmp(m_SelectedLanguage.String(),m_OriginalLanguage.String()) )
	{
		Confirm("#GameUI_ChangeLanguageRestart_Title","#GameUI_ChangeLanguageRestart_Info","RestartWithNewLanguage");
		return this;
	}
	return BaseClass::NavigateBack();
}
void Audio::OnCommand( const char *command )
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp(command,"3rdPartyCredits") ) { ShowNativeOptionsCredits(this,m_hCredits); return; }
	if ( const char *suffix=StringAfterPrefix(command,"_language") )
	{
		int index=(unsigned char)*suffix-'0';
		if ( m_Languages.IsValidIndex(index) ) m_SelectedLanguage=m_Languages[index];
		return;
	}
	if ( !Q_stricmp(command,"RestartWithNewLanguage") )
	{
		// G0CF20: client-side restart owner. Preserve actual application ID and
		// actual selected language; no Steam call or successful relaunch is assumed.
		vgui::system()->SetRegistryString("HKEY_CURRENT_USER\\Software\\Valve\\Source\\Relaunch URL",CFmtStr("steam://run/%d/%s",engine->GetAppID(),m_SelectedLanguage.String()));
		OnClose(); engine->ClientCmd_Unrestricted("quit\n"); return;
	}
	if ( ApplyChoice(command,s_Speakers,ARRAYSIZE(s_Speakers)) )
	{ SetOption("dsp_enhance_stereo",!GetOptionInt("dsp_slow_cpu") && GetOptionInt("snd_pitchquality") && !GetOptionInt("snd_surround_speakers")); return; }
	if ( !Q_stricmp(command,"#GameUI_High") || !Q_stricmp(command,"#GameUI_Medium") || !Q_stricmp(command,"#GameUI_Low") )
	{
		SetOption("dsp_slow_cpu",!Q_stricmp(command,"#GameUI_Low")); SetOption("snd_pitchquality",!Q_stricmp(command,"#GameUI_High"));
		SetOption("dsp_enhance_stereo",!GetOptionInt("dsp_slow_cpu") && GetOptionInt("snd_pitchquality") && !GetOptionInt("snd_surround_speakers")); return;
	}
	if ( SetCaption(command) ) return;
	if ( !Q_stricmp(command,"VoiceCommunicationEnabled") || !Q_stricmp(command,"VoiceCommunicationDisabled") )
	{ SetOption("voice_modenable",!Q_stricmp(command,"VoiceCommunicationEnabled")); if ( !GetOptionInt("voice_modenable") ) EndMicrophoneTest(); RefreshVoice(); return; }
	if ( !Q_stricmp(command,"VoiceCommunicationOpenMic") || !Q_stricmp(command,"VoiceCommunicationPushToTalk") )
	{ SetOption("voice_vox",!Q_stricmp(command,"VoiceCommunicationOpenMic")); RefreshVoice(); return; }
	if ( !Q_stricmp(command,"BoostMicrophoneEnabled") || !Q_stricmp(command,"BoostMicrophoneDisabled") )
	{ if ( m_pVoice ) m_pVoice->SetControlFloat(MicBoost,!Q_stricmp(command,"BoostMicrophoneEnabled") ? 1.0f : 0.0f); return; }
	if ( !Q_stricmp(command,"TestMicrophone") )
	{ if ( m_bTesting ) EndMicrophoneTest(); else if ( m_pVoice ) m_bTesting=m_pVoice->StartVoiceTweakMode()!=0; RefreshVoice(); return; }
	BaseClass::OnCommand(command);
}
static const OptionChoice_t s_AudioVideo[] = {
	{ "DrpColorMode","#L4D360UI_ColorMode_Television","mat_monitorgamma_tv_enabled","1" },
	{ "DrpColorMode","#L4D360UI_ColorMode_LCD","mat_monitorgamma_tv_enabled","0" },
	{ "DrpSplitScreenDirection","#L4D360UI_SplitScreenDirection_Default","ss_splitmode","0" },
	{ "DrpSplitScreenDirection","#L4D360UI_SplitScreenDirection_Horizontal","ss_splitmode","1" },
	{ "DrpSplitScreenDirection","#L4D360UI_SplitScreenDirection_Vertical","ss_splitmode","2" },
	{ "DrpGore","#L4D360UI_Gore_High","z_wound_client_disabled","0" },
	{ "DrpGore","#L4D360UI_Gore_Low","z_wound_client_disabled","1" }
};
AudioVideo::AudioVideo(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_AudioVideo") {}
void AudioVideo::OnOpen() { if ( !LoadLayout("resource/ui/l4d360ui/audiovideo.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); ResetSliders(this); RefreshChoices(s_AudioVideo,ARRAYSIZE(s_AudioVideo)); Select("DrpCaptioning",CaptionSelection()); }
void AudioVideo::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( ApplyChoice(command,s_AudioVideo,ARRAYSIZE(s_AudioVideo)) || SetCaption(command) ) return;
	if ( const char *language=StringAfterPrefix(command,"#GameUI_Language_") ) { SetOption("english",!Q_stricmp(language,"English")); return; }
	BaseClass::OnCommand(command);
}
Cloud::Cloud(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Cloud"), m_bCloud(false) {}
void Cloud::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/cloud.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen();
	m_bCloud=GetOptionInt("cl_cloud_settings")!=0;
	Select("DrpCloud",m_bCloud ? "SteamCloudEnabled" : "SteamCloudDisabled");
	if ( CL4DFooterPanel *footer=GetL4DFooterPanel(this) ) footer->SetButtons(3,4,false);
	// This changes only the real local preference. No profile/cloud backend is
	// contacted and no synchronized settings or account metadata is fabricated.
}
void Cloud::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp(command,"SteamCloudEnabled") ) { m_bCloud=true; return; }
	if ( !Q_stricmp(command,"SteamCloudDisabled") ) { m_bCloud=false; return; }
	BaseClass::OnCommand(command);
}
void Cloud::OnClose() { SetOption("cl_cloud_settings",m_bCloud ? 3 : 0); CloseWithoutSaving(); }
}
