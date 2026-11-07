//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "tier1/KeyValues.h"
#include "tier1/utldict.h"

#include "tier0/memdbgon.h"

namespace
{
	// The target sound-emitter enum stops at female. Keep the original game
	// identities here without changing that engine interface or its layout.
	const char *const s_pszActorType[] =
	{
		"none", "male", "female", "namvet", "teengirl", "biker", "manager",
		"gambler", "producer", "coach", "mechanic", "ceda", "crawler",
		"undistractable", "fallen", "riot_control", "clown", "jimmy",
		"hospital_patient", "bride", "police", "male_l4d1", "female_l4d1"
	};

	class CInfectedActorTypes : public CAutoGameSystem
	{
	public:
		CInfectedActorTypes() : CAutoGameSystem( "InfectedActorTypes" ), m_bAttemptedLoad( false ) {}

		virtual void LevelInitPreEntity() OVERRIDE
		{
			Load();
		}

		virtual void LevelShutdownPostEntity() OVERRIDE
		{
			m_ActorTypes.Purge();
			m_bAttemptedLoad = false;
		}

		int ForModel( const char *pszModel )
		{
			Load();
			if ( !pszModel || !pszModel[0] )
				return 0;

			// Retail GetActorGender (soundemittersystem 0x71C0 -> 0x9840):
			// last directory component, remove the final extension, max255 bytes.
			const char *pszBase = pszModel;
			for ( const char *p = pszModel; *p; ++p )
			{
				if ( *p == '/' || *p == '\\' )
					pszBase = p + 1;
			}
			const char *pszEnd = pszBase + Q_strlen( pszBase );
			for ( const char *p = pszEnd; p > pszBase; )
			{
				if ( *--p == '.' )
				{
					pszEnd = p;
					break;
				}
			}
			char szActor[256];
			int nLength = MIN( (int)( pszEnd - pszBase ), (int)sizeof( szActor ) - 1 );
			Q_memcpy( szActor, pszBase, nLength );
			szActor[nLength] = 0;
			int i = m_ActorTypes.Find( szActor );
			return i == m_ActorTypes.InvalidIndex() ? 0 : m_ActorTypes[i];
		}

	private:
		void Load()
		{
			if ( m_bAttemptedLoad )
				return;
			m_bAttemptedLoad = true;
			KeyValues *pRoot = new KeyValues( "globalactors" );
			if ( !pRoot->LoadFromFile( filesystem, "scripts/global_actors.txt", "GAME" ) )
			{
				Warning( "Unable to load scripts/global_actors.txt\n" );
				pRoot->deleteThis();
				return;
			}
			for ( KeyValues *pActor = pRoot->GetFirstSubKey(); pActor; pActor = pActor->GetNextKey() )
			{
				int nType = 0;
				for ( int i = 1; i < ARRAYSIZE( s_pszActorType ); ++i )
				{
					if ( !Q_stricmp( pActor->GetString(), s_pszActorType[i] ) )
					{
						nType = i;
						break;
					}
				}
				int i = m_ActorTypes.Find( pActor->GetName() );
				if ( i == m_ActorTypes.InvalidIndex() )
					m_ActorTypes.Insert( pActor->GetName(), nType );
				else
					m_ActorTypes[i] = nType;
			}
			pRoot->deleteThis();
		}

		CUtlDict<int> m_ActorTypes;
		bool m_bAttemptedLoad;
	};

	CInfectedActorTypes s_InfectedActorTypes;
}

int InfectedModelTypeForModel( const char *pszModelName )
{
	return s_InfectedActorTypes.ForModel( pszModelName );
}
