//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/achievements_l4d2.h"
#include "achievementmgr.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/effects/terror_glow_visibility.h"
#include "steam/steam_api.h"
#include "tier1/KeyValues.h"
#include "vgui/ILocalize.h"
#include "c_playerresource.h"
#include "tier0/icommandline.h"
#include <time.h>
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

static C_TerrorPlayer *AchievementPlayer() { return dynamic_cast<C_TerrorPlayer *>( C_BasePlayer::GetLocalPlayer() ); }
static C_TerrorPlayer *AchievementUser( int userid ) { return dynamic_cast<C_TerrorPlayer *>( UTIL_PlayerByUserId( userid ) ); }
static bool EventIs( const char *name, const char *expected ) { return Q_stricmp( name, expected ) == 0; }
static bool LocalUser( IGameEvent *event, const char *field = "userid" ) { C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); return player && player->GetUserID() == event->GetInt( field ); }
static bool CooperativeAchievements() { return g_TerrorModes.IsBaseMode("coop") || g_TerrorModes.IsBaseMode("realism"); }
static bool LiveSurvivor( C_TerrorPlayer *player ) { return player && player->m_lifeState == LIFE_ALIVE && !player->m_isIncapacitated; }
static bool IsAchievementMap( const char *map ) { char current[MAX_PATH]; V_FileBase( engine->GetLevelName(), current, sizeof(current) ); return !Q_stricmp(current,map); }
static int IncrementNative32( int &value ) { value = (int)( (unsigned int)value + 1u ); return value; }

CLeft4DeadAchievement::CLeft4DeadAchievement() : m_l4dID(0), m_team(-1), m_category(-1), m_failed(false), m_events(NULL), m_componentLabels(NULL), m_eventCount(0) {}
void CLeft4DeadAchievement::Configure( int internalID, int l4dID, const char *name, int points, int goal, int flags, int team, int category )
{
 SetAchievementID(internalID); m_l4dID=l4dID; SetName(name); SetPointValue(points); SetHideUntilAchieved(false); SetGoal(goal); SetFlags(flags); SetGameDirFilter("mod_l4d2"); m_team=team; m_category=category;
}
const char *CLeft4DeadAchievement::MapName() const { return m_pAchievementMgr->GetMapName(); }
void CLeft4DeadAchievement::AddDistinct( CUtlVector<int> &list, int value ) { if (list.Find(value)==list.InvalidIndex()) list.AddToTail(value); }
bool CLeft4DeadAchievement::IsActive() { return !IsAchieved() && (!m_pMapNameFilter || !Q_strcmp(MapName(),m_pMapNameFilter)); } //233210,10D60.
void CLeft4DeadAchievement::PrintAdditionalStatus() { if(m_failed) Msg("%-20s","FAILED"); }
void CLeft4DeadAchievement::ResetNativeState() { m_iCount=0; SetAchieved(false); if(HasComponents()) SetComponentBits(0); m_failed=false; } //233460; no transition-vector clear.
void CLeft4DeadAchievement::UpdateSurvivalBestTimes( KeyValues *records ) {} // Native non-medal classes do not consume this callback.
const char *CLeft4DeadAchievement::GetComponentName( int index ) const { return (m_iFlags & ACH_HAS_COMPONENTS) && index>=0 && index<m_iNumComponents && m_componentLabels ? m_componentLabels[index] : ""; }
const char *CLeft4DeadAchievement::GetIcon()
{
 static char icon[MAX_PATH];
 if(IsAchieved()) { Q_strncpy(icon,"achievements/",sizeof(icon)); Q_strncat(icon,GetName(),sizeof(icon)); }
 else { static const char *locked[]={"ACH_COOP_LOCKED","ACH_COOP_LOCKED","ACH_VERSUS_LOCKED","ACH_SCAVENGE_LOCKED","ASSET_LOCKED","ASSET_DLC1_LOCKED","ASSET_DLC2_LOCKED"}; if((unsigned)m_category<ARRAYSIZE(locked)) Q_snprintf(icon,sizeof(icon),"achievements/%s",locked[m_category]); }
 return icon; // Native category7/default deliberately retains the shared previous string.
}
void CLeft4DeadAchievement::IncrementCount( int increment )
{
 C_TerrorPlayer *player=AchievementPlayer();
 if(RequiresAlive() && player && (player->GetObserverMode()!=OBS_MODE_NONE || player->m_lifeState != LIFE_ALIVE)) return;
 if(!player) return; // Skip only the native NULL reads before its late pointer gate.
 if((player->GetTeamNumber()==m_team || m_team==0) && !player->IsBot()) CBaseAchievement::IncrementCount(increment);
}
void CLeft4DeadAchievement::OnCampaignStart() { m_failed=false; m_completedMaps.RemoveAll(); }
void CLeft4DeadAchievement::BeginCampaignIfFirstChapter()
{
 KeyValues *details=TerrorBuildConsumedClientGameProjection(); if(!details) return;
 KeyValues *mission=NULL; KeyValues *chapter=g_TerrorModes.GetChapterInfo(details,&mission);
 if(chapter && mission && mission->GetInt("BuiltIn",0) && details->GetInt("Game/chapter",0)==1) OnCampaignStart();
 details->deleteThis();
 // Native match-user-storage per-slot save233370->0039F0 unavailable under admission#F781 boundaryC.
}
bool CLeft4DeadAchievement::PlayedEntireCampaign() const
{
 KeyValues *details=TerrorBuildConsumedClientGameProjection(); if(!details) return false;
 KeyValues *mission=NULL; KeyValues *chapter=g_TerrorModes.GetChapterInfo(details,&mission);
 if(!chapter || !mission || !mission->GetInt("BuiltIn",0)) { details->deleteThis(); return false; }
 int number=details->GetInt("Game/chapter",0); details->SetInt("Game/chapter",(int)((unsigned)number+1u));
 bool complete=false;
 if(!g_TerrorModes.GetChapterInfo(details,NULL) && number>=1 && m_completedMaps.Count()>=number-1)
 {
  complete=true;
  for(int i=m_completedMaps.Count()-1,n=number-1;i>=m_completedMaps.Count()-number+1;--i,--n)
  {
   details->SetInt("Game/chapter",n); KeyValues *previous=g_TerrorModes.GetChapterInfo(details,NULL);
   if(!previous || Q_stricmp(previous->GetString("map",""),m_completedMaps[i].String())) { complete=false; break; }
  }
 }
 details->deleteThis(); return complete;
}
void CLeft4DeadAchievement::ListenForEvents()
{
 if(m_events) { for(int i=0;i<m_eventCount;++i) ListenForGameEvent(m_events[i]); }
 else DevWarning("Achievement with no custom events defined: %s",GetName());
 ListenForGameEvent("map_transition"); ListenForGameEvent("finale_win"); ListenForGameEvent("difficulty_changed"); ListenForGameEvent("vote_passed"); ListenForGameEvent("round_end");
 OnMapStart(); BeginCampaignIfFirstChapter();
}
void CLeft4DeadAchievement::FireGameEvent_Internal( IGameEvent *event )
{
 const char *name=event->GetName();
 if(m_events) for(int i=0;i<m_eventCount;++i) if(!Q_stricmp(name,m_events[i]))
 {
  if(GetFlags()&0x800) { C_TerrorPlayer *player=AchievementPlayer(); if(player && event->GetInt("userid")==player->GetUserID()) OnLocalEvent(player,name,event); }
  OnEvent(name,event); // Native local hook precedes ordinary hook, duplicate names still dispatch repeatedly.
 }
 if(!Q_strcmp(name,"map_transition")) { OnMapEnd(); m_completedMaps.AddToTail(CUtlString(MapName())); }
 else if(!Q_strcmp(name,"difficulty_changed")) OnDifficultyChanged(event->GetInt("newDifficulty"),event->GetInt("oldDifficulty"));
 else if(!Q_strcmp(name,"finale_win")) { OnMapEnd(); if(PlayedEntireCampaign()) OnCampaignFinish(event->GetString("map_name",""),event->GetInt("difficulty")); m_completedMaps.RemoveAll(); }
 else if(!Q_strcmp(name,"vote_passed")) { const char *details=event->GetString("details",""); if(!Q_stricmp(details,"#L4D_vote_passed_restart_game") || !Q_stricmp(details,"#L4D_vote_passed_return_to_lobby")) OnCampaignStart(); }
 else if(!Q_strcmp(name,"round_end") && event->GetInt("reason")==5 && TerrorGameRules() && TerrorGameRules()->IsScavengeSecondHalf())
 {
  OnMapEnd();
  KeyValues *details=TerrorBuildConsumedClientGameProjection(); bool final=true;
  if(details) { details->SetInt("Game/chapter",(int)((unsigned)details->GetInt("Game/chapter")+1u)); final=!g_TerrorModes.GetChapterInfo(details,NULL); details->deleteThis(); }
  if(final) { if(PlayedEntireCampaign()) OnCampaignFinish(MapName(),0); m_completedMaps.RemoveAll(); }
  else { bool found=false; FOR_EACH_VEC(m_completedMaps,i) if(!Q_stricmp(m_completedMaps[i].String(),MapName())) found=true; if(!found) m_completedMaps.AddToTail(CUtlString(MapName())); }
 }
}
void CLeft4DeadCompetitiveAchievement::ListenForEvents() { ListenForGameEvent("start_score_animation"); ListenForGameEvent("scavenge_round_start"); ListenForGameEvent("versus_round_start"); CLeft4DeadAchievement::ListenForEvents(); ResetCompetitive(); }
void CLeft4DeadCompetitiveAchievement::OnScoreAnimation() { ResetCompetitive(); m_failed=true; }
void CLeft4DeadCompetitiveAchievement::OnCompetitiveRoundStart() { ResetCompetitive(); m_failed=false; }
void CLeft4DeadCompetitiveAchievement::IncrementCount( int increment ) { if(!m_failed) CLeft4DeadAchievement::IncrementCount(increment); }
void CLeft4DeadCompetitiveAchievement::FireGameEvent_Internal( IGameEvent *event )
{
 if(!g_TerrorModes.IsBaseMode("versus") && !g_TerrorModes.IsBaseMode("scavenge") && !(g_TerrorModes.IsBaseMode("survival") && g_TerrorModes.IsPlayerControlledZombies())) return;
 CLeft4DeadAchievement::FireGameEvent_Internal(event);
 if(!Q_strcmp(event->GetName(),"start_score_animation")) OnScoreAnimation();
 else if(!Q_strcmp(event->GetName(),"scavenge_round_start") || !Q_strcmp(event->GetName(),"versus_round_start")) OnCompetitiveRoundStart();
}

bool CLeft4DeadAchievement::LocalPlayerCanEarn()
{
 // C27CCD0 uses the INTERNAL achievement ID, not the exported L4D award ID.
 if(TerrorGameRules() && TerrorGameRules()->IsScriptedModeActive() && GetAchievementID()!=60 && GetAchievementID()!=67 && GetAchievementID()!=66)
 { KeyValues *mode=g_TerrorModes.GetCurrentMode(); if(mode && !Q_stricmp(mode->GetName(),"ultrarealism")) return false; }
 return true;
}
void CLeft4DeadAchievement::OnComponentEvent( const char *name ) { if(LocalPlayerCanEarn()) CBaseAchievement::OnComponentEvent(name); }
void CLeft4DeadAchievement::EnsureComponentBitSetAndEvaluate( int bit ) { if((unsigned)bit<64u && LocalPlayerCanEarn()) CBaseAchievement::EnsureComponentBitSetAndEvaluate(bit); }

static const char *AchievementGenderName( int gender ) { static const char *names[]={"ceda","crawler","undistractable","fallen","riot_control","clown","jimmy","hospital_patient"}; return gender>=11 && gender<=18 ? names[gender-11] : ""; }

const wchar_t *CLeft4DeadAchievement::GetComponentLabel( int index ) { return g_pVGuiLocalize->Find(GetComponentName(index)); }

