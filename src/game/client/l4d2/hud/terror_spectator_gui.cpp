//========= Copyright Valve Corporation, All rights reserved. ============//
// C2CBEF0..C2CD1BC. Native CTerrorViewportPanel, never CSpectatorGUI/HL2MP.
#include "cbase.h"
#include "l4d2/hud/terror_spectator_gui.h"
#include "l4d2/hud/terror_death_panel.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_transitioning_player.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/entities/c_scavenge_progress_display.h"
#include "l4d2/hud/terror_client_colors.h"
#include "hud.h"
#include "iclientmode.h"
#include "vgui/IScheme.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/TextImage.h"
#include "vgui_controls/AnimationController.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#if _MSC_VER >= 1933
#pragma fp_contract(off)
#endif
#endif
static C_TerrorPlayerResource *SpectatorResource(){return static_cast<C_TerrorPlayerResource*>(g_PR);}
static int NativeSpectatorInteger(float value){return _mm_cvttss_si32(_mm_set_ss(value));}
static int NativeGhostDelay(C_TerrorPlayer *player,bool scrimmage)
{
 if(scrimmage?player->m_scrimmageType==1:player->m_scrimmageType!=0)return -1;
 if(player->GetBecomeGhostAt()-(double)gpGlobals->curtime<0.0)return 0;
 return NativeSpectatorInteger((float)(player->GetBecomeGhostAt()-(double)gpGlobals->curtime));
}
CTerrorSpectatorGUI::CTerrorSpectatorGUI(IViewPort *viewport):BaseClass(viewport,"specgui"),m_deathData(NULL),m_scoreboard(NULL),m_team(0),m_teamWritten(false),m_scoreboardVisible(false){}
CTerrorSpectatorGUI::~CTerrorSpectatorGUI(){if(m_deathData)m_deathData->deleteThis();}
bool CTerrorSpectatorGUI::IsVisible(){static ConVarRef menu("gameui_ingame_menu_active");return !menu.GetBool()&&BaseClass::IsVisible();}
bool CTerrorSpectatorGUI::NeedsUpdate(){C_TerrorPlayer *p=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer());return !p||!p->IsAlive();}
void CTerrorSpectatorGUI::SetData(KeyValues *data){if(m_deathData)m_deathData->deleteThis();m_deathData=data->MakeCopy();InvalidateLayout(false,true);}
void CTerrorSpectatorGUI::ShowPanel(bool state)
{
 if(BaseClass::IsVisible()==state)return;
 if(state)InvalidateLayout(false,true);
 else{g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("DeathPanelClose");if(m_deathData){m_deathData->deleteThis();m_deathData=NULL;SetControlVisible("death",false);}}
 BaseClass::ShowPanel(state);
}
void CTerrorSpectatorGUI::ApplySchemeSettings(vgui::IScheme *scheme)
{
 C_TerrorPlayer *local=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer());KeyValues *conditions=NULL;
 if(local&&local->GetTeamNumber()==TEAM_INFECTED)
 {
  CTerrorGameRules *rules=TerrorGameRules();
  if(rules&&(rules->IsScavengeMode()||rules->IsSurvivalMode())&&!rules->IsInIntro()){conditions=new KeyValues("conditions");conditions->SetInt("if_overlap_scavenge_timer",1);}
  else{CHudElement *element=gHUD.FindElement("CHudScavengeProgress");CHudScavengeProgress *progress=dynamic_cast<CHudScavengeProgress*>(element);if(progress&&progress->ShouldDraw()){conditions=new KeyValues("conditions");conditions->SetInt("if_overlap_scavenge_can",1);}}
  LoadControlSettings("Resource/UI/SpectatorInfected.res",NULL,NULL,conditions);
  DeathPanel *death=dynamic_cast<DeathPanel*>(FindChildByName("death"));if(death)death->SetDeathStats(m_deathData);
 }
 else{LoadControlSettings(local&&local->GetTeamNumber()==TEAM_SURVIVOR?"Resource/UI/SpectatorSurvivor.res":"Resource/UI/Spectator.res");g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("DeathPanelClose");}
 if(conditions)conditions->deleteThis();BaseClass::ApplySchemeSettings(scheme);
 vgui::Label *label=dynamic_cast<vgui::Label*>(FindChildByName("playerlabel"));
 if(label){label->SetFont(scheme->GetFont("SpectatorTarget",true));label->SetVisible(false);if(label->GetTextImage()){vgui::IScheme *own=vgui::scheme()->GetIScheme(GetScheme());vgui::HFont fallback=own->GetFont("DefaultVerySmallFallBack",false);if(fallback)label->GetTextImage()->SetUseFallbackFont(true,fallback);}}
 SetControlVisible("RescueMessage",TerrorGameRules()&&TerrorGameRules()->IsCoopMode());
}
void CTerrorSpectatorGUI::UpdatePlayerLabel(int observerMode,C_BaseEntity *target)
{
 if(!target||!g_PR){SetControlString("playerlabel","");return;}
 vgui::Label *label=dynamic_cast<vgui::Label*>(FindChildByName("playerlabel"));if(!label)return;
 wchar_t text[80];
 if(target->IsPlayer())
 {
  C_BasePlayer *player=static_cast<C_BasePlayer*>(target);Color color=GetPlayerColor(player);label->SetFgColor(color);
  wchar_t name[64];g_pVGuiLocalize->ConvertANSIToUnicode(UTIL_SafeName(player->GetPlayerName()),name,sizeof(name));
  const wchar_t *format=g_pVGuiLocalize->Find("#Spec_PlayerItem");if(!format)return;g_pVGuiLocalize->ConstructString(text,sizeof(text),format,1,name);
 }
 else
 {
  C_TransitioningPlayer *transition=dynamic_cast<C_TransitioningPlayer*>(target);char name[32];Q_strncpy(name,transition?transition->GetPlayerName():target->GetIDString(),sizeof(name));Color color=transition?transition->GetTeamColor():g_PR->GetTeamColor(target->GetTeamNumber());label->SetFgColor(color);g_pVGuiLocalize->ConvertANSIToUnicode(name,text,sizeof(text));
 }
 label->SetVisible(observerMode==OBS_MODE_IN_EYE);label->SetText(text);
 // Native cached health/name/color/observer handle have no reader in this
 // concrete owner; omitted, not mirrored into a second player resource.
}
void CTerrorSpectatorGUI::UpdateRescueMessage()
{
 C_TerrorPlayer *local=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer());if(!local)return;
 const char *token="";
 if(local->GetTeamNumber()!=TEAM_SPECTATOR)
 {
  C_TerrorPlayerResource *resource=SpectatorResource();
  if(resource&&resource->IsFinale())token="#L4D_Scenario_Finale_Death_Message";
  else
  {
   bool human=false,standing=false;
   for(int i=1;i<=gpGlobals->maxClients;++i){C_TerrorPlayer *p=ToTerrorPlayer(UTIL_PlayerByIndex(i));if(p&&p!=local&&p->IsAlive()&&!p->IsDormant()&&p->GetTeamNumber()==TEAM_SURVIVOR){if(!p->IsBot())human=true;if(!p->IsIncapacitated())standing=true;}}
   if(human&&standing){if(TerrorGameRules()&&TerrorGameRules()->IsCoopMode())token="#L4D_Instructor_notify_awaiting_rescue";}
   else token="#L4D_Scenario_Survivors_Dead";
  }
 }
 const wchar_t *text=g_pVGuiLocalize->Find(token);SetDialogVariable("rescuemsg",text?text:L"");
}
void CTerrorSpectatorGUI::Update()
{
 if(!m_scoreboard)m_scoreboard=m_pViewport->FindPanelByName("scores");
 if(m_scoreboard&&m_scoreboard->IsVisible()!=m_scoreboardVisible){m_scoreboardVisible=m_scoreboard->IsVisible();SetControlVisible("SkullIcon",!m_scoreboardVisible);SetControlVisible("DeathMessage",!m_scoreboardVisible);SetControlVisible("RescueMessage",!m_scoreboardVisible);}
 CTerrorGameRules *rules=TerrorGameRules();
 if(rules&&(rules->IsSurvivalMode()||(rules->IsScavengeMode()&&!rules->IsInIntro()))){SetControlVisible("SkullIcon",false);SetControlVisible("DeathMessage",false);SetControlVisible("RescueMessage",false);}
 C_TerrorPlayer *local=ToTerrorPlayer(C_BasePlayer::GetLocalPlayer());if(!local)return;
 int team=local->GetTeamNumber();if(m_teamWritten&&team!=m_team)InvalidateLayout(true,true);m_team=team;m_teamWritten=true;
 C_TerrorPlayerResource *resource=SpectatorResource();if(resource){bool show=resource->IsSurvivorTeamReadyTime()&&resource->GetSurvivorTeamReadyTime()>=0;SetControlVisible("Seconds_1",show);SetControlVisible("Seconds_2",show);SetControlVisible("Countdown",show);char number[32];Q_snprintf(number,sizeof(number),"%d",resource->GetSurvivorTeamReadyTime());SetControlString("Countdown",number);}
 int mode=local->GetObserverMode();C_BaseEntity *target=local->GetObserverTarget();if(target&&target->IsDormant())target=NULL;if(mode!=OBS_MODE_CHASE&&mode!=OBS_MODE_IN_EYE)target=NULL;UpdatePlayerLabel(mode,target);UpdateRescueMessage();
 if(team!=TEAM_INFECTED)return;
 const char *text="";
 if((NativeGhostDelay(local,false)>=0||NativeGhostDelay(local,true)>=0||local->m_ghostSpawnState!=0||local->m_scrimmageType==2)&&local->State_Get()==static_cast<CSPlayerState>(6))
 {
  switch(local->m_ghostSpawnState){case 1:text="Spawning has been disabled...";break;case 2:text="#L4D_pz_waiting_for_survivors";break;case 4:text="#L4D_pz_finale_setup";break;case 8:text="#L4D_pz_tank_battle";break;case 16:text="#L4D_pz_survivors_escaped";break;case 64:text="#L4D_pz_finale_wave";break;
  default:{int seconds=NativeSpectatorInteger(local->m_scrimmageStartTime);bool waiting=(local->m_scrimmageType==1&&seconds>=0)||local->m_scrimmageType==2;if(!waiting)seconds=NativeGhostDelay(local,false);const char *token=waiting?"L4D_pz_scrimmage_wait":"L4D_pz_spawn_countdown";if(waiting||seconds>1){const wchar_t *format=g_pVGuiLocalize->Find(token);if(format&&(!waiting||seconds>0)){char value[32];wchar_t number[32],output[128];Q_snprintf(value,sizeof(value),"%d",seconds);g_pVGuiLocalize->ConvertANSIToUnicode(value,number,sizeof(number));g_pVGuiLocalize->ConstructString(output,sizeof(output),format,1,number);SetControlString("InfectedState",output);return;}text="";}else text=seconds==1?"#L4D_pz_spawn_countdown_single":"#L4D_pz_spawn_spawning";break;}}
 }
 SetControlString("InfectedState",text);
}
#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
