//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "inputsystem/iinputsystem.h"
#include "tier1/fmtstr.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"
namespace L4D360UI
{
static int OptionInt(const char *name) { ConVar *v=cvar->FindVar(name); return v ? v->GetInt() : 0; }
static void OptionSet(const char *name,int value) { if ( ConVar *v=cvar->FindVar(name) ) v->SetValue(value); }
static const OptionChoice_t s_Controller[] = {
	{ "DrpDuckMode","#L4D360UI_Controller_Hold","option_duck_method","0" },
	{ "DrpDuckMode","#L4D360UI_Controller_Toggle","option_duck_method","1" },
	{ "DrpLookType","#L4D360UI_Controller_Normal","joy_inverty","0" },
	{ "DrpLookType","#L4D360UI_Controller_Inverted","joy_inverty","1" }
};
ControllerOptions::ControllerOptions(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Controller_Title") {}
void ControllerOptions::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/controlleroptions.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); ResetSliders(this); RefreshChoices(s_Controller,ARRAYSIZE(s_Controller));
	KeyValues::AutoDelete buttons(new KeyValues(""));
	if ( LoadTerrorUiResource(buttons,"resource/ui/l4d360ui/controlleroptionsbuttons.res") )
		if ( L4D360HybridButton *button=dynamic_cast<L4D360HybridButton *>(FindChildByName("BtnEditButtons",true)) )
			button->SetCurrentSelection(buttons->GetString(CFmtStr("BtnSpec%d/labelText",clamp(OptionInt("joy_cfg_preset"),0,3)+1),""));
	KeyValues::AutoDelete sticks(new KeyValues(""));
	if ( LoadTerrorUiResource(sticks,"resource/ui/l4d360ui/controlleroptionssticks.res") )
		if ( L4D360HybridButton *button=dynamic_cast<L4D360HybridButton *>(FindChildByName("BtnEditSticks",true)) )
			button->SetCurrentSelection(sticks->GetString(OptionInt("joy_movement_stick") ? "BtnSouthpaw/labelText" : "BtnDefault/labelText",""));
}
void ControllerOptions::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp(command,"EditButtons") ) { OpenChild("ControllerOptionsButtons"); return; }
	if ( !Q_stricmp(command,"EditSticks") ) { OpenChild("ControllerOptionsSticks"); return; }
	if ( ApplyChoice(command,s_Controller,ARRAYSIZE(s_Controller)) ) return;
	if ( !Q_stricmp(command,"Defaults") ) { Confirm("#L4D360UI_Controller_Default","#L4D360UI_Controller_Default_Details","ControllerDefaultsOK"); return; }
	if ( !Q_stricmp(command,"ControllerDefaultsOK") )
	{
		// G29B60, sole available local slot. These are original mounted cfgs;
		// M7Input owns their actual toggle_duck/quickturn/joystick consumers.
		engine->ClientCmd_Unrestricted("exec config.360.cfg\nexec joy_preset_1.cfg\n"); return;
	}
	BaseClass::OnCommand(command);
}
ControllerOptionsButtons::ControllerOptionsButtons(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Controller_Buttons_Title") {}
void ControllerOptionsButtons::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/controlleroptionsbuttons.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); RefreshBindings();
	if ( CL4DFooterPanel *footer=GetL4DFooterPanel(this) ) { footer->SetButtons(1,4,true); footer->SetButtonText(1,"#L4D360UI_Select"); }
}
void ControllerOptionsButtons::RefreshBindings()
{
	static const char *keys[]={ "A_BUTTON","B_BUTTON","X_BUTTON","Y_BUTTON","L_SHOULDER","R_SHOULDER","STICK1","STICK2","L_TRIGGER","R_TRIGGER" };
	static const char *labels[]={ "LblAButton","LblBButton","LblXButton","LblYButton","LblLShoulder","LblRShoulder","LblLStick","LblRStick","LblLTrigger","LblRTrigger" };
	static const char *commands[]={ "+duck","+jump","+use","+attack","+attack2","+reload","lastinv","+zoom","+lookspin","vocalize smartlook" };
	static const char *tokens[]={ "#L4D360UI_Controller_Crouch","#L4D360UI_Controller_Jump","#L4D360UI_Controller_Use","#L4D360UI_Controller_Attack","#L4D360UI_Controller_Melee","#L4D360UI_Controller_Reload","#L4D360UI_Controller_LastInv","#L4D360UI_Controller_Zoom","#L4D360UI_Controller_Lookspin","#L4D360UI_Controller_Vocalize" };
	for ( int k=0;k<ARRAYSIZE(keys);++k )
	{
		const char *binding=engine->Key_BindingForKey(inputsystem->StringToButtonCode(keys[k]));
		if ( !binding || !*binding ) { SetControlString(labels[k],L"<Not Bound>"); continue; }
		for ( int i=0;i<ARRAYSIZE(commands);++i ) if ( Q_stristr(binding,commands[i]) ) SetControlString(labels[k],tokens[i]);
	}
}
void ControllerOptionsButtons::OnThink() { BaseClass::OnThink(); if ( IsVisible() ) RefreshBindings(); }
void ControllerOptionsButtons::OnCommand(const char *command)
{
	static const char *buttons[]={ "BtnSpec1","BtnSpec2","BtnSpec3","BtnSpec4" };
	for ( int i=0;i<ARRAYSIZE(buttons);++i ) if ( !Q_stricmp(command,buttons[i]) )
	{ OptionSet("joy_cfg_preset",i); engine->ClientCmd_Unrestricted(CFmtStr("exec joy_preset_%d.cfg\n",i+1)); return; }
	BaseClass::OnCommand(command);
}
ControllerOptionsSticks::ControllerOptionsSticks(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Controller_Sticks_Title") {}
void ControllerOptionsSticks::Refresh()
{
	bool southpaw=OptionInt("joy_movement_stick")!=0, legacy=OptionInt("joy_legacy")!=0;
	SetControlVisible("Normal_Move",!southpaw,true); SetControlVisible("Normal_Look",!southpaw,true);
	SetControlVisible("Southpaw_Move",southpaw,true); SetControlVisible("Southpaw_Look",southpaw,true);
	if ( CL4DFooterPanel *footer=GetL4DFooterPanel(this) )
	{
		footer->SetButtons(9,11,true); footer->SetButtonText(1,"#L4D360UI_Select"); footer->SetButtonText(8,legacy ? "#L4D360UI_LegacyOn" : "#L4D360UI_LegacyOff");
		footer->SetHelpText(legacy ? (southpaw ? "L4D360UI_Controller_Tooltip_Sticks_Legacy_Southpaw" : "L4D360UI_Controller_Tooltip_Sticks_Legacy") : (southpaw ? "L4D360UI_Controller_Tooltip_Sticks_Southpaw" : "L4D360UI_Controller_Tooltip_Sticks_Default"));
	}
}
void ControllerOptionsSticks::OnOpen() { if ( !LoadLayout("resource/ui/l4d360ui/controlleroptionssticks.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); Refresh(); }
void ControllerOptionsSticks::OnCommand(const char *command)
{
	if ( !Q_stricmp(command,"BtnDefault") || !Q_stricmp(command,"BtnSouthpaw") ) { OptionSet("joy_movement_stick",!Q_stricmp(command,"BtnSouthpaw")); Refresh(); return; }
	BaseClass::OnCommand(command);
}
void ControllerOptionsSticks::OnKeyCodePressed(vgui::KeyCode code)
{
	if ( code==KEY_XBUTTON_Y ) { OptionSet("joy_legacy",!OptionInt("joy_legacy")); Refresh(); return; }
	BaseClass::OnKeyCodePressed(code);
}
static const OptionChoice_t s_Keyboard[] = {
	{ "DrpMouseFilter","MouseFilterEnabled","m_filter","1" }, { "DrpMouseFilter","MouseFilterDisabled","m_filter","0" },
	{ "DrpRawMouse","RawMouseEnabled","m_rawinput","1" }, { "DrpRawMouse","RawMouseDisabled","m_rawinput","0" },
	{ "DrpMouseAcceleration","MouseAccelerationEnabled","m_customaccel","3" }, { "DrpMouseAcceleration","MouseAccelerationDisabled","m_customaccel","0" },
	{ "DrpDeveloperConsole","DeveloperConsoleEnabled","con_enable","1" }, { "DrpDeveloperConsole","DeveloperConsoleDisabled","con_enable","0" },
	{ "DrpGamepadEnable","GamepadEnabled","joystick","1" }, { "DrpGamepadEnable","GamepadDisabled","joystick","0" },
	{ "DrpGamepadYInvert","GamepadYInvertEnabled","joy_inverty","1" }, { "DrpGamepadYInvert","GamepadYInvertDisabled","joy_inverty","0" },
	{ "DrpGamepadSwapSticks","GamepadSwapSticksEnabled","joy_movement_stick","1" }, { "DrpGamepadSwapSticks","GamepadSwapSticksDisabled","joy_movement_stick","0" },
	{ "DrpGamepadAutoAim","GamepadAutoAimEnabled","cl_autoaim","1" }, { "DrpGamepadAutoAim","GamepadAutoAimDisabled","cl_autoaim","0" },
	{ "DrpGamepadVibration","GamepadVibrationEnabled","joy_vibration","1" }, { "DrpGamepadVibration","GamepadVibrationDisabled","joy_vibration","0" }
};
KeyboardMouse::KeyboardMouse(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_KeyboardMouse") {}
void KeyboardMouse::Refresh()
{
	RefreshChoices(s_Keyboard,ARRAYSIZE(s_Keyboard));
	ConVar *pitch=cvar->FindVar("m_pitch"); Select("DrpMouseYInvert",pitch && pitch->GetFloat()<0 ? "MouseYInvertEnabled" : "MouseYInvertDisabled");
	SetControlEnabled("SldMouseAcceleration",OptionInt("m_customaccel")!=0,true);
	static const char *controls[]={ "SldGamepadHSensitivity","SldGamepadVSensitivity","DrpGamepadYInvert","DrpGamepadSwapSticks","DrpGamepadAutoAim","DrpGamepadVibration" };
	for ( int i=0;i<ARRAYSIZE(controls);++i ) SetControlEnabled(controls[i],OptionInt("joystick")!=0,true);
	ResetSliders(this);
}
void KeyboardMouse::OnOpen() { if ( !LoadLayout("resource/ui/l4d360ui/keyboardmouse.res") ) { NavigateBack(); return; } BaseClass::OnOpen(); CenterDialogOnScreen(); Refresh(); }
void KeyboardMouse::OnCommand(const char *command)
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp(command,"#L4D360UI_Controller_Edit_Keys_Buttons") ) { OpenNativeKeyboardBindings(this); return; }
	if ( !Q_stricmp(command,"MouseYInvertEnabled") || !Q_stricmp(command,"MouseYInvertDisabled") )
	{
		ConVar *pitch=cvar->FindVar("m_pitch");
		if ( pitch && ( !Q_stricmp(command,"MouseYInvertEnabled") ? pitch->GetFloat()>0 : pitch->GetFloat()<0 ) ) pitch->SetValue(-pitch->GetFloat());
		return;
	}
	if ( !Q_stricmp(command,"GamepadEnabled") && !OptionInt("joystick") ) engine->ClientCmd_Unrestricted("exec 360controller.cfg\n");
	if ( !Q_stricmp(command,"GamepadDisabled") && OptionInt("joystick") ) engine->ClientCmd_Unrestricted("exec undo360controller.cfg\n");
	if ( ApplyChoice(command,s_Keyboard,ARRAYSIZE(s_Keyboard)) )
	{
		if ( !Q_stricmp(command,"GamepadVibrationEnabled") || !Q_stricmp(command,"GamepadVibrationDisabled") ) OptionSet("steam_controller_haptics",!Q_stricmp(command,"GamepadVibrationEnabled"));
		Refresh(); return;
	}
	BaseClass::OnCommand(command);
}
GameOptions::GameOptions(vgui::Panel *parent,const char *name) : BaseClass(parent,name,"#L4D360UI_Game") {}
void GameOptions::OnOpen()
{
	if ( !LoadLayout("resource/ui/l4d360ui/gameoptions.res") ) { NavigateBack(); return; } BaseClass::OnOpen();
	const char *names[]={ "SpnInvertYAxis","SpnVibration","SpnAutoCrouch","SpnLookSensitivity" };
	const char *cvars[]={ "joy_inverty","cl_rumblescale","cl_autocrouch" };
	static const int booleanValues[]={0,1}, vibrationValues[]={0,1,2,3,4,5}, lookValues[]={1,2,3,4,5};
	for ( int i=0;i<ARRAYSIZE(names);++i )
	{
		SpinnerControl *spinner=dynamic_cast<SpinnerControl *>(FindChildByName(names[i],true)); if ( !spinner ) continue;
		const int *values=i==1 ? vibrationValues : i==3 ? lookValues : booleanValues;
		const int count=i==1 ? ARRAYSIZE(vibrationValues) : i==3 ? ARRAYSIZE(lookValues) : ARRAYSIZE(booleanValues);
		if ( !spinner->GetItemCount() )
		{
			if ( i==0 ) { spinner->AddItem("#L4D360UI_DisabledDefault",0); spinner->AddItem("#L4D360UI_Enabled",1); }
			if ( i==1 ) { for ( int n=0;n<count;++n ) spinner->AddItem(CFmtStr("#L4D360UI_ControllerVibration%d",values[n]),values[n]); }
			if ( i==2 ) { spinner->AddItem("#L4D360UI_Disabled",0); spinner->AddItem("#L4D360UI_EnabledDefault",1); }
			if ( i==3 ) { for ( int n=0;n<count;++n ) spinner->AddItem(CFmtStr("#L4D360UI_LookSensitivity%d",values[n]),values[n]); }
		}
		spinner->AddActionSignalTarget(this);
		int value;
		if ( i==3 ) value=(int)(CUIGameData::Get()->GetLookSensitivity()*3.0f);
		else
		{
			ConVar *setting=cvar->FindVar(cvars[i]); if ( !setting ) continue;
			value=i==1 ? (int)(setting->GetFloat()*5.0f) : setting->GetInt();
		}
		for ( int n=0;n<count;++n ) if ( values[n]==value ) { spinner->SetCurrentItem(n); break; }
	}
}
void GameOptions::OnSpinnerChanged(KeyValues *data)
{
	const char *name=data->GetString("panelName");
	SpinnerControl *spinner=dynamic_cast<SpinnerControl *>(FindChildByName(name,true)); if ( !spinner ) return;
	int value=spinner->GetCurrentValue();
	if ( !Q_stricmp(name,"SpnInvertYAxis") ) OptionSet("joy_inverty",value);
	else if ( !Q_stricmp(name,"SpnVibration") ) { if ( ConVar *rumble=cvar->FindVar("cl_rumblescale") ) rumble->SetValue(value*0.2f); }
	else if ( !Q_stricmp(name,"SpnAutoCrouch") ) OptionSet("cl_autocrouch",value); // Native invalid ConVarRef has no registration in any pinned PC module.
	else if ( !Q_stricmp(name,"SpnLookSensitivity") )
	{ float x=(float)value-2.0f; float adjustment=(x-1.0f)*0.5f; if ( x<=0.0f ) adjustment*=0.5f; CUIGameData::Get()->SetLookSensitivity(adjustment+1.0f); }
}
void GameOptions::OnCommand(const char *command) { BaseClass::OnCommand(command); }
}
