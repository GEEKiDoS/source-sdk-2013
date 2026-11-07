// Included INSIDE the existing CCommentarySystem definition for L4D2 only.
// Native S9BB90/S9C110/S9CD30/S99E20, same actual singleton/handle vector.
public:
	bool IsRoundPreservedEntity( CBaseEntity *entity ) const
	{
		FOR_EACH_VEC( m_hSpawnedEntities, i ) if ( m_hSpawnedEntities[i].Get() == entity ) return true;
		return entity->GetEntityName() == MAKE_STRING( COMMENTARY_SPAWNED_SEMAPHORE );
	}
	void ApplyMapModifications()
	{
		if ( !m_pMapModifications ) return; // S99E20 actual owned-root guard.
		for ( KeyValues *entity = m_pMapModifications->GetFirstSubKey(); entity; entity = entity->GetNextKey() )
		{
			KeyValues *modify = entity->FindKey( "modify" );
			if ( !modify ) continue;
			if ( entity->FindKey( "find_by_modelname" ) )
			{
				for ( CBaseEntity *target = gEntList.FindEntityByModel( NULL, entity->GetString( "find_by_modelname" ) ); target;
					target = gEntList.FindEntityByModel( target, entity->GetString( "find_by_modelname" ) ) )
					for ( KeyValues *value = modify->GetFirstSubKey(); value; value = value->GetNextKey() )
						target->KeyValue( value->GetName(), value->GetString() );
			}
			if ( entity->FindKey( "find_by_targetname" ) )
			{
				for ( CBaseEntity *target = gEntList.FindEntityByName( NULL, entity->GetString( "find_by_targetname" ) ); target;
					target = gEntList.FindEntityByName( target, entity->GetString( "find_by_targetname" ) ) )
					for ( KeyValues *value = modify->GetFirstSubKey(); value; value = value->GetNextKey() )
						target->KeyValue( value->GetName(), value->GetString() );
			}
		}
	}
