//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NAV_H
#define TERROR_NAV_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_mesh.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_world.h"

class CTerrorPlayer;

class TerrorNavMesh : public CNavMesh
{
public:
	DECLARE_CLASS( TerrorNavMesh, CNavMesh );

	TerrorNavMesh();
	virtual ~TerrorNavMesh();
	virtual void OnServerActivate() OVERRIDE;
	virtual void OnRoundRestart() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void OnAreaBlocked( CNavArea *area, bool affectsFlow ) OVERRIDE;
	virtual void OnAreaUnblocked( CNavArea *area, bool affectsFlow ) OVERRIDE;
	void BindWorldEntities();
	void OnDoorCreated( CBaseEntity *door );
	CTerrorCheckpoint *GetCheckpoint( const Vector &position ) const;
	CTerrorCheckpoint *GetExitCheckpoint() const;
	CTerrorCheckpoint *GetTransitionCheckpoint() const;
	const CUtlVector< CTerrorCheckpoint * > &GetCheckpoints() const { return m_checkpoints; }
	bool HasFlowDistances() const { return m_hasFlowDistances; }
	void OnAreaCleared( TerrorNavArea *area );
	void QueueWanderingPopulationArea( TerrorNavArea *area );
	void MaintainWanderingPopulation( bool force );
	void PopulateBackground( int count, bool restricted );
	virtual CNavArea *CreateArea() const;
	virtual void DestroyArea( CNavArea *area ) const OVERRIDE;
	virtual void PreLoadAreas( int nAreas );
	virtual void Reset();
	using CNavMesh::OnEditDestroyNotify;
	virtual void OnEditDestroyNotify( CNavArea *deadArea );
	virtual unsigned int GetSubVersionNumber() const { return 14; }
	virtual bool Save() const;
	virtual NavErrorType PostLoad( unsigned int version );
	virtual void SaveCustomDataPreArea( CUtlBuffer &fileBuffer ) const;
	virtual void LoadCustomDataPreArea( CUtlBuffer &fileBuffer, unsigned int subVersion );
	virtual void SaveCustomData( CUtlBuffer &fileBuffer ) const;
	virtual void LoadCustomData( CUtlBuffer &fileBuffer, unsigned int subVersion );

	static bool IsSupportedVersion( unsigned int version, unsigned int subVersion );
	bool BeginLoad( unsigned int version, unsigned int subVersion );
	bool IsRetailFormat() const { return m_loadedSubVersion == 13 || m_loadedSubVersion == 14; }
	bool IsVisibilityReady() const { return IsLoaded() && !m_visibilityNeedsRebuild; }
	// Requires the actual loaded world. Pre14 dirty data is never silently
	// declared ready or regenerated merely by structural loading.
	void RebuildVisibility();
	unsigned int GetLoadedSubVersion() const { return m_loadedSubVersion; }
	NavErrorType GetLoadError() const { return m_loadError; }
	void SetLoadError( NavErrorType error ) { m_loadError = error; }

	// These run at the existing PlaceDirectory seams. Places stay native Places;
	// the population directory below is independent and never uses Place IDs.
	void LoadPlaceDirectory( PlaceDirectory &directory, CUtlBuffer &fileBuffer, int version );
	void PreparePlaceDirectory( PlaceDirectory &directory ) const;
	void SavePlaceDirectory( const PlaceDirectory &directory, CUtlBuffer &fileBuffer ) const;

	const char *GetDefaultPopulationName() const;
	int GetPopulationCount() const { return m_populationDirectory.Count(); }
	const char *GetPopulationName( int index ) const;
	unsigned int FindPopulationIndex( const char *name ) const;
	const char *GetPopulationNameForIndex( unsigned int index ) const;
	unsigned int GetSelectedFogPopulation() const { return m_selectedFogPopulation; }

	void CommandNavFogSet( const CCommand &args );
	void CommandNavFogMark();
	void CommandNavFogPick();
	void CommandNavFogSelectPlace( const CCommand &args );
	void CommandNavFogReplace( const CCommand &args );
	void CommandNavFogPrintUsed();
	void CommandNavSelectVisible( bool completely );
	void CommandNavSelectViewDistance();
	void CommandNavSelectVisibleSetSize( const CCommand &args );
	void CommandNavPrintVisibilityInfo();
	void CommandNavAutoBuild();
	void CommandNavEditorAttribute( const CCommand &args, bool clear );
	void CommandNavTroubleReport( unsigned int mask, bool suggestions );
	void CommandNavTravelDistance();

