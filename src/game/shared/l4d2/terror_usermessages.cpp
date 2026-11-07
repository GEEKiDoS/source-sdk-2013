//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared L4D2 usermessage registration metadata.
//
//=============================================================================//

#include "cbase.h"
#include "usermessages.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Original server registrar RVA 0x24C030. Registration does not install handlers.
void RegisterUserMessages()
{
	usermessages->Register( "Geiger", 1 );
	usermessages->Register( "Train", 1 );
	usermessages->Register( "HudText", -1 );
	usermessages->Register( "SayText", -1 );
	usermessages->Register( "SayText2", -1 );
	usermessages->Register( "TextMsg", -1 );
	usermessages->Register( "HudMsg", -1 );
	usermessages->Register( "ResetHUD", 1 );
	usermessages->Register( "GameTitle", 0 );
	usermessages->Register( "ItemPickup", -1 );
	usermessages->Register( "ShowMenu", -1 );
	usermessages->Register( "Shake", 13 );
	usermessages->Register( "Fade", 10 );
	usermessages->Register( "VGUIMenu", -1 );
	usermessages->Register( "Rumble", 3 );
	usermessages->Register( "CloseCaption", -1 );
	usermessages->Register( "CloseCaptionDirect", -1 );
	usermessages->Register( "SendAudio", -1 );
	usermessages->Register( "RawAudio", -1 );
	usermessages->Register( "VoiceMask", 9 );
	usermessages->Register( "RequestState", 0 );
	usermessages->Register( "BarTime", -1 );
	usermessages->Register( "Damage", -1 );
	usermessages->Register( "RadioText", -1 );
	usermessages->Register( "HintText", -1 );
	usermessages->Register( "KeyHintText", -1 );
	usermessages->Register( "ReloadEffect", 4 );
	usermessages->Register( "PlayerAnimEvent", -1 );
	usermessages->Register( "AmmoDenied", 2 );
	usermessages->Register( "UpdateRadar", -1 );
	usermessages->Register( "KillCam", -1 );
	usermessages->Register( "MarkAchievement", -1 );
	usermessages->Register( "Splatter", 1 );
	usermessages->Register( "MeleeSlashSplatter", 1 );
	usermessages->Register( "MeleeClubSplatter", 1 );
	usermessages->Register( "MudSplatter", 1 );
	usermessages->Register( "SplatterClear", 0 );
	usermessages->Register( "MessageText", -1 );
	usermessages->Register( "TransitionRestore", 0 );
	usermessages->Register( "Spawn", 1 );
	usermessages->Register( "CreditsLine", -1 );
	usermessages->Register( "CreditsMsg", 0 );
	usermessages->Register( "JoinLateMsg", 0 );
	usermessages->Register( "StatsCrawlMsg", 0 );
	usermessages->Register( "StatsSkipState", 2 );
	usermessages->Register( "ShowStats", -1 );
	usermessages->Register( "BlurFade", 0 );
	usermessages->Register( "MusicCmd", -1 );
	usermessages->Register( "WitchBloodSplatter", -1 );
	usermessages->Register( "AchievementEvent", -1 );
	usermessages->Register( "PZDmgMsg", -1 );
	usermessages->Register( "AllPlayersConnectedGameStarting", 0 );
	usermessages->Register( "VoteRegistered", 1 );
	usermessages->Register( "DisconnectToLobby", 0 );
	usermessages->Register( "CallVoteFailed", 1 );
	usermessages->Register( "SteamWeaponStatData", -1 );
	usermessages->Register( "CurrentTimescale", 4 );
	usermessages->Register( "DesiredTimescale", 16 );
	usermessages->Register( "PZEndGamePanelMsg", 1 );
	usermessages->Register( "PZEndGamePanelVoteRegisteredMsg", 1 );
	usermessages->Register( "PZEndGameVoteStatsMsg", 8 );
	usermessages->Register( "VoteStart", -1 );
	usermessages->Register( "VotePass", -1 );
	usermessages->Register( "VoteFail", 1 );
	usermessages->Register( "SavedConvar", -1 );
}