static const char *s_melee[] = { "cricket_bat", "crowbar", "electric_guitar", "fireaxe", "frying_pan", "katana", "machete", "tonfa" };
static const char *s_melee_labels[] = { "#L4D_Melee_Cricket_Bat", "#L4D_Melee_Crowbar", "#L4D_Melee_Electric_Guitar", "#L4D_Melee_FireAxe", "#L4D_Melee_Frying_Pan", "#L4D_Melee_Katana", "#L4D_Melee_Machete", "#L4D_Melee_Tonfa" };
static const char *s_campaigns[] = { "c1m4_atrium", "c2m5_concert", "c3m4_plantation", "c4m5_milltown_escape", "c5m5_bridge" };
static const char *s_campaign_labels[] = { "#L4D360UI_CampaignName_C1", "#L4D360UI_CampaignName_C2", "#L4D360UI_CampaignName_C3", "#L4D360UI_CampaignName_C4", "L4D360UI_CampaignName_C5" };
static const char *s_crowbars[] = { "c7m1_docks", "c7m2_barge", "c7m3_port", "c8m2_subway", "c8m3_sewers", "c8m4_interior", "c8m5_rooftop", "c9m1_alleys", "c9m2_lots", "c10m2_drainage", "c10m3_ranchhouse", "c10m4_mainstreet", "c10m5_houseboat", "c11m2_offices", "c11m3_garage", "c11m4_terminal", "c11m5_runway", "c12m2_traintunnel", "c12m3_bridge", "c12m5_cornfield", "c14m1_junkyard", "c14m2_lighthouse" };
static const char *s_crowbar_labels[] = { "#L4D360UI_LevelName_SURVIVAL_C7M1", "#L4D360UI_LevelName_SCAVENGE_C7M2", "#L4D360UI_LevelName_SURVIVAL_C7M3", "#L4D360UI_LevelName_SURVIVAL_C8M2", "#L4D360UI_LevelName_SURVIVAL_C8M3", "#L4D360UI_LevelName_SURVIVAL_C8M4", "#L4D360UI_LevelName_SURVIVAL_C8M5", "#L4D360UI_LevelName_SURVIVAL_C9M1", "#L4D360UI_LevelName_SURVIVAL_C9M2", "#L4D360UI_LevelName_SURVIVAL_C10M2", "#L4D360UI_LevelName_SURVIVAL_C10M3", "#L4D360UI_LevelName_SURVIVAL_C10M4", "#L4D360UI_LevelName_SURVIVAL_C10M5", "#L4D360UI_LevelName_SCAVENGE_C11M2", "#L4D360UI_LevelName_SCAVENGE_C11M3", "#L4D360UI_LevelName_SCAVENGE_C11M4", "#L4D360UI_LevelName_SCAVENGE_C11M5", "#L4D360UI_LevelName_SURVIVAL_C12M2", "#L4D360UI_LevelName_SCAVENGE_C12M3", "#L4D360UI_LevelName_SCAVENGE_C12M5", "#L4D360UI_LevelName_SURVIVAL_C14M1", "#L4D360UI_LevelName_SURVIVAL_C14M2" };
static const char *s_uncommon[] = { "ceda", "crawler", "undistractable", "clown" };
static const char *s_uncommon_labels[] = { "#L4D_Gender_CEDA", "#L4D_Gender_Crawler", "#L4D_Gender_Undistractable", "#L4D_Gender_Clown" };
static const char *s_survival[] = { "survival_c1m2_streets", "survival_c1m4_atrium", "survival_c2m1_highway", "survival_c2m4_barns", "survival_c2m5_concert", "survival_c3m1_plankcountry", "survival_c3m3_shantytown", "survival_c3m4_plantation", "survival_c4m1_milltown_a", "survival_c4m2_sugarmill_a", "survival_c4m3_sugarmill_b", "survival_c5m1_waterfront", "survival_c5m2_park", "survival_c5m3_cemetery", "survival_c5m4_quarter", "survival_c5m5_bridge", "survival_c6m1_riverbank", "survival_c6m2_bedlam", "survival_c6m3_port", "survival_c7m1_docks", "survival_c7m2_barge", "survival_c7m3_port", "survival_c8m2_subway", "survival_c8m3_sewers", "survival_c8m4_interior", "survival_c8m5_rooftop", "survival_c9m1_alleys", "survival_c9m2_lots", "survival_c10m2_drainage", "survival_c10m3_ranchhouse", "survival_c10m4_mainstreet", "survival_c10m5_houseboat", "survival_c11m2_offices", "survival_c11m3_garage", "survival_c11m4_terminal", "survival_c11m5_runway", "survival_c12m2_traintunnel", "survival_c12m3_bridge", "survival_c12m5_cornfield", "survival_c13m3_memorialbridge", "survival_c13m4_cutthroatcreek", "survival_c14m1_junkyard", "survival_c14m2_lighthouse" };
static const char *s_survival_labels[] = { "#L4D360UI_LevelName_SURVIVAL_C1M2", "#L4D360UI_LevelName_SURVIVAL_C1M4", "#L4D360UI_LevelName_SURVIVAL_C2M1", "#L4D360UI_LevelName_SURVIVAL_C2M4", "#L4D360UI_LevelName_SURVIVAL_C2M5", "#L4D360UI_LevelName_SURVIVAL_C3M1", "#L4D360UI_LevelName_SURVIVAL_C3M3", "#L4D360UI_LevelName_SURVIVAL_C3M4", "#L4D360UI_LevelName_SURVIVAL_C4M1", "#L4D360UI_LevelName_SURVIVAL_C4M2", "#L4D360UI_LevelName_SURVIVAL_C4M3", "#L4D360UI_LevelName_SURVIVAL_C5M1", "#L4D360UI_LevelName_SURVIVAL_C5M2", "#L4D360UI_LevelName_SURVIVAL_C5M3", "#L4D360UI_LevelName_SURVIVAL_C5M4", "#L4D360UI_LevelName_SURVIVAL_C5M5", "#L4D360UI_LevelName_SURVIVAL_C6M1", "#L4D360UI_LevelName_SURVIVAL_C6M2", "#L4D360UI_LevelName_SURVIVAL_C6M3", "#L4D360UI_LevelName_SURVIVAL_C7M1", "#L4D360UI_LevelName_SCAVENGE_C7M2", "#L4D360UI_LevelName_SURVIVAL_C7M3", "#L4D360UI_LevelName_SURVIVAL_C8M2", "#L4D360UI_LevelName_SURVIVAL_C8M3", "#L4D360UI_LevelName_SURVIVAL_C8M4", "#L4D360UI_LevelName_SURVIVAL_C8M5", "#L4D360UI_LevelName_SURVIVAL_C9M1", "#L4D360UI_LevelName_SURVIVAL_C9M2", "#L4D360UI_LevelName_SURVIVAL_C10M2", "#L4D360UI_LevelName_SURVIVAL_C10M3", "#L4D360UI_LevelName_SURVIVAL_C10M4", "#L4D360UI_LevelName_SURVIVAL_C10M5", "#L4D360UI_LevelName_SCAVENGE_C11M2", "#L4D360UI_LevelName_SCAVENGE_C11M3", "#L4D360UI_LevelName_SCAVENGE_C11M4", "#L4D360UI_LevelName_SCAVENGE_C11M5", "#L4D360UI_LevelName_SURVIVAL_C12M2", "#L4D360UI_LevelName_SCAVENGE_C12M3", "#L4D360UI_LevelName_SCAVENGE_C12M5", "#L4D360UI_LevelName_SURVIVAL_C13M3", "#L4D360UI_LevelName_SURVIVAL_C13M4", "#L4D360UI_LevelName_SURVIVAL_C14M1", "#L4D360UI_LevelName_SURVIVAL_C14M2" };
static const char *s_survival_campaign_labels[] = { "#L4D360UI_CampaignName_C1", "#L4D360UI_CampaignName_C1", "#L4D360UI_CampaignName_C2", "#L4D360UI_CampaignName_C2", "#L4D360UI_CampaignName_C2", "#L4D360UI_CampaignName_C3", "#L4D360UI_CampaignName_C3", "#L4D360UI_CampaignName_C3", "#L4D360UI_CampaignName_C4", "#L4D360UI_CampaignName_C4", "#L4D360UI_CampaignName_C4", "#L4D360UI_CampaignName_C5", "#L4D360UI_CampaignName_C5", "#L4D360UI_CampaignName_C5", "#L4D360UI_CampaignName_C5", "#L4D360UI_CampaignName_C5", "#L4D360UI_CampaignName_C6", "#L4D360UI_CampaignName_C6", "#L4D360UI_CampaignName_C6", "#L4D360UI_CampaignName_C7", "#L4D360UI_CampaignName_C7", "#L4D360UI_CampaignName_C7", "#L4D360UI_CampaignName_C8", "#L4D360UI_CampaignName_C8", "#L4D360UI_CampaignName_C8", "#L4D360UI_CampaignName_C8", "#L4D360UI_CampaignName_C9", "#L4D360UI_CampaignName_C9", "#L4D360UI_CampaignName_C10", "#L4D360UI_CampaignName_C10", "#L4D360UI_CampaignName_C10", "#L4D360UI_CampaignName_C10", "#L4D360UI_CampaignName_C11", "#L4D360UI_CampaignName_C11", "#L4D360UI_CampaignName_C11", "#L4D360UI_CampaignName_C11", "#L4D360UI_CampaignName_C12", "#L4D360UI_CampaignName_C12", "#L4D360UI_CampaignName_C12", "#L4D360UI_CampaignName_C13", "#L4D360UI_CampaignName_C13", "#L4D360UI_CampaignName_C14", "#L4D360UI_CampaignName_C14" };
static const char *s_challenge_labels[] = { "#L4D360UI_Mutation1_Component", "#L4D360UI_Mutation2_Component", "#L4D360UI_Mutation3_Component", "#L4D360UI_Mutation4_Component", "#L4D360UI_Mutation5_Component", "#L4D360UI_Mutation6_Component", "#L4D360UI_Mutation7_Component", "#L4D360UI_Mutation8_Component", "#L4D360UI_Mutation9_Component", "#L4D360UI_Mutation10_Component", "#L4D360UI_Mutation11_Component", "#L4D360UI_Mutation14_Component", "#L4D360UI_Mutation13_Component", "#L4D360UI_Mutation12_Component", "#L4D360UI_Mutation15_Component", "#L4D360UI_Mutation16_Component", "#L4D360UI_Mutation17_Component", "#L4D360UI_Mutation18_Component", "#L4D360UI_Mutation19_Component", "#L4D360UI_Mutation20_Component" };


class CAchievementSurviveCampaign : public CLeft4DeadAchievement
{
protected:
 const char *m_finalMap;
public:
 CAchievementSurviveCampaign() : m_finalMap(NULL) {}
protected:
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnAchieved() OVERRIDE {} // Native233AE0 and DLC equivalents.
 virtual void OnCampaignFinish( const char *map, int difficulty ) OVERRIDE
 { C_TerrorPlayer *p=AchievementPlayer(); if(CooperativeAchievements() && !Q_strnicmp(map,m_finalMap,Q_strlen(m_finalMap)) && !m_failed && LiveSurvivor(p)) IncrementCount(); }
};
class CBaseAchievementSurviveAllAtDifficulty : public CLeft4DeadAchievement
{
protected:
 int m_difficulty;
public:
 CBaseAchievementSurviveAllAtDifficulty() : m_difficulty(0) {}
protected:
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnDifficultyChanged( int now, int old ) OVERRIDE { m_failed=true; }
 virtual void OnCampaignFinish( const char *map, int difficulty ) OVERRIDE
 { if(CooperativeAchievements() && difficulty==m_difficulty && !m_failed && LiveSurvivor(AchievementPlayer())) OnComponentEvent(map); }
};
class CAchievementEarnAllMedalsBase : public CLeft4DeadAchievement
{
protected:
 int64 m_seconds;
 int m_medal;
 wchar_t *m_labels[43];
public:
 CAchievementEarnAllMedalsBase() : m_seconds(0),m_medal(-1) { Q_memset(m_labels,0,sizeof(m_labels)); }
 virtual void UpdateSurvivalBestTimes( KeyValues *records ) OVERRIDE
 {
  // Native238770 breaks at its FIRST qualifying record, not a sweep of all records.
  for(KeyValues *row=records ? records->GetFirstSubKey() : NULL;row;row=row->GetNextKey())
  { KeyValues *time=records->FindKey(row->GetName()); if(time && (double)(int64)time->GetUint64("besttime",0) * (double)0.01f >= (double)m_seconds) { OnComponentEvent(row->GetName()); break; } }
 }
 virtual const wchar_t *GetComponentLabel( int index ) OVERRIDE
 {
  if((unsigned)index>=ARRAYSIZE(m_labels)) return NULL; // Standing actual OOB skip.
  if(m_labels[index]) return m_labels[index];
  const wchar_t *campaign=g_pVGuiLocalize->Find(s_survival_campaign_labels[index]), *map=g_pVGuiLocalize->Find(s_survival_labels[index]);
  if(!campaign || !map) return NULL;
  m_labels[index]=new wchar_t[128]; g_pVGuiLocalize->ConstructString(m_labels[index],256,L"%s1 - %s2",2,map,campaign); return m_labels[index];
 }
protected:
 void InitMedals( int id, int seconds ) { static const char *events[]={"survival_round_start","survival_goal_reached"}; SetEvents(events,ARRAYSIZE(events)); SetComponents(s_survival,ARRAYSIZE(s_survival),s_survival_labels); SetGoal(43); m_medal=id; m_seconds=seconds; }
 virtual bool RequiresAlive() const OVERRIDE { return false; }
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("survival") || g_TerrorModes.IsPlayerControlledZombies()) return;
  if(EventIs(name,"survival_round_start")) m_failed=false;
  else if(EventIs(name,"survival_goal_reached") && event->GetInt("id")==m_medal)
  { C_TerrorPlayer *p=AchievementPlayer(); const char *goal=event->GetString("goalname",""); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed && (EventIs(goal,"nomedal") || EventIs(goal,"medal"))) { char key[MAX_PATH]; Q_snprintf(key,sizeof(key),"survival_%s",MapName()); OnComponentEvent(key); } }
 }
};

// Native factory 0x23a9f0; internal manager/message ID 47, L4D-facing ID 1.
class CAchievementSurviveMall : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c1m4_atrium";
 }
protected:
};

// Native factory 0x233bb0; internal manager/message ID 48, L4D-facing ID 2.
class CAchievementSurviveRoad : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c2m5_concert";
 }
protected:
};

// Native factory 0x233c80; internal manager/message ID 49, L4D-facing ID 3.
class CAchievementSurviveSwamp : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c3m4_plantation";
 }
protected:
};

// Native factory 0x233d50; internal manager/message ID 50, L4D-facing ID 4.
class CAchievementSurviveTown : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c4m5_milltown_escape";
 }
protected:
};

// Native factory 0x233e20; internal manager/message ID 9, L4D-facing ID 5.
class CAchievementSurviveBridge : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c5m5_bridge";
 }
protected:
};

// Native factory 0x233ef0; internal manager/message ID 82, L4D-facing ID 81.
class CAchievementSurviveLighthouse : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c14m2_lighthouse";
 }
protected:
};

// Native factory 0x234070; internal manager/message ID 83, L4D-facing ID 82.
class CAchievementSurviveLighthouseExpertRealism : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c14m2_lighthouse";
 }
protected:
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnDifficultyChanged( int now,int old ) OVERRIDE { m_failed=true; }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(g_TerrorModes.IsBaseMode("realism") && difficulty==3 && !m_failed && LiveSurvivor(AchievementPlayer()) && !Q_strnicmp(map,m_finalMap,Q_strlen(m_finalMap))) IncrementCount(); }
};

// Native factory 0x240ed0; internal manager/message ID 28, L4D-facing ID 6.
class CAchievementSurviveAllExpert : public CBaseAchievementSurviveAllAtDifficulty
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_difficulty=3; SetComponents(s_campaigns,ARRAYSIZE(s_campaigns),s_campaign_labels); SetGoal(5);
 }
protected:
};

// Native factory 0x23ae20; internal manager/message ID 29, L4D-facing ID 7.
class CAchievementSurviveExpertRealism : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnDifficultyChanged( int now,int old ) OVERRIDE { m_failed=true; }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(g_TerrorModes.IsBaseMode("realism") && difficulty==3 && !m_failed && LiveSurvivor(AchievementPlayer())) IncrementCount(); }
};

// Native factory 0x23af20; internal manager/message ID 11, L4D-facing ID 8.
class CAchievementMeleeOnly : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "non_melee_fired" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  m_failed=true;
 }
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(!m_failed) IncrementCount(); }
};

// Native factory 0x23b020; internal manager/message ID 21, L4D-facing ID 9.
class CAchievementHitHeadsWithMelee : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_decapitated" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x23b120; internal manager/message ID 12, L4D-facing ID 10.
class CAchievementKillWithEveryMelee : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
  SetComponents(s_melee,ARRAYSIZE(s_melee),s_melee_labels); SetGoal(8);
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer();
   if(p && LocalUser(event,"attacker") && event->GetInt("weapon_id")==19) { CTerrorMeleeWeapon *weapon=dynamic_cast<CTerrorMeleeWeapon *>(p->GetActiveWeapon()); if(weapon && weapon->GetMeleeInfo()) OnComponentEvent(weapon->GetMeleeWeaponScriptName()); }
 }
};

// Native factory 0x23b220; internal manager/message ID 14, L4D-facing ID 11.
class CAchievementKillInfectedWithChainsaw : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(LocalUser(event,"attacker") && event->GetInt("weapon_id")==20) IncrementCount();
 }
};

// Native factory 0x23b320; internal manager/message ID 13, L4D-facing ID 12.
class CAchievementKillTankMeleeOnly : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "tank_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(event->GetBool("melee_only") && AchievementUser(event->GetInt("attacker"))) IncrementCount();
 }
};

