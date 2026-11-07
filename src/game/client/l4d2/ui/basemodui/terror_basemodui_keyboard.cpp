//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "filesystem.h"
#include "inputsystem/iinputsystem.h"
#include "tier1/utlbuffer.h"
#include "tier1/fmtstr.h"
#include "vgui/IScheme.h"
#include "vgui_controls/PropertyPage.h"
#include "vgui_controls/PropertyDialog.h"
#include "vgui_controls/PropertySheet.h"
#include "vgui_controls/SectionedListPanel.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/QueryBox.h"
#include "tier0/memdbgon.h"

// Original global classes: GameUI G76FF0 mode1 opens only this native keyboard
// page. The client now owns its real binding edits, not a second keybind file.
class COptionsSubKeyboardAdvancedDlg : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(COptionsSubKeyboardAdvancedDlg,vgui::Frame);
public:
	COptionsSubKeyboardAdvancedDlg(vgui::Panel *parent) : BaseClass(parent,"COptionsSubKeyboardAdvancedDlg")
	{
		SetTitle("#GameUI_KeyboardAdvanced_Title",true); SetSize(280,140); SetSizeable(false); SetDeleteSelfOnClose(true);
		m_pConsole=new vgui::CheckButton(this,"ConsoleCheck",""); m_pFastSwitch=new vgui::CheckButton(this,"FastSwitchCheck","");
		KeyValues::AutoDelete data(new KeyValues("")); if ( LoadTerrorUiResource(data,"resource/OptionsSubKeyboardAdvancedDlg.res") ) LoadControlSettings("resource/OptionsSubKeyboardAdvancedDlg.res","GAME",data);
		ConVar *console=cvar->FindVar("con_enable"), *fast=cvar->FindVar("hud_fastswitch");
		m_pConsole->SetSelected(console && console->GetBool()); m_pFastSwitch->SetSelected(fast && fast->GetBool());
		MoveToCenterOfScreen(); Activate();
	}
	void OnCommand(const char *command) OVERRIDE
	{
		if ( !Q_stricmp(command,"OK") )
		{
			if ( ConVar *v=cvar->FindVar("con_enable") ) v->SetValue(m_pConsole->IsSelected());
			if ( ConVar *v=cvar->FindVar("hud_fastswitch") ) v->SetValue(m_pFastSwitch->IsSelected());
			Close(); return;
		}
		BaseClass::OnCommand(command);
	}
private:
	vgui::CheckButton *m_pConsole,*m_pFastSwitch;
};
class COptionsSubKeyboard : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE(COptionsSubKeyboard,vgui::PropertyPage);
public:
	COptionsSubKeyboard(vgui::Panel *parent) : BaseClass(parent,"COptionsSubKeyboard"), m_bTrapping(false), m_nCaptureItem(-1)
	{
		m_pList=new vgui::SectionedListPanel(this,"listpanel_keybindlist");
		m_pChange=new vgui::Button(this,"ChangeKeyButton","",this,"ChangeKey"); m_pClear=new vgui::Button(this,"ClearKeyButton","",this,"ClearKey");
		KeyValues::AutoDelete settings(new KeyValues("")); if ( LoadTerrorUiResource(settings,"resource/OptionsSubKeyboard.res") ) LoadControlSettings("resource/OptionsSubKeyboard.res","GAME",settings);
		m_pChange->SetEnabled(false); m_pClear->SetEnabled(false); ParseActionDescriptions(); FillInCurrentBindings();
	}
	bool IsTrapping() const { return m_bTrapping; }
	void OnCommand(const char *command) OVERRIDE
	{
		if ( m_bTrapping ) return;
		if ( !Q_stricmp(command,"ChangeKey") )
		{
			m_nCaptureItem=m_pList->GetSelectedItem();
			if ( m_nCaptureItem>=0 ) { m_bTrapping=true; engine->StartKeyTrapMode(); m_pChange->SetEnabled(false); m_pClear->SetEnabled(false); }
			return;
		}
		if ( !Q_stricmp(command,"ClearKey") ) { SetItemKey(m_pList->GetSelectedItem(),""); return; }
		if ( !Q_stricmp(command,"Advanced") ) { new COptionsSubKeyboardAdvancedDlg(this); return; }
		if ( !Q_stricmp(command,"Defaults") )
		{
			vgui::QueryBox *query=new vgui::QueryBox("#GameUI_KeyboardSettings","#GameUI_KeyboardSettingsText",this);
			query->SetOKCommand(new KeyValues("Command","command","DefaultsOK")); query->AddActionSignalTarget(this); query->DoModal(); return;
		}
		if ( !Q_stricmp(command,"DefaultsOK") ) { RestoreDefaults(); return; }
		BaseClass::OnCommand(command);
	}
	void OnKeyCodeTyped(vgui::KeyCode code) OVERRIDE
	{
		if ( !m_bTrapping && code==KEY_DELETE ) { SetItemKey(m_pList->GetSelectedItem(),""); return; }
		if ( !m_bTrapping && code==KEY_ENTER ) { OnCommand("ChangeKey"); return; }
		BaseClass::OnKeyCodeTyped(code);
	}
