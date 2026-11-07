// Native S22E470 ordinary-function phase, EXACT global order0..97.
// Include inside VScriptServerInit under L4D2 in place of its SDK ordinary
// registration block. Existing callback owners are reused; no late duplicates.
// 0: server0x206e20, descriptor0x835fd0
ScriptRegisterFunctionNamed( g_pScriptVM, UTIL_ShowMessageAll, "ShowMessage", "Print a hud message on all clients" );
// 1: server0x21fb40, descriptor0x835fa0
ScriptRegisterFunctionNamed( g_pScriptVM, SendToConsole, "SendToConsole", "Send a string to the console as a client command" );
// 2: server0x21c130, descriptor0x835f70
ScriptRegisterFunctionNamed( g_pScriptVM, SendToServerConsole, "SendToServerConsole", "Send a string to the console as a server command" );
// 3: server0x21c120, descriptor0x835f40
ScriptRegisterFunctionNamed( g_pScriptVM, Time, "Time", "Get the current server time" );
// 4: server0x21e730, descriptor0x835f10
ScriptRegisterFunctionNamed( g_pScriptVM, DoEntFire, "DoEntFire", "#EntFire:Generate and entity i/o event" );
// 5: server0x21c160, descriptor0x835ee0
ScriptRegisterFunctionNamed( g_pScriptVM, DoUniqueString, "DoUniqueString", "#UniqueString:Generate a string guaranteed to be unique across the life of the script VM, with an optional root string. Useful for adding data to tables when not sure what keys are already in use in that table." );
// 6: server0x223fe0, descriptor0x835eb0
ScriptRegisterFunctionNamed( g_pScriptVM, DoIncludeScript, "DoIncludeScript", "Execute a script (internal)" );
// 7: server0x21e4c0, descriptor0x835e80
ScriptRegisterFunctionNamed( g_pScriptVM, RegisterScriptGameEventListener, "RegisterScriptGameEventListener", "Register as a listener for a game event from script." );
// 8: server0x21e880, descriptor0x835e50
ScriptRegisterFunctionNamed( g_pScriptVM, EntIndexToHScript, "EntIndexToHScript", "Turn an entity index integer to an HScript representing that entity's script instance." );
// 9: server0x21e8a0, descriptor0x835e20
ScriptRegisterFunctionNamed( g_pScriptVM, PlayerInstanceFromIndex, "PlayerInstanceFromIndex", "Get a script instance of a player by index." );
// 10: server0x224090, descriptor0x835df0
ScriptRegisterFunctionNamed( g_pScriptVM, ScriptFireGameEvent, "FireGameEvent", "Fire a game event to a listening callback function in script. Parameters are passed in a squirrel table." );
// 11: server0x221000, descriptor0x835dc0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SpawnEntityFromTable, "SpawnEntityFromTable", "Spawn entity from KeyValues in table - 'name' is entity name, rest are KeyValues for spawn." );
// 12: server0x221090, descriptor0x835d90
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SpawnEntityGroupFromTable, "SpawnEntityGroupFromTable", "Hierarchically spawn an entity group from a set of spawn tables." );
// 13: server0x2212f0, descriptor0x835d60
ScriptRegisterFunctionNamed( g_pScriptVM, Script_PrecacheItemFromTable, "PrecacheEntityFromTable", "Precache an entity from KeyValues in table" );
// 14: server0x21c190, descriptor0x835d30
ScriptRegisterFunctionNamed( g_pScriptVM, RotatePosition, "RotatePosition", "Rotate a Vector around a point." );
// 15: server0x21c270, descriptor0x835d00
ScriptRegisterFunctionNamed( g_pScriptVM, RotateOrientation, "RotateOrientation", "Rotate a QAngle by another QAngle." );
// 16: server0x21e8c0, descriptor0x835cd0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_EmitSoundOn, "EmitSoundOn", "Play named sound on Entity" );
// 17: server0x21e950, descriptor0x835ca0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_EmitSoundOnClient, "EmitSoundOnClient", "Play named sound only on the client for the passed in player" );
// 18: server0x21e910, descriptor0x835c70
ScriptRegisterFunctionNamed( g_pScriptVM, Script_StopSoundOn, "StopSoundOn", "Stop named sound on Entity" );
// 19: server0x21fbf0, descriptor0x835c40
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetPhysVelocity, "GetPhysVelocity", "Get Velocity for VPHYS or normal object" );
// 20: server0x21fc80, descriptor0x835c10
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetPhysAngularVelocity, "GetPhysAngularVelocity", "Get Angular Velocity for VPHYS or normal object" );
// 21: server0x21fd60, descriptor0x835be0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_Say, "Say", "Have Entity say string, and teamOnly or not" );
// 22: server0x2215d0, descriptor0x835bb0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetInfectedStats, "GetInfectedStats", "Fill passed in table with current stats about infected in the world" );
// 23: server0x21ebe0, descriptor0x835b80
ScriptRegisterFunctionNamed( g_pScriptVM, Script_AddThinkToEnt, "AddThinkToEnt", "Adds a late bound think function to the C++ think tables for the obj" );
// 24: server0x21c770, descriptor0x835b50
ScriptRegisterFunctionNamed( g_pScriptVM, Script_StartAssault, "StartAssault", "Tells all existing nextbots to swap into assault mode and not hide/dither/loiter" );
// 25: server0x35fa60, descriptor0x835b20
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ZSpawn, "ZSpawn", "Does a z_spawn like spawn based on an input table" );
// 26: server0x21ec80, descriptor0x835af0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetCharacterDisplayName, "GetCharacterDisplayName", "Return null if not a survivor, else the survivors 'real name'" );
// 27: server0x21eb10, descriptor0x835ac0
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetFriction, "GetFriction", "Returns the Friction on a player entity, meaningless if not a player" );
// 28: server0x21ed10, descriptor0x835a90
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetPlayerFromCharacter, "GetPlayerFromCharacter", "Given a character type, return the entity, or null" );
// 29: server0x21ecf0, descriptor0x835a60
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetPlayerFromUserID, "GetPlayerFromUserID", "Given a user id, return the entity, or null" );
// 30: server0x21ed30, descriptor0x835a30
ScriptRegisterFunctionNamed( g_pScriptVM, Script_IsPlayerABot, "IsPlayerABot", "Is this player/entity a bot" );
// 31: server0xa0440, descriptor0x835a00
ScriptRegisterFunctionNamed( g_pScriptVM, NDebugOverlay::ScreenTextLine, "DebugDrawScreenTextLine", "Draw text with a line offset" );
// 32: server0xa2210, descriptor0x8359cc
ScriptRegisterFunctionNamed( g_pScriptVM, NDebugOverlay::Text, "DebugDrawText", "Draw text in 3d (origin, text, bViewCheck, duration)" );
// 33: server0xa04b0, descriptor0x83599c
ScriptRegisterFunctionNamed( g_pScriptVM, NDebugOverlay::Box, "DebugDrawBox", "Draw a debug overlay box" );
// 34: server0xa05e0, descriptor0x83596c
ScriptRegisterFunctionNamed( g_pScriptVM, NDebugOverlay::Line, "DebugDrawLine", "Draw a debug overlay line" );
// 35: server0x21c610, descriptor0x83593c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_OverlayCircle, "DebugDrawCircle", "Draw a debug circle (center, rad, vRgb, a, ztest, duration)" );
// 36: server0x21c690, descriptor0x83590c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_OverlayLine_vCol, "DebugDrawLine_vCol", "Draw a debug line using color vec (start, end, vRgb, a, ztest, duration)" );
// 37: server0x21c6d0, descriptor0x8358dc
ScriptRegisterFunctionNamed( g_pScriptVM, Script_OverlayBoxDirection, "DebugDrawBoxDirection", "Draw a debug forward box (cent, min, max, forward, vRgb, a, duration)" );
// 38: server0x21c710, descriptor0x8358ac
ScriptRegisterFunctionNamed( g_pScriptVM, Script_OverlayBoxAngles, "DebugDrawBoxAngles", "Draw a debug oriented box (cent, min, max, angles(p,y,r), vRgb, a, duration)" );
// 39: server0x21c750, descriptor0x83587c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_OverlayClear, "DebugDrawClear", "Try to clear all the debug overlay info" );
// 40: server0x2c46e0, descriptor0x83584c
ScriptRegisterFunctionNamed( g_pScriptVM, CSpeechScriptBridge::Script_AddDecisionRule, "rr_AddDecisionRule", "Add a rule to the decision database." );
// 41: server0x2c5060, descriptor0x83581c
ScriptRegisterFunctionNamed( g_pScriptVM, CSpeechScriptBridge::Script_FindBestResponse, "rr_QueryBestResponse", "Params: (entity, query) : tests 'query' against entity's response system and returns the best response found (or null if none found)." );
// 42: server0x2c4d80, descriptor0x8357ec
ScriptRegisterFunctionNamed( g_pScriptVM, CSpeechScriptBridge::Script_CommitAIResponse, "rr_CommitAIResponse", "Commit the result of QueryBestResponse back to the given entity to play. Call with params (entity, airesponse)" );
// 43: server0x2c2d80, descriptor0x8357bc
ScriptRegisterFunctionNamed( g_pScriptVM, CSpeechScriptBridge::Script_GetExpressers, "rr_GetResponseTargets", "Retrieve a table of all available expresser targets, in the form { name : handle, name: handle }." );
// 44: server0x21ea00, descriptor0x83578c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetCurrentFlowDistanceForPlayer, "GetCurrentFlowDistanceForPlayer", "Gets the distance of how far the given player has made it to the goal." );
// 45: server0x21ea70, descriptor0x83575c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetCurrentFlowPercentForPlayer, "GetCurrentFlowPercentForPlayer", "Gets the percentage of how far the given player has made it to the goal." );
// 46: server0x21c2e0, descriptor0x83572c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetFlowDistanceForPosition, "GetFlowDistanceForPosition", "Gets distance from the start of the map for a given position." );
// 47: server0x21c330, descriptor0x8356fc
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetFlowPercentForPosition, "GetFlowPercentForPosition", "Gets the percent of where in the flow a given position is." );
// 48: server0x21c3c0, descriptor0x8356cc
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetMaxFlowDistance, "GetMaxFlowDistance", "Gets the max flow distance for the entire map." );
// 49: server0x21fbb0, descriptor0x83569c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetAverageSurvivorFlowDistance, "GetAverageSurvivorFlowDistance", "Gets the average flow distance of all the survivors." );
// ordinals50..53; same existing HUD owner, corrected native internal order.
RegisterTerrorScriptedHUDFunctions();
// 54: server0x2213e0, descriptor0x8355ac
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetInvTable, "GetInvTable", "Put Player's inventory data into a passed in table" );
// 55: server0x21c4a0, descriptor0x83557c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_RestoreTable, "RestoreTable", "Put saved data back into the passed in table" );
// 56: server0x21c480, descriptor0x83554c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SaveTable, "SaveTable", "Save the passed in table" );
// 57: server0x21c4c0, descriptor0x83551c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ClearSavedTables, "ClearSavedTables", "Remove all saved tables" );
// 58: server0x21eb50, descriptor0x8354ec
ScriptRegisterFunctionNamed( g_pScriptVM, Script_PickupObject, "PickupObject", "Have a player pickup a nearby named entity" );
// 59: server0x21c4d0, descriptor0x8354bc
ScriptRegisterFunctionNamed( g_pScriptVM, Script_DropSpit, "DropSpit", "Drop a spit pool from the specified location" );
// 60: server0x21c570, descriptor0x83548c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_DropFire, "DropFire", "Drop a fire pool from the specified location" );
// 61: server0x21ed90, descriptor0x83545c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_StringToFile, "StringToFile", "Store a string to a file for later reading" );
// 62: server0x21ee60, descriptor0x83542c
ScriptRegisterFunctionNamed( g_pScriptVM, Script_FileToString, "FileToString", "Reads a string from a file to send to script" );
// 63: server0x21c780, descriptor0x8353fc
ScriptRegisterFunctionNamed( g_pScriptVM, Script_LoadMOTD, "ReloadMOTD", "Reloads the MotD file" );
// 64: server0x2217a0, descriptor0x8353c8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_TraceLine, "TraceLine", "Pass table - Inputs: start, end, mask, ignore  -- outputs: pos, fraction, hit, enthit, startsolid" );
// 65: server0x2240f0, descriptor0x835398
ScriptRegisterFunctionNamed( g_pScriptVM, Script_CommandABot, "CommandABot", "Pass table - Inputs: bot, target, pos, cmd -- cmd BOT_CMD_MOVE, _ATTACK, _RETREAT, _RESET" );
// 66: server0x2243d0, descriptor0x835368
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ReapplyInfectedFlags, "ReapplyInfectedFlags", "Apply passed flags to passed infected or all commons if 2nd param is null" );
// 67: server0x21c8a0, descriptor0x835338
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetFrameCount, "GetFrameCount", "Returns the engines current frame count" );
// 68: server0x21ef80, descriptor0x835308
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ClientPrint, "ClientPrint", "Print a client message" );
// 69: server0x21f010, descriptor0x8352d8
ScriptRegisterFunctionNamed( g_pScriptVM, ScriptEmitAmbientSoundOn, "EmitAmbientSoundOn", "Play named ambient sound on an entity." );
// 70: server0x21f0a0, descriptor0x8352a8
ScriptRegisterFunctionNamed( g_pScriptVM, ScriptStopAmbientSoundOn, "StopAmbientSoundOn", "Stop named ambient sound on an entity." );
// 71: server0x21fe00, descriptor0x835278
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SetFakeClientConVarValue, "SetFakeClientConVarValue", "Sets a USERINFO client ConVar for a fakeclient" );
// 72: server0x21c8b0, descriptor0x835248
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ScreenShake, "ScreenShake", "Start a screenshake with the following parameters. vecCenter, flAmplitude, flFrequency, flDuration, flRadius, eCommand( SHAKE_START = 0, SHAKE_STOP = 1 ), bAirShake" );
// 73: server0x21f130, descriptor0x835218
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ScreenFade, "ScreenFade", "Start a screenfade with the following parameters. player, red, green, blue, alpha, flFadeTime, flFadeHold, flags" );
// 74: server0x21c8f0, descriptor0x8351e8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_PrecacheModel, "PrecacheModel", "Precache a model." );
// 75: server0x21c900, descriptor0x8351b8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_PrecacheSound, "PrecacheSound", "Precache a sound." );
// 76: server0x21c910, descriptor0x835188
ScriptRegisterFunctionNamed( g_pScriptVM, Script_IsModelPrecached, "IsModelPrecached", "Checks if the modelname is precached." );
// 77: server0x21c930, descriptor0x835158
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetModelIndex, "GetModelIndex", "Returns the index of the named model." );
// 78: server0x21c950, descriptor0x835128
ScriptRegisterFunctionNamed( g_pScriptVM, Script_IsDedicatedServer, "IsDedicatedServer", "Returns true if this server is a dedicated server." );
// 79: server0x21f1c0, descriptor0x8350f8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetListenServerHost, "GetListenServerHost", "Get the local player on a listen server." );
// 80: server0x21c960, descriptor0x8350c8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetSoundDuration, "GetSoundDuration", "Returns float duration of the sound. Takes soundname and optional actormodelname." );
// 81: server0x21c970, descriptor0x835098
ScriptRegisterFunctionNamed( g_pScriptVM, Script_IsSoundPrecached, "IsSoundPrecached", "Takes a sound name" );
// 82: server0x21c990, descriptor0x835068
ScriptRegisterFunctionNamed( g_pScriptVM, Script_HasPlayerControlledZombies, "HasPlayerControlledZombies", "Does the current game mode have humans playing as zombies?" );
// 83: server0x221bb0, descriptor0x835038
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetLocalTime, "LocalTime", "Fills out a table with the local time (second, minute, hour, day, month, year, dayofweek, dayofyear, daylightsavings)" );
// 84: server0x21c9b0, descriptor0x835008
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ForceVersusStart, "ForceVersusStart", "Starts the versus round, even if the survivors haven't left the safe room" );
// 85: server0x21c9e0, descriptor0x834fd8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ForceSurvivalStart, "ForceSurvivalStart", "Starts the survival round, even if the survivors haven't activated the radio" );
// 86: server0x21ca10, descriptor0x834fa8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_ForceScavengeStart, "ForceScavengeStart", "Starts the scavenge round, even if the setup timer hasn't elapsed" );
// 87: server0x21fe80, descriptor0x834f78
ScriptRegisterFunctionNamed( g_pScriptVM, Script_QueueSpeak, "QueueSpeak", "(hEntity, szConcept, flDelay, szCriteria) Queue a speech concept" );
// 88: server0x224440, descriptor0x834f48
ScriptRegisterFunctionNamed( g_pScriptVM, Script_RushVictim, "RushVictim", "(hVictim, flRange) Make all common zombies in range rush the victim" );
// 89: server0x21ca50, descriptor0x834f18
ScriptRegisterFunctionNamed( g_pScriptVM, Script_IsMissionFinalMap, "IsMissionFinalMap", "True if the current map is the final map of the campaign" );
// 90: server0x21f200, descriptor0x834ee8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_FindRescueAreaTrigger, "FindRescueAreaTrigger", "Returns the trigger for the rescue area" );
// 91: server0x21ca60, descriptor0x834eb8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetDifficulty, "GetDifficulty", "Returns the current difficulty as a numeric value." );
// 92: server0x21ca70, descriptor0x834e88
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetDifficultyString, "GetDifficultyString", "Returns the current difficulty as a string." );
// 93: server0x21ca80, descriptor0x834e58
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetScavengeItemsGoal, "GetScavengeItemsGoal", "Returns number of Scavenge items needed." );
// 94: server0x21ca90, descriptor0x834e28
ScriptRegisterFunctionNamed( g_pScriptVM, Script_GetScavengeItemsRemaining, "GetScavengeItemsRemaining", "Returns number of Scavenge items remaining." );
// 95: server0x21caa0, descriptor0x834df8
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SetScavengeItemsGoal, "SetScavengeItemsGoal", "Set the number of Scavenge items needed." );
// 96: server0x21cac0, descriptor0x834dc4
ScriptRegisterFunctionNamed( g_pScriptVM, Script_SetScavengeItemsRemaining, "SetScavengeItemsRemaining", "Set the number of Scavenge items remaining." );
// 97: server0x21cae0, descriptor0x834d94
ScriptRegisterFunctionNamed( g_pScriptVM, Script_HasConfigurableDifficultySetting, "HasConfigurableDifficultySetting", "Returns true if the mode has more than a single difficulty." );