// Native factory 0x23b420; internal manager/message ID 98, L4D-facing ID 98.
class CAchievementKillWitchMeleeOnly : public CLeft4DeadAchievement
{
protected:
 CUtlVector<int> m_witches;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "witch_spawn", "witch_killed", "player_incapacitated", "player_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  int id=event->GetInt("witchid");
   if(EventIs(name,"witch_spawn")) AddDistinct(m_witches,id);
   else if(EventIs(name,"player_incapacitated") || EventIs(name,"player_death")) m_witches.FindAndRemove(event->GetInt("attackerentid"));
   else if(EventIs(name,"witch_killed") && m_witches.Find(id)!=m_witches.InvalidIndex() && event->GetBool("melee_only") && AchievementUser(event->GetInt("userid"))) IncrementCount();
 }
 virtual void OnMapStart() OVERRIDE { m_witches.RemoveAll(); }
};

// Native factory 0x23b540; internal manager/message ID 17, L4D-facing ID 13.
class CAchievementResSurvivorsWithDefib : public CLeft4DeadAchievement
{
protected:
 CUtlVector<int> m_teamVictims;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "defibrillator_used", "player_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *local=AchievementPlayer(); if(!local) return;
   if(EventIs(name,"defibrillator_used") && LocalUser(event)) { if(m_teamVictims.Find(event->GetInt("subject"))==m_teamVictims.InvalidIndex()) IncrementCount(); }
   else if(EventIs(name,"player_death")) { C_TerrorPlayer *attacker=AchievementUser(event->GetInt("attacker")); if(attacker && attacker->GetTeamNumber()==2) AddDistinct(m_teamVictims,event->GetInt("userid")); }
 }
 virtual void OnMapStart() OVERRIDE { m_teamVictims.RemoveAll(); }
};

// Native factory 0x23b670; internal manager/message ID 15, L4D-facing ID 14.
class CAchievementSpeedReviveWithAdrenaline : public CLeft4DeadAchievement
{
protected:
 CUtlVector<int> m_teamVictims;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "revive_success", "player_incapacitated" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *local=AchievementPlayer(); if(!local) return;
   if(EventIs(name,"revive_success") && LocalUser(event)) { if(m_teamVictims.Find(event->GetInt("subject"))==m_teamVictims.InvalidIndex()) { if(local->IsAdrenalineActive()) IncrementCount(); } }
   else if(EventIs(name,"player_incapacitated")) { C_TerrorPlayer *attacker=AchievementUser(event->GetInt("attacker")); if(attacker && attacker->GetTeamNumber()==2) AddDistinct(m_teamVictims,event->GetInt("userid")); }
 }
 virtual void OnMapStart() OVERRIDE { m_teamVictims.RemoveAll(); }
};

// Native factory 0x23b7a0; internal manager/message ID 90, L4D-facing ID 90.
class CAchievementRescueXPlayersTogether : public CLeft4DeadAchievement
{
protected:
 int m_door, m_rescued;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survivor_rescued" };
  SetEvents(events,ARRAYSIZE(events));
  m_door=0; m_rescued=0;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"survivor_rescued") && LocalUser(event,"rescuer")) { int door=event->GetInt("dooridx"); if(door!=m_door) { m_door=door; m_rescued=0; } if(IncrementNative32(m_rescued)==3) IncrementCount(); }
 }
};

// Native factory 0x23b890; internal manager/message ID 91, L4D-facing ID 91.
class CAchievementFreeSelfFromSmokerWithMelee : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "tongue_pull_stopped" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(event->GetInt("victim")==event->GetInt("userid") && event->GetInt("release_type")==4 && event->GetInt("damage_type")==4) IncrementCount();
 }
};

// Native factory 0x23b980; internal manager/message ID 92, L4D-facing ID 92.
class CAchievementKillLeapingHunterWithShotgun : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "pounce_attempt_stopped" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  int id=event->GetInt("weaponid"); if(id==3 || id==4 || id==8 || id==11) IncrementCount();
 }
};

// Native factory 0x23ba70; internal manager/message ID 93, L4D-facing ID 92.
class CAchievementDestroyTankRock : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "tank_rock_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x23bb60; internal manager/message ID 84, L4D-facing ID 84.
class CAchievementReviveWithActiveTank : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "revive_success" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *local=AchievementPlayer(); if(!local || !EventIs(name,"revive_success") || !LocalUser(event)) return;
   CUtlVector<C_TerrorPlayer *> tanks;
   for(int i=1;i<=gpGlobals->maxClients;++i) { C_TerrorPlayer *p=dynamic_cast<C_TerrorPlayer *>(UTIL_PlayerByIndex(i)); if(p && !p->IsDormant() && GameResources() && GameResources()->IsConnected(i) && p->GetTeamNumber()==3 && p->GetZombieClass()==8 && p->m_lifeState == LIFE_ALIVE && !p->IsGhost()) tanks.AddToTail(p); }
   FOR_EACH_VEC(tanks,i) { C_TerrorPlayer *p=tanks[i]; if(p && (p->GetAbsOrigin()-local->GetAbsOrigin()).LengthSqr()<=1000000.0f) IncrementCount(); }
 }
};

// Native factory 0x23bc50; internal manager/message ID 94, L4D-facing ID 94.
class CAchievementOneShotKillWitchBride : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "witch_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(event->GetBool("oneshot") && event->GetBool("bride") && !Q_stricmp(z_difficulty.GetString(),"Impossible")) IncrementCount();
 }
};

// Native factory 0x240f70; internal manager/message ID 95, L4D-facing ID 95.
class CAchievementPounceForXDamage : public CLeft4DeadCompetitiveAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "lunge_pounce" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(event->GetInt("damage")>=25) IncrementCount();
 }
};

// Native factory 0x23be00; internal manager/message ID 96, L4D-facing ID 96.
class CAchievementKillXJockeysWithMelee : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "jockey_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  const char *weapon=event->GetString("weapon",""); if(LocalUser(event,"attacker") && (EventIs(weapon,"shovel") || EventIs(weapon,"pitchfork"))) IncrementCount();
 }
};

// Native factory 0x23bef0; internal manager/message ID 97, L4D-facing ID 99.
class CAchievementKillXInfectedLifetime : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"infected_death") && LocalUser(event,"attacker")) IncrementCount();
 }
};

// Native factory 0x23bfe0; internal manager/message ID 18, L4D-facing ID 15.
class CAchievementDeployAmmoUpgrade : public CLeft4DeadAchievement
{
protected:
 int m_upgrade; CUtlVector<int> m_users;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "upgrade_pack_used", "upgrade_pack_added" };
  SetEvents(events,ARRAYSIZE(events));
  m_upgrade=-1;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"upgrade_pack_added")) { if(event->GetInt("upgradeid")==m_upgrade) { AddDistinct(m_users,event->GetInt("userid")); if(m_users.Count()==4) IncrementCount(); } }
   else if(EventIs(name,"upgrade_pack_used") && LocalUser(event)) { m_upgrade=event->GetInt("upgradeid"); m_users.RemoveAll(); }
 }
 virtual void OnMapStart() OVERRIDE { m_users.RemoveAll(); m_upgrade=-1; }
};

// Native factory 0x23c100; internal manager/message ID 19, L4D-facing ID 16.
class CAchievementIgniteInfectedFireAmmo : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "zombie_ignited" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(event->GetString("victimname",""),"Infected") && event->GetBool("fire_ammo")) IncrementCount();
 }
};

// Native factory 0x23c200; internal manager/message ID 20, L4D-facing ID 17.
class CAchievementKillZombiesGrenadeBlast : public CLeft4DeadAchievement
{
protected:
 int m_kills; float m_deadline;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
  m_kills=0; m_deadline=0.0f;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(LocalUser(event,"attacker") && event->GetInt("weapon_id")==21 && event->GetBool("blast")) { if(!(gpGlobals->curtime>=m_deadline)) { if(IncrementNative32(m_kills)>=15) IncrementCount(); } else { m_kills=1; m_deadline=gpGlobals->curtime+1.0f; } }
 }
 virtual void OnMapStart() OVERRIDE { m_kills=0; m_deadline=0.0f; }
};

// Native factory 0x23c300; internal manager/message ID 16, L4D-facing ID 18.
class CAchievementBileTank : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "vomit_bomb_tank" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(LocalUser(event)) IncrementCount();
 }
};

// Native factory 0x23c400; internal manager/message ID 6, L4D-facing ID 19.
class CAchievementKillEveryUncommonInfected : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
  SetComponents(s_uncommon,ARRAYSIZE(s_uncommon),s_uncommon_labels); SetGoal(4);
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(LocalUser(event,"attacker")) OnComponentEvent(AchievementGenderName(event->GetInt("gender")));
 }
};

// Native factory 0x23c500; internal manager/message ID 8, L4D-facing ID 20.
class CAchievementKillSubmergedMudMen : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"infected_death") && LocalUser(event,"attacker") && event->GetBool("submerged") && event->GetInt("gender")==12) IncrementCount();
 }
};

// Native factory 0x23c600; internal manager/message ID 10, L4D-facing ID 21.
class CAchievementCollectCEDAVials : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "weapon_pickup" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"weapon_pickup") && event->GetInt("weaponid")==25 && event->GetInt("dropped_by_infected")==11) IncrementCount();
 }
};

// Native factory 0x23c720; internal manager/message ID 102, L4D-facing ID 83.
class CAchievementCollectGoldenCrowbars : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "golden_crowbar_pickup" };
  SetEvents(events,ARRAYSIZE(events));
  SetComponents(s_crowbars,ARRAYSIZE(s_crowbars),s_crowbar_labels); SetGoal(22);
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  OnComponentEvent(MapName());
 }
};

// Native factory 0x23c820; internal manager/message ID 1, L4D-facing ID 22.
class CAchievementHonkAClownsNose : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "punched_clown" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x23c920; internal manager/message ID 2, L4D-facing ID 23.
class CAchievementIncendiaryClownPosse : public CLeft4DeadAchievement
{
protected:
 int m_kills; bool m_clown; float m_start;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "molotov_thrown", "zombie_ignited" };
  SetEvents(events,ARRAYSIZE(events));
  m_kills=0; m_clown=false; m_start=-30.0f;
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"molotov_thrown")) { m_start=gpGlobals->curtime; m_kills=0; m_clown=false; }
   else if(EventIs(name,"zombie_ignited") && EventIs(event->GetString("victimname",""),"Infected") && (float)(gpGlobals->curtime-m_start)<30.0f) { if(event->GetInt("gender")==16) m_clown=true; if(IncrementNative32(m_kills)==11 && m_clown) IncrementCount(); }
 }
 virtual void OnMapStart() OVERRIDE { m_kills=0; m_clown=false; m_start=-30.0f; }
};

// Native factory 0x23ca20; internal manager/message ID 3, L4D-facing ID 24.
class CAchievementMeleeACharger : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "charger_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"charger_killed") && LocalUser(event,"attacker") && event->GetBool("melee") && event->GetBool("charging")) IncrementCount();
 }
};

// Native factory 0x23cb20; internal manager/message ID 4, L4D-facing ID 25.
class CAchievementKillSpitterFast : public CLeft4DeadAchievement
{
protected:
 CUtlVector<int> m_spitters;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "spitter_killed", "ability_use" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  int id=event->GetInt("userid");
   if(EventIs(name,"ability_use")) { if(EventIs(event->GetString("ability",""),"ability_spit")) AddDistinct(m_spitters,id); }
   else if(EventIs(name,"spitter_killed")) { if(LocalUser(event,"attacker") && m_spitters.Find(id)==m_spitters.InvalidIndex()) IncrementCount(); m_spitters.FindAndRemove(id); }
 }
 virtual void OnMapStart() OVERRIDE { m_spitters.RemoveAll(); }
};

// Native factory 0x23cc50; internal manager/message ID 5, L4D-facing ID 26.
class CAchievementSavePlayerFromJockeyFast : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "jockey_ride_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  float ride=event->GetFloat("ride_length"); if(EventIs(name,"jockey_ride_end") && LocalUser(event,"rescuer") && ride>0.0f && ride<=2.0f) IncrementCount();
 }
};

// Native factory 0x23cd50; internal manager/message ID 7, L4D-facing ID 27.
class CAchievementStashWhacker : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "stashwhacker_game_won" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"stashwhacker_game_won") && C_BasePlayer::GetLocalPlayer()) IncrementCount();
 }
};

// Native factory 0x23ce50; internal manager/message ID 26, L4D-facing ID 28.
class CAchievementStrongmanGame : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "strongman_bell_knocked_off" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"strongman_bell_knocked_off") && LocalUser(event)) IncrementCount();
 }
};

// Native factory 0x23cf50; internal manager/message ID 27, L4D-facing ID 30.
class CAchievementAirlineFinaleNoDamage : public CLeft4DeadAchievement
{
protected:
 bool m_started;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_hurt_concise", "create_panic_event", "panic_event_finished" };
  SetEvents(events,ARRAYSIZE(events));
  m_started=false;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || Q_stricmp(MapName(),"c3m2_swamp") || m_failed) return;
   if(EventIs(name,"create_panic_event")) m_started=true;
   else if(EventIs(name,"player_hurt_concise")) { if(m_started && LocalUser(event)) m_failed=true; }
   else if(EventIs(name,"panic_event_finished") && m_started && !m_failed) IncrementCount();
 }
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnMapStart() OVERRIDE { m_started=false; }
};

// Native factory 0x23d050; internal manager/message ID 23, L4D-facing ID 31.
class CAchievementWitchTownNoWitchKills : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "witch_killed", "player_entered_checkpoint", "round_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || Q_stricmp(MapName(),"c4m2_sugarmill_a")) return;
   if(EventIs(name,"witch_killed")) m_failed=true;
   else if(!m_failed && !Q_strcmp(name,"player_entered_checkpoint") && !Q_strcmp("checkpoint_entrance",event->GetString("doorname",""))) { if(LocalUser(event)) IncrementCount(); }
   else if(EventIs(name,"round_end")) m_failed=false;
 }
 virtual void OnMapStart() OVERRIDE { m_failed=false; }
};

// Native factory 0x23d150; internal manager/message ID 22, L4D-facing ID 32.
class CAchievementImpoundLotNoAlarms : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "triggered_car_alarm", "player_entered_checkpoint", "round_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || Q_stricmp(MapName(),"c5m3_cemetery")) return;
   if(EventIs(name,"triggered_car_alarm")) m_failed=true;
   else if(!m_failed && !Q_strcmp(name,"player_entered_checkpoint") && !Q_strcmp("checkpoint_entrance",event->GetString("doorname",""))) { if(LocalUser(event)) IncrementCount(); }
   else if(EventIs(name,"round_end")) m_failed=false;
 }
 virtual void OnMapStart() OVERRIDE { m_failed=false; }
};