private:
	struct CommentaryMapData_t
	{
		int offset;
		int length;
	};
	void LoadTerrorCommentaryMapEntities()
	{
		char filename[512];
		Q_snprintf( filename, sizeof( filename ), "maps/%s_commentary.txt", STRING( gpGlobals->mapname ) );
		KeyValues *file = new KeyValues( "Commentary" );
		if ( !file->LoadFromFile( filesystem, filename, "GAME" ) )
		{
			Msg( "Commentary: Could not find commentary data file '%s'. \n", filename );
			file->deleteThis();
			return;
		}
		Msg( "Commentary: Loading commentary data from %s. \n", filename );
		CUtlVector< CPointTemplate * > templates;
		CUtlVector< CommentaryMapData_t > mapData;
		CUtlBuffer data( 0, filesystem->Size( filename, "GAME" ) );
		CUtlBuffer serialized( 0, 20480, CUtlBuffer::TEXT_BUFFER );
		for ( KeyValues *node = file->GetFirstSubKey(); node; node = node->GetNextKey() )
		{
			const char *classname = node->GetName();
			if ( !Q_strncmp( classname, "trackinfo", 9 ) ) continue;
			if ( node->FindKey( "classname" ) ) classname = node->GetString( "classname" );
			const bool modification = !Q_stricmp( node->GetName(), "modify_entity" );
			const bool admitted = IsInCommentaryMode() || !Q_strncmp( classname, "info_remarkable", 16 ) ||
				!Q_strncmp( classname, "info_item_position", 19 ) || node->FindKey( "mapupdate" ) != NULL;
			if ( !admitted && !modification ) continue;
			if ( modification )
			{
				if ( !m_pMapModifications ) m_pMapModifications = new KeyValues( "Entities" );
				m_pMapModifications->AddSubKey( node->MakeCopy() );
			}
			// Native still tries to create an admitted modify_entity after
			// storing its copy; retain the real factory failure/diagnostic.
			CBaseEntity *entity = CreateEntityByName( classname );
			if ( !entity )
			{
				Warning( "Commentary: Failed to spawn commentary entity, type: '%s'\n", classname );
				continue;
			}
			ParseEntKVBlock( entity, node );
			const int index = m_hSpawnedEntities.AddToTail( entity );
			serialized.Clear();
			node->RecursiveSaveToFile( serialized, 0 );
			const char *begin = static_cast< const char * >( serialized.Base() );
			const char *open = strchr( begin, '{' );
			const char *close = strrchr( begin, '}' );
			// Same root-brace stripping as native09BB90; serialized real KVs
			// supply both delimiters. No fake entity data or empty fallback.
			CommentaryMapData_t entry = { data.TellPut(), (int)( close - 1 - ( open + 2 ) ) };
			data.Put( open + 2, entry.length );
			mapData.EnsureCount( index + 1 );
			mapData[index] = entry;
			CPointCommentaryNode *commentaryNode = dynamic_cast< CPointCommentaryNode * >( entity );
			if ( commentaryNode ) commentaryNode->SetNodeNumber( ++m_iCommentaryNodeCount );
			CPointTemplate *pointTemplate = dynamic_cast< CPointTemplate * >( entity );
			if ( pointTemplate ) templates.AddToTail( pointTemplate );
		}
		ApplyMapModifications(); // Native before point-template building, not merely after replay.
		if ( templates.Count() != 0 )
		{
			CUtlVector< CBaseEntity * > entities;
			FOR_EACH_VEC( m_hSpawnedEntities, i ) entities.AddToTail( m_hSpawnedEntities[i].Get() );
			FOR_EACH_VEC( templates, i )
			{
				CPointTemplate *pointTemplate = templates[i];
				if ( DispatchSpawn( pointTemplate ) < 0 )
				{
					UTIL_Remove( pointTemplate );
					gEntList.CleanupDeleteList();
					continue;
				}
				pointTemplate->StartBuildingTemplates();
				const int count = pointTemplate->GetNumTemplateEntities();
				for ( int item = 0; item < count; ++item )
				{
					CBaseEntity *entity = pointTemplate->GetTemplateEntity( item );
					FOR_EACH_VEC( entities, entityIndex )
					{
						if ( entities[entityIndex] != entity ) continue;
						const CommentaryMapData_t &entry = mapData[entityIndex];
						pointTemplate->AddTemplate( entity, static_cast< const char * >( data.Base() ) + entry.offset, entry.length );
						if ( pointTemplate->ShouldRemoveTemplateEntities() )
						{
							UTIL_Remove( entity );
							gEntList.CleanupDeleteList();
							entities[entityIndex] = NULL;
						}
						break;
					}
				}
				pointTemplate->FinishBuildingTemplates();
			}
			for ( int i = entities.Count() - 1; i >= 0; --i ) if ( entities[i] == NULL ) m_hSpawnedEntities.Remove( i );
		}
		file->deleteThis();
	}
	void LoadTerrorMutationMapEntities( const char *mode )
	{
		char filename[MAX_PATH], emsFilename[MAX_PATH];
		if ( mode ) Q_snprintf( filename, sizeof( filename ), "maps\\%s_entities_%s.txt", STRING( gpGlobals->mapname ), mode );
		else Q_snprintf( filename, sizeof( filename ), "maps\\%s_entities.txt", STRING( gpGlobals->mapname ) );
		const char *path = filename;
		if ( !filesystem->FileExists( filename ) )
		{
			Q_snprintf( emsFilename, sizeof( emsFilename ), "ems\\%s", filename );
			path = emsFilename;
		}
		KeyValues *file = new KeyValues( "EntityFile" );
		KeyValues *entities = new KeyValues( "Entities" );
		file->AddSubKey( entities );
		if ( entities->LoadFromFile( filesystem, path, "GAME" ) )
		{
			Msg( "Scripted Mode: Loading entity data from %s. \n", path );
			for ( KeyValues *node = file->GetFirstSubKey(); node; node = node->GetNextKey() )
			{
				const char *classname = node->GetName();
				if ( node->FindKey( "classname" ) ) classname = node->GetString( "classname" );
				if ( !Q_stricmp( classname, "func_instance" ) )
				{
					classname = "info_item_position";
					node->SetString( "targetname", node->GetString( "spawnpositionname" ) );
				}
				CBaseEntity *entity = CreateEntityByName( classname );
				if ( !entity )
				{
					Warning( "Scripted Mode: Failed to spawn entity, type: '%s'\n", classname );
					continue;
				}
				ParseEntKVBlock( entity, node );
				if ( entity->GetRefEHandle().ToInt() == INVALID_EHANDLE_INDEX ) UTIL_RemoveImmediate( entity );
				else m_hSpawnedEntities.AddToTail( entity );
			}
		}
		else Msg( "Scripted Mode: Could not find entity data file '%s'. \n", path );
		file->deleteThis();
	}
	void InitTerrorMapEntities()
	{
		cvar->InstallGlobalChangeCallback( CV_GlobalChange_Commentary );
		m_flNextTeleportTime = 0.0f;
		m_iTeleportStage = TELEPORT_NONE;
		m_hLastCommentaryNode = NULL;
		if ( gEntList.FindEntityByName( NULL, COMMENTARY_SPAWNED_SEMAPHORE ) ) return;
		CBaseEntity *semaphore = CreateEntityByName( "info_target" );
		semaphore->SetName( MAKE_STRING( COMMENTARY_SPAWNED_SEMAPHORE ) );
		const bool oldLock = engine->LockNetworkStringTables( false );
		if ( m_pMapModifications ) { m_pMapModifications->deleteThis(); m_pMapModifications = NULL; }
		LoadTerrorCommentaryMapEntities();
		if ( TheDirector->GetScriptedMode()->IsEnabled() ) LoadTerrorMutationMapEntities( TheDirector->GetGameMode() );
		FOR_EACH_VEC( m_hSpawnedEntities, i ) DispatchSpawn( m_hSpawnedEntities[i].Get() );
		PrecachePointTemplates();
		FOR_EACH_VEC( m_hSpawnedEntities, i ) if ( m_hSpawnedEntities[i].Get() ) m_hSpawnedEntities[i]->Activate();
		engine->LockNetworkStringTables( oldLock );
	}
	KeyValues *m_pMapModifications; // Native44; owned, lazy09BB90 copy writer, Init/Shutdown delete.
	bool m_bOriginalSvCheats; // Native40, actual CalculateCommentaryState capture/Shutdown restore.
public:
