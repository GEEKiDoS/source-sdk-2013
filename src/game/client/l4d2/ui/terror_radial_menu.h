// Copyright (c) 2006 Turtle Rock Studios, Inc.
// Public Alien Swarm SDK ancestry: swarm/vgui/radialmenu.h (SDK license).
// Mirror: ReactiveDrop/reactivedrop_public_src, commit
// 9231d40422194dfecca8b698698df4af7e0d99f1, src/game/client/swarm/vgui.
#ifndef TERROR_RADIAL_MENU_H
#define TERROR_RADIAL_MENU_H
#ifdef _WIN32
#pragma once
#endif
#include "game/client/iviewport.h"
#include "vgui_controls/Frame.h"
class CRadialButton;
class CRadialMenu : public vgui::Frame, public IViewPortPanel
{
 DECLARE_CLASS_SIMPLE(CRadialMenu,vgui::Frame);
public:
 enum ButtonDir { CENTER,NORTH,NORTH_EAST,EAST,SOUTH_EAST,SOUTH,SOUTH_WEST,WEST,NORTH_WEST,NUM_BUTTON_DIRS };
 explicit CRadialMenu(IViewPort *viewport);
 virtual ~CRadialMenu();
 virtual const char *GetName() OVERRIDE {return "radialmenu";}
 virtual void SetData(KeyValues *data) OVERRIDE;
 virtual void Reset() OVERRIDE {} // Native inherited nullsub, not an absent behavior.
 virtual void Update() OVERRIDE;
 virtual bool NeedsUpdate() OVERRIDE {return false;}
 virtual bool HasInputElements() OVERRIDE {return true;}
 virtual void ShowPanel(bool show) OVERRIDE;
 virtual GameActionSet_t GetPreferredActionSet() OVERRIDE {return GAME_ACTION_SET_NONE;}
 virtual vgui::VPANEL GetVPanel() OVERRIDE {return BaseClass::GetVPanel();}
 virtual bool IsVisible() OVERRIDE {return BaseClass::IsVisible();}
 virtual void SetParent(vgui::VPANEL parent) OVERRIDE {BaseClass::SetParent(parent);}
 void ChooseArmedButton();
 void StartFade();
 bool IsFading() {return BaseClass::IsVisible() && m_fading;}
 void OnCursorEnteredButton(int x,int y,CRadialButton *button);
 static const char *ButtonNameFromDir(ButtonDir dir);
 static ButtonDir DirFromButtonName(const char *name);
protected:
 virtual vgui::Panel *CreateControlByName(const char *name) OVERRIDE;
 virtual void ApplySchemeSettings(vgui::IScheme *scheme) OVERRIDE;
 virtual void Paint() OVERRIDE;
 virtual void OnCommand(const char *command) OVERRIDE;
 virtual void OnThink() OVERRIDE;
private:
 void HandleControlSettings();
 void SetArmedButtonDir(ButtonDir dir);
 void UpdateButtonBounds();
 void SendCommand(const char *command) const;
 IViewPort *m_viewport;
 CRadialButton *m_buttons[NUM_BUTTON_DIRS];
 ButtonDir m_armedButtonDir;
 int m_cursorX,m_cursorY;
 bool m_fading;
 float m_fadeStart;
 Color m_lineColor;
 KeyValues *m_resource,*m_menuData;
 int m_minButtonX,m_minButtonY,m_maxButtonX,m_maxButtonY;
};
bool IsRadialMenuOpen(const char *name,bool includeFadingOut=false);
bool HasTerrorRadialInputOverride(); // Native107C97A8, owned by ShowPanel/destruction.
void OpenRadialMenu(const char *name);
void CloseRadialMenu(const char *name,bool sendCommand);
void FlushClientMenus();
#endif
