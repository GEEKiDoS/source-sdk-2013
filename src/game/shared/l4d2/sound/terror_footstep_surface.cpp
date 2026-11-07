//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_footstep_surface.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "physics.h"
#include "tier1/KeyValues.h"
#include "tier1/utlsymbol.h"
#include "tier0/memdbgon.h"

struct InfectedFootstepSounds_t
{
	CUtlSymbol m_Sound[4];
};

static const char *const s_pszInfectedFootstepKeys[] =
{
	"walkleft", "walkright", "runleft", "runright"
};

// Retail surface data has four footstep strings (client 0xEEE60, +44..+50).
// The target vphysics ABI has only stepleft/stepright. Read just these extra
// game-owned fields from the same mounted manifest, without changing that ABI,
// replacing the original files, or substituting walking sounds for running.
class CInfectedFootstepSurfaces : public CAutoGameSystem
{
public:
	CInfectedFootstepSurfaces() : CAutoGameSystem( "InfectedFootstepSurfaces" ), m_bLoaded( false )
	{
	}

	virtual void LevelInitPreEntity() OVERRIDE
	{
		// PhysicsDLLInit has parsed the native surface properties by this point.
		// Their indices and mounted manifest live for this game module's lifetime.
		if ( !m_bLoaded )
			m_bLoaded = Load();
	}

	virtual void Shutdown() OVERRIDE
	{
		m_SurfaceSounds.Purge();
		m_Defined.Purge();
		m_SoundNames.RemoveAll();
		m_bLoaded = false;
	}

	const char *GetSound( int nSurface, bool bLeftFoot, bool bRunning ) const
	{
		if ( !m_bLoaded || !m_SurfaceSounds.IsValidIndex( nSurface ) )
			return NULL;

		int nSound = ( bRunning ? 2 : 0 ) + ( bLeftFoot ? 0 : 1 );
		CUtlSymbol sound = m_SurfaceSounds[nSurface].m_Sound[nSound];
		return sound.IsValid() ? m_SoundNames.String( sound ) : NULL;
	}

private:
	bool LoadFile( const char *pszFile )
	{
		KeyValues::AutoDelete file( new KeyValues( pszFile ) );
		if ( !file->LoadFromFile( filesystem, pszFile, "GAME" ) )
		{
			Error( "Unable to load infected footstep surface file '%s'\n", pszFile );
			return false;
		}

		// Surface files contain peer roots, not a single wrapping section.
		for ( KeyValues *pSurface = file; pSurface; pSurface = pSurface->GetNextKey() )
		{
			int nSurface = physprops->GetSurfaceIndex( pSurface->GetName() );
			if ( !m_SurfaceSounds.IsValidIndex( nSurface ) )
				continue; // No physics surface owns these optional sound fields.

			// The mounted manifest specifies that later definitions override
			// earlier ones (notably snow in surfaceproperties_patch.txt).
			InfectedFootstepSounds_t sounds;
			int nDefault = physprops->GetSurfaceIndex( "default" );
			if ( m_Defined.IsValidIndex( nDefault ) && m_Defined[nDefault] )
				sounds = m_SurfaceSounds[nDefault];

			// Process base and overrides in file order, just as the surface parser
			// does. Only the four footstep fields belong to this extension.
			for ( KeyValues *pValue = pSurface->GetFirstSubKey(); pValue; pValue = pValue->GetNextKey() )
			{
				if ( !Q_stricmp( pValue->GetName(), "base" ) )
				{
					int nBase = physprops->GetSurfaceIndex( pValue->GetString() );
					// Native vphysics 92FF0 ignores unresolved/forward bases and
					// retains fields already read before continuing this surface.
					if ( !m_Defined.IsValidIndex( nBase ) || !m_Defined[nBase] )
						continue;
					sounds = m_SurfaceSounds[nBase];
					continue;
				}

				for ( int i = 0; i < ARRAYSIZE( s_pszInfectedFootstepKeys ); ++i )
				{
					if ( !Q_stricmp( pValue->GetName(), s_pszInfectedFootstepKeys[i] ) )
					{
						const char *pszSound = pValue->GetString();
						sounds.m_Sound[i] = pszSound[0] ? m_SoundNames.AddString( pszSound ) : CUtlSymbol();
						break;
					}
				}
			}

			m_SurfaceSounds[nSurface] = sounds;
			m_Defined[nSurface] = true;
		}
		return true;
	}

	bool Load()
	{
		m_SurfaceSounds.SetCount( physprops->SurfacePropCount() );
		m_Defined.SetCount( m_SurfaceSounds.Count() );
		for ( int i = 0; i < m_Defined.Count(); ++i )
			m_Defined[i] = false;

		const char *pszManifest = "scripts/surfaceproperties_manifest.txt";
		KeyValues::AutoDelete manifest( new KeyValues( pszManifest ) );
		if ( !manifest->LoadFromFile( filesystem, pszManifest, "GAME" ) )
		{
			Error( "Unable to load infected footstep surface manifest '%s'\n", pszManifest );
			return false;
		}
		for ( KeyValues *pFile = manifest->GetFirstSubKey(); pFile; pFile = pFile->GetNextKey() )
		{
			if ( !Q_stricmp( pFile->GetName(), "file" ) && !LoadFile( pFile->GetString() ) )
				return false;
		}
		return true;
	}

	CUtlVector<InfectedFootstepSounds_t> m_SurfaceSounds;
	CUtlVector<bool> m_Defined;
	CUtlSymbolTable m_SoundNames;
	bool m_bLoaded;
};

static CInfectedFootstepSurfaces s_InfectedFootstepSurfaces;

const char *TerrorFootstepSurfaceSound( int surfaceIndex, bool left, bool running )
{
	return s_InfectedFootstepSurfaces.GetSound( surfaceIndex, left, running );
}