// Native factory 0x23d250; internal manager/message ID 24, L4D-facing ID 33.
class CAchievementBridgeTimedRun : public CLeft4DeadAchievement
{
protected:
 float m_started;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "gauntlet_finale_start", "finale_win", "mission_lost" };
  SetEvents(events,ARRAYSIZE(events));
  m_started=0.0f; m_failed=false;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || Q_stricmp(MapName(),"c5m5_bridge") || m_failed) return;
   if(EventIs(name,"gauntlet_finale_start")) m_started=gpGlobals->curtime;
   else { if(EventIs(name,"finale_win")) { if((float)(gpGlobals->curtime-m_started)<180.0f) IncrementCount(); } else if(!EventIs(name,"mission_lost")) return; m_failed=false; m_started=0.0f; }
 }
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnMapStart() OVERRIDE { m_started=0.0f; m_failed=false; }
};

// Native factory 0x23d350; internal manager/message ID 101, L4D-facing ID 102.
class CAchievementLastStandTimed : public CLeft4DeadAchievement
{
protected:
 float m_total, m_mapStart; bool m_departed;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_left_safe_area", "finale_vehicle_leaving", "player_death" };
  SetEvents(events,ARRAYSIZE(events));
  m_total=0.0f; m_mapStart=0.0f; m_departed=false;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"player_death")) { C_TerrorPlayer *p=AchievementUser(event->GetInt("userid")); if(p && p->GetTeamNumber()==2) m_failed=true; }
   else if(EventIs(name,"player_left_safe_area")) m_mapStart=gpGlobals->curtime;
   else if(EventIs(name,"finale_vehicle_leaving")) { m_total=(float)(gpGlobals->curtime-m_mapStart)+m_total; m_departed=true; if(event->GetInt("survivorcount")<4) m_failed=true; }
 }
 virtual bool RequiresAlive() const OVERRIDE { return true; }
 virtual void OnMapEnd() OVERRIDE { if(!m_departed) m_total=(float)(gpGlobals->curtime-m_mapStart)+m_total; }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(!m_failed && !g_TerrorModes.IsPlayerControlledZombies() && !Q_strcmp("c14m2_lighthouse",map) && m_total<780.0f) IncrementCount(); }
 virtual void OnCampaignStart() OVERRIDE { CLeft4DeadAchievement::OnCampaignStart(); m_total=0.0f; m_departed=false; }
};

// Native factory 0x23d440; internal manager/message ID 100, L4D-facing ID 101.
class CAchievementBeatAtriumBeforeFirstTank : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_start", "finale_win", "tank_spawn" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(Q_stricmp(MapName(),"c1m4_atrium")) return;
   if(EventIs(name,"finale_start")) m_failed=false;
   else if(EventIs(name,"tank_spawn")) m_failed=true;
   else if(EventIs(name,"finale_win") && !m_failed) IncrementCount();
 }
 virtual bool RequiresAlive() const OVERRIDE { return true; }
};

// Native factory 0x23d530; internal manager/message ID 99, L4D-facing ID 99.
class CAchievementBeatConcertFromStage : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_start", "player_left_concert_stage", "finale_vehicle_incoming" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"finale_start")) m_failed=false;
   else if(EventIs(name,"player_left_concert_stage")) { C_TerrorPlayer *p=AchievementUser(event->GetInt("userid")); if(p && !p->IsBot() && (p->GetTeamNumber()==2 || p->GetTeamNumber()==4)) m_failed=true; }
   else if(EventIs(name,"finale_vehicle_incoming") && EventIs(event->GetString("campaign",""),"L4D2C2") && !m_failed) IncrementCount();
 }
};

// Native factory 0x23d620; internal manager/message ID 103, L4D-facing ID 103.
class CAchievementFindLighthouseHiddenRoom : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "break_breakable" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!Q_stricmp(MapName(),"c14m1_junkyard") && event->GetInt("achievement")==103) IncrementCount();
 }
};

// Native factory 0x23d710; internal manager/message ID 46, L4D-facing ID 34.
class CAchievementLeaveSaferoomToDefib : public CLeft4DeadCompetitiveAchievement
{
protected:
 bool m_entered; CUtlVector<int> m_teamVictims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "defibrillator_used", "player_entered_checkpoint", "player_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("versus") || !AchievementPlayer()) return;
   if(!Q_strcmp(name,"player_entered_checkpoint") && !Q_strcmp(event->GetString("doorname",""),"checkpoint_entrance")) { if(LocalUser(event)) m_entered=true; }
   else if(!Q_strcmp(name,"defibrillator_used") && m_entered && LocalUser(event)) { if(m_teamVictims.Find(event->GetInt("subject"))==m_teamVictims.InvalidIndex()) IncrementCount(); }
   else if(!Q_strcmp(name,"player_death")) { C_TerrorPlayer *p=AchievementUser(event->GetInt("attacker")); if(p && p->GetTeamNumber()==2) AddDistinct(m_teamVictims,event->GetInt("userid")); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_entered=false; m_teamVictims.RemoveAll(); }
};

// Native factory 0x23d840; internal manager/message ID 37, L4D-facing ID 35.
class CAchievementWinATeamVsTeamGame : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_match_finished", "versus_match_finished" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!Q_strnicmp(g_TerrorModes.GetName(),"team",4)) { C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_iVersusTeam==event->GetInt("winners")) IncrementCount(); }
 }
};

// Native factory 0x23d940; internal manager/message ID 42, L4D-facing ID 36.
class CAchievementVsJockeyLongRide : public CLeft4DeadCompetitiveAchievement
{
protected:
 float m_start;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "jockey_ride", "jockey_ride_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"jockey_ride")) { if(LocalUser(event)) m_start=gpGlobals->curtime; }
   else if(EventIs(name,"jockey_ride_end")) { if(LocalUser(event) && event->GetFloat("ride_length")>=12.0f) IncrementCount(); ResetCompetitive(); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_start=0.0f; }
 virtual void OnScoreAnimation() OVERRIDE { if(m_start>0.0f && (float)(gpGlobals->curtime-m_start)>=12.0f) IncrementCount(); ResetCompetitive(); m_failed=true; }
};

// Native factory 0x23da40; internal manager/message ID 43, L4D-facing ID 37.
class CAchievementVsJockeyTwoPlayers : public CLeft4DeadCompetitiveAchievement
{
protected:
 int m_rides;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "jockey_ride", "jockey_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"jockey_killed")) ResetCompetitive(); else if(EventIs(name,"jockey_ride") && IncrementNative32(m_rides)==2) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_rides=0; }
};

// Native factory 0x23db40; internal manager/message ID 44, L4D-facing ID 38.
class CAchievementVsJockeyIntoAcid : public CLeft4DeadCompetitiveAchievement
{
protected:
 int m_victim;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "jockey_ride", "jockey_ride_end", "entered_spit" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"jockey_ride")) { if(LocalUser(event)) m_victim=event->GetInt("victim"); }
   else if(EventIs(name,"jockey_ride_end")) { if(LocalUser(event)) ResetCompetitive(); }
   else if(EventIs(name,"entered_spit") && event->GetInt("userid")==m_victim) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_victim=-1; }
};

// Native factory 0x23dc40; internal manager/message ID 41, L4D-facing ID 39.
class CAchievementVsSpitOnEnemyTeam : public CLeft4DeadCompetitiveAchievement
{
protected:
 bool m_ourSpit; CUtlVector<int> m_victims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "spit_burst", "entered_spit" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"spit_burst")) { if(LocalUser(event)) { ResetCompetitive(); m_ourSpit=true; } else m_ourSpit=false; }
   else if(EventIs(name,"entered_spit") && m_ourSpit) { AddDistinct(m_victims,event->GetInt("userid")); if(m_victims.Count()==4) IncrementCount(); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_ourSpit=false; m_victims.RemoveAll(); }
};

// Native factory 0x23dd70; internal manager/message ID 40, L4D-facing ID 40.
class CAchievementVsSpitOnSmokeredPlayer : public CLeft4DeadCompetitiveAchievement
{
protected:
 bool m_ourSpit; CUtlVector<int> m_victims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "spit_burst", "entered_spit", "choke_start", "choke_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"choke_start")) AddDistinct(m_victims,event->GetInt("victim"));
   else if(EventIs(name,"choke_end")) m_victims.FindAndRemove(event->GetInt("victim"));
   else if(EventIs(name,"spit_burst")) m_ourSpit=LocalUser(event);
   else if(EventIs(name,"entered_spit") && m_ourSpit && m_victims.Find(event->GetInt("userid"))!=m_victims.InvalidIndex()) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_ourSpit=false; m_victims.RemoveAll(); }
};

// Native factory 0x23de90; internal manager/message ID 86, L4D-facing ID 86.
class CAchievementVsSpitOnChargedPlayer : public CLeft4DeadCompetitiveAchievement
{
protected:
 bool m_ourSpit; CUtlVector<int> m_victims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "spit_burst", "entered_spit", "charger_pummel_start", "charger_pummel_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"charger_pummel_start")) AddDistinct(m_victims,event->GetInt("victim"));
   else if(EventIs(name,"charger_pummel_end")) m_victims.FindAndRemove(event->GetInt("victim"));
   else if(EventIs(name,"spit_burst")) m_ourSpit=LocalUser(event);
   else if(EventIs(name,"entered_spit") && m_ourSpit && m_victims.Find(event->GetInt("userid"))!=m_victims.InvalidIndex()) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_ourSpit=false; m_victims.RemoveAll(); }
};

// Native factory 0x23dfb0; internal manager/message ID 87, L4D-facing ID 87.
class CAchievementVsSpitOnPouncedPlayer : public CLeft4DeadCompetitiveAchievement
{
protected:
 bool m_ourSpit; CUtlVector<int> m_victims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "spit_burst", "entered_spit", "lunge_pounce", "pounce_stopped" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"lunge_pounce")) AddDistinct(m_victims,event->GetInt("victim"));
   else if(EventIs(name,"pounce_stopped")) m_victims.FindAndRemove(event->GetInt("victim"));
   else if(EventIs(name,"spit_burst")) m_ourSpit=LocalUser(event);
   else if(EventIs(name,"entered_spit") && m_ourSpit && m_victims.Find(event->GetInt("userid"))!=m_victims.InvalidIndex()) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_ourSpit=false; m_victims.RemoveAll(); }
};

// Native factory 0x23e0d0; internal manager/message ID 39, L4D-facing ID 41.
class CAchievementVsBowlEnemyTeam : public CLeft4DeadCompetitiveAchievement
{
protected:
 CUtlVector<int> m_victims;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "charger_charge_start", "charger_carry_start", "charger_impact" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"charger_charge_start")) { if(LocalUser(event)) m_victims.RemoveAll(); }
   else if((EventIs(name,"charger_impact") || EventIs(name,"charger_carry_start")) && LocalUser(event)) { AddDistinct(m_victims,event->GetInt("victim")); if(m_victims.Count()==4) IncrementCount(); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_victims.RemoveAll(); }
};

// Native factory 0x23e200; internal manager/message ID 38, L4D-facing ID 42.
class CAchievementVsChargerGroundSmash : public CLeft4DeadCompetitiveAchievement
{
protected:
 int m_victim; float m_start;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "charger_pummel_start", "charger_pummel_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"charger_pummel_start")) { if(LocalUser(event)) { m_start=gpGlobals->curtime; m_victim=event->GetInt("victim"); } }
   else if(EventIs(name,"charger_pummel_end") && LocalUser(event)) { if(!m_failed && m_victim==event->GetInt("victim") && (float)(gpGlobals->curtime-m_start)>15.0f) IncrementCount(); ResetCompetitive(); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_victim=-1; m_start=0.0f; }
 virtual void OnScoreAnimation() OVERRIDE { if(AchievementPlayer() && m_victim>-1 && m_start>0.0f && !m_failed && (float)(gpGlobals->curtime-m_start)>15.0f) { IncrementCount(); return; } ResetCompetitive(); m_failed=true; }
};

// Native factory 0x23e300; internal manager/message ID 45, L4D-facing ID 43.
class CAchievementVsChargerLongCarry : public CLeft4DeadCompetitiveAchievement
{
protected:
 int m_victim; Vector m_origin;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "charger_carry_start", "charger_carry_end" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(!p || !LocalUser(event)) return;
   if(EventIs(name,"charger_carry_start")) { m_origin=p->GetAbsOrigin(); m_victim=event->GetInt("victim"); }
   else if(EventIs(name,"charger_carry_end") && !m_failed && m_victim==event->GetInt("victim")) { Vector delta=m_origin-p->GetAbsOrigin(); if(sqrt((float)(delta.x*delta.x)+(float)(delta.y*delta.y))>1000.0) IncrementCount(); }
 }
 virtual void ResetCompetitive() OVERRIDE { m_victim=-1; m_origin.Init(); }
};

// Native factory 0x23e400; internal manager/message ID 89, L4D-facing ID 89.
class CAchievementVsChargerInstantKill : public CLeft4DeadCompetitiveAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "charger_carry_kill" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x23e500; internal manager/message ID 88, L4D-facing ID 88.
class CAchievementVsBilePlayerNoLineOfSight : public CLeft4DeadCompetitiveAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "player_now_it" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *attacker=AchievementUser(event->GetInt("attacker")), *local=AchievementPlayer();
   if(attacker && local && attacker->GetUserID()==local->GetUserID() && !event->GetBool("exploded")) { C_TerrorPlayer *victim=AchievementUser(event->GetInt("userid")); if(victim && !TerrorGlow_IsAbleToSee(attacker,victim)) IncrementCount(); }
 }
};

// Native factory 0x23e600; internal manager/message ID 36, L4D-facing ID 44.
class CAchievementGetMedalWithOnlyMelee : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survival_goal_reached", "non_melee_fired", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("survival")) return;
   if(EventIs(name,"survival_round_start")) m_failed=false;
   else if(EventIs(name,"non_melee_fired")) m_failed=true;
   else if(EventIs(name,"survival_goal_reached")) { C_TerrorPlayer *p=AchievementPlayer(); const char *goal=event->GetString("goalname",""); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed  && (EventIs(goal,"nomedal") || EventIs(goal,"medal"))) IncrementCount(); }
 }
};

// Native factory 0x23e700; internal manager/message ID 78, L4D-facing ID 78.
class CAchievementGetBronzeMedal : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survival_goal_reached", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("survival")) return;
   if(EventIs(name,"survival_round_start")) m_failed=false;
   
   else if(EventIs(name,"survival_goal_reached")) { C_TerrorPlayer *p=AchievementPlayer(); const char *goal=event->GetString("goalname",""); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed && event->GetInt("id")==0 && (EventIs(goal,"nomedal") || EventIs(goal,"medal"))) IncrementCount(); }
 }
};

// Native factory 0x23e800; internal manager/message ID 79, L4D-facing ID 79.
class CAchievementGetSilverMedal : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survival_goal_reached", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("survival")) return;
   if(EventIs(name,"survival_round_start")) m_failed=false;
   
   else if(EventIs(name,"survival_goal_reached")) { C_TerrorPlayer *p=AchievementPlayer(); const char *goal=event->GetString("goalname",""); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed && event->GetInt("id")==1 && (EventIs(goal,"nomedal") || EventIs(goal,"medal"))) IncrementCount(); }
 }
};

