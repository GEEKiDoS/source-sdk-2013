// Native L4D2 purchase-preset console backend (C21FE80..C221B03).
// This is not CS gameplay: the pinned L4D2 cost calculator always returns -1,
// verbose query always returns false, and inventory capture is a genuine nullsub.
#include "cbase.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "tier1/KeyValues.h"
#include "tier1/utlvector.h"
#include "vgui/ILocalize.h"
#include "tier3/tier3.h"
#include "tier0/memdbgon.h"

struct BuyPresetWeapon
{
	BuyPresetWeapon() : m_weapon( 0 ), m_ammo( 0 ), m_fill( true ) {}
	int m_weapon;
	int m_ammo;
	bool m_fill;
};
struct BuyPresetSet
{
	BuyPresetSet() : m_armor( 0 ), m_helmet( false ), m_smoke( false ), m_he( false ), m_flash( 0 ), m_defuser( false ), m_nvgs( false ) {}
	BuyPresetWeapon m_primary, m_secondary;
	int m_armor;
	bool m_helmet, m_smoke, m_he;
	int m_flash;
	bool m_defuser, m_nvgs;
};
class BuyPreset
{
public:
	BuyPreset()
	{
		m_name[0] = 0;
		const wchar_t *blank = g_pVGuiLocalize->Find( "#Cstrike_BuyPresetBlank" );
		if ( blank ) wcsncpy( m_name, blank, ARRAYSIZE( m_name ) );
		m_name[63] = 0;
	}
	BuyPreset( const BuyPreset &other ) { *this = other; }
	BuyPreset &operator=( const BuyPreset &other )
	{
		wcsncpy( m_name, other.m_name, ARRAYSIZE( m_name ) );
		m_sets.RemoveAll();
		FOR_EACH_VEC( other.m_sets, i ) m_sets.AddToTail( other.m_sets[i] );
		return *this;
	}
	void Parse( KeyValues *data )
	{
		m_name[0] = 0;
		m_sets.RemoveAll();
		if ( !data ) return;
		wcsncpy( m_name, data->GetWString( "PresetName", L"" ), ARRAYSIZE( m_name ) );
		m_name[63] = 0;
		if ( data->GetInt( "Version" ) != 4 ) return;
		const char *equipment = data->GetString( "Equipment", NULL );
		// Primary/Secondary are fetched but never parsed by this L4D2 binary.
		if ( !equipment ) return;
		BuyPresetSet set;
		char token[1024];
		while ( ( equipment = engine->ParseFile( equipment, token, sizeof( token ) ) ) != NULL )
		{
			if ( strlen( token ) >= 32 ) break;
			char word[32];
			V_strncpy( word, token, sizeof( word ) );
			for ( char *c = word; *c; ++c ) if ( *c == '/' ) *c = ' ';
			char type[32];
			int count;
			if ( sscanf( word, "%s %d", type, &count ) != 2 ) break;
			if ( !strcmp( type, "vest" ) ) { set.m_helmet = false; set.m_armor = count > 0 ? 100 : 0; }
			else if ( !strcmp( type, "vesthelm" ) ) { set.m_helmet = true; set.m_armor = count > 0 ? 100 : 0; }
			else if ( !strcmp( type, "defuser" ) ) set.m_defuser = count > 0;
			else if ( !strcmp( type, "nvgs" ) ) set.m_nvgs = count > 0;
			else if ( !strcmp( type, "sgren" ) ) set.m_smoke = count > 0;
			else if ( !strcmp( type, "hegren" ) ) set.m_he = count > 0;
			else if ( !strcmp( type, "flash" ) ) set.m_flash = clamp( count, 0, 2 );
		}
		m_sets.AddToTail( set );
	}
	void Write( KeyValues *root ) const
	{
		KeyValues *data = root->CreateNewKey();
		data->SetWString( "PresetName", m_name );
		data->SetInt( "Version", 4 );
		if ( !m_sets.Count() ) return;
		const BuyPresetSet &set = m_sets[0];
		char text[1024];
		V_snprintf( text, sizeof( text ), "%s/%d%c", WeaponIDToAlias( set.m_primary.m_weapon ), set.m_primary.m_ammo, set.m_primary.m_fill ? '+' : '=' );
		data->SetString( "Primary", text );
		V_snprintf( text, sizeof( text ), "%s/%d%c", WeaponIDToAlias( set.m_secondary.m_weapon ), set.m_secondary.m_ammo, set.m_secondary.m_fill ? '+' : '=' );
		data->SetString( "Secondary", text );
		V_snprintf( text, sizeof( text ), "vest%s/%d flash/%d sgren/%d hegren/%d defuser/%d nvgs/%d", set.m_helmet ? "helm" : "", set.m_armor, set.m_flash, set.m_smoke, set.m_he, set.m_defuser, set.m_nvgs );
		data->SetString( "Equipment", text );
	}
	wchar_t m_name[64];
	CUtlVector< BuyPresetSet > m_sets;
};
class BuyPresetManager
{
public:
	void Load()
	{
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player ) return;
		const int team = player->GetTeamNumber();
		if ( team != 2 && team != 3 ) return;
		// Native +40 starts at 0 and is never assigned; team comparison always
		// reloads for 2/3. Preserve this bug, not an invented loaded-team cache.
		m_presets.RemoveAll();
		const bool loaded = Read( m_presets, team == 3 ? "cfg/BuyPresets_CT.vdf" : "cfg/BuyPresets_TER.vdf" );
		if ( !m_presets.Count() ) Read( m_presets, team == 3 ? "cfg/BuyPresetsDefault_CT.vdf" : "cfg/BuyPresetsDefault_TER.vdf" );
		while ( m_presets.Count() < 4 ) m_presets.AddToTail();
		Copy( m_edits, m_presets );
		if ( !loaded ) Save();
	}
	void Save()
	{
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player ) return;
		const int team = player->GetTeamNumber();
		if ( team != 2 && team != 3 ) return;
		KeyValues *data = new KeyValues( "Presets" );
		FOR_EACH_VEC( m_presets, i ) m_presets[i].Write( data );
		data->SaveToFile( filesystem, team == 3 ? "cfg/BuyPresets_CT.vdf" : "cfg/BuyPresets_TER.vdf", NULL );
		data->deleteThis();
	}
	void ResetDefaults()
	{
		Load();
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player || ( player->GetTeamNumber() != 2 && player->GetTeamNumber() != 3 ) ) return;
		m_edits.RemoveAll();
		Read( m_edits, player->GetTeamNumber() == 3 ? "cfg/BuyPresetsDefault_CT.vdf" : "cfg/BuyPresetsDefault_TER.vdf" );
	}
	void PrintUsage()
	{
		Load();
		if ( !m_presets.Count() ) { Msg( "cl_buy_favorite: no favorites are defined\n" ); return; }
		Load();
		Msg( "usage:  cl_buy_favorite <1...%d>\n", m_presets.Count() );
		Load();
		for ( int i = 0; i < m_presets.Count(); ++i )
		{
			if ( m_presets[i].m_name[0] )
			{
				char name[64];
				g_pVGuiLocalize->ConvertUnicodeToANSI( m_presets[i].m_name, name, sizeof( name ) );
				Msg( " %d. %s\n", i + 1, name );
			}
			Load();
		}
	}
	static void Copy( CUtlVector< BuyPreset > &dest, const CUtlVector< BuyPreset > &source )
	{
		dest.RemoveAll();
		FOR_EACH_VEC( source, i ) dest.AddToTail( source[i] );
	}
	CUtlVector< BuyPreset > m_presets, m_edits;
