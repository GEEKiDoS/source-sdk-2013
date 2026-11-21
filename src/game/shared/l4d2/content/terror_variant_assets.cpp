//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/content/terror_variant_assets.h"
#include "l4d2/weapons/weapon_variants.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "filesystem.h"
#include "studio.h"
#include "optimize.h"
#include "eventlist.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "soundchars.h"
#include "tier1/KeyValues.h"
#include "tier1/checksum_sha1.h"
#include "tier1/fmtstr.h"
#include "tier1/refcount.h"
#include "tier1/smartptr.h"
#include "tier1/utlbuffer.h"
#include "tier1/utldict.h"

#if defined( CLIENT_DLL )
#if defined( _WIN32 )
#include "winlite.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#endif

#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;

namespace TerrorVariantAssets
{
namespace
{
using Bytes = CUtlVector<byte>;
using Names = CUtlDict<bool>;
using Paths = CUtlVector<CUtlString>;

class KV
{
public:
	explicit KV( const char *name ) : m_pValue( new KeyValues( name ) ) {}
	~KV() { Reset( NULL ); }
	KeyValues *Get() const { return m_pValue; }
	KeyValues *operator->() const { return m_pValue; }
	KeyValues *Detach() { KeyValues *value = m_pValue; m_pValue = NULL; return value; }
	void Reset( KeyValues *value )
	{
		if ( m_pValue )
			m_pValue->deleteThis();
		m_pValue = value;
	}
private:
	KV( const KV & );
	KV &operator=( const KV & );
	KeyValues *m_pValue;
};

const char CACHE_DIR[] = "weapon_variant_cache";
const int CACHE_VERSION = 2;
const size_t MAX_ASSET_SIZE = 128 * 1024 * 1024;
const size_t MAX_TEXT_SIZE = 4 * 1024 * 1024;

bool StartsWith( const CUtlString &text, const char *prefix )
{
	return V_strncmp( text.String(), prefix, V_strlen( prefix ) ) == 0;
}

bool EndsWith( const CUtlString &text, const char *suffix )
{
	const int len = V_strlen( suffix );
	const int textLen = text.Length();
	return textLen >= len && !V_strcmp( text.String() + textLen - len, suffix );
}

CUtlString Join( const CUtlString &root, const CUtlString &relative )
{
	if ( root.IsEmpty() || relative.IsEmpty() )
		return root.IsEmpty() ? relative : root;
	return root[root.Length() - 1] == '/' ? root + relative : root + "/" + relative;
}

CUtlString Slashes( CUtlString path )
{
	path.FixSlashes( '/' );
	path.TrimRight( '/' );
	return path;
}

CUtlString Lower( CUtlString text )
{
	if ( !text.IsEmpty() )
		for ( char *c = text.Access(); *c; ++c )
			if ( *c >= 'A' && *c <= 'Z' )
				*c += 'a' - 'A';
	return text;
}

// No add-on-controlled path can escape a source or the cache. Empty paths are
// only meaningful to callers that explicitly accept the root material folder.
bool Normalize( const char *input, CUtlString &relative )
{
	if ( !input || V_strlen( input ) >= MAX_PATH )
		return false;
	relative = Lower( Slashes( input ) );
	if ( relative.IsEmpty() || relative[0] == '/' )
		return false;
	for ( const char *p = relative.String(); *p; )
	{
		const char *end = strchr( p, '/' );
		if ( !end )
			end = p + V_strlen( p );
		if ( end == p || end[-1] == '.' || end[-1] == ' ' )
			return false;
		for ( const char *c = p; c != end; ++c )
			if ( static_cast<unsigned char>( *c ) < 32 || strchr( ":*?\"<>|", *c ) )
				return false;
		p = *end ? end + 1 : end;
	}
	return true;
}

void SearchPaths( const char *pathID, bool packs, Paths &paths )
{
	const int length = filesystem->GetSearchPath( pathID, packs, NULL, 0 );
	if ( length <= 0 )
		return;
	CUtlVector<char> buffer;
	buffer.SetCount( length + 1 );
	filesystem->GetSearchPath( pathID, packs, buffer.Base(), buffer.Count() );
	buffer[length] = 0;
	for ( const char *p = buffer.Base(); *p; )
	{
		const char *next = strchr( p, ';' );
		const CUtlString value = Slashes( next ? CUtlString( p, int( next - p ) ) : CUtlString( p ) );
		if ( !value.IsEmpty() )
			paths.AddToTail( value );
		if ( !next )
			break;
		p = next + 1;
	}
}

class Blob : public CRefCounted<>, public Bytes
{
};

struct File
{
	File( Blob *bytes = NULL, long modified = 0 ) : data( bytes ), mtime( modified ) {}
	File( const File &other ) : data( other.data ), mtime( other.mtime ) { SafeAddRef( data ); }
	File &operator=( const File &other )
	{
		SafeAssign( &data, other.data );
		mtime = other.mtime;
		return *this;
	}
	~File() { SafeRelease( &data ); }
	Blob *data;
	long mtime;
};

class Sources
{
public:
	explicit Sources( const Paths &paths ) : m_bExact( false ) { m_Paths = paths; }
	explicit Sources( const char *path ) : m_bExact( true ), m_ExactRoot( path )
	{
		filesystem->AddSearchPath( path, "WVProbe", PATH_ADD_TO_TAIL );
	}
	~Sources()
	{
		if ( m_bExact )
			filesystem->RemoveSearchPath( m_ExactRoot, "WVProbe" );
	}
	bool IsOwned( const CUtlString &, const CUtlString &relative )
	{
		return m_bExact && filesystem->FileExists( relative, "WVProbe" );
	}
	File Read( const CUtlString &relative )
	{
		const int found = m_Files.Find( relative );
		if ( m_Files.IsValidIndex( found ) )
		{
			Location &entry = m_Files[found];
			if ( entry.readFailed )
				return File();
			if ( entry.file.data || entry.path.IsEmpty() )
				return entry.file;
			return ReadAbsolute( entry.path, entry, m_bExact ? "WVProbe" : NULL );
		}
		Location &entry = m_Files[m_Files.Insert( relative )];
		if ( m_bExact )
		{
			if ( !IsOwned( m_ExactRoot, relative ) )
				return File();
			return ReadAbsolute( relative, entry, "WVProbe" );
		}
		for ( const CUtlString &root : m_Paths )
		{
			CUtlString path = Join( root, relative );
			if ( path.Length() >= MAX_PATH )
				continue;
			path.FixSlashes();
			File file = ReadAbsolute( path, entry, NULL );
			if ( file.data )
				return file;
			if ( entry.readFailed )
				break;
		}
		return File();
	}

	// Resolve the owning VTF source without inflating the file until we know
	// the model belongs to a replacement.
	bool Contains( const CUtlString &relative )
	{
		const int found = m_Files.Find( relative );
		if ( m_Files.IsValidIndex( found ) )
			return !m_Files[found].path.IsEmpty();
		Location &entry = m_Files[m_Files.Insert( relative )];
		if ( m_bExact )
		{
			if ( !IsOwned( m_ExactRoot, relative ) )
				return false;
			entry.path = relative;
			entry.file.mtime = filesystem->GetFileTime( relative, "WVProbe" );
			return true;
		}
		for ( const CUtlString &root : m_Paths )
		{
			CUtlString path = Join( root, relative );
			if ( path.Length() >= MAX_PATH )
				continue;
			path.FixSlashes();
			FileHandle_t handle = filesystem->Open( path, "rb", NULL );
			if ( handle == FILESYSTEM_INVALID_HANDLE )
				continue;
			filesystem->Close( handle );
			entry.path = path;
			entry.file.mtime = filesystem->GetFileTime( path, NULL );
			return true;
		}
		return false;
	}

private:
	struct Location
	{
		CUtlString path;
		File file;
		bool readFailed = false;
	};

	File ReadAbsolute( const CUtlString &path, Location &entry, const char *pathID )
	{
		FileHandle_t handle = filesystem->Open( path, "rb", pathID );
		if ( handle == FILESYSTEM_INVALID_HANDLE )
			return File();
		const bool located = !entry.path.IsEmpty();
		if ( !located )
			entry.path = path;
		const size_t size = filesystem->Size( handle );
		File file;
		if ( size && size <= MAX_ASSET_SIZE )
		{
			file.data = new Blob;
			file.data->SetCount( int( size ) );
			const int count = filesystem->Read( file.data->Base(), int( size ), handle );
			if ( count != int( size ) )
			{
				DevMsg( "Weapon variants: unreadable %s (%d/%u bytes)\n", path.String(), count, unsigned( size ) );
				SafeRelease( &file.data );
			}
		}
		filesystem->Close( handle );
		if ( !located )
			entry.file.mtime = filesystem->GetFileTime( path, pathID );
		if ( !file.data )
		{
			entry.readFailed = true;
			return file;
		}
		file.mtime = entry.file.mtime;
		// Keep repeatedly read catalog files, not all vertex and texture data.
		if ( size <= 512 * 1024 && m_nCachedBytes + size <= 16 * 1024 * 1024 )
		{
			entry.file = file;
			m_nCachedBytes += size;
		}
		return file;
	}

