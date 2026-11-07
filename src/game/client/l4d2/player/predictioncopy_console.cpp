// Native cl_predictioncopy_describe C1801C0; flatten/sort/runs C17E1B0,
// C17D080, C17E310 and diagnostic C17D770/C17D680. SDK datamap_t has no native
// optimized-map pointer, so the exact diagnostic layout is cached game-side.
// This does not replace prediction, alter datamap ABI, or fake native offsets.
#include "cbase.h"
#include "cliententitylist.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"

struct TerrorPredictionField
{
	const typedescription_t *description;
	int offset;
	unsigned short depth;
};
struct TerrorPredictionRun
{
	int first, last, offset, size;
};
struct TerrorPredictionLayout
{
	CUtlVector< TerrorPredictionField > fields[2];
	CUtlVector< TerrorPredictionRun > runs[2];
};
static void FlattenPredictionFields( datamap_t *map, int type, int depth, int offset,
	int &maxDepth, CUtlVector< TerrorPredictionField > &fields )
{
	if ( map->baseMap ) FlattenPredictionFields( map->baseMap, type, depth + 1, offset, maxDepth, fields );
	maxDepth = MAX( maxDepth, depth );
	for ( int i = 0; i < map->dataNumFields; ++i )
	{
		const typedescription_t &description = map->dataDesc[i];
		if ( description.fieldType == FIELD_VOID ) continue;
		const bool embedded = description.fieldType == FIELD_EMBEDDED;
		if ( !embedded && ( description.flags & FTYPEDESC_PRIVATE ) ) continue;
		if ( !embedded && ( ( description.flags & FTYPEDESC_INSENDTABLE ) != 0 ) == ( type != 0 ) )
		{
			TerrorPredictionField field;
			field.description = &description;
			field.offset = offset + description.fieldOffset[TD_OFFSET_NORMAL];
			field.depth = (unsigned short)depth;
			fields.AddToTail( field );
		}
		if ( embedded )
		{
			Assert( !( description.flags & FTYPEDESC_PTR ) );
			FlattenPredictionFields( description.td, type, depth,
				offset + description.fieldOffset[TD_OFFSET_NORMAL], maxDepth, fields );
		}
		if ( description.flags & FTYPEDESC_OVERRIDE )
		{
			// Preserve the native Count-2 starting index, even when this field
			// was filtered from this particular copy-type list.
			for ( int j = fields.Count() - 2; j >= 0; --j )
				if ( fields[j].description->fieldType != FIELD_EMBEDDED &&
					!Q_stricmp( fields[j].description->fieldName, description.fieldName ) ) fields.Remove( j );
		}
	}
}
static int __cdecl PredictionFieldOrder( const TerrorPredictionField *a, const TerrorPredictionField *b )
{
	if ( a->offset < b->offset ) return -1;
	if ( a->offset > b->offset ) return 1;
	if ( a->depth < b->depth ) return -1;
	return a->depth > b->depth;
}
static void BuildPredictionRuns( const CUtlVector< TerrorPredictionField > &fields, CUtlVector< TerrorPredictionRun > &runs )
{
	if ( !fields.Count() ) return;
	int first = 0, begin = fields[0].offset, end = begin;
	FOR_EACH_VEC( fields, i )
	{
		const int offset = fields[i].offset;
		if ( fields[i].description->fieldType == FIELD_EMBEDDED ) continue;
		if ( end != offset )
		{
			TerrorPredictionRun run = { first, i - 1, begin, end - begin };
			runs.AddToTail( run );
			first = i;
			begin = offset;
		}
		end = offset + fields[i].description->fieldSizeInBytes;
	}
	if ( end != begin )
	{
		TerrorPredictionRun run = { first, fields.Count() - 1, begin, end - begin };
		runs.AddToTail( run );
	}
}
class TerrorPredictionDescriptionCache
{
public:
	TerrorPredictionDescriptionCache() { SetDefLessFunc( m_layouts ); }
	~TerrorPredictionDescriptionCache()
	{
		FOR_EACH_MAP( m_layouts, i ) delete m_layouts[i];
	}
	TerrorPredictionLayout *Get( datamap_t *map )
	{
		unsigned short index = m_layouts.Find( map );
		if ( index != m_layouts.InvalidIndex() ) return m_layouts[index];
		TerrorPredictionLayout *layout = new TerrorPredictionLayout;
		for ( int type = 0; type < 2; ++type )
		{
			int maxDepth = 0;
			FlattenPredictionFields( map, type, 0, 0, maxDepth, layout->fields[type] );
			FOR_EACH_VEC( layout->fields[type], j ) layout->fields[type][j].depth = (unsigned short)( maxDepth - layout->fields[type][j].depth );
			layout->fields[type].Sort( PredictionFieldOrder );
			BuildPredictionRuns( layout->fields[type], layout->runs[type] );
		}
		m_layouts.Insert( map, layout );
		return layout;
	}
private:
	CUtlMap< datamap_t *, TerrorPredictionLayout * > m_layouts;
};
static TerrorPredictionDescriptionCache s_PredictionDescriptionCache;
static void PrintPredictionDescription( datamap_t *map, const TerrorPredictionLayout &layout, int type )
{
	static const char *types[] = { "Non-Sendtable", "SendTable" };
	Msg( "->Sorted %s for copy type: %s, packing: Normal\n", map->dataClassName, types[type] );
	const CUtlVector< TerrorPredictionField > &fields = layout.fields[type];
	int begin = 0, end = 0, count = 0, bytes = 0;
	FOR_EACH_VEC( fields, i )
	{
		const TerrorPredictionField &field = fields[i];
		if ( i == 0 ) begin = end = field.offset;
		if ( field.description->fieldType != FIELD_EMBEDDED )
		{
			if ( end != field.offset )
			{
				Msg( "  gap of %d bytes [last run %d]\n", field.offset - end, end - begin );
				++count;
				bytes += end - begin;
				begin = field.offset;
			}
			end = field.offset + field.description->fieldSizeInBytes;
		}
		Msg( "group %s [flat %d] [sort %d] %d bytes\n", field.description->fieldName,
			field.offset, field.offset, field.description->fieldSizeInBytes );
	}
	if ( fields.Count() && end != begin )
	{
		Msg( "Last run %d\n", end - begin );
		++count;
		bytes += end - begin;
	}
	if ( count ) Msg( "%d runs, %d bytes in runs, %f avg bytes per run\n", count, bytes, (double)bytes / count );
	Msg( "->\n" );
	Msg( "   Runs for copy type: %s, packing: Normal\n", types[type] );
	FOR_EACH_VEC( layout.runs[type], i )
	{
		const TerrorPredictionRun &run = layout.runs[type][i];
		Msg( "     %5d:  %5d -> %5d (%5d bytes): %s to %s\n", i, run.offset, run.offset + run.size, run.size,
			fields[run.first].description->fieldName, fields[run.last].description->fieldName );
	}
}
static void CC_PredictionCopyDescribe( const CCommand &args )
{
	if ( args.ArgC() <= 1 ) { Msg( "Usage:  %s <entindex>", args[0] ); return; }
	const int index = atoi( args[1] );
	C_BaseEntity *entity = ClientEntityList().GetBaseEntity( index );
	if ( !entity ) { Msg( "cl_predictioncopy_describe:  no such entity %d\n", index ); return; }
	datamap_t *map = entity->GetPredDescMap();
	if ( !map ) return;
	TerrorPredictionLayout *layout = s_PredictionDescriptionCache.Get( map );
	for ( int type = 0; type < 2; ++type ) PrintPredictionDescription( map, *layout, type );
}
#include "l4d2/player/predictioncopy_console_registrations.inc"
