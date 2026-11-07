//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "activitylist.h"
#include "utlvector.h"

#include "tier0/memdbgon.h"

namespace
{
	struct TerrorActivityInverseName_t
	{
		const char *input;
		const char *output;
	};

	CUtlVector<Activity> s_TerrorActivityInverse;
	const char *const s_TerrorActivityNames[] =
	{
#define TERROR_ACTIVITY( name ) #name,
#include "l4d2/terror_activity_names.inc"
#undef TERROR_ACTIVITY
	};
	Activity s_NativeActivityByOriginal[ARRAYSIZE( s_TerrorActivityNames )];
	CUtlVector<int> s_OriginalActivityByNative;
	const TerrorActivityInverseName_t s_TerrorActivityInverseNames[] =
	{
#define TERROR_ACTIVITY_INVERSE( input, output ) { input, output },
#include "l4d2/terror_activity_inverse.inc"
#undef TERROR_ACTIVITY_INVERSE
	};
}

void RegisterTerrorActivities()
{
	for ( int i = 0; i < ARRAYSIZE( s_TerrorActivityNames ); ++i )
	{
		if ( ActivityList_IndexForName( s_TerrorActivityNames[i] ) == ACT_INVALID )
			ActivityList_RegisterPrivateActivity( s_TerrorActivityNames[i] );
	}

	s_TerrorActivityInverse.SetCount( ActivityList_HighestIndex() + 1 );
	s_OriginalActivityByNative.SetCount( s_TerrorActivityInverse.Count() );
	for ( int i = 0; i < s_OriginalActivityByNative.Count(); ++i )
		s_OriginalActivityByNative[i] = -1;
	COMPILE_TIME_ASSERT( ARRAYSIZE( s_TerrorActivityNames ) == 1937 );
	COMPILE_TIME_ASSERT( ARRAYSIZE( s_TerrorActivityNames ) == NUM_TERROR_ORIGINAL_ACTIVITIES );
	COMPILE_TIME_ASSERT( ARRAYSIZE( s_TerrorActivityNames ) - 1 <= 2047 );
	for ( int original = 0; original < ARRAYSIZE( s_TerrorActivityNames ); ++original )
	{
		const int native = ActivityList_IndexForName( s_TerrorActivityNames[original] );
		s_NativeActivityByOriginal[original] = (Activity)native;
		s_OriginalActivityByNative[native] = original;
	}
	for ( int i = 0; i < s_TerrorActivityInverse.Count(); ++i )
		s_TerrorActivityInverse[i] = ACT_IDLE;
	for ( int i = 0; i < ARRAYSIZE( s_TerrorActivityInverseNames ); ++i )
	{
		const int input = ActivityList_IndexForName( s_TerrorActivityInverseNames[i].input );
		const Activity output = (Activity)ActivityList_IndexForName( s_TerrorActivityInverseNames[i].output );
		s_TerrorActivityInverse[input] = output;
	}
}

void FreeTerrorActivities()
{
	s_TerrorActivityInverse.Purge();
	s_OriginalActivityByNative.Purge();
}

Activity UntranslateTerrorActivity( Activity activity )
{
	// S:0x33BC30 / C:0x27DE60 distinguish INVALID from the default IDLE.
	if ( activity == ACT_INVALID )
		return ACT_INVALID;
	return (unsigned)activity < (unsigned)s_TerrorActivityInverse.Count() ?
		s_TerrorActivityInverse[activity] : ACT_IDLE;
}

TerrorOriginalActivity LookupTerrorOriginalActivity( Activity activity )
{
	if ( activity == ACT_INVALID )
		return TERROR_ORIGINAL_ACT_INVALID;
	if ( (unsigned)activity < (unsigned)s_OriginalActivityByNative.Count() &&
		 s_OriginalActivityByNative[activity] >= 0 )
		return static_cast< TerrorOriginalActivity >( s_OriginalActivityByNative[activity] );
	return TERROR_ORIGINAL_UNKNOWN_ACTIVITY;
}