	Paths m_Paths;
	bool m_bExact;
	CUtlString m_ExactRoot;
	CUtlDict<Location> m_Files;
	size_t m_nCachedBytes = 0;
};


bool ParseKV( KeyValues *kv, const CUtlString &path, const File &file, bool escape = false )
{
	if ( !file.data || size_t( file.data->Count() ) > MAX_TEXT_SIZE )
		return false;
	CUtlString text( reinterpret_cast<const char *>( file.data->Base() ), file.data->Count() );
	// Authored VMTs use /* ... */ around disabled proxy blocks. SDK
	// KeyValues skips // comments only. Preserve lines and quoted values.
	if ( EndsWith( path, ".vmt" ) && strstr( text.String(), "/*" ) )
	{
		bool quoted = false;
		for ( char *p = text.Access(); *p; ++p )
		{
			if ( *p == '"' )
				quoted = !quoted;
			else if ( !quoted && p[0] == '/' && p[1] == '/' )
				p += strcspn( p, "\r\n" ) - 1;
			else if ( !quoted && p[0] == '/' && p[1] == '*' )
			{
				char *end = strstr( p + 2, "*/" );
				if ( !end )
					return false;
				for ( ; p != end + 2; ++p )
					if ( *p != '\n' && *p != '\r' )
						*p = ' ';
				--p;
			}
		}
	}
	kv->UsesEscapeSequences( escape );
	return kv->LoadFromBuffer( path, text );
}

struct Weapon
{
	CUtlString name;
	CUtlVector<Model> models;
	Names sounds;
};

void AddRole( Weapon &weapon, const char *role, const char *value )
{
	CUtlString path;
	if ( !Normalize( value, path ) || !StartsWith( path, "models/" ) || !EndsWith( path, ".mdl" ) )
		return;
	for ( const Model &model : weapon.models )
		if ( model.role == role && model.vanilla == path )
			return;
	weapon.models.AddToTail( Model{ role, path, {} } );
}

void ReadCatalog( Sources &vanilla, const char *manifestPath, bool melee, CUtlVector<Weapon> &weapons )
{
	KV manifest( melee ? "meleeweaponinfo" : "weaponscripts" );
	ParseKV( manifest.Get(), manifestPath, vanilla.Read( manifestPath ), true );
	// The retail manifest is a precache list, not a catalog (it omits
	// the AK and even comments out the M16). Include gameplay's ID table.
	Paths scripts;
	if ( !melee )
		for ( int id = 1; WeaponIDToAlias( id ); ++id )
			scripts.AddToTail( CFmtStr( "scripts/weapon_%s.txt", WeaponIDToAlias( id ) ).String() );

	for ( KeyValues *entry = manifest->GetFirstValue(); entry; entry = entry->GetNextValue() )
	{
		CUtlString path;
		if ( Q_stricmp( entry->GetName(), "file" ) || !Normalize( entry->GetString(), path ) ||
			!StartsWith( path, "scripts/" ) || !EndsWith( path, ".txt" ) )
			continue;
		if ( !scripts.HasElement( path ) )
			scripts.AddToTail( path );
	}
	for ( const CUtlString &path : scripts )
	{
		KV script( melee ? "meleeweapon" : "WeaponData" );
		if ( !ParseKV( script.Get(), path, vanilla.Read( path ), !melee ) )
			continue;

		// Use the same manifest and FileWeaponInfo/CMeleeWeaponInfo fields as
		// gameplay, but don't populate the live database with mounted scripts.
		Weapon &weapon = *weapons.AddToTailGetPtr();
		const char *basename = V_UnqualifiedFileName( path );
		weapon.name = CUtlString( melee ? "melee:" : "" ) + CUtlString( basename, V_strlen( basename ) - 4 );
		AddRole( weapon, WEAPON_VARIANT_ROLE_VIEW, script->GetString( "viewmodel" ) );
		const char *player = script->GetString( "playermodel" );
		const char *world = melee ? player : script->GetString( "worldmodel", player );
		if ( weapon.name == "weapon_defibrillator" )
			world = "models/w_models/weapons/w_eq_defibrillator.mdl";
		AddRole( weapon, WEAPON_VARIANT_ROLE_WORLD, world );
		AddRole( weapon, WEAPON_VARIANT_ROLE_PLAYER, world );
		if ( weapon.name != "weapon_defibrillator" )
			AddRole( weapon, WEAPON_VARIANT_ROLE_WORLD, player );
		if ( !melee )
		{
			AddRole( weapon, WEAPON_VARIANT_ROLE_VIEW, script->GetString( "viewmodel_dual" ) );
			AddRole( weapon, WEAPON_VARIANT_ROLE_PLAYER, player );
			AddRole( weapon, WEAPON_VARIANT_ROLE_PLAYER, script->GetString( "playermodel_dual" ) );
		}
		if ( weapon.name == "weapon_defibrillator" )
		{
			AddRole( weapon, WEAPON_VARIANT_ROLE_WORLD, "models/w_models/weapons/w_eq_defibrillator_no_paddles.mdl" );
			AddRole( weapon, WEAPON_VARIANT_ROLE_PLAYER, "models/w_models/weapons/w_eq_defibrillator_paddles.mdl" );
		}
		KeyValues *sounds = script->FindKey( "SoundData" );
		for ( KeyValues *sound = sounds ? sounds->GetFirstValue() : NULL; sound; sound = sound->GetNextValue() )
		{
			const CUtlString name = Lower( sound->GetString() );
			if ( !name.IsEmpty() && !weapon.sounds.HasElement( name ) )
				weapon.sounds.Insert( name, true );
		}
		if ( weapon.models.IsEmpty() )
			weapons.RemoveMultipleFromTail( 1 );
		else
			weapon.models.Sort( []( const Model *a, const Model *b )
			{
				const int role = Q_strcmp( a->role, b->role );
				return role ? role : Q_strcmp( a->vanilla, b->vanilla );
			} );
	}
}

bool Range( size_t size, int64 offset, int64 count, size_t stride )
{
	return count >= 0 && offset >= 0 && uint64( offset ) <= size && uint64( count ) <= ( size - size_t( offset ) ) / stride;
}

bool Table( size_t size, int64 offset, int count, size_t stride )
{
	return count == 0 || ( offset >= int( sizeof( studiohdr_t ) ) && Range( size, offset, count, stride ) );
}

int ReadInt( const Bytes &data, size_t offset )
{
	int value;
	memcpy( &value, data.Base() + offset, sizeof( value ) );
	return value;
}

#ifdef CLIENT_DLL
void SetInt( Bytes &data, size_t offset, int value )
{
	memcpy( data.Base() + offset, &value, sizeof( value ) );
}
#endif

const char *StringAt( const Bytes &data, int64 offset )
{
	if ( !Range( data.Count(), offset, 1, 1 ) )
		return NULL;
	const char *p = reinterpret_cast<const char *>( data.Base() + offset );
	return memchr( p, 0, data.Count() - size_t( offset ) ) ? p : NULL;
}

struct Studio
{
	int checksum;
	int hdr2;
	int textureCount;
	bool includes;
	CUtlString ani;
	size_t aniLength = 0;
	Paths directories;
	Paths textures;
	CUtlVector<size_t> unterminatedOptions;
	CUtlMap<size_t, CUtlString, int> soundEvents{ DefLessFunc( size_t ) };
};

bool ReadStudio( const Bytes &data, Studio &studio )
{
	const size_t size = data.Count();
	if ( !Range( size, 0, 1, sizeof( studiohdr_t ) ) )
		return false;
	studiohdr_t hdr;
	memcpy( &hdr, data.Base(), sizeof( hdr ) );
	if ( memcmp( data.Base(), "IDST", 4 ) || hdr.version < 44 || hdr.version > 49 || hdr.length != int( size ) ||
		hdr.numcdtextures > 64 || hdr.numtextures > 4096 ||
		!memchr( hdr.name, 0, sizeof( hdr.name ) ) )
		return false;

	// Validate the top-level disk tables before following any relative offset.
	if ( !Table( size, hdr.boneindex, hdr.numbones, sizeof( mstudiobone_t ) ) ||
		 !Table( size, hdr.bonecontrollerindex, hdr.numbonecontrollers, sizeof( mstudiobonecontroller_t ) ) ||
		 !Table( size, hdr.hitboxsetindex, hdr.numhitboxsets, sizeof( mstudiohitboxset_t ) ) ||
		 !Table( size, hdr.localanimindex, hdr.numlocalanim, sizeof( mstudioanimdesc_t ) ) ||
		 !Table( size, hdr.localseqindex, hdr.numlocalseq, sizeof( mstudioseqdesc_t ) ) ||
		 !Table( size, hdr.textureindex, hdr.numtextures, sizeof( mstudiotexture_t ) ) ||
		 !Table( size, hdr.cdtextureindex, hdr.numcdtextures, sizeof( int ) ) ||
		 !Table( size, hdr.bodypartindex, hdr.numbodyparts, sizeof( mstudiobodyparts_t ) ) ||
		 !Table( size, hdr.localattachmentindex, hdr.numlocalattachments, sizeof( mstudioattachment_t ) ) ||
		 !Table( size, hdr.localnodenameindex, hdr.numlocalnodes, sizeof( int ) ) ||
		 !Table( size, hdr.localnodeindex, hdr.numlocalnodes, size_t( MAX( 1, hdr.numlocalnodes ) ) ) ||
		 !Table( size, hdr.flexdescindex, hdr.numflexdesc, sizeof( mstudioflexdesc_t ) ) ||
		 !Table( size, hdr.flexcontrollerindex, hdr.numflexcontrollers, sizeof( mstudioflexcontroller_t ) ) ||
		 !Table( size, hdr.flexruleindex, hdr.numflexrules, sizeof( mstudioflexrule_t ) ) ||
		 !Table( size, hdr.ikchainindex, hdr.numikchains, sizeof( mstudioikchain_t ) ) ||
		 !Table( size, hdr.mouthindex, hdr.nummouths, sizeof( mstudiomouth_t ) ) ||
		 !Table( size, hdr.localposeparamindex, hdr.numlocalposeparameters, sizeof( mstudioposeparamdesc_t ) ) ||
		 !Table( size, hdr.keyvalueindex, hdr.keyvaluesize, 1 ) ||
		 !Table( size, hdr.localikautoplaylockindex, hdr.numlocalikautoplaylocks, sizeof( mstudioiklock_t ) ) ||
		 !Table( size, hdr.animblockindex, hdr.numanimblocks, sizeof( mstudioanimblock_t ) ) ||
		 !Table( size, hdr.bonetablebynameindex, hdr.numbones, 1 ) ||
		 !Table( size, hdr.flexcontrolleruiindex, hdr.numflexcontrollerui, sizeof( mstudioflexcontrollerui_t ) ) ||
		 hdr.numskinref < 0 || !Table( size, hdr.skinindex, hdr.numskinfamilies, sizeof( short ) * MAX( 1, hdr.numskinref ) ) ||
		 ( hdr.surfacepropindex && !StringAt( data, hdr.surfacepropindex ) ) )
		return false;
	studio.includes = hdr.numincludemodels != 0;

	studio.checksum = hdr.checksum;
	studio.hdr2 = hdr.studiohdr2index;
	if ( studio.hdr2 )
	{
		if ( !Table( size, studio.hdr2, 1, sizeof( studiohdr2_t ) ) )
			return false;
		const int name = ReadInt( data, studio.hdr2 + offsetof( studiohdr2_t, sznameindex ) );
		if ( name && !StringAt( data, int64( studio.hdr2 ) + name ) )
			return false;
	}
	for ( int i = 0; i < hdr.numbodyparts; ++i )
	{
		const size_t base = hdr.bodypartindex + i * sizeof( mstudiobodyparts_t );
		mstudiobodyparts_t body;
		memcpy( &body, data.Base() + base, sizeof( body ) );
		const int64 models = int64( base ) + body.modelindex;
		if ( !StringAt( data, int64( base ) + body.sznameindex ) ||
			!Table( size, models, body.nummodels, sizeof( mstudiomodel_t ) ) )
			return false;
		for ( int j = 0; j < body.nummodels; ++j )
		{
			const size_t offset = size_t( models ) + j * sizeof( mstudiomodel_t );
			mstudiomodel_t model;
			memcpy( &model, data.Base() + offset, sizeof( model ) );
			if ( !memchr( model.name, 0, sizeof( model.name ) ) ||
				!Table( size, int64( offset ) + model.meshindex, model.nummeshes, sizeof( mstudiomesh_t ) ) ||
				!Table( size, int64( offset ) + model.eyeballindex, model.numeyeballs, sizeof( mstudioeyeball_t ) ) )
				return false;
		}
	}

	if ( hdr.numanimblocks > 1 )
	{
		const char *name = StringAt( data, hdr.szanimblocknameindex );
		if ( !Normalize( name, studio.ani ) || !StartsWith( studio.ani, "models/" ) || !EndsWith( studio.ani, ".ani" ) )
			return false;
		for ( int i = 1; i < hdr.numanimblocks; ++i )
		{
			const size_t offset = hdr.animblockindex + sizeof( mstudioanimblock_t ) * i;
			mstudioanimblock_t block;
			memcpy( &block, data.Base() + offset, sizeof( block ) );
			if ( block.datastart < 0 || block.dataend < block.datastart )
				return false;
			studio.aniLength = MAX( studio.aniLength, size_t( block.dataend ) );
		}
	}
	for ( int i = 0; i < hdr.numcdtextures; ++i )
	{
		const char *name = StringAt( data, ReadInt( data, hdr.cdtextureindex + i * sizeof( int ) ) );
		if ( !name )
			return false;
		// StudioMDL may emit an empty CD path for a root-relative texture.
		if ( *name == '\\' || *name == '/' )
			++name;
		CUtlString dir;
		if ( *name && !Normalize( name, dir ) )
			return false;
		studio.directories.AddToTail( dir );
	}
	studio.textureCount = hdr.numtextures;
	if ( studio.directories.IsEmpty() )
		studio.directories.AddToTail();
	for ( int i = 0; i < hdr.numtextures; ++i )
	{
		const size_t offset = hdr.textureindex + i * sizeof( mstudiotexture_t );
		const char *text = StringAt( data, int64( offset ) + ReadInt( data, offset ) );
		if ( text && ( *text == '\\' || *text == '/' ) )
			++text;
		CUtlString name;
		if ( !Normalize( text, name ) )
			return false;
		studio.textures.AddToTail( name );
	}

	for ( int i = 0; i < hdr.numlocalseq; ++i )
	{
		const size_t base = hdr.localseqindex + i * sizeof( mstudioseqdesc_t );
		mstudioseqdesc_t seq;
		memcpy( &seq, data.Base() + base, sizeof( seq ) );
		const int64 events = int64( base ) + seq.eventindex;
		if ( seq.numevents && ( seq.eventindex < int( sizeof( seq ) ) || !Table( size, events, seq.numevents, sizeof( mstudioevent_t ) ) ) )
			return false;
		for ( int j = 0; j < seq.numevents; ++j )
		{
			const size_t offset = size_t( events ) + j * sizeof( mstudioevent_t );
			mstudioevent_t event;
			memcpy( &event, data.Base() + offset, sizeof( event ) );
			// Some compilers fill all 64 bytes (e.g. long particle options).
			// Bound our read and terminate the relocated copy for the engine.
			if ( !memchr( event.options, 0, sizeof( event.options ) ) )
			{
				event.options[sizeof( event.options ) - 1] = 0;
				studio.unterminatedOptions.AddToTail( offset + offsetof( mstudioevent_t, options ) + sizeof( event.options ) - 1 );
			}
			const char *name = event.szeventindex ? StringAt( data, int64( offset ) + event.szeventindex ) : NULL;
			if ( event.szeventindex && !name )
				return false;
			if ( event.options[0] && ( ( name && ( !Q_stricmp( name, "AE_CL_PLAYSOUND" ) || !Q_stricmp( name, "AE_SV_PLAYSOUND" ) ) ) ||
				( !( event.type & AE_TYPE_NEWEVENTSYSTEM ) && ( event.event == 5004 || event.event == 1004 || event.event == 1008 ) ) ) )
				studio.soundEvents.InsertOrReplace( offset + offsetof( mstudioevent_t, options ), Lower( event.options ) );
		}
	}
	return true;
}

enum class Kind { Model, Material, Vanilla, Wave, SoundEntry };

struct Asset
{
	File source;
	CUtlString material;
};

struct AssetKey
{
	Kind kind;
	CUtlString path;
};

bool AssetKeyLess( const AssetKey &a, const AssetKey &b )
{
	return a.kind != b.kind ? a.kind < b.kind : V_strcmp( a.path, b.path ) < 0;
}

class AssetMap : public CUtlMap<AssetKey, Asset, int>
{
public:
	AssetMap() : CUtlMap<AssetKey, Asset, int>( AssetKeyLess ) {}
	void AddMissing( const AssetKey &key, const Asset &asset )
	{
		if ( !IsValidIndex( Find( key ) ) )
			Insert( key, asset );
	}
	void AddFrom( const AssetMap &assets )
	{
		FOR_EACH_MAP( assets, i )
			AddMissing( assets.Key( i ), assets[i] );
	}
};

CUtlString KVText( KeyValues *kv );
const char KEY_TOKEN[] = "@weapon_variant_key@";

CUtlString Relocated( Kind kind, const CUtlString &path, const CUtlString &key )
{
	switch ( kind )
	{
	case Kind::Model:    return CUtlString( "models/variants/" ) + key + "/" + ( path.String() + 7 );
	case Kind::Material: return CUtlString( "materials/variants/" ) + key + "/" + ( path.String() + 10 );
	case Kind::Vanilla:  return CUtlString( "materials/variants/vanilla/" ) + ( path.String() + 10 );
	case Kind::Wave:     return CUtlString( "sound/variants/" ) + key + "/" + ( path.String() + 6 );
	default:            return CUtlString();
	}
}

bool AddAsset( AssetMap &assets, Kind kind, const CUtlString &path, const File &source )
{
	if ( !source.data )
		return false;
	assets.AddMissing( { kind, path }, { source, {} } );
	return true;
}

bool MaterialPath( const char *value, const char *suffix, CUtlString &path )
{
	if ( !Normalize( value, path ) )
		return false;
	if ( !StartsWith( path, "materials/" ) )
		path = CUtlString( "materials/" ) + path;
	if ( !EndsWith( path, suffix ) )
		path += suffix;
	return true;
}

bool IsTextureParameter( const CUtlString &name )
{
	// KeyValues has no shader parameter type. These are the texture slots
	// authored by SDK/L4D2 model shaders, including common custom-shader slots.
	static const char *const names[] = {
		"$basetexture", "$basetexture2", "$bumpmap", "$bumpmap2", "$normalmap", "$detail", "$detail2",
		"$envmap", "$envmapmask", "$envmapmask2", "$selfillummask", "$selfillumtexture", "$phongexponenttexture",
		"$phongwarptexture", "$lightwarptexture", "$blendmodulatetexture", "$masktexture", "$texture",
		"$iris", "$corneatexture", "$dudvmap", "$flowmap", "$flow_noise_texture", "$refracttinttexture",
		"$reflecttinttexture", "$fleshinteriortexture", "$fleshinteriornoisetexture", "$fleshbordertexture1d",
		"$fleshnormaltexture", "$fleshsubsurfacetexture", "$fleshcubetexture", "$emissiveblendbasetexture",
		"$emissiveblendflowtexture", "$emissiveblendtexture", "$tintmasktexture", "$mraotexture", "$heightmap"
	};
	for ( const char *param : names )
		if ( name == param )
			return true;
	return false;
}

struct TextureRef
{
	KeyValues *value;
	Kind kind;
	CUtlString path;
};

void CollectTextures( KeyValues *shader, Sources *addon, Sources &vanilla, AssetMap &assets,
	CUtlVector<TextureRef> &refs, bool &changed, int depth = 0 )
{
	if ( depth >= 16 )
		return;
	for ( KeyValues *value = shader->GetFirstSubKey(); value; value = value->GetNextKey() )
	{
		const CUtlString name = Lower( value->GetName() );
		if ( value->GetFirstSubKey() )
		{
			if ( name == "insert" || name == "replace" || ( !name.IsEmpty() && name[0] == '<' ) )
				CollectTextures( value, addon, vanilla, assets, refs, changed, depth + 1 );
			continue;
		}
		if ( !IsTextureParameter( name ) )
			continue;
		CUtlString path;
		if ( !MaterialPath( value->GetString(), ".vtf", path ) ||
			StartsWith( path, "materials/_rt_" ) || path == "materials/env_cubemap.vtf" )
			continue;
		if ( addon && AddAsset( assets, Kind::Material, path, addon->Read( path ) ) )
		{
			refs.AddToTail( TextureRef{ value, Kind::Material, path } );
			changed = true;
		}
		else if ( vanilla.Contains( path ) )
		{
			assets.AddMissing( { Kind::Vanilla, path }, Asset{} );
			refs.AddToTail( TextureRef{ value, Kind::Vanilla, path } );
		}
	}
}

// Try the add-on material first. If only its VTFs are replaced, make a new
// VMT from vanilla, but keep shared vanilla fallback files fully isolated.
bool CollectMaterial( const CUtlString &path, Sources *addon, Sources &vanilla,
	AssetMap &assets, Names &stack, bool &changed )
{
	if ( stack.Count() >= 16 || stack.HasElement( path ) )
		return false;
	// Missing and malformed are different: an existing, invalid patch must
	// not accidentally become a successful vanilla-only candidate.
	File file = addon ? addon->Read( path ) : File{};
	const bool fromAddon = !!file.data;
	if ( !fromAddon )
	{
		if ( addon && addon->Contains( path ) )
			return false;
		file = vanilla.Read( path );
	}
	bool local = fromAddon;
	stack.Insert( path, true );
	KV kv( "" );
	bool ok = ParseKV( kv.Get(), path, file ) && kv->GetFirstSubKey() && !kv->GetNextKey() &&
		V_strcmp( kv->GetName(), "{" ) != 0;
	CUtlVector<TextureRef> refs;
	if ( ok && !Q_stricmp( kv->GetName(), "patch" ) )
	{
		CUtlString basePath;
		bool baseReplaced = false;
		KeyValues *include = kv->FindKey( "include" );
		ok = include && MaterialPath( include->GetString(), ".vmt", basePath ) &&
			CollectMaterial( basePath, addon, vanilla, assets, stack, baseReplaced );
		if ( ok )
		{
			local |= baseReplaced;
			include->SetString( (const char *)NULL, Relocated( baseReplaced ? Kind::Material : Kind::Vanilla, basePath, KEY_TOKEN ) );
		}
	}
	if ( ok )
	{
		CollectTextures( kv.Get(), addon, vanilla, assets, refs, local );
		for ( const TextureRef &ref : refs )
		{
			CUtlString relative = Relocated( ref.kind, ref.path, KEY_TOKEN );
			relative.Truncate( relative.Length() - 4 ); // Shader parameters omit materials/ and .vtf.
			ref.value->SetString( (const char *)NULL, relative.String() + 10 );
		}
		assets.AddMissing( { local ? Kind::Material : Kind::Vanilla, path }, { file, KVText( kv.Get() ) } );
		changed |= local;
	}
	stack.Remove( path );
	return ok;
}

bool CollectMaterials( const Studio &studio, Sources &addon, Sources &vanilla, AssetMap &assets, bool &changed, CUtlString &problem )
{
	for ( const CUtlString &texture : studio.textures )
	{
		// StudioRender tries all relocated directories first. Mirror that
		// decision here, with the original CD path order inside each group.
		// Preserve the empty CD entry too: stock rifle viewmodels use it to
		// resolve texture names that already contain their full directory.
		AssetMap fallback;
		for ( const CUtlString &dir : studio.directories )
		{
			CUtlString materialName = texture;
			if ( !StartsWith( texture, "models/" ) )
				materialName = Join( dir, texture );
			CUtlString path;
			if ( !MaterialPath( materialName, ".vmt", path ) )
			{
				problem = texture;
				return false;
			}
			AssetMap material;
			Names stack;
			bool local = false;
			if ( !CollectMaterial( path, &addon, vanilla, material, stack, local ) )
			{
				if ( addon.Contains( path ) )
				{
					problem = path;
					return false;
				}
				continue;
			}
			if ( local )
			{
				changed = true;
				fallback.Swap( material );
				break;
			}
			if ( !fallback.Count() )
				fallback.Swap( material );
		}
		if ( !fallback.Count() )
		{
			problem = texture;
			return false;
		}
		assets.AddFrom( fallback );
	}
	return true;
}

const char *const SIDECARS[] = { ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx", ".dx11.vtx", ".360.vtx", ".phy" };

bool ReadSidecar( const Bytes &data, size_t type, Studio &studio )
{
	const int checksum = studio.checksum;
	if ( type == 0 )
	{
		if ( size_t( data.Count() ) < sizeof( vertexFileHeader_t ) )
			return false;
		vertexFileHeader_t hdr;
		memcpy( &hdr, data.Base(), sizeof( hdr ) );
		return hdr.id == MODEL_VERTEX_FILE_ID && hdr.version == MODEL_VERTEX_FILE_VERSION &&
			hdr.checksum == checksum && hdr.numLODs > 0 && hdr.numLODs <= MAX_NUM_LODS &&
			Range( data.Count(), hdr.fixupTableStart, hdr.numFixups, sizeof( vertexFileFixup_t ) ) &&
			Range( data.Count(), hdr.vertexDataStart, hdr.numLODVertexes[0], sizeof( mstudiovertex_t ) ) &&
			( !hdr.tangentDataStart || Range( data.Count(), hdr.tangentDataStart, hdr.numLODVertexes[0], sizeof( Vector4D ) ) );
	}
	if ( type == ARRAYSIZE( SIDECARS ) - 1 )
		return data.Count() >= 16 && ReadInt( data, 0 ) == 16 && ReadInt( data, 12 ) == checksum;

	using namespace OptimizedModel;
	if ( size_t( data.Count() ) < sizeof( FileHeader_t ) )
		return false;
	FileHeader_t hdr;
	memcpy( &hdr, data.Base(), sizeof( hdr ) );
	if ( type == ARRAYSIZE( SIDECARS ) - 2 )
		return BigLong( hdr.version ) == OPTIMIZED_MODEL_FILE_VERSION && uint32( BigLong( hdr.checkSum ) ) == uint32( checksum );
	if ( hdr.version != OPTIMIZED_MODEL_FILE_VERSION || hdr.checkSum != checksum || hdr.numLODs <= 0 ||
		hdr.numLODs > MAX_NUM_LODS || !Range( data.Count(), hdr.bodyPartOffset, hdr.numBodyParts, sizeof( BodyPartHeader_t ) ) ||
		!Range( data.Count(), hdr.materialReplacementListOffset, hdr.numLODs, sizeof( MaterialReplacementListHeader_t ) ) )
		return false;
	Paths textures;
	for ( int i = 0; i < hdr.numLODs; ++i )
	{
		const size_t offset = hdr.materialReplacementListOffset + i * sizeof( MaterialReplacementListHeader_t );
		MaterialReplacementListHeader_t list;
		memcpy( &list, data.Base() + offset, sizeof( list ) );
		const int64 replacements = int64( offset ) + list.replacementOffset;
		if ( list.numReplacements && !Range( data.Count(), replacements, list.numReplacements, sizeof( MaterialReplacementHeader_t ) ) )
			return false;
		for ( int j = 0; j < list.numReplacements; ++j )
		{
			const size_t base = size_t( replacements ) + j * sizeof( MaterialReplacementHeader_t );
			MaterialReplacementHeader_t replacement;
			memcpy( &replacement, data.Base() + base, sizeof( replacement ) );
			if ( replacement.materialID < 0 || replacement.materialID >= studio.textureCount )
				return false;
			const char *text = StringAt( data, int64( base ) + replacement.replacementMaterialNameOffset );
			if ( text && ( *text == '\\' || *text == '/' ) )
				++text;
			CUtlString name;
			if ( !Normalize( text, name ) )
				return false;
			textures.AddToTail( name );
		}
	}
	for ( const CUtlString &texture : textures )
		if ( !studio.textures.HasElement( texture ) )
			studio.textures.AddToTail( texture );
	return true;
}

struct Replacement
{
	Studio studio;
	AssetMap assets;
};

bool RejectRole( const char *source, const CUtlString &model, const char *reason, const char *detail = NULL )
{
	DevMsg( "Weapon variants: %s rejected %s: %s%s%s\n", source, model.String(),
		reason, detail ? " " : "", detail ? detail : "" );
	return false;
}

bool CollectModel( const char *sourceName, const CUtlString &path, Sources &addon, Sources &vanilla, Replacement &out )
{
	File model = addon.Read( path );
	bool changed = !!model.data;
	if ( !changed )
	{
		if ( addon.Contains( path ) )
			return RejectRole( sourceName, path, "unreadable MDL" );
		model = vanilla.Read( path );
	}
	if ( !model.data )
		return false;
	if ( !ReadStudio( *model.data, out.studio ) )
		return changed ? RejectRole( sourceName, path, "malformed or unsupported MDL" ) : false;

	if ( changed && out.studio.includes )
		return RejectRole( sourceName, path, "external include models are not relocatable" );
	const CUtlString stem = path.Left( path.Length() - 4 );
	File sidecars[ARRAYSIZE( SIDECARS )];
	for ( size_t i = 0; i < ARRAYSIZE( SIDECARS ); ++i )
	{
		const CUtlString sidecar = stem + SIDECARS[i];
		sidecars[i] = addon.Read( sidecar );
		if ( !sidecars[i].data && addon.Contains( sidecar ) )
			return RejectRole( sourceName, path, "unreadable sidecar", SIDECARS[i] );
		changed |= !!sidecars[i].data;
	}
	// A reskin still needs the base LOD texture names; defer the larger
	// vertex/physics reads unless a replacement actually qualifies.
	for ( size_t i = 1; i < ARRAYSIZE( SIDECARS ) - 1; ++i )
	{
		if ( sidecars[i].data )
		{
			if ( !ReadSidecar( *sidecars[i].data, i, out.studio ) )
				return RejectRole( sourceName, path, "invalid sidecar", SIDECARS[i] );
		}
		else
		{
			const File file = vanilla.Read( stem + SIDECARS[i] );
			if ( file.data && ReadSidecar( *file.data, i, out.studio ) )
				sidecars[i] = file;
		}
	}
	CUtlString materialProblem;
	if ( !CollectMaterials( out.studio, addon, vanilla, out.assets, changed, materialProblem ) )
		return RejectRole( sourceName, path, "missing or invalid material", materialProblem );
	if ( !changed && ( out.studio.ani.IsEmpty() || !addon.Read( out.studio.ani ).data ) )
		return false;

	for ( const size_t i : { size_t( 0 ), size_t( ARRAYSIZE( SIDECARS ) - 1 ) } )
	{
		if ( sidecars[i].data )
		{
			if ( !ReadSidecar( *sidecars[i].data, i, out.studio ) )
				return RejectRole( sourceName, path, "invalid sidecar", SIDECARS[i] );
		}
		else
		{
			const File file = vanilla.Read( stem + SIDECARS[i] );
			if ( file.data && ReadSidecar( *file.data, i, out.studio ) )
				sidecars[i] = file;
			else if ( file.data && i == ARRAYSIZE( SIDECARS ) - 1 )
				return RejectRole( sourceName, path, "incompatible vanilla physics fallback" );
		}
	}
	if ( !sidecars[0].data || ( !sidecars[1].data && !sidecars[4].data ) )
		return RejectRole( sourceName, path, "missing compatible VVD/VTX" );
	for ( size_t i = 0; i < ARRAYSIZE( SIDECARS ); ++i )
		AddAsset( out.assets, Kind::Model, stem + SIDECARS[i], sidecars[i] );
	// SDK2013 PC's model cache looks up .dx90.vtx, including when the
	// add-on spells the equivalent little-endian sidecar simply .vtx.
	if ( !sidecars[1].data && sidecars[4].data )
		AddAsset( out.assets, Kind::Model, stem + SIDECARS[1], sidecars[4] );
	FOR_EACH_MAP( out.assets, i )
	{
		File &source = out.assets[i].source;
		if ( !source.data )
		{
			source = vanilla.Read( out.assets.Key( i ).path );
			if ( !source.data )
				return RejectRole( sourceName, path, "unreadable vanilla texture", out.assets.Key( i ).path );
		}
	}
	if ( !out.studio.ani.IsEmpty() )
	{
		// The external animation belongs to the same model source.
		File ani = addon.Read( out.studio.ani );
		if ( !ani.data )
			ani = vanilla.Read( out.studio.ani );
		if ( !ani.data || size_t( ani.data->Count() ) < out.studio.aniLength ||
			!AddAsset( out.assets, Kind::Model, out.studio.ani, ani ) )
			return RejectRole( sourceName, path, "missing or invalid ANI", out.studio.ani );
	}
	return AddAsset( out.assets, Kind::Model, path, model );
}

void SoundScripts( Sources &source, Paths &paths )
{
	const char *manifestPath = "scripts/game_sounds_manifest.txt";
	KV manifest( "game_sounds_manifest" );
	if ( ParseKV( manifest.Get(), manifestPath, source.Read( manifestPath ) ) )
	{
		for ( KeyValues *entry = manifest->GetFirstValue(); entry; entry = entry->GetNextValue() )
		{
			CUtlString path;
			if ( Normalize( entry->GetString(), path ) && StartsWith( path, "scripts/" ) && EndsWith( path, ".txt" ) )
				paths.AddToTail( path );
		}
	}
}

class SoundDefinitions : public CUtlDict<KeyValues *>
{
public:
	~SoundDefinitions() { Clear(); }
	void Clear()
	{
		for ( int i = First(); IsValidIndex( i ); i = Next( i ) )
			Element( i )->deleteThis();
		RemoveAll();
	}
	void Set( const char *name, KeyValues *definition )
	{
		const int i = Find( name );
		if ( IsValidIndex( i ) )
		{
			Element( i )->deleteThis();
			Element( i ) = definition;
		}
		else
			Insert( name, definition );
	}
};

void ReadSounds( Sources &source, const Paths &paths, SoundDefinitions &sounds )
{
	for ( const CUtlString &path : paths )
	{
		KV script( "" );
		if ( !ParseKV( script.Get(), path, source.Read( path ) ) )
			continue;
		for ( KeyValues *sound = script.Get(); sound; sound = sound->GetNextKey() )
			if ( sound->GetFirstSubKey() && ( sound->FindKey( "wave" ) || sound->FindKey( "rndwave" ) ) )
				sounds.Set( Lower( sound->GetName() ), sound->MakeCopy() );
	}
}

CUtlString KVText( KeyValues *kv )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	kv->RecursiveSaveToFile( buf, 0, false, true );
	return CUtlString( static_cast<const char *>( buf.Base() ), buf.TellPut() );
}

bool RelocateWave( KeyValues *entry, Sources &addon, Sources &vanilla, AssetMap &assets, bool &local )
{
	const char *value = entry->GetString();
	const char *wave = PSkipSoundChars( value );
	CUtlString path;
	if ( !Normalize( wave, path ) || !EndsWith( path, ".wav" ) )
		return false;
	if ( !StartsWith( path, "sound/" ) )
		path = CUtlString( "sound/" ) + path;
	File file = addon.Read( path );
	local |= !!file.data;
	if ( !file.data )
		file = vanilla.Read( path );
	if ( !AddAsset( assets, Kind::Wave, path, file ) )
		return false;
	const CUtlString relocated = Relocated( Kind::Wave, path, KEY_TOKEN );
	entry->SetString( (const char *)NULL, CUtlString( value, int( wave - value ) ) + ( relocated.String() + 6 ) );
	return true;
}

void CollectSounds( const Names &names, const SoundDefinitions &localDefs,
	const SoundDefinitions &vanillaDefs, Sources &addon, Sources &vanilla, AssetMap &assets, SoundDefinitions &entries )
{
	FOR_EACH_DICT( names, i )
	{
		const char *name = names.GetElementName( i );
		const int localDef = localDefs.Find( name );
		const int vanillaDef = vanillaDefs.Find( name );
		KV kv( name );
		if ( localDefs.IsValidIndex( localDef ) )
			kv.Reset( localDefs[localDef]->MakeCopy() );
		else if ( vanillaDefs.IsValidIndex( vanillaDef ) )
			kv.Reset( vanillaDefs[vanillaDef]->MakeCopy() );
		else if ( EndsWith( name, ".wav" ) )
		{
			kv->SetString( "channel", "CHAN_AUTO" );
			kv->SetString( "wave", name );
		}
		else
			continue;
		bool local = localDefs.IsValidIndex( localDef );
		const CUtlString sourceText = KVText( kv.Get() );
		AssetMap waves;
		bool ok = true;
		int found = 0;
		for ( KeyValues *value = kv->GetFirstSubKey(); ok && value; value = value->GetNextKey() )
		{
			if ( !Q_stricmp( value->GetName(), "wave" ) )
			{
				++found;
				ok = RelocateWave( value, addon, vanilla, waves, local );
			}
			else if ( !Q_stricmp( value->GetName(), "rndwave" ) )
			{
				for ( KeyValues *item = value->GetFirstValue(); ok && item; item = item->GetNextValue() )
				{
					if ( Q_stricmp( item->GetName(), "wave" ) )
						continue;
					++found;
					ok = RelocateWave( item, addon, vanilla, waves, local );
				}
			}
		}
		if ( !ok || !found || !local )
			continue;
		assets.AddFrom( waves );
		File source( new Blob );
		source.data->CopyArray( reinterpret_cast<const byte *>( sourceText.String() ), sourceText.Length() );
		AddAsset( assets, Kind::SoundEntry, name, source );
		entries.Set( name, kv.Detach() );
	}
}

void HashString( CSHA1 &hash, const char *value )
{
	hash.Update( reinterpret_cast<unsigned char *>( const_cast<char *>( value ) ), V_strlen( value ) + 1 );
}

CUtlString HexDigest( CSHA1 &hash )
{
	const char alphabet[] = "0123456789abcdef";
	unsigned char digest[k_cubHash];
	hash.Final();
	hash.GetHash( digest );
	char key[2 * k_cubHash + 1];
	int i = 0;
	for ( unsigned char n : digest )
	{
		key[i++] = alphabet[n >> 4];
		key[i++] = alphabet[n & 15];
	}
	key[i] = 0;
	return CUtlString( key );
}

CUtlString ContentKey( const Variant &variant, const AssetMap &assets )
{
	CSHA1 hash;
	HashString( hash, CFmtStr( "WeaponVariants/%d", CACHE_VERSION ) );
	HashString( hash, variant.weapon );
	for ( const Model &model : variant.models )
	{
		HashString( hash, model.role );
		HashString( hash, model.vanilla );
	}
	HashString( hash, "" );
	FOR_EACH_MAP( assets, i )
	{
		const AssetKey &key = assets.Key( i );
		const Bytes &bytes = *assets[i].source.data;
		HashString( hash, CFmtStrN<MAX_PATH + 16>( "%d/%s", int( key.kind ), key.path.String() ) );
		HashString( hash, CFmtStr( "%d", bytes.Count() ) );
		hash.Update( const_cast<byte *>( bytes.Base() ), bytes.Count() );
		HashString( hash, "" );
	}
	return HexDigest( hash );
}

struct Candidate
{
	~Candidate() { models.PurgeAndDeleteElements(); }
	Variant variant;
	CUtlString provider;
	AssetMap assets;
	CUtlDict<Replacement *> models;
	SoundDefinitions sounds;
	uint64 sourceSize = 0;
	long sourceTime = 0;
};

bool Scan( const Weapon &weapon, const CUtlString &provider, Sources &addon, Sources &vanilla,
	const SoundDefinitions &localDefs, const SoundDefinitions &vanillaDefs, Candidate &out )
{
	out.variant.weapon = weapon.name;
	out.provider = provider;
	Names results;
	Names sounds;
	FOR_EACH_DICT( weapon.sounds, i )
		sounds.Insert( weapon.sounds.GetElementName( i ), true );
	for ( const Model &role : weapon.models )
	{
		int result = results.Find( role.vanilla );
		if ( !results.IsValidIndex( result ) )
		{
			CPlainAutoPtr<Replacement> model( new Replacement );
			const bool replaced = CollectModel( provider, role.vanilla, addon, vanilla, *model );
			result = results.Insert( role.vanilla, replaced );
			if ( replaced )
			{
				FOR_EACH_MAP( model->studio.soundEvents, i )
				{
					const CUtlString &name = model->studio.soundEvents[i];
					if ( !sounds.HasElement( name ) )
						sounds.Insert( name, true );
				}
				out.assets.AddFrom( model->assets );
				model->assets.RemoveAll();
				out.models.Insert( role.vanilla, model.Detach() );
			}
		}
		if ( results[result] )
			out.variant.models.AddToTail( role );
	}
	if ( out.variant.models.IsEmpty() )
		return false;

	CollectSounds( sounds, localDefs, vanillaDefs, addon, vanilla, out.assets, out.sounds );
	out.variant.key = ContentKey( out.variant, out.assets );
	for ( Model &model : out.variant.models )
		model.relocated = Relocated( Kind::Model, model.vanilla, out.variant.key );
	FOR_EACH_DICT( out.sounds, i )
	{
		const CFmtStr name( "WV.%s.%d", out.variant.key.String(), out.variant.sounds.Count() );
		out.variant.sounds.AddToTail( Sound{ out.sounds.GetElementName( i ), name.String() } );
	}
	if ( !out.variant.sounds.IsEmpty() )
		out.variant.soundScript = CUtlString( "scripts/weapon_variants/" ) + out.variant.key + "_sounds.txt";

	if ( EndsWith( Lower( provider ), ".vpk" ) )
	{
		out.sourceSize = filesystem->Size( provider, NULL );
		out.sourceTime = filesystem->GetFileTime( provider, NULL );
	}
	else
	{
		// A directory timestamp does not capture writes to existing members.
		// Keep total input size/latest timestamp as hints, never as identity.
		FOR_EACH_MAP( out.assets, i )
		{
			const File &source = out.assets[i].source;
			out.sourceSize += source.data->Count();
			out.sourceTime = MAX( out.sourceTime, source.mtime );
		}
	}
	return true;
}

using Pool = CUtlDict<Candidate *>;

class Pools : public CUtlDict<Pool *>
{
public:
	~Pools()
	{
		for ( int i = First(); IsValidIndex( i ); i = Next( i ) )
			Element( i )->PurgeAndDeleteElements();
		PurgeAndDeleteElements();
	}
	Pool &ForWeapon( const char *weapon )
	{
		const int i = Find( weapon );
		return IsValidIndex( i ) ? *Element( i ) : *Element( Insert( weapon, new Pool ) );
	}
};

void AppendVariant( CUtlVector<Variant> &variants, Variant &source )
{
	Variant &variant = *variants.AddToTailGetPtr();
	variant.weapon = source.weapon;
	variant.key = source.key;
	variant.soundScript = source.soundScript;
	variant.models.Swap( source.models );
	variant.sounds.Swap( source.sounds );
}

#ifdef CLIENT_DLL

int AppendString( Bytes &bytes, const CUtlString &text )
{
	const int offset = bytes.Count();
	bytes.AddMultipleToTail( text.Length() + 1, reinterpret_cast<const byte *>( text.String() ) );
	return offset;
}

void RelocateStudio( Bytes &bytes, const Bytes &original, const CUtlString &path, const Studio &studio, const Variant &variant )
{
	bytes.CopyArray( original.Base(), original.Count() );
	for ( size_t offset : studio.unterminatedOptions )
		bytes[offset] = 0;
	CSHA1 nameHash;
	HashString( nameHash, path );
	const CUtlString identity = CUtlString( "WV/" ) + variant.key + "/" + HexDigest( nameHash ).Left( 16 );
	char *name = reinterpret_cast<char *>( bytes.Base() + offsetof( studiohdr_t, name ) );
	memset( name, 0, sizeof( studiohdr_t::name ) );
	memcpy( name, identity.String(), identity.Length() );
	if ( studio.hdr2 )
	{
		const int offset = AppendString( bytes, Relocated( Kind::Model, path, variant.key ) );
		SetInt( bytes, studio.hdr2 + offsetof( studiohdr2_t, sznameindex ), offset - studio.hdr2 );
	}
	if ( !studio.ani.IsEmpty() )
	{
		const int offset = AppendString( bytes, Relocated( Kind::Model, studio.ani, variant.key ) );
		SetInt( bytes, offsetof( studiohdr_t, szanimblocknameindex ), offset );
	}
	const int rowCount = studio.directories.Count() * 2;
	const int table = ( bytes.Count() + 3 ) & ~3;
	const int padding = table - bytes.Count();
	const int appendBytes = padding + rowCount * int( sizeof( int ) );
	bytes.AddMultipleToTail( appendBytes );
	memset( bytes.Base() + table - padding, 0, appendBytes );
	size_t row = 0;
	const char *const keys[] = { variant.key.String(), "vanilla" };
	for ( const char *key : keys )
	{
		const CUtlString prefix = CUtlString( "variants/" ) + key;
		for ( const CUtlString &directory : studio.directories )
		{
			const int offset = AppendString( bytes, Join( prefix, directory ) + "/" );
			SetInt( bytes, table + ( row++ ) * sizeof( int ), offset );
		}
	}
	SetInt( bytes, offsetof( studiohdr_t, numcdtextures ), int( rowCount ) );
	SetInt( bytes, offsetof( studiohdr_t, cdtextureindex ), int( table ) );

	// Studio events keep an inline char[64], not a relocatable string
	// offset. Rewrite only known sound events with successfully copied waves.
	FOR_EACH_MAP( studio.soundEvents, i )
	{
		for ( const Sound &sound : variant.sounds )
		{
			if ( sound.vanilla != studio.soundEvents[i] )
				continue;
			constexpr int length = sizeof( mstudioevent_t::options );
			if ( sound.relocated.Length() < length )
			{
				byte *options = bytes.Base() + studio.soundEvents.Key( i );
				memset( options, 0, length );
				memcpy( options, sound.relocated.String(), sound.relocated.Length() );
			}
			break;
		}
	}
	SetInt( bytes, offsetof( studiohdr_t, length ), bytes.Count() );
}

CUtlString WithKey( const CUtlString &text, const CUtlString &key )
{
	return text.Replace( KEY_TOKEN, key, true );
}

// Only the writable cache uses host rename/remove operations; all source
// discovery, reads and generated file writes go through the mounted filesystem.
bool WriteCacheFile( const CUtlString &path, const void *bytes, size_t length )
{
	if ( path.Length() + 4 >= MAX_PATH || length > INT_MAX )
		return false;
	const CUtlString tmp = path + ".tmp";
	filesystem->CreateDirHierarchy( path.StripFilename(), "MOD" );
	FileHandle_t file = filesystem->Open( tmp, "wb", "MOD" );
	if ( file == FILESYSTEM_INVALID_HANDLE )
		return false;
	const bool written = filesystem->Write( bytes, int( length ), file ) == int( length );
	filesystem->Close( file );
#ifdef _WIN32
	if ( written && MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
#else
	if ( written && rename( tmp, path ) == 0 )
#endif
		return true;
	filesystem->RemoveFile( tmp, "MOD" );
	return false;
}

using CacheFiles = CUtlDict<size_t>;

void ReadCacheFiles( KeyValues *record, CacheFiles &files )
{
	for ( KeyValues *entry = record ? record->GetFirstTrueSubKey() : NULL; entry; entry = entry->GetNextTrueSubKey() )
	{
		if ( Q_stricmp( entry->GetName(), "file" ) )
			continue;
		CUtlString relative;
		if ( Normalize( entry->GetString( "path" ), relative ) && !files.HasElement( relative ) )
			files.Insert( relative, size_t( entry->GetUint64( "size" ) ) );
	}
}

bool CacheIsComplete( const CUtlString &cache, const Candidate &candidate, const CacheFiles &files )
{
	const bool hasSounds = candidate.sounds.Count() != 0;
	if ( files.Count() != candidate.assets.Count() - candidate.sounds.Count() + ( hasSounds ? 1 : 0 ) )
		return false;
	auto exists = [&]( const CUtlString &relative )
	{
		const int i = files.Find( relative );
		const CUtlString path = Join( cache, relative );
		return files.IsValidIndex( i ) && files[i] && filesystem->Size( path, "MOD" ) == files[i];
	};
	FOR_EACH_MAP( candidate.assets, i )
	{
		const AssetKey &key = candidate.assets.Key( i );
		if ( key.kind != Kind::SoundEntry && !exists( Relocated( key.kind, key.path, candidate.variant.key ) ) )
			return false;
	}
	return !hasSounds || exists( candidate.variant.soundScript );
}

bool PublishFiles( const CUtlString &cache, Candidate &candidate, CacheFiles &files )
{
	const Variant &variant = candidate.variant;
	files.RemoveAll();
	FOR_EACH_MAP( candidate.assets, i )
	{
		const AssetKey &key = candidate.assets.Key( i );
		if ( key.kind == Kind::SoundEntry )
			continue;
		const CUtlString relative = Relocated( key.kind, key.path, variant.key );
		const CUtlString absolute = Join( cache, relative );
		const Asset &asset = candidate.assets[i];
		const int studio = candidate.models.Find( key.path );
		const Bytes *bytes = asset.source.data;
		Bytes mdl;
		CUtlString text;
		const void *data;
		size_t size;
		if ( key.kind == Kind::Model && candidate.models.IsValidIndex( studio ) )
		{
			RelocateStudio( mdl, *bytes, key.path, candidate.models[studio]->studio, variant );
			bytes = &mdl;
		}
		if ( !asset.material.IsEmpty() )
		{
			text = WithKey( asset.material, variant.key );
			data = text.String();
			size = text.Length();
		}
		else
		{
			data = bytes->Base();
			size = bytes->Count();
		}
		if ( !WriteCacheFile( absolute, data, size ) )
			return false;
		files.Insert( relative, size );
	}
	if ( !variant.soundScript.IsEmpty() )
	{
		CUtlBuffer script( 0, 0, CUtlBuffer::TEXT_BUFFER );
		for ( const Sound &sound : variant.sounds )
		{
			const int i = candidate.sounds.Find( sound.vanilla );
			KeyValues *definition = candidate.sounds[i];
			definition->SetName( sound.relocated );
			const CUtlString text = WithKey( KVText( definition ), variant.key );
			script.Put( text.String(), text.Length() );
		}
		if ( !WriteCacheFile( Join( cache, variant.soundScript ), script.Base(), script.TellPut() ) )
			return false;
		files.Insert( variant.soundScript, script.TellPut() );
	}
	return true;
}

void ListCacheDirectory( const CUtlString &path, Paths &children )
{
	CUtlString pattern = Join( path, "*" );
	pattern.FixSlashes();
	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *name = filesystem->FindFirst( pattern, &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( Q_strcmp( name, "." ) && Q_strcmp( name, ".." ) )
			children.AddToTail( name );
	}
	if ( find != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( find );
}

void RemoveCacheTree( const CUtlString &path )
{
	// Do not recurse through links or junctions in the writable cache.
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA( path );
	if ( attributes == INVALID_FILE_ATTRIBUTES )
		return;
	if ( !( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
	{
		DeleteFileA( path );
		return;
	}
	const bool traverse = !( attributes & FILE_ATTRIBUTE_REPARSE_POINT );
#else
	struct stat fileInfo;
	if ( lstat( path, &fileInfo ) != 0 )
		return;
	if ( !S_ISDIR( fileInfo.st_mode ) )
	{
		unlink( path );
		return;
	}
	const bool traverse = true;
#endif
	if ( traverse )
	{
		Paths children;
		ListCacheDirectory( path, children );
		for ( const CUtlString &child : children )
			RemoveCacheTree( Join( path, child ) );
	}
#ifdef _WIN32
	RemoveDirectoryA( path );
#else
	rmdir( path );
#endif
}

void BuildCache( const CUtlString &cache, Pools &pools, CUtlVector<Variant> &variants )
{
	const CUtlString path = Join( cache, "manifest.txt" );
	KV old( "WeaponVariants" );
	CUtlDict<KeyValues *> previous;
	if ( old->LoadFromFile( filesystem, path, "MOD", true ) && old->GetInt( "version" ) == CACHE_VERSION )
	{
		for ( KeyValues *entry = old->GetFirstTrueSubKey(); entry; entry = entry->GetNextTrueSubKey() )
		{
			const char *key = entry->GetString( "key" );
			if ( !Q_stricmp( entry->GetName(), "variant" ) && IsKey( key ) && !previous.HasElement( key ) )
				previous.Insert( key, entry );
		}
	}

	KV manifest( "WeaponVariants" );
	manifest->SetInt( "version", CACHE_VERSION );
	Names live;
	bool dirty = false;
	FOR_EACH_DICT( pools, i )
	{
		Pool &pool = *pools[i];
		if ( pool.Count() < 2 )
			continue;
		FOR_EACH_DICT( pool, j )
		{
			Candidate &candidate = *pool[j];
			const Variant &variant = candidate.variant;
			const int found = previous.Find( variant.key );
			KeyValues *record = previous.IsValidIndex( found ) ? previous[found] : NULL;
			CacheFiles files;
			ReadCacheFiles( record, files );
			const bool reuse = CacheIsComplete( cache, candidate, files );
			if ( !reuse )
			{
				if ( !PublishFiles( cache, candidate, files ) )
				{
					Warning( "Weapon variants: could not write cache for %s (%s)\n", variant.weapon.String(), variant.key.String() );
					continue;
				}
				dirty = true;
			}
			DevMsg( "Weapon variants: %s cache %s (%s)\n", variant.weapon.String(),
				reuse ? "reused" : "published", variant.key.String() );
			// The content has been hashed again, including loose files and
			// vanilla dependencies. A timestamp-only change updates metadata;
			// no need to rewrite an already complete content-addressed entry.
			dirty |= !record || candidate.provider != record->GetString( "source" ) ||
				candidate.sourceSize != record->GetUint64( "source_size" ) ||
				candidate.sourceTime != record->GetInt( "source_mtime" );
			KeyValues *saved = new KeyValues( "variant" );
			manifest->AddSubKey( saved );
			saved->SetString( "key", variant.key );
			saved->SetString( "weapon", variant.weapon );
			saved->SetString( "source", candidate.provider );
			saved->SetUint64( "source_size", candidate.sourceSize );
			saved->SetInt( "source_mtime", candidate.sourceTime );
			for ( const Model &model : variant.models )
			{
				KeyValues *role = new KeyValues( "role" );
				saved->AddSubKey( role );
				role->SetString( "name", model.role );
				role->SetString( "vanilla", model.vanilla );
				role->SetString( "path", model.relocated );
			}
			for ( const Sound &sound : variant.sounds )
			{
				KeyValues *mapping = new KeyValues( "sound" );
				saved->AddSubKey( mapping );
				mapping->SetString( "vanilla", sound.vanilla );
				mapping->SetString( "name", sound.relocated );
			}
			FOR_EACH_DICT( files, k )
			{
				KeyValues *file = new KeyValues( "file" );
				saved->AddSubKey( file );
				file->SetString( "path", files.GetElementName( k ) );
				file->SetUint64( "size", files[k] );
			}
			live.Insert( variant.key, true );
			AppendVariant( variants, candidate.variant );
		}
	}

	if ( dirty || previous.Count() != live.Count() )
	{
		const CUtlString text = KVText( manifest.Get() );
		if ( !WriteCacheFile( path, text.String(), text.Length() ) )
		{
			Warning( "Weapon variants: could not publish cache manifest\n" );
			return; // Do not prune until the new manifest is committed.
		}
	}
	// Also prune partial generations that never made it into a manifest, but
	// only touch entries in the cache's reserved content-key namespaces.
	for ( const char *dir : { "models/variants", "materials/variants", "sound/variants", "scripts/weapon_variants" } )
	{
		const CUtlString root = Join( cache, dir );
		Paths children;
		ListCacheDirectory( root, children );
		for ( const CUtlString &child : children )
		{
			CUtlString key = child;
			if ( EndsWith( key, "_sounds.txt" ) )
				key.Truncate( key.Length() - V_strlen( "_sounds.txt" ) );
			if ( IsKey( key ) && !live.HasElement( key ) )
				RemoveCacheTree( Join( root, child ) );
		}
	}
}
#endif

} // namespace

bool IsKey( const char *key )
{
	if ( !key || strlen( key ) != k_cubHash * 2 )
		return false;
	for ( ; *key; ++key )
		if ( ( *key < '0' || *key > '9' ) && ( *key < 'a' || *key > 'f' ) )
			return false;
	return true;
}

void Rebuild( CUtlVector<Variant> &variants )
{
	variants.RemoveAll();
	Paths mod;
	SearchPaths( "MOD", false, mod );
	if ( mod.IsEmpty() )
	{
		Msg( "Weapon variants: scanned 0 add-ons, 0 variants for 0 weapons (no MOD cache path)\n" );
		return;
	}
	const CUtlString addonDir = Lower( Join( mod.Tail(), "addons" ) ) + "/";
	const CUtlString cache = Join( mod.Tail(), CACHE_DIR );
	const CUtlString lowerCache = Lower( cache );
	const CUtlString cacheDir = lowerCache + "/";
	Paths gamePaths, vanillaPaths, addonPaths;
	SearchPaths( "GAME", true, gamePaths );
	for ( const CUtlString &path : gamePaths )
	{
		const CUtlString lower = Lower( path );
		if ( StartsWith( lower, addonDir ) )
			addonPaths.AddToTail( path );
		else if ( lower != lowerCache && !StartsWith( lower, cacheDir ) )
			vanillaPaths.AddToTail( path );
	}
#ifdef CLIENT_DLL
	filesystem->RemoveSearchPath( cache, "GAME" );
#endif

	Pools pools;
	if ( addonPaths.Count() >= 2 )
	{
		Sources vanilla( vanillaPaths );
		CUtlVector<Weapon> weapons;
		ReadCatalog( vanilla, "scripts/weapon_manifest.txt", false, weapons );
		ReadCatalog( vanilla, "scripts/melee/melee_manifest.txt", true, weapons );
		if ( weapons.IsEmpty() )
			DevMsg( "Weapon variants: no vanilla weapon or melee manifest entries found\n" );
		Paths soundPaths;
		SoundScripts( vanilla, soundPaths );
		SoundDefinitions vanillaSounds;
		ReadSounds( vanilla, soundPaths, vanillaSounds );

		for ( const CUtlString &path : addonPaths )
		{
			DevMsg( "Weapon variants: scanning %s\n", path.String() );
			Sources addon( path.String() );
			Paths localPaths;
			localPaths = soundPaths;
			SoundScripts( addon, localPaths );
			SoundDefinitions localSounds;
			ReadSounds( addon, localPaths, localSounds );
			for ( const Weapon &weapon : weapons )
			{
				CPlainAutoPtr<Candidate> candidate( new Candidate );
				if ( Scan( weapon, path, addon, vanilla, localSounds, vanillaSounds, *candidate ) )
				{
					Pool &pool = pools.ForWeapon( weapon.name );
					const CUtlString &key = candidate->variant.key;
					if ( pool.HasElement( key ) )
					{
						DevMsg( "Weapon variants: %s duplicates %s content\n", V_UnqualifiedFileName( path ), weapon.name.String() );
						continue;
					}
#ifndef CLIENT_DLL
					// The server publishes identities only; release the source
					// buffers as soon as the canonical key has been computed.
					candidate->assets.RemoveAll();
					candidate->models.PurgeAndDeleteElements();
					candidate->sounds.Clear();
#endif
					pool.Insert( key, candidate.Detach() );
				}
			}
		}
	}
	FOR_EACH_DICT( pools, i )
	{
		const Pool &pool = *pools[i];
		// A shared stock material can affect other weapons too. A lone
		// match isn't a pool and won't get a network index or cache entry.
		if ( pool.Count() >= 2 )
			DevMsg( "Weapon variants: %s: %d distinct replacements\n", pools.GetElementName( i ), pool.Count() );
	}

#ifdef CLIENT_DLL
	BuildCache( cache, pools, variants );
	if ( !variants.IsEmpty() )
		filesystem->AddSearchPath( cache, "GAME", PATH_ADD_TO_TAIL );
#else
	FOR_EACH_DICT( pools, i )
	{
		Pool &pool = *pools[i];
		if ( pool.Count() >= 2 )
			FOR_EACH_DICT( pool, j )
				AppendVariant( variants, pool[j]->variant );
	}
#endif
	int weaponCount = 0;
	const Variant *previous = NULL;
	for ( const Variant &variant : variants )
	{
		if ( !previous || variant.weapon != previous->weapon )
			++weaponCount;
		previous = &variant;
	}
	Msg( "Weapon variants: scanned %d add-ons, %d variants for %d weapons (cache: %s)\n",
		addonPaths.Count(), variants.Count(), weaponCount, cache.String() );
}

void RegisterSoundOverrides( const CUtlVector<Variant> &variants )
{
#ifndef CLIENT_DLL
	if ( engine->IsDedicatedServer() )
		return;
#endif
	if ( !soundemitterbase )
		return;
	// SDK2013 MP x64 VSoundEmitter002: slot 39 takes (this, path, preload)
	// and calls the sound file loader as an override, matching the header.
	for ( const Variant &variant : variants )
		if ( !variant.soundScript.IsEmpty() && filesystem->FileExists( variant.soundScript, "GAME" ) )
			soundemitterbase->AddSoundOverrides( variant.soundScript, true );
}
} // namespace TerrorVariantAssets
