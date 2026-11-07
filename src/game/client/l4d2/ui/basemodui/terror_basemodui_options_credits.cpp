//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "vgui/IScheme.h"
#include "tier0/memdbgon.h"
// G77980/G77810 and G82370/G81F70: separate original global Frame classes.
// Mounted original resources own legal text; native callers own only weak caches.
class COptionsSubAudioThirdPartyCreditsDlg : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(COptionsSubAudioThirdPartyCreditsDlg,vgui::Frame);
public:
	COptionsSubAudioThirdPartyCreditsDlg(vgui::Panel *caller) : BaseClass(caller->GetParent(),"COptionsSubAudioThirdPartyCreditsDlg"), m_bReady(false)
	{
		SetProportional(true); SetScheme(caller->GetScheme()); SetTitle("#GameUI_ThirdPartyAudio_Title",true);
		SetSize(vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),500),vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),200));
		SetSizeable(false); SetDeleteSelfOnClose(true);
		KeyValues::AutoDelete data(new KeyValues(""));
		m_bReady=LoadTerrorUiResource(data,"resource/OptionsSubAudioThirdPartyDlg.res");
		if ( m_bReady ) LoadControlSettings("resource/OptionsSubAudioThirdPartyDlg.res","GAME",data);
		MoveToCenterOfScreen();
	}
	bool IsReady() const { return m_bReady; }
private:
	bool m_bReady;
};
class COptionsSubVideoThirdPartyCreditsDlg : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(COptionsSubVideoThirdPartyCreditsDlg,vgui::Frame);
public:
	COptionsSubVideoThirdPartyCreditsDlg(vgui::Panel *caller) : BaseClass(caller->GetParent(),"COptionsSubVideoThirdPartyCreditsDlg"), m_bReady(false)
	{
		SetProportional(true); SetScheme(caller->GetScheme()); SetTitle("#GameUI_ThirdPartyVideo_Title",true);
		SetSize(vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),500),vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),200));
		SetSizeable(false); SetDeleteSelfOnClose(true);
		KeyValues::AutoDelete data(new KeyValues(""));
		m_bReady=LoadTerrorUiResource(data,"resource/OptionsSubVideoThirdPartyDlg.res");
		if ( m_bReady ) LoadControlSettings("resource/OptionsSubVideoThirdPartyDlg.res","GAME",data);
		MoveToCenterOfScreen();
	}
	bool IsReady() const { return m_bReady; }
private:
	bool m_bReady;
};
namespace L4D360UI
{
void ShowNativeOptionsCredits(Audio *caller,vgui::DHANDLE<vgui::Frame> &credits)
{
	FlyoutMenu::CloseActiveMenu(); vgui::Frame *dialog=credits.Get();
	if ( !dialog )
	{
		COptionsSubAudioThirdPartyCreditsDlg *created=new COptionsSubAudioThirdPartyCreditsDlg(caller);
		if ( !created->IsReady() ) { delete created; return; }
		credits=created; dialog=created;
	}
	dialog->Activate();
}
void ShowNativeOptionsCredits(Video *caller,vgui::DHANDLE<vgui::Frame> &credits)
{
	FlyoutMenu::CloseActiveMenu(); vgui::Frame *dialog=credits.Get();
	if ( !dialog )
	{
		COptionsSubVideoThirdPartyCreditsDlg *created=new COptionsSubVideoThirdPartyCreditsDlg(caller);
		if ( !created->IsReady() ) { delete created; return; }
		credits=created; dialog=created;
	}
	dialog->Activate();
}
} // namespace L4D360UI