	// S2E2AF0 + S281700 + S2E3710. Does NOT clear the real selected set.
	// The radius constrains the collector; native traversal itself is unlimited,
	// options3 (incoming + ignore-blocked), teamANY. Returns actual newly selected count.
	int SelectNativeRegion( CNavArea *start, unsigned int required, unsigned int excluded,
		unsigned int population = 0xFFFF, float minLight = 0.0f,
		float maxLight = 1.0f, float radius = -1.0f );

	TerrorNavArea *ComputeFlowDistances(); // native2F2060 owns the real endpoint selection
	float GetMaxFlowDistance() const { return m_maxFlowDistance; }
	float GetMaxNonCheckpointFlowDistance() const { return m_maxNonCheckpointFlowDistance; }
	TerrorNavArea *GetStartArea() const { return m_startArea; }
	TerrorNavArea *GetGoalArea() const { return m_goalArea; }
	TerrorNavArea *GetRescueVehicleArea() const { return m_rescueVehicleArea; }
	void InitializeFlowDistances();
	void RecomputeFlowDistances();
	void ShowFlowErrors( CTerrorPlayer *onlyPlayer ) const;
	virtual void OnBreakableCreated( CBaseEntity *breakable ) OVERRIDE;
	virtual void OnBreakableBroken( CBaseEntity *broken ) OVERRIDE;
	// S:0x2F9F90 / 0x25C810. Supply the real base-mode + mutation result;
	// an mp_gamemode string comparison is not that predicate.
	void ApplySurvivorRescuePolicy( bool allowed );

private:
	friend class TerrorNavArea;
	bool PrepareSave();
	bool CheckNavFileWritable();
	CNavArea *CreateAutoBuiltArea( const Vector &corner, const Vector &otherCorner );
	void UpdateDebugSelectedArea();
	bool ComputeFlowDistance( TerrorNavArea *startArea, TerrorNavArea *goalArea, TerrorFlowDirection direction );
	void RelaxFlowDistance( TerrorNavArea *from, TerrorNavArea *to, float length, TerrorFlowDirection direction );
	void ClearCheckpoints();
	void BuildCheckpoints();
	void ApplyNavAttributeRegions();
	virtual void PostCustomAnalysis() OVERRIDE;
	virtual void RemoveNavArea( CNavArea *area ) OVERRIDE;
	TerrorNavArea *SelectFlowGoalArea() const;
	void SelectRescueVehicleArea( const CUtlVector< TerrorNavArea * > &rescueAreas );
	void InitializeBoundaryDistances();
	void UpdateBoundarySpread();
	void UpdateBreakableNavAttributes();
	void UpdateFlowRecomputeTimer();
	void ReportUnconnectedFlowAreas() const;

	unsigned int m_loadedSubVersion;
	NavErrorType m_loadError;
	bool m_visibilityNeedsRebuild;
	float m_maxFlowDistance;
	float m_maxNonCheckpointFlowDistance;
	CUtlVector< string_t > m_placeDirectoryNames;
	CUtlVector< Place > m_loadedPlaces;
	bool m_hasUnnamedPlaces;
	CUtlVector< string_t > m_populationDirectory;
	CUtlVector< CTerrorCheckpoint * > m_checkpoints;
	float m_earliestBattleStationFlow; // native1536
	TerrorNavArea *m_startArea; // native1540, written before each executed flow pass
	TerrorNavArea *m_rescueVehicleArea; // native1544, retained on early computation failure
	TerrorNavArea *m_goalArea; // native1548, retained on spawn failure
	bool m_hasFlowDistances; // native1552
	CountdownTimer m_boundarySpreadTimer; // native1556
	CUtlVector< TerrorNavArea * > m_boundarySeeds; // native1568
	int m_boundarySpreadIndex; // native1588
	CUtlVector< TerrorNavArea * > m_boundarySpreadAreas; // native1592
	CNavArea *m_debugSelectedArea; // Native1612; retained while z_debug is off.
	unsigned int m_selectedFogPopulation; // Native1636; one-based directory index.
	CUtlVector< EHANDLE > m_changedBreakables; // native1660
	CUtlVector< Extent > m_changedBreakableBounds; // native1680
	CountdownTimer m_navTimer1700; // Native1700, actual retained timer with compare readers.
	CountdownTimer m_wanderingSpawnTimer; // native1712
	CountdownTimer m_wanderingPopulationTimer; // native1724
	CountdownTimer m_flowRecomputeTimer; // native1736
	static CUtlVector< TerrorNavArea * > m_wanderingPopulationAreas;
	static int s_nextWanderingCharacter;
};

inline TerrorNavMesh *TheTerrorNavMesh()
{
	return static_cast< TerrorNavMesh * >( TheNavMesh );
}

#endif // TERROR_NAV_H