private:
	static bool Read( CUtlVector< BuyPreset > &dest, const char *filename )
	{
		KeyValues *data = new KeyValues( "Presets" );
		const bool loaded = data->LoadFromFile( filesystem, filename, NULL );
		for ( KeyValues *entry = data->GetFirstSubKey(); entry; entry = entry->GetNextKey() )
		{
			BuyPreset preset;
			preset.Parse( entry );
			dest.AddToTail( preset );
		}
		data->deleteThis();
		return loaded;
	}
};
static BuyPresetManager *s_pBuyPresetManager;
static BuyPresetManager &GetBuyPresetManager()
{
	if ( !s_pBuyPresetManager ) s_pBuyPresetManager = new BuyPresetManager;
	return *s_pBuyPresetManager; // Native process-lifetime allocation, no invented shutdown.
}
static void CC_BuyFavorite( const CCommand &args )
{
	if ( !engine->IsInGame() ) return;
	BuyPresetManager &manager = GetBuyPresetManager();
	if ( args.ArgC() != 2 ) { manager.PrintUsage(); return; }
	const int index = (int)( (unsigned int)atoi( args[1] ) - 1u );
	if ( index < 0 ) { manager.PrintUsage(); return; }
	manager.Load();
	if ( index >= manager.m_presets.Count() ) { manager.PrintUsage(); return; }
	// C21FE80 always stores cost=-1; no set can be bought or already bought.
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player ) player->EmitSound( "BuyPreset.CantBuy" );
}
static void CC_SetBuyFavorite( const CCommand &args )
{
	if ( !engine->IsInGame() ) return;
	BuyPresetManager &manager = GetBuyPresetManager();
	if ( args.ArgC() != 2 ) { manager.PrintUsage(); return; }
	const int index = (int)( (unsigned int)atoi( args[1] ) - 1u );
	if ( index < 0 ) { manager.PrintUsage(); return; }
	manager.Load();
	if ( index >= manager.m_presets.Count() ) { manager.PrintUsage(); return; }
	BuyPreset preset( manager.m_presets[index] );
	BuyPresetSet set; // Native inventory-capture nullsub leaves the default set.
	if ( preset.m_sets.Count() ) preset.m_sets[0] = set;
	else preset.m_sets.AddToTail( set );
	manager.m_presets[index] = preset;
	manager.Save();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player ) player->EmitSound( "BuyPreset.Updated" );
}
static void CC_ResetBuyFavorites()
{
	if ( !engine->IsInGame() ) return;
	BuyPresetManager &manager = GetBuyPresetManager();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || ( player->GetTeamNumber() != 2 && player->GetTeamNumber() != 3 ) ) return;
	manager.ResetDefaults();
	BuyPresetManager::Copy( manager.m_presets, manager.m_edits );
	manager.Save();
}
#include "l4d2/weapons/buy_presets_registrations.inc"