int TerrorActivityToOriginal( Activity activity )
{
	// An unrecognized activity is not a fatal network/animation error.
	// Keep its unknown sentinel so the normal gesture lookup rejects it.
	return LookupTerrorOriginalActivity( activity );
}

Activity TerrorActivityFromOriginal( int activity )
{
	if ( activity == -1 )
		return ACT_INVALID;
	if ( (unsigned)activity < ARRAYSIZE( s_NativeActivityByOriginal ) )
		return s_NativeActivityByOriginal[activity];
	// Native C147C50 leaves unmatched gestures to its diagnostic/skip path.
	return ACT_INVALID;
}

const char *GetZombieClassName( int zombieClass )
{
	static const char *const names[] =
	{
		"NORMAL", "SMOKER", "BOOMER", "HUNTER", "SPITTER", "JOCKEY", "CHARGER", "WITCH", "TANK"
	};
	return zombieClass >= 0 && zombieClass < ARRAYSIZE( names ) ? names[zombieClass] : names[0];
}

// S350DE0: this is the display name, not the response-system character key.
int GetTerrorSurvivorDisplayCharacter( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( character )
		{
		case 0: return 4;
		case 1: return 5;
		case 2: return 7;
		case 3: return 6;
		}
	}
	return character;
}

const char *GetSurvivorCharacterDisplayName( int character )
{
	character = GetTerrorSurvivorDisplayCharacter( character );
	switch ( character )
	{
	case 0: return "Nick";
	case 1: return "Rochelle";
	case 2: return "Coach";
	case 3: return "Ellis";
	case 4: return "Bill";
	case 5: return "Zoey";
	case 6: return "Francis";
	case 7: return "Louis";
	default: return "Survivor";
	}
}
// Raw character domain, NOT display names or MapTerrorSurvivorCharacter's INVERSE
// mapping. int input retains native Unknown behavior outside the named enum.
const char *GetSurvivorCharacterName( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( character )
		{
		case 0: character = 4; break;
		case 1: character = 5; break;
		case 2: character = 7; break;
		case 3: character = 6; break;
		}
	}
	switch ( character )
	{
	case 0: return "Gambler";
	case 1: return "Producer";
	case 2: return "Coach";
	case 3: return "Mechanic";
	case 4: return "NamVet";
	case 5: return "TeenGirl";
	case 6: return "Biker";
	case 7: return "Manager";
	default: return "Unknown";
	}
}

// S350D40/C2864E0: inverse display-domain remap using the actual cached set.
int MapTerrorSurvivorCharacter( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( character )
		{
		case 4: return 0;
		case 5: return 1;
		case 6: return 3;
		case 7: return 2;
		}
	}
	return character;
}

// S3510F0/C2867C0. One shared owner for mounted weapons and Jockey.
float GetSurvivorCharacterScale( SurvivorCharacterType character )
{
	int actualCharacter = static_cast< int >( character );
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( actualCharacter )
		{
		case 0: actualCharacter = 4; break;
		case 1: actualCharacter = 5; break;
		case 2: actualCharacter = 7; break;
		case 3: actualCharacter = 6; break;
		}
	}
	switch ( actualCharacter )
	{
	case 1:
	case 5: return 0.88800001f;
	case 2: return 1.05f;
	case 3: return 0.95499998f;
	default: return 1.0f;
	}
}

void TerrorDebugPrintf( FileHandle_t file, const char *format, ... )
{
	char buffer[4096];
	va_list marker;
	va_start( marker, format );
	Q_vsnprintf( buffer, sizeof( buffer ), format, marker );
	va_end( marker );
	// S33C180/C27E680: Main's formatted-text policy replaces Msg(buffer).
	Msg( "%s", buffer );
	if ( file )
		filesystem->FPrintf( file, "%s", buffer );
}