protected:
	void OnThink() OVERRIDE
	{
		BaseClass::OnThink();
		if ( m_bTrapping )
		{
			ButtonCode_t code=BUTTON_CODE_INVALID;
			if ( engine->CheckDoneKeyTrapping(code) )
			{
				m_bTrapping=false;
				if ( code!=KEY_ESCAPE && code!=BUTTON_CODE_INVALID && code!=BUTTON_CODE_NONE ) SetItemKey(m_nCaptureItem,inputsystem->ButtonCodeToString(code));
				m_pList->RequestFocus();
			}
		}
		m_pChange->SetEnabled(!m_bTrapping && m_pList->GetSelectedItem()>=0); m_pClear->SetEnabled(!m_bTrapping && m_pList->GetSelectedItem()>=0);
	}
	MESSAGE_FUNC(OnResetData,"ResetData") { FillInCurrentBindings(); }
	MESSAGE_FUNC(OnApplyChanges,"ApplyChanges")
	{
		// G79720 applies only keys replaced/cleared in the editor; unrelated
		// bindings and the real M7Input preset consumers remain untouched.
		for ( int i=0;i<m_UnboundKeys.Count();++i ) engine->ClientCmd_Unrestricted(CFmtStr("unbind \"%s\"\n",m_UnboundKeys[i].String()));
		m_UnboundKeys.RemoveAll();
		for ( int row=0;row<m_pList->GetItemCount();++row )
		{
			KeyValues *item=m_pList->GetItemData(m_pList->GetItemIDFromRow(row));
			const char *key=item->GetString("Key"), *binding=item->GetString("Binding");
			if ( *key && *binding ) engine->ClientCmd_Unrestricted(CFmtStr("bind \"%s\" \"%s\"\n",key,binding));
		}
		engine->ClientCmd_Unrestricted("exec userconfig.cfg\nhost_writeconfig\n");
	}