// Native factory 0x23e900; internal manager/message ID 80, L4D-facing ID 80.
class CAchievementGetGoldMedal : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survival_goal_reached", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("survival")) return;
   if(EventIs(name,"survival_round_start")) m_failed=false;
   
   else if(EventIs(name,"survival_goal_reached")) { C_TerrorPlayer *p=AchievementPlayer(); const char *goal=event->GetString("goalname",""); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed && event->GetInt("id")==2 && (EventIs(goal,"nomedal") || EventIs(goal,"medal"))) IncrementCount(); }
 }
};

// Native factory 0x241010; internal manager/message ID 104, L4D-facing ID 104.
class CAchievementEarnAllBronzeMedals : public CAchievementEarnAllMedalsBase
{
public:
 virtual void Init() OVERRIDE
 {
  InitMedals(0,240);
 }
protected:
};

// Native factory 0x23eb40; internal manager/message ID 105, L4D-facing ID 105.
class CAchievementEarnAllSilverMedals : public CAchievementEarnAllMedalsBase
{
public:
 virtual void Init() OVERRIDE
 {
  InitMedals(1,420);
 }
protected:
};

// Native factory 0x23ec40; internal manager/message ID 106, L4D-facing ID 106.
class CAchievementEarnAllGoldMedals : public CAchievementEarnAllMedalsBase
{
public:
 virtual void Init() OVERRIDE
 {
  InitMedals(2,600);
 }
protected:
};

// Native factory 0x23ed40; internal manager/message ID 108, L4D-facing ID 97.
class CAchievementSurviveXMinutesVersusSurvival : public CLeft4DeadCompetitiveAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "survival_at_10min", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"survival_round_start")) m_failed=false;
   else if(g_TerrorModes.IsPlayerControlledZombies() && EventIs(name,"survival_at_10min")) { C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_bWasPresentAtSurvivalStart && p->m_iVersusTeam!=3 && !m_failed) IncrementCount(); }
 }
};

// Native factory 0x23ee40; internal manager/message ID 81, L4D-facing ID 97.
class CAchievementSurviveXMinutesSurvival : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "survival_at_30min", "survival_round_start" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"survival_round_start")) m_failed=false;
   else if(!g_TerrorModes.IsPlayerControlledZombies() && EventIs(name,"survival_at_30min")) { C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_bWasPresentAtSurvivalStart && !m_failed) IncrementCount(); }
 }
};

// Native factory 0x23ef40; internal manager/message ID 34, L4D-facing ID 45.
class CAchievementWinAScavengeGame : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_match_finished" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_iVersusTeam==event->GetInt("winners")) IncrementCount();
 }
};

// Native factory 0x23f040; internal manager/message ID 35, L4D-facing ID 46.
class CAchievementScavengeCapGrind : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "gascan_pour_completed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(g_TerrorModes.IsBaseMode("scavenge") && EventIs(name,"gascan_pour_completed")) IncrementCount();
 }
};

// Native factory 0x23f140; internal manager/message ID 31, L4D-facing ID 47.
class CAchievementScavengeCollectAllCans : public CLeft4DeadAchievement
{
protected:
 int m_cans;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_round_start", "gascan_pour_completed" };
  SetEvents(events,ARRAYSIZE(events));
  m_cans=0;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(!p || !g_TerrorModes.IsBaseMode("scavenge")) return;
   if(EventIs(name,"scavenge_round_start")) m_cans=0;
   else if(EventIs(name,"gascan_pour_completed")) { C_TerrorPlayer *pourer=AchievementUser(event->GetInt("userid")); if(pourer && pourer->GetTeamNumber()==p->GetTeamNumber() && IncrementNative32(m_cans)==15) IncrementCount(); }
 }
 virtual void OnMapStart() OVERRIDE { m_cans=0; }
};

// Native factory 0x23f240; internal manager/message ID 30, L4D-facing ID 48.
class CAchievementScavengeShutOut : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_round_start", "scavenge_round_finished", "scavenge_round_halftime", "scavenge_match_finished", "gascan_pour_completed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(!p || !g_TerrorModes.IsBaseMode("scavenge")) return;
   if(EventIs(name,"gascan_pour_completed")) m_failed=true;
   else if(EventIs(name,"scavenge_round_start")) m_failed=p->GetTeamNumber()==2;
   else if(!m_failed && (EventIs(name,"scavenge_round_finished") || EventIs(name,"scavenge_round_halftime") || EventIs(name,"scavenge_match_finished"))) IncrementCount();
 }
 virtual void OnCampaignStart() OVERRIDE { CLeft4DeadAchievement::OnCampaignStart(); m_failed=true; }
};

// Native factory 0x23f340; internal manager/message ID 32, L4D-facing ID 49.
class CAchievementScavengeMakeSurvivorDropCan : public CLeft4DeadAchievement
{
protected:
 bool m_overtime;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_round_start", "begin_scavenge_overtime", "gas_can_forced_drop", "scavenge_round_finished", "scavenge_round_halftime" };
  SetEvents(events,ARRAYSIZE(events));
  m_overtime=false;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!AchievementPlayer() || !g_TerrorModes.IsBaseMode("scavenge")) return;
   if(m_overtime && EventIs(name,"gas_can_forced_drop")) { if(LocalUser(event)) IncrementCount(); }
   else if(EventIs(name,"begin_scavenge_overtime")) m_overtime=true;
   else if(EventIs(name,"scavenge_round_finished") || EventIs(name,"scavenge_round_start") || EventIs(name,"scavenge_round_halftime")) m_overtime=false;
 }
 virtual void OnMapStart() OVERRIDE { m_overtime=false; }
};

// Native factory 0x23f440; internal manager/message ID 33, L4D-facing ID 50.
class CAchievementScavengeCanDropGrind : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "gas_can_forced_drop" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(g_TerrorModes.IsBaseMode("scavenge")) IncrementCount();
 }
};

// Native factory 0x23f540; internal manager/message ID 51, L4D-facing ID 51.
class CAchievementSurviveAllAnyDifficulty : public CBaseAchievementSurviveAllAtDifficulty
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_difficulty=1; SetComponents(s_campaigns,ARRAYSIZE(s_campaigns),s_campaign_labels); SetGoal(5);
 }
protected:
 virtual void OnDifficultyChanged( int now,int old ) OVERRIDE {} //2390D0.
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(CooperativeAchievements() && !m_failed && LiveSurvivor(AchievementPlayer())) OnComponentEvent(map); }
};

// Native factory 0x23f640; internal manager/message ID 52, L4D-facing ID 52.
class CAchievementKillThenThousandZombies : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"infected_death") && LocalUser(event,"attacker")) IncrementCount();
 }
};

// Native factory 0x23f740; internal manager/message ID 53, L4D-facing ID 53.
class CAchievementGnomeRescueAsset : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || !EventIs(name,"finale_win") || Q_strnicmp(MapName(),"c2m5_concert",Q_strlen("c2m5_concert")) || m_failed || !AchievementPlayer()) return;
   for(int i=0;i<32;++i) { C_TerrorPlayer *p=dynamic_cast<C_TerrorPlayer *>(cl_entitylist->GetEnt(i)); CTerrorWeapon *weapon=p ? dynamic_cast<CTerrorWeapon *>(p->GetActiveWeapon()) : NULL; if(weapon && weapon->GetWeaponID()==27) { IncrementCount(); break; } }
 }
};

// Native factory 0x23f840; internal manager/message ID 25, L4D-facing ID 29.
class CAchievementGnomeRescue : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!CooperativeAchievements() || !EventIs(name,"finale_win") || Q_strnicmp(MapName(),"c2m5_concert",Q_strlen("c2m5_concert")) || m_failed || !AchievementPlayer()) return;
   for(int i=0;i<32;++i) { C_TerrorPlayer *p=dynamic_cast<C_TerrorPlayer *>(cl_entitylist->GetEnt(i)); CTerrorWeapon *weapon=p ? dynamic_cast<CTerrorWeapon *>(p->GetActiveWeapon()) : NULL; if(weapon && weapon->GetWeaponID()==27) { IncrementCount(); break; } }
 }
 virtual void OnAchieved() OVERRIDE {} //Native2394D0.
};

// Native factory 0x23f940; internal manager/message ID 54, L4D-facing ID 54.
class CAchievementWin10Versus : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "versus_match_finished" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_iVersusTeam==event->GetInt("winners")) IncrementCount();
 }
};

// Native factory 0x23fa30; internal manager/message ID 55, L4D-facing ID 55.
class CAchievementWin10ScavengeAsset : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_match_finished" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementPlayer(); if(p && p->m_iVersusTeam==event->GetInt("winners")) IncrementCount();
 }
};

// Native factory 0x2396b0; internal manager/message ID 56, L4D-facing ID 56.
class CAchievementSurviveThePassing : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c6m3_port";
 }
protected:
};

// Native factory 0x23fb80; internal manager/message ID 57, L4D-facing ID 57.
class CAchievementVsChargeThroughChairs : public CLeft4DeadCompetitiveAchievement
{
protected:
 int m_chairs; bool m_carrying;
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "charger_carry_start", "charger_carry_end", "chair_charged" };
  SetEvents(events,ARRAYSIZE(events));
  m_chairs=0; m_carrying=false;
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"charger_carry_start")) { m_carrying=true; m_chairs=0; }
   else if(EventIs(name,"charger_carry_end")) m_carrying=false;
   else if(EventIs(name,"chair_charged") && m_carrying && IncrementNative32(m_chairs)>=8) IncrementCount();
 }
 virtual void ResetCompetitive() OVERRIDE { m_chairs=0; m_carrying=false; }
};

// Native factory 0x239890; internal manager/message ID 74, L4D-facing ID 37.
class CAchievementVsCompleteDeadAir : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c11m5_runway";
 }
protected:
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(g_TerrorModes.IsBaseMode("versus") && !Q_strnicmp(map,m_finalMap,Q_strlen(m_finalMap)) && !m_failed) IncrementCount(); }
};

// Native factory 0x23fce0; internal manager/message ID 58, L4D-facing ID 58.
class CAchievementKillManyWithM60 : public CLeft4DeadAchievement
{
protected:
 int m_kills;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_death", "m60_streak_ended" };
  SetEvents(events,ARRAYSIZE(events));
  m_kills=0;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"infected_death")) { if(event->GetInt("weapon_id")==37 && LocalUser(event,"attacker") && IncrementNative32(m_kills)>=25) IncrementCount(); }
   else if(EventIs(name,"m60_streak_ended")) m_kills=0;
 }
};

// Native factory 0x23fde0; internal manager/message ID 59, L4D-facing ID 59.
class CAchievementScavengeFromFallenSurvivor : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "weapon_pickup" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"weapon_pickup") && event->GetInt("dropped_by_infected")==14) IncrementCount();
 }
};

// Native factory 0x23fee0; internal manager/message ID 60, L4D-facing ID 60.
class CAchievementCompleteChallengeModes : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_spawn" };
  SetEvents(events,ARRAYSIZE(events));
  m_iNumComponents=20; m_componentLabels=s_challenge_labels;
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"player_spawn") && TerrorGameRules() && TerrorGameRules()->IsScriptedModeActive()) { KeyValues *mode=g_TerrorModes.GetCurrentMode(); int index=mode ? mode->GetInt("achievementindex",-1) : -1; if(index>=0) EnsureComponentBitSetAndEvaluate(index); }
 }
};

// Native factory 0x23fff0; internal manager/message ID 61, L4D-facing ID 61.
class CAchievementGolfClubHeads : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_decapitated" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(event->GetString("weapon",""),"golfclub")) IncrementCount();
 }
};

// Native factory 0x2400f0; internal manager/message ID 62, L4D-facing ID 62.
class CAchievementHearBallad : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "song_played" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!Q_strnicmp(MapName(),"c6m",3)) IncrementCount();
 }
};

// Native factory 0x2401f0; internal manager/message ID 63, L4D-facing ID 63.
class CAchievementKillTankL4D1Only : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "tank_killed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(IsAchievementMap("c6m3_port") && event->GetBool("l4d1_only")) IncrementCount();
 }
};

// Native factory 0x2402f0; internal manager/message ID 64, L4D-facing ID 64.
class CAchievementOpenFootLockers : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "foot_locker_opened" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x2403f0; internal manager/message ID 65, L4D-facing ID 65.
class CAchievementPlayC6FinaleScavenge : public CLeft4DeadAchievement
{
protected:
 bool m_participated;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "scavenge_round_start", "scavenge_match_finished" };
  SetEvents(events,ARRAYSIZE(events));
  m_participated=false;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!IsAchievementMap("c6m3_port")) return;
   if(EventIs(name,"scavenge_round_start")) { if(event->GetInt("round")==1 && event->GetBool("firsthalf")) m_participated=true; }
   else if(m_participated && EventIs(name,"scavenge_match_finished")) IncrementCount();
 }
};

// Native factory 0x23a060; internal manager/message ID 68, L4D-facing ID 68.
class CAchievementDLC2CompleteTheSacrifice : public CAchievementSurviveCampaign
{
protected:
 bool m_sacrificeAliveGate;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win", "player_complete_sacrifice" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c7m3_port"; m_sacrificeAliveGate=true;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(EventIs(name,"player_complete_sacrifice") && IsAchievementMap("c7m3_port") && AchievementUser(event->GetInt("userid"))==AchievementPlayer() && AchievementPlayer() && PlayedEntireCampaign()) { m_sacrificeAliveGate=false; IncrementCount(); m_sacrificeAliveGate=true; }
 }
 virtual bool RequiresAlive() const OVERRIDE { return m_sacrificeAliveGate; }
};

// Native factory 0x240550; internal manager/message ID 69, L4D-facing ID 69.
class CAchievementDLC2SacrificeBill : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_complete_sacrifice" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  C_TerrorPlayer *p=AchievementUser(event->GetInt("userid")); if(IsAchievementMap("c7m3_port") && p && p->m_survivorCharacter==0) IncrementCount();
 }
};

// Native factory 0x240650; internal manager/message ID 70, L4D-facing ID 70.
class CAchievementDLC2KillSpecialWithExplodingBarrel : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "infected_explosive_barrel_kill" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x240750; internal manager/message ID 71, L4D-facing ID 71.
class CAchievementDLC2RunAllGeneratorsAtOnce : public CLeft4DeadAchievement
{
protected:
 int m_generators; float m_start;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "generator_started" };
  SetEvents(events,ARRAYSIZE(events));
  m_generators=0; m_start=-1.0f;
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(IsAchievementMap("c7m3_port") && EventIs(name,"generator_started")) { int count=IncrementNative32(m_generators); if(count==1) m_start=gpGlobals->curtime; else if(count>2 && (float)(m_start+10.0f)>gpGlobals->curtime) IncrementCount(); }
 }
 virtual void OnMapStart() OVERRIDE { m_generators=0; m_start=-1.0f; }
};

// Native factory 0x240850; internal manager/message ID 72, L4D-facing ID 72.
class CAchievementDLC2PreventSacrifice : public CLeft4DeadAchievement
{
protected:
 CUtlVector<int> m_running;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_begin_sacrifice_run", "player_incapacitated" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnEvent( const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!g_TerrorModes.IsBaseMode("versus") || !IsAchievementMap("c7m3_port")) return;
   if(!Q_strcmp(name,"player_begin_sacrifice_run")) AddDistinct(m_running,event->GetInt("userid"));
   else if(!Q_strcmp(name,"player_incapacitated") && m_running.Find(event->GetInt("userid"))!=m_running.InvalidIndex()) { C_TerrorPlayer *p=AchievementPlayer(),*a=AchievementUser(event->GetInt("attacker")); if(p && a && p==a && a->GetTeamNumber()==3) IncrementCount(); }
 }
 virtual void OnMapStart() OVERRIDE { m_running.RemoveAll(); }
};

// Native factory 0x23a450; internal manager/message ID 73, L4D-facing ID 73.
class CAchievementSurviveColdStream : public CAchievementSurviveCampaign
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_finalMap="c13m4_cutthroatcreek";
 }
protected:
};

// Native factory 0x2409e0; internal manager/message ID 85, L4D-facing ID 85.
class CAchievementVSSpitOnXGasCans : public CLeft4DeadCompetitiveAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  ResetCompetitive();
  static const char *events[] = { "scavenge_gas_can_destroyed" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  IncrementCount();
 }
};

// Native factory 0x240ae0; internal manager/message ID 75, L4D-facing ID 75.
class CAchievementCollectHolidayGifts : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  SetFlags(128); SetGoal(3); SetStoreProgressInSteam(true);
 }
protected:
 virtual void FireGameEvent_Internal( IGameEvent *event ) OVERRIDE
 {
  if(g_TerrorModes.IsBaseMode("versus") && !Q_strcmp(event->GetName(),"christmas_gift_grab")) { C_BaseEntity *p=cl_entitylist->GetEnt(engine->GetPlayerForUserID(event->GetInt("userid"))); if(p && p==AchievementPlayer()) IncrementCount(); }
 }
 virtual void ListenForEvents() OVERRIDE { ListenForGameEvent("christmas_gift_grab"); }
};

// Native factory 0x240be0; internal manager/message ID 76, L4D-facing ID 76.
class CAchievementHelpFreePlayerSurvive : public CLeft4DeadAchievement
{
protected:
 bool m_promotionUser;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_promotionUser=steamapicontext && steamapicontext->SteamApps() && steamapicontext->SteamApps()->BIsSubscribedFromFreeWeekend(); 
 }
protected:
 virtual void IncrementCount( int increment=0 ) OVERRIDE { if(!m_promotionUser) CLeft4DeadAchievement::IncrementCount(increment); }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(CooperativeAchievements() && !m_failed && m_promotionUser) engine->ServerCmdKeyValues(new KeyValues("FreeWeekendUserEscaped")); }
};

// Native factory 0x240ce0; internal manager/message ID 77, L4D-facing ID 77.
class CAchievementHelpChristmasPlayerSurvive : public CLeft4DeadAchievement
{
protected:
 bool m_promotionUser;
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "finale_win" };
  SetEvents(events,ARRAYSIZE(events));
  m_promotionUser=steamapicontext && steamapicontext->SteamApps() && steamapicontext->SteamApps()->BIsSubscribedApp(271840); if(m_promotionUser) DevMsg("Holiday Promo\n");
 }
protected:
 virtual void IncrementCount( int increment=0 ) OVERRIDE { if(!m_promotionUser) CLeft4DeadAchievement::IncrementCount(increment); }
 virtual void OnCampaignFinish( const char *map,int difficulty ) OVERRIDE { if(CooperativeAchievements() && !m_failed && m_promotionUser) engine->ServerCmdKeyValues(new KeyValues("FreeChristmasUserEscaped")); }
};

// Native factory 0x240de0; internal manager/message ID 109, L4D-facing ID 109.
class CAchievementGlobalGnomeAlone : public CLeft4DeadAchievement
{
public:
 virtual void Init() OVERRIDE
 {
  static const char *events[] = { "player_spawn" };
  SetEvents(events,ARRAYSIZE(events));
 }
protected:
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) OVERRIDE
 {
  if(!IsAchieved() && _time64(NULL)>1605850200LL) IncrementCount();
 }
};

void TerrorAchievementsUpdateSurvivalBestTimes( KeyValues *records )
{
 IAchievementMgr *manager=engine->GetAchievementMgr();
 if(!manager) return;
 for(int i=0;i<manager->GetAchievementCount();++i)
 {
  CLeft4DeadAchievement *achievement=dynamic_cast<CLeft4DeadAchievement *>(manager->GetAchievementByIndex(i));
  if(achievement) achievement->UpdateSurvivalBestTimes(records);
 }
}

#pragma float_control( pop )