private:
	void ParseActionDescriptions()
	{
		CUtlBuffer file(0,0,CUtlBuffer::TEXT_BUFFER);
		if ( !filesystem->ReadFile("scripts/kb_act.lst","GAME",file) ) return;
		const char *cursor=(const char *)file.Base(); char binding[256],description[512]; int section=0;
		while ( cursor && *cursor )
		{
			cursor=engine->ParseFile(cursor,binding,sizeof(binding)); if ( !cursor || !*binding ) break;
			cursor=engine->ParseFile(cursor,description,sizeof(description)); if ( !cursor || !*description ) break;
			if ( description[0]=='=' ) continue;
			if ( !Q_stricmp(binding,"blank") )
			{
				++section; m_pList->AddSection(section,description);
				int actionWidth=IsProportional() ? vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),286) : 286;
				int keyWidth=IsProportional() ? vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),128) : 128;
				m_pList->AddColumnToSection(section,"Action",description,vgui::SectionedListPanel::COLUMN_BRIGHT,actionWidth);
				m_pList->AddColumnToSection(section,"Key","#GameUI_KeyButton",vgui::SectionedListPanel::COLUMN_BRIGHT,keyWidth); continue;
			}
			KeyValues::AutoDelete item(new KeyValues("Item")); item->SetString("Action",description); item->SetString("Binding",binding); item->SetString("Key",""); m_pList->AddItem(section,item);
		}
	}
	void FillInCurrentBindings()
	{
		m_UnboundKeys.RemoveAll();
		for ( int row=0;row<m_pList->GetItemCount();++row )
		{
			int id=m_pList->GetItemIDFromRow(row); KeyValues *item=m_pList->GetItemData(id); item->SetString("Key","");
			for ( int key=KEY_FIRST;key<BUTTON_CODE_LAST;++key )
			{
				const char *binding=engine->Key_BindingForKey((ButtonCode_t)key);
				if ( binding && !Q_stricmp(binding,item->GetString("Binding")) ) item->SetString("Key",inputsystem->ButtonCodeToString((ButtonCode_t)key));
			}
			m_pList->InvalidateItem(id);
		}
	}
	void SetItemKey(int id,const char *key)
	{
		KeyValues *item=m_pList->GetItemData(id); if ( !item ) return;
		const char *old=item->GetString("Key"); if ( !Q_stricmp(old,key) ) return;
		if ( *old ) m_UnboundKeys.AddToTail(CUtlString(old));
		for ( int row=0;row<m_pList->GetItemCount();++row )
		{
			int other=m_pList->GetItemIDFromRow(row); KeyValues *data=m_pList->GetItemData(other);
			if ( *key && !Q_stricmp(data->GetString("Key"),key) ) { data->SetString("Key",""); m_pList->InvalidateItem(other); }
		}
		item->SetString("Key",key); m_pList->InvalidateItem(id); PostActionSignal(new KeyValues("ApplyButtonEnable"));
	}
	void RestoreDefaults()
	{
		CUtlBuffer file(0,0,CUtlBuffer::TEXT_BUFFER); if ( !filesystem->ReadFile("cfg/config_default.cfg","GAME",file) ) return;
		engine->ClientCmd_Unrestricted("unbindall\n");
		for ( int row=0;row<m_pList->GetItemCount();++row ) { int id=m_pList->GetItemIDFromRow(row); m_pList->GetItemData(id)->SetString("Key",""); m_pList->InvalidateItem(id); }
		const char *cursor=(const char *)file.Base(); char command[64],key[256],value[256];
		while ( cursor && *cursor )
		{
			cursor=engine->ParseFile(cursor,command,sizeof(command)); if ( !cursor || !*command ) break;
			if ( !Q_stricmp(command,"bind") )
			{
				cursor=engine->ParseFile(cursor,key,sizeof(key)); if ( !cursor ) break;
				cursor=engine->ParseFile(cursor,value,sizeof(value)); if ( !cursor ) break;
				for ( int row=0;row<m_pList->GetItemCount();++row ) { int id=m_pList->GetItemIDFromRow(row); if ( !Q_stricmp(m_pList->GetItemData(id)->GetString("Binding"),value) ) SetItemKey(id,key); }
			}
			else if ( ConVar *variable=cvar->FindVar(command) ) { cursor=engine->ParseFile(cursor,value,sizeof(value)); if ( cursor ) variable->SetValue(value); }
		}
		PostActionSignal(new KeyValues("ApplyButtonEnable"));
	}
	vgui::SectionedListPanel *m_pList;
	vgui::Button *m_pChange,*m_pClear;
	CUtlVector<CUtlString> m_UnboundKeys;
	bool m_bTrapping;
	int m_nCaptureItem;
};
class COptionsDialog : public vgui::PropertyDialog
{
	DECLARE_CLASS_SIMPLE(COptionsDialog,vgui::PropertyDialog);
public:
	COptionsDialog(L4D360UI::CL4DFrame *caller) : BaseClass(caller->GetParent(),"OptionsDialog")
	{
		m_hCaller=caller;
		SetProportional(true); SetSize(vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),512),vgui::scheme()->GetProportionalScaledValueEx(GetScheme(),415)); SetTitle("#L4D360UI_Controller_Edit_Keys_Buttons",true);
		SetSizeable(false); SetDeleteSelfOnClose(true); m_pKeyboard=new COptionsSubKeyboard(this); AddPage(m_pKeyboard,"#GameUI_Console_UserSettings"); GetPropertySheet()->SetTabWidth(84); MoveToCenterOfScreen();
		SetApplyButtonVisible(true);
	}
	void SetCaller(L4D360UI::CL4DFrame *caller) { m_hCaller=caller; }
	void Activate() OVERRIDE { BaseClass::Activate(); EnableApplyButton(false); }
	void OnClose() OVERRIDE { if ( m_pKeyboard->IsTrapping() ) return; if ( m_hCaller.Get() ) { m_hCaller->SetVisible(true); m_hCaller->NavigateTo(); } BaseClass::OnClose(); }
protected:
	void OnCancel() OVERRIDE { if ( !m_pKeyboard->IsTrapping() ) BaseClass::OnCancel(); }
private:
	COptionsSubKeyboard *m_pKeyboard;
	vgui::DHANDLE<L4D360UI::CL4DFrame> m_hCaller;
};
namespace L4D360UI
{
	// G0AA90: one global legacy editor, outside the native frame inventory.
	static vgui::DHANDLE<COptionsDialog> s_hKeyboardBindings;
	void OpenNativeKeyboardBindings(CL4DFrame *caller)
	{
		COptionsDialog *dialog=s_hKeyboardBindings.Get();
		if ( !dialog ) { dialog=new COptionsDialog(caller); s_hKeyboardBindings=dialog; }
		else dialog->SetCaller(caller);
		caller->SetVisible(false); dialog->Activate();
	}
}