static CBaseAchievement *Create_CAchievementSurviveMall()
{
 CAchievementSurviveMall *achievement=new CAchievementSurviveMall;
 achievement->Configure( 47, 1, "ACH_SURVIVE_MALL", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveMall( Create_CAchievementSurviveMall );
static CBaseAchievement *Create_CAchievementSurviveRoad()
{
 CAchievementSurviveRoad *achievement=new CAchievementSurviveRoad;
 achievement->Configure( 48, 2, "ACH_SURVIVE_ROAD", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveRoad( Create_CAchievementSurviveRoad );
static CBaseAchievement *Create_CAchievementSurviveSwamp()
{
 CAchievementSurviveSwamp *achievement=new CAchievementSurviveSwamp;
 achievement->Configure( 49, 3, "ACH_SURVIVE_SWAMP", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveSwamp( Create_CAchievementSurviveSwamp );
static CBaseAchievement *Create_CAchievementSurviveTown()
{
 CAchievementSurviveTown *achievement=new CAchievementSurviveTown;
 achievement->Configure( 50, 4, "ACH_SURVIVE_TOWN", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveTown( Create_CAchievementSurviveTown );
static CBaseAchievement *Create_CAchievementSurviveBridge()
{
 CAchievementSurviveBridge *achievement=new CAchievementSurviveBridge;
 achievement->Configure( 9, 5, "ACH_SURVIVE_BRIDGE", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveBridge( Create_CAchievementSurviveBridge );
static CBaseAchievement *Create_CAchievementSurviveLighthouse()
{
 CAchievementSurviveLighthouse *achievement=new CAchievementSurviveLighthouse;
 achievement->Configure( 82, 81, "ACH_SURVIVE_LIGHTHOUSE", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveLighthouse( Create_CAchievementSurviveLighthouse );
static CBaseAchievement *Create_CAchievementSurviveLighthouseExpertRealism()
{
 CAchievementSurviveLighthouseExpertRealism *achievement=new CAchievementSurviveLighthouseExpertRealism;
 achievement->Configure( 83, 82, "ACH_SURVIVE_LIGHTHOUSE_EXPERT_REALISM", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveLighthouseExpertRealism( Create_CAchievementSurviveLighthouseExpertRealism );
static CBaseAchievement *Create_CAchievementSurviveAllExpert()
{
 CAchievementSurviveAllExpert *achievement=new CAchievementSurviveAllExpert;
 achievement->Configure( 28, 6, "ACH_BEAT_CAMPAIGNS_EXPERT_MODE", 35, 5, 160, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveAllExpert( Create_CAchievementSurviveAllExpert );
static CBaseAchievement *Create_CAchievementSurviveExpertRealism()
{
 CAchievementSurviveExpertRealism *achievement=new CAchievementSurviveExpertRealism;
 achievement->Configure( 29, 7, "ACH_SURVIVE_EXPERT_REALISM", 35, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveExpertRealism( Create_CAchievementSurviveExpertRealism );
static CBaseAchievement *Create_CAchievementMeleeOnly()
{
 CAchievementMeleeOnly *achievement=new CAchievementMeleeOnly;
 achievement->Configure( 11, 8, "ACH_FINISH_CAMPAIGN_MELEE_ONLY", 30, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementMeleeOnly( Create_CAchievementMeleeOnly );
static CBaseAchievement *Create_CAchievementHitHeadsWithMelee()
{
 CAchievementHitHeadsWithMelee *achievement=new CAchievementHitHeadsWithMelee;
 achievement->Configure( 21, 9, "ACH_CUT_OFF_HEADS_MELEE", 15, 200, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementHitHeadsWithMelee( Create_CAchievementHitHeadsWithMelee );
static CBaseAchievement *Create_CAchievementKillWithEveryMelee()
{
 CAchievementKillWithEveryMelee *achievement=new CAchievementKillWithEveryMelee;
 achievement->Configure( 12, 10, "ACH_KILL_WITH_EVERY_MELEE", 15, 8, 160, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillWithEveryMelee( Create_CAchievementKillWithEveryMelee );
static CBaseAchievement *Create_CAchievementKillInfectedWithChainsaw()
{
 CAchievementKillInfectedWithChainsaw *achievement=new CAchievementKillInfectedWithChainsaw;
 achievement->Configure( 14, 11, "ACH_KILL_INFECTED_WITH_CHAINSAW", 15, 100, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillInfectedWithChainsaw( Create_CAchievementKillInfectedWithChainsaw );
static CBaseAchievement *Create_CAchievementKillTankMeleeOnly()
{
 CAchievementKillTankMeleeOnly *achievement=new CAchievementKillTankMeleeOnly;
 achievement->Configure( 13, 12, "ACH_KILL_TANK_MELEE_ONLY", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillTankMeleeOnly( Create_CAchievementKillTankMeleeOnly );
static CBaseAchievement *Create_CAchievementKillWitchMeleeOnly()
{
 CAchievementKillWitchMeleeOnly *achievement=new CAchievementKillWitchMeleeOnly;
 achievement->Configure( 98, 98, "ACH_KILL_WITCH_MELEE_ONLY", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillWitchMeleeOnly( Create_CAchievementKillWitchMeleeOnly );
static CBaseAchievement *Create_CAchievementResSurvivorsWithDefib()
{
 CAchievementResSurvivorsWithDefib *achievement=new CAchievementResSurvivorsWithDefib;
 achievement->Configure( 17, 13, "ACH_RES_SURVIVORS_WITH_DEFIB", 30, 10, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementResSurvivorsWithDefib( Create_CAchievementResSurvivorsWithDefib );
static CBaseAchievement *Create_CAchievementSpeedReviveWithAdrenaline()
{
 CAchievementSpeedReviveWithAdrenaline *achievement=new CAchievementSpeedReviveWithAdrenaline;
 achievement->Configure( 15, 14, "ACH_SPEED_REVIVE_WITH_ADRENALINE", 30, 10, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSpeedReviveWithAdrenaline( Create_CAchievementSpeedReviveWithAdrenaline );
static CBaseAchievement *Create_CAchievementRescueXPlayersTogether()
{
 CAchievementRescueXPlayersTogether *achievement=new CAchievementRescueXPlayersTogether;
 achievement->Configure( 90, 90, "ACH_RESCUE_X_PLAYERS_TOGETHER", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementRescueXPlayersTogether( Create_CAchievementRescueXPlayersTogether );
static CBaseAchievement *Create_CAchievementFreeSelfFromSmokerWithMelee()
{
 CAchievementFreeSelfFromSmokerWithMelee *achievement=new CAchievementFreeSelfFromSmokerWithMelee;
 achievement->Configure( 91, 91, "ACH_FREE_SELF_FROM_SMOKER_WITH_MELEE", 0, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementFreeSelfFromSmokerWithMelee( Create_CAchievementFreeSelfFromSmokerWithMelee );
static CBaseAchievement *Create_CAchievementKillLeapingHunterWithShotgun()
{
 CAchievementKillLeapingHunterWithShotgun *achievement=new CAchievementKillLeapingHunterWithShotgun;
 achievement->Configure( 92, 92, "ACH_KILL_LEAPING_HUNTER_WITH_SHOTGUN", 0, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillLeapingHunterWithShotgun( Create_CAchievementKillLeapingHunterWithShotgun );
static CBaseAchievement *Create_CAchievementDestroyTankRock()
{
 CAchievementDestroyTankRock *achievement=new CAchievementDestroyTankRock;
 achievement->Configure( 93, 92, "ACH_DESTROY_TANK_ROCK", 0, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDestroyTankRock( Create_CAchievementDestroyTankRock );
static CBaseAchievement *Create_CAchievementReviveWithActiveTank()
{
 CAchievementReviveWithActiveTank *achievement=new CAchievementReviveWithActiveTank;
 achievement->Configure( 84, 84, "ACH_REVIVE_WITH_ACTIVE_TANK", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementReviveWithActiveTank( Create_CAchievementReviveWithActiveTank );
static CBaseAchievement *Create_CAchievementOneShotKillWitchBride()
{
 CAchievementOneShotKillWitchBride *achievement=new CAchievementOneShotKillWitchBride;
 achievement->Configure( 94, 94, "ACH_ONE_SHOT_KILL_WITCH_BRIDE", 0, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementOneShotKillWitchBride( Create_CAchievementOneShotKillWitchBride );
static CBaseAchievement *Create_CAchievementPounceForXDamage()
{
 CAchievementPounceForXDamage *achievement=new CAchievementPounceForXDamage;
 achievement->Configure( 95, 95, "ACH_CAUSE_X_POUNCE_DAMAGE", 0, 1, 2176, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementPounceForXDamage( Create_CAchievementPounceForXDamage );
static CBaseAchievement *Create_CAchievementKillXJockeysWithMelee()
{
 CAchievementKillXJockeysWithMelee *achievement=new CAchievementKillXJockeysWithMelee;
 achievement->Configure( 96, 96, "ACH_KILL_X_JOCKEYS_WITH_MELEE", 0, 20, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillXJockeysWithMelee( Create_CAchievementKillXJockeysWithMelee );
static CBaseAchievement *Create_CAchievementKillXInfectedLifetime()
{
 CAchievementKillXInfectedLifetime *achievement=new CAchievementKillXInfectedLifetime;
 achievement->Configure( 97, 99, "ACH_KILL_X_INFECTED_LIFETIME", 0, 53599, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillXInfectedLifetime( Create_CAchievementKillXInfectedLifetime );
static CBaseAchievement *Create_CAchievementDeployAmmoUpgrade()
{
 CAchievementDeployAmmoUpgrade *achievement=new CAchievementDeployAmmoUpgrade;
 achievement->Configure( 18, 15, "ACH_DEPLOY_AMMO_UPGRADE", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDeployAmmoUpgrade( Create_CAchievementDeployAmmoUpgrade );
static CBaseAchievement *Create_CAchievementIgniteInfectedFireAmmo()
{
 CAchievementIgniteInfectedFireAmmo *achievement=new CAchievementIgniteInfectedFireAmmo;
 achievement->Configure( 19, 16, "ACH_IGNITE_INFECTED_FIRE_AMMO", 15, 50, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementIgniteInfectedFireAmmo( Create_CAchievementIgniteInfectedFireAmmo );
static CBaseAchievement *Create_CAchievementKillZombiesGrenadeBlast()
{
 CAchievementKillZombiesGrenadeBlast *achievement=new CAchievementKillZombiesGrenadeBlast;
 achievement->Configure( 20, 17, "ACH_KILL_ZOMBIES_GRENADE_BLAST", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillZombiesGrenadeBlast( Create_CAchievementKillZombiesGrenadeBlast );
static CBaseAchievement *Create_CAchievementBileTank()
{
 CAchievementBileTank *achievement=new CAchievementBileTank;
 achievement->Configure( 16, 18, "ACH_BILE_A_TANK", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementBileTank( Create_CAchievementBileTank );
static CBaseAchievement *Create_CAchievementKillEveryUncommonInfected()
{
 CAchievementKillEveryUncommonInfected *achievement=new CAchievementKillEveryUncommonInfected;
 achievement->Configure( 6, 19, "ACH_KILL_EVERY_UNCOMMON_INFECTED", 20, 4, 160, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillEveryUncommonInfected( Create_CAchievementKillEveryUncommonInfected );
static CBaseAchievement *Create_CAchievementKillSubmergedMudMen()
{
 CAchievementKillSubmergedMudMen *achievement=new CAchievementKillSubmergedMudMen;
 achievement->Configure( 8, 20, "ACH_KILL_SUBMERGED_MUDMEN", 20, 10, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillSubmergedMudMen( Create_CAchievementKillSubmergedMudMen );
static CBaseAchievement *Create_CAchievementCollectCEDAVials()
{
 CAchievementCollectCEDAVials *achievement=new CAchievementCollectCEDAVials;
 achievement->Configure( 10, 21, "ACH_COLLECT_CEDA_VIALS", 15, 10, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementCollectCEDAVials( Create_CAchievementCollectCEDAVials );
static CBaseAchievement *Create_CAchievementCollectGoldenCrowbars()
{
 CAchievementCollectGoldenCrowbars *achievement=new CAchievementCollectGoldenCrowbars;
 achievement->Configure( 102, 83, "ACH_COLLECT_GOLDEN_CROWBARS", 0, 20, 160, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementCollectGoldenCrowbars( Create_CAchievementCollectGoldenCrowbars );
static CBaseAchievement *Create_CAchievementHonkAClownsNose()
{
 CAchievementHonkAClownsNose *achievement=new CAchievementHonkAClownsNose;
 achievement->Configure( 1, 22, "ACH_HONK_A_CLOWNS_NOSE", 15, 10, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementHonkAClownsNose( Create_CAchievementHonkAClownsNose );
static CBaseAchievement *Create_CAchievementIncendiaryClownPosse()
{
 CAchievementIncendiaryClownPosse *achievement=new CAchievementIncendiaryClownPosse;
 achievement->Configure( 2, 23, "ACH_INCENDIARY_CLOWN_POSSE", 15, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementIncendiaryClownPosse( Create_CAchievementIncendiaryClownPosse );
static CBaseAchievement *Create_CAchievementMeleeACharger()
{
 CAchievementMeleeACharger *achievement=new CAchievementMeleeACharger;
 achievement->Configure( 3, 24, "ACH_MELEE_A_CHARGER", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementMeleeACharger( Create_CAchievementMeleeACharger );
static CBaseAchievement *Create_CAchievementKillSpitterFast()
{
 CAchievementKillSpitterFast *achievement=new CAchievementKillSpitterFast;
 achievement->Configure( 4, 25, "ACH_KILL_SPITTER_FAST", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillSpitterFast( Create_CAchievementKillSpitterFast );
static CBaseAchievement *Create_CAchievementSavePlayerFromJockeyFast()
{
 CAchievementSavePlayerFromJockeyFast *achievement=new CAchievementSavePlayerFromJockeyFast;
 achievement->Configure( 5, 26, "ACH_SAVE_PLAYER_FROM_JOCKEY_FAST", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSavePlayerFromJockeyFast( Create_CAchievementSavePlayerFromJockeyFast );
static CBaseAchievement *Create_CAchievementStashWhacker()
{
 CAchievementStashWhacker *achievement=new CAchievementStashWhacker;
 achievement->Configure( 7, 27, "ACH_STASH_WHACKER", 15, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementStashWhacker( Create_CAchievementStashWhacker );
static CBaseAchievement *Create_CAchievementStrongmanGame()
{
 CAchievementStrongmanGame *achievement=new CAchievementStrongmanGame;
 achievement->Configure( 26, 28, "ACH_STRONGMAN_GAME", 15, 1, 2176, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementStrongmanGame( Create_CAchievementStrongmanGame );
static CBaseAchievement *Create_CAchievementAirlineFinaleNoDamage()
{
 CAchievementAirlineFinaleNoDamage *achievement=new CAchievementAirlineFinaleNoDamage;
 achievement->Configure( 27, 30, "ACH_AIRLINE_FINALE_NO_DAMAGE", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementAirlineFinaleNoDamage( Create_CAchievementAirlineFinaleNoDamage );
static CBaseAchievement *Create_CAchievementWitchTownNoWitchKills()
{
 CAchievementWitchTownNoWitchKills *achievement=new CAchievementWitchTownNoWitchKills;
 achievement->Configure( 23, 31, "ACH_WITCH_TOWN_NO_WITCH_KILLS", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementWitchTownNoWitchKills( Create_CAchievementWitchTownNoWitchKills );
static CBaseAchievement *Create_CAchievementImpoundLotNoAlarms()
{
 CAchievementImpoundLotNoAlarms *achievement=new CAchievementImpoundLotNoAlarms;
 achievement->Configure( 22, 32, "ACH_IMPOUND_LOT_NO_ALARMS", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementImpoundLotNoAlarms( Create_CAchievementImpoundLotNoAlarms );
static CBaseAchievement *Create_CAchievementBridgeTimedRun()
{
 CAchievementBridgeTimedRun *achievement=new CAchievementBridgeTimedRun;
 achievement->Configure( 24, 33, "ACH_BRIDGE_TIMED_RUN", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementBridgeTimedRun( Create_CAchievementBridgeTimedRun );
static CBaseAchievement *Create_CAchievementLastStandTimed()
{
 CAchievementLastStandTimed *achievement=new CAchievementLastStandTimed;
 achievement->Configure( 101, 102, "ACH_LAST_STAND_TIMED", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementLastStandTimed( Create_CAchievementLastStandTimed );
static CBaseAchievement *Create_CAchievementBeatAtriumBeforeFirstTank()
{
 CAchievementBeatAtriumBeforeFirstTank *achievement=new CAchievementBeatAtriumBeforeFirstTank;
 achievement->Configure( 100, 101, "ACH_ESCAPE_MALL_FINALE_NO_TANKS", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementBeatAtriumBeforeFirstTank( Create_CAchievementBeatAtriumBeforeFirstTank );
static CBaseAchievement *Create_CAchievementBeatConcertFromStage()
{
 CAchievementBeatConcertFromStage *achievement=new CAchievementBeatConcertFromStage;
 achievement->Configure( 99, 99, "ACH_CONCERT_FINALE_DONT_LEAVE_STAGE", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementBeatConcertFromStage( Create_CAchievementBeatConcertFromStage );
static CBaseAchievement *Create_CAchievementFindLighthouseHiddenRoom()
{
 CAchievementFindLighthouseHiddenRoom *achievement=new CAchievementFindLighthouseHiddenRoom;
 achievement->Configure( 103, 103, "ACH_FIND_LIGHTHOUSE_HIDDEN_ROOM", 0, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementFindLighthouseHiddenRoom( Create_CAchievementFindLighthouseHiddenRoom );
static CBaseAchievement *Create_CAchievementLeaveSaferoomToDefib()
{
 CAchievementLeaveSaferoomToDefib *achievement=new CAchievementLeaveSaferoomToDefib;
 achievement->Configure( 46, 34, "ACH_VS_LEAVE_SAFE_ROOM_DEFIB_TEAMMATE", 20, 1, 128, 2, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementLeaveSaferoomToDefib( Create_CAchievementLeaveSaferoomToDefib );
static CBaseAchievement *Create_CAchievementWinATeamVsTeamGame()
{
 CAchievementWinATeamVsTeamGame *achievement=new CAchievementWinATeamVsTeamGame;
 achievement->Configure( 37, 35, "ACH_VS_FORM_PARTY_AND_WIN_4V4", 15, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementWinATeamVsTeamGame( Create_CAchievementWinATeamVsTeamGame );
static CBaseAchievement *Create_CAchievementVsJockeyLongRide()
{
 CAchievementVsJockeyLongRide *achievement=new CAchievementVsJockeyLongRide;
 achievement->Configure( 42, 36, "ACH_VS_JOCKEY_LONG_RIDE", 15, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsJockeyLongRide( Create_CAchievementVsJockeyLongRide );
static CBaseAchievement *Create_CAchievementVsJockeyTwoPlayers()
{
 CAchievementVsJockeyTwoPlayers *achievement=new CAchievementVsJockeyTwoPlayers;
 achievement->Configure( 43, 37, "ACH_VS_JOCKEY_TWO_PLAYERS", 15, 1, 2176, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsJockeyTwoPlayers( Create_CAchievementVsJockeyTwoPlayers );
static CBaseAchievement *Create_CAchievementVsJockeyIntoAcid()
{
 CAchievementVsJockeyIntoAcid *achievement=new CAchievementVsJockeyIntoAcid;
 achievement->Configure( 44, 38, "ACH_VS_JOCKEY_INTO_ACID", 20, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsJockeyIntoAcid( Create_CAchievementVsJockeyIntoAcid );
static CBaseAchievement *Create_CAchievementVsSpitOnEnemyTeam()
{
 CAchievementVsSpitOnEnemyTeam *achievement=new CAchievementVsSpitOnEnemyTeam;
 achievement->Configure( 41, 39, "ACH_VS_SPIT_ON_ENEMY_TEAM", 15, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsSpitOnEnemyTeam( Create_CAchievementVsSpitOnEnemyTeam );
static CBaseAchievement *Create_CAchievementVsSpitOnSmokeredPlayer()
{
 CAchievementVsSpitOnSmokeredPlayer *achievement=new CAchievementVsSpitOnSmokeredPlayer;
 achievement->Configure( 40, 40, "ACH_VS_SPIT_ON_SMOKERED_PLAYER", 15, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsSpitOnSmokeredPlayer( Create_CAchievementVsSpitOnSmokeredPlayer );
static CBaseAchievement *Create_CAchievementVsSpitOnChargedPlayer()
{
 CAchievementVsSpitOnChargedPlayer *achievement=new CAchievementVsSpitOnChargedPlayer;
 achievement->Configure( 86, 86, "ACH_VS_SPIT_ON_CHARGED_PLAYER", 0, 1, 128, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsSpitOnChargedPlayer( Create_CAchievementVsSpitOnChargedPlayer );
static CBaseAchievement *Create_CAchievementVsSpitOnPouncedPlayer()
{
 CAchievementVsSpitOnPouncedPlayer *achievement=new CAchievementVsSpitOnPouncedPlayer;
 achievement->Configure( 87, 87, "ACH_VS_SPIT_ON_POUNCED_PLAYER", 0, 1, 128, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsSpitOnPouncedPlayer( Create_CAchievementVsSpitOnPouncedPlayer );
static CBaseAchievement *Create_CAchievementVsBowlEnemyTeam()
{
 CAchievementVsBowlEnemyTeam *achievement=new CAchievementVsBowlEnemyTeam;
 achievement->Configure( 39, 41, "ACH_VS_BOWL_ENEMY_TEAM", 20, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsBowlEnemyTeam( Create_CAchievementVsBowlEnemyTeam );
static CBaseAchievement *Create_CAchievementVsChargerGroundSmash()
{
 CAchievementVsChargerGroundSmash *achievement=new CAchievementVsChargerGroundSmash;
 achievement->Configure( 38, 42, "ACH_VS_CHARGER_GROUND_SMASH", 20, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsChargerGroundSmash( Create_CAchievementVsChargerGroundSmash );
static CBaseAchievement *Create_CAchievementVsChargerLongCarry()
{
 CAchievementVsChargerLongCarry *achievement=new CAchievementVsChargerLongCarry;
 achievement->Configure( 45, 43, "ACH_VS_CHARGER_LONG_CARRY", 15, 1, 128, 0, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsChargerLongCarry( Create_CAchievementVsChargerLongCarry );
static CBaseAchievement *Create_CAchievementVsChargerInstantKill()
{
 CAchievementVsChargerInstantKill *achievement=new CAchievementVsChargerInstantKill;
 achievement->Configure( 89, 89, "ACH_VS_CHARGER_INSTANT_KILL", 0, 1, 2176, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsChargerInstantKill( Create_CAchievementVsChargerInstantKill );
static CBaseAchievement *Create_CAchievementVsBilePlayerNoLineOfSight()
{
 CAchievementVsBilePlayerNoLineOfSight *achievement=new CAchievementVsBilePlayerNoLineOfSight;
 achievement->Configure( 88, 88, "ACH_VS_BILE_PLAYER_NO_LINE_OF_SIGHT", 0, 1, 128, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsBilePlayerNoLineOfSight( Create_CAchievementVsBilePlayerNoLineOfSight );
static CBaseAchievement *Create_CAchievementGetMedalWithOnlyMelee()
{
 CAchievementGetMedalWithOnlyMelee *achievement=new CAchievementGetMedalWithOnlyMelee;
 achievement->Configure( 36, 44, "ACH_SURVIVAL_GET_MEDAL_MELEE_ONLY", 15, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGetMedalWithOnlyMelee( Create_CAchievementGetMedalWithOnlyMelee );
static CBaseAchievement *Create_CAchievementGetBronzeMedal()
{
 CAchievementGetBronzeMedal *achievement=new CAchievementGetBronzeMedal;
 achievement->Configure( 78, 78, "ACH_SURVIVAL_BRONZE_MEDAL", 0, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGetBronzeMedal( Create_CAchievementGetBronzeMedal );
static CBaseAchievement *Create_CAchievementGetSilverMedal()
{
 CAchievementGetSilverMedal *achievement=new CAchievementGetSilverMedal;
 achievement->Configure( 79, 79, "ACH_SURVIVAL_SILVER_MEDAL", 0, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGetSilverMedal( Create_CAchievementGetSilverMedal );
static CBaseAchievement *Create_CAchievementGetGoldMedal()
{
 CAchievementGetGoldMedal *achievement=new CAchievementGetGoldMedal;
 achievement->Configure( 80, 80, "ACH_SURVIVAL_GOLD_MEDAL", 0, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGetGoldMedal( Create_CAchievementGetGoldMedal );
static CBaseAchievement *Create_CAchievementEarnAllBronzeMedals()
{
 CAchievementEarnAllBronzeMedals *achievement=new CAchievementEarnAllBronzeMedals;
 achievement->Configure( 104, 104, "ACH_EARN_ALL_BRONZE_MEDALS", 0, 43, 160, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementEarnAllBronzeMedals( Create_CAchievementEarnAllBronzeMedals );
static CBaseAchievement *Create_CAchievementEarnAllSilverMedals()
{
 CAchievementEarnAllSilverMedals *achievement=new CAchievementEarnAllSilverMedals;
 achievement->Configure( 105, 105, "ACH_EARN_ALL_SILVER_MEDALS", 0, 43, 160, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementEarnAllSilverMedals( Create_CAchievementEarnAllSilverMedals );
static CBaseAchievement *Create_CAchievementEarnAllGoldMedals()
{
 CAchievementEarnAllGoldMedals *achievement=new CAchievementEarnAllGoldMedals;
 achievement->Configure( 106, 106, "ACH_EARN_ALL_GOLD_MEDALS", 0, 43, 160, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementEarnAllGoldMedals( Create_CAchievementEarnAllGoldMedals );
static CBaseAchievement *Create_CAchievementSurviveXMinutesVersusSurvival()
{
 CAchievementSurviveXMinutesVersusSurvival *achievement=new CAchievementSurviveXMinutesVersusSurvival;
 achievement->Configure( 108, 97, "ACH_SURVIVAL_VERSUS_X_MINUTES", 0, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveXMinutesVersusSurvival( Create_CAchievementSurviveXMinutesVersusSurvival );
static CBaseAchievement *Create_CAchievementSurviveXMinutesSurvival()
{
 CAchievementSurviveXMinutesSurvival *achievement=new CAchievementSurviveXMinutesSurvival;
 achievement->Configure( 81, 97, "ACH_SURVIVAL_X_MINUTES", 0, 1, 128, 2, 1 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveXMinutesSurvival( Create_CAchievementSurviveXMinutesSurvival );
static CBaseAchievement *Create_CAchievementWinAScavengeGame()
{
 CAchievementWinAScavengeGame *achievement=new CAchievementWinAScavengeGame;
 achievement->Configure( 34, 45, "ACH_SCAVENGE_WIN_A_GAME", 15, 1, 128, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementWinAScavengeGame( Create_CAchievementWinAScavengeGame );
static CBaseAchievement *Create_CAchievementScavengeCapGrind()
{
 CAchievementScavengeCapGrind *achievement=new CAchievementScavengeCapGrind;
 achievement->Configure( 35, 46, "ACH_SCAVENGE_COLLECT_CAN_GRIND", 20, 100, 2176, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeCapGrind( Create_CAchievementScavengeCapGrind );
static CBaseAchievement *Create_CAchievementScavengeCollectAllCans()
{
 CAchievementScavengeCollectAllCans *achievement=new CAchievementScavengeCollectAllCans;
 achievement->Configure( 31, 47, "ACH_SCAVENGE_COLLECT_ALL_CANS", 20, 1, 128, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeCollectAllCans( Create_CAchievementScavengeCollectAllCans );
static CBaseAchievement *Create_CAchievementScavengeShutOut()
{
 CAchievementScavengeShutOut *achievement=new CAchievementScavengeShutOut;
 achievement->Configure( 30, 48, "ACH_SCAVENGE_SHUT_OUT", 15, 1, 128, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeShutOut( Create_CAchievementScavengeShutOut );
static CBaseAchievement *Create_CAchievementScavengeMakeSurvivorDropCan()
{
 CAchievementScavengeMakeSurvivorDropCan *achievement=new CAchievementScavengeMakeSurvivorDropCan;
 achievement->Configure( 32, 49, "ACH_SCAVENGE_MAKE_SURVIVOR_DROP_CAN", 15, 1, 128, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeMakeSurvivorDropCan( Create_CAchievementScavengeMakeSurvivorDropCan );
static CBaseAchievement *Create_CAchievementScavengeCanDropGrind()
{
 CAchievementScavengeCanDropGrind *achievement=new CAchievementScavengeCanDropGrind;
 achievement->Configure( 33, 50, "ACH_SCAVENGE_CAN_DROP_GRIND", 20, 25, 2176, 0, 3 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeCanDropGrind( Create_CAchievementScavengeCanDropGrind );
static CBaseAchievement *Create_CAchievementSurviveAllAnyDifficulty()
{
 CAchievementSurviveAllAnyDifficulty *achievement=new CAchievementSurviveAllAnyDifficulty;
 achievement->Configure( 51, 51, "ASSET_MED_KIT", 0, 5, 160, 2, 4 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveAllAnyDifficulty( Create_CAchievementSurviveAllAnyDifficulty );
static CBaseAchievement *Create_CAchievementKillThenThousandZombies()
{
 CAchievementKillThenThousandZombies *achievement=new CAchievementKillThenThousandZombies;
 achievement->Configure( 52, 52, "ASSET_ZOMBIE_HAND_SHIRT", 0, 10000, 128, 2, 4 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillThenThousandZombies( Create_CAchievementKillThenThousandZombies );
static CBaseAchievement *Create_CAchievementGnomeRescueAsset()
{
 CAchievementGnomeRescueAsset *achievement=new CAchievementGnomeRescueAsset;
 achievement->Configure( 53, 53, "ASSET_DEPECHE_MODE_SHIRT", 0, 1, 128, 2, 4 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGnomeRescueAsset( Create_CAchievementGnomeRescueAsset );
static CBaseAchievement *Create_CAchievementGnomeRescue()
{
 CAchievementGnomeRescue *achievement=new CAchievementGnomeRescue;
 achievement->Configure( 25, 29, "ACH_GNOME_RESCUE", 30, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGnomeRescue( Create_CAchievementGnomeRescue );
static CBaseAchievement *Create_CAchievementWin10Versus()
{
 CAchievementWin10Versus *achievement=new CAchievementWin10Versus;
 achievement->Configure( 54, 54, "ASSET_BULL_SHIFTERS_SHIRT", 0, 10, 128, 0, 4 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementWin10Versus( Create_CAchievementWin10Versus );
static CBaseAchievement *Create_CAchievementWin10ScavengeAsset()
{
 CAchievementWin10ScavengeAsset *achievement=new CAchievementWin10ScavengeAsset;
 achievement->Configure( 55, 55, "ASSET_LEFT_4_DEAD_2_SHIRT", 0, 10, 128, 0, 4 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementWin10ScavengeAsset( Create_CAchievementWin10ScavengeAsset );
static CBaseAchievement *Create_CAchievementSurviveThePassing()
{
 CAchievementSurviveThePassing *achievement=new CAchievementSurviveThePassing;
 achievement->Configure( 56, 56, "ACH_SURVIVE_THE_PASSING", 20, 1, 128, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveThePassing( Create_CAchievementSurviveThePassing );
static CBaseAchievement *Create_CAchievementVsChargeThroughChairs()
{
 CAchievementVsChargeThroughChairs *achievement=new CAchievementVsChargeThroughChairs;
 achievement->Configure( 57, 57, "ACH_VS_CHARGE_THROUGH_CHAIRS", 30, 1, 2176, 3, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsChargeThroughChairs( Create_CAchievementVsChargeThroughChairs );
static CBaseAchievement *Create_CAchievementVsCompleteDeadAir()
{
 CAchievementVsCompleteDeadAir *achievement=new CAchievementVsCompleteDeadAir;
 achievement->Configure( 74, 37, "ACH_VS_COMPLETE_DEAD_AIR", 15, 1, 2176, 0, 7 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVsCompleteDeadAir( Create_CAchievementVsCompleteDeadAir );
static CBaseAchievement *Create_CAchievementKillManyWithM60()
{
 CAchievementKillManyWithM60 *achievement=new CAchievementKillManyWithM60;
 achievement->Configure( 58, 58, "ACH_KILLS_WITH_M60", 20, 1, 128, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillManyWithM60( Create_CAchievementKillManyWithM60 );
static CBaseAchievement *Create_CAchievementScavengeFromFallenSurvivor()
{
 CAchievementScavengeFromFallenSurvivor *achievement=new CAchievementScavengeFromFallenSurvivor;
 achievement->Configure( 59, 59, "ACH_SCAVENGE_FALLEN_SURVIVOR", 25, 10, 128, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementScavengeFromFallenSurvivor( Create_CAchievementScavengeFromFallenSurvivor );
static CBaseAchievement *Create_CAchievementCompleteChallengeModes()
{
 CAchievementCompleteChallengeModes *achievement=new CAchievementCompleteChallengeModes;
 achievement->Configure( 60, 60, "ACH_COMPLETE_CHALLENGE_MODES", 30, 6, 2208, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementCompleteChallengeModes( Create_CAchievementCompleteChallengeModes );
static CBaseAchievement *Create_CAchievementGolfClubHeads()
{
 CAchievementGolfClubHeads *achievement=new CAchievementGolfClubHeads;
 achievement->Configure( 61, 61, "ACH_HEADS_WITH_GOLF_CLUB", 25, 18, 2176, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGolfClubHeads( Create_CAchievementGolfClubHeads );
static CBaseAchievement *Create_CAchievementHearBallad()
{
 CAchievementHearBallad *achievement=new CAchievementHearBallad;
 achievement->Configure( 62, 62, "ACH_HEAR_BALLAD", 20, 1, 128, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementHearBallad( Create_CAchievementHearBallad );
static CBaseAchievement *Create_CAchievementKillTankL4D1Only()
{
 CAchievementKillTankL4D1Only *achievement=new CAchievementKillTankL4D1Only;
 achievement->Configure( 63, 63, "ACH_KILL_TANK_L4D1_ONLY", 30, 1, 128, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementKillTankL4D1Only( Create_CAchievementKillTankL4D1Only );
static CBaseAchievement *Create_CAchievementOpenFootLockers()
{
 CAchievementOpenFootLockers *achievement=new CAchievementOpenFootLockers;
 achievement->Configure( 64, 64, "ACH_OPEN_FOOT_LOCKERS", 20, 5, 2176, 2, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementOpenFootLockers( Create_CAchievementOpenFootLockers );
static CBaseAchievement *Create_CAchievementPlayC6FinaleScavenge()
{
 CAchievementPlayC6FinaleScavenge *achievement=new CAchievementPlayC6FinaleScavenge;
 achievement->Configure( 65, 65, "ACH_SCAVENGE_PLAY_C6_FINALE", 30, 5, 128, 0, 5 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementPlayC6FinaleScavenge( Create_CAchievementPlayC6FinaleScavenge );
static CBaseAchievement *Create_CAchievementDLC2CompleteTheSacrifice()
{
 CAchievementDLC2CompleteTheSacrifice *achievement=new CAchievementDLC2CompleteTheSacrifice;
 achievement->Configure( 68, 68, "ACH_COMPLETE_THE_SACRIFICE", 50, 1, 128, 2, 6 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDLC2CompleteTheSacrifice( Create_CAchievementDLC2CompleteTheSacrifice );
static CBaseAchievement *Create_CAchievementDLC2SacrificeBill()
{
 CAchievementDLC2SacrificeBill *achievement=new CAchievementDLC2SacrificeBill;
 achievement->Configure( 69, 69, "ACH_SACRIFICE_BILL", 50, 1, 128, 2, 6 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDLC2SacrificeBill( Create_CAchievementDLC2SacrificeBill );
static CBaseAchievement *Create_CAchievementDLC2KillSpecialWithExplodingBarrel()
{
 CAchievementDLC2KillSpecialWithExplodingBarrel *achievement=new CAchievementDLC2KillSpecialWithExplodingBarrel;
 achievement->Configure( 70, 70, "ACH_KILL_SPECIAL_WITH_BARREL", 50, 1, 2176, 2, 6 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDLC2KillSpecialWithExplodingBarrel( Create_CAchievementDLC2KillSpecialWithExplodingBarrel );
static CBaseAchievement *Create_CAchievementDLC2RunAllGeneratorsAtOnce()
{
 CAchievementDLC2RunAllGeneratorsAtOnce *achievement=new CAchievementDLC2RunAllGeneratorsAtOnce;
 achievement->Configure( 71, 71, "ACH_RUN_ALL_GENERATORS", 50, 1, 128, 2, 6 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDLC2RunAllGeneratorsAtOnce( Create_CAchievementDLC2RunAllGeneratorsAtOnce );
static CBaseAchievement *Create_CAchievementDLC2PreventSacrifice()
{
 CAchievementDLC2PreventSacrifice *achievement=new CAchievementDLC2PreventSacrifice;
 achievement->Configure( 72, 72, "ACH_VS_PREVENT_SACRIFICE", 50, 1, 128, 3, 6 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementDLC2PreventSacrifice( Create_CAchievementDLC2PreventSacrifice );
static CBaseAchievement *Create_CAchievementSurviveColdStream()
{
 CAchievementSurviveColdStream *achievement=new CAchievementSurviveColdStream;
 achievement->Configure( 73, 73, "ACH_SURVIVE_COLD_STREAM", 10, 1, 128, 2, 7 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementSurviveColdStream( Create_CAchievementSurviveColdStream );
static CBaseAchievement *Create_CAchievementVSSpitOnXGasCans()
{
 CAchievementVSSpitOnXGasCans *achievement=new CAchievementVSSpitOnXGasCans;
 achievement->Configure( 85, 85, "ACH_VS_SPIT_ON_GAS_CANS", 0, 20, 2176, 3, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementVSSpitOnXGasCans( Create_CAchievementVSSpitOnXGasCans );
static CBaseAchievement *Create_CAchievementCollectHolidayGifts()
{
 CAchievementCollectHolidayGifts *achievement=new CAchievementCollectHolidayGifts;
 achievement->Configure( 75, 75, "ACH_COLLECT_HOLIDAY_GIFTS", 10, 1, 128, 2, 2 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementCollectHolidayGifts( Create_CAchievementCollectHolidayGifts );
static CBaseAchievement *Create_CAchievementHelpFreePlayerSurvive()
{
 CAchievementHelpFreePlayerSurvive *achievement=new CAchievementHelpFreePlayerSurvive;
 achievement->Configure( 76, 76, "ACH_HELP_FREE_PLAYER_SURVIVE", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementHelpFreePlayerSurvive( Create_CAchievementHelpFreePlayerSurvive );
static CBaseAchievement *Create_CAchievementHelpChristmasPlayerSurvive()
{
 CAchievementHelpChristmasPlayerSurvive *achievement=new CAchievementHelpChristmasPlayerSurvive;
 achievement->Configure( 77, 77, "ACH_HELP_FREE_CHRISTMAS_PLAYER_SURVIVE", 20, 1, 128, 2, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementHelpChristmasPlayerSurvive( Create_CAchievementHelpChristmasPlayerSurvive );
static CBaseAchievement *Create_CAchievementGlobalGnomeAlone()
{
 CAchievementGlobalGnomeAlone *achievement=new CAchievementGlobalGnomeAlone;
 achievement->Configure( 109, 109, "GLOBAL_GNOME_ALONE", 0, 1, 2176, 0, 0 );
 return achievement;
}
static CBaseAchievementHelper g_CAchievementGlobalGnomeAlone( Create_CAchievementGlobalGnomeAlone );
