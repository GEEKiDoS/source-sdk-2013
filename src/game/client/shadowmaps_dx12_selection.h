//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SHADOWMAPS_DX12_SELECTION_H
#define SHADOWMAPS_DX12_SELECTION_H
#include "bspfile.h"
#include "shadowmap_bsp.h"
#include "tier1/utlvector.h"

// CPU-only selection policy. All scratch storage is allocated at map admission.
// The BSP loader owns disk-format/version handling; this class owns validation,
// uncapped leaf walks and Source visibility RLE (never render-view APIs).
class CShadowCameraPvs
{
public:
	CShadowCameraPvs() : clusters(0), rowBytes(0), cameraAllVisible(true), cachedCluster(-2), visitGeneration(0) {}
	CUtlVector<dplane_t> planes;
	CUtlVector<dnode_t> nodes;
	CUtlVector<short> leafClusters;
	CUtlVector<unsigned char> visibility;
	int clusters, rowBytes;

	void Clear()
	{
		planes.Purge(); nodes.Purge(); leafClusters.Purge(); visibility.Purge(); offsets.Purge();
		cameraRow.Purge(); decodedRow.Purge(); cameraClusters.Purge(); cachedClusters.Purge();
		stack.Purge(); visits.Purge(); clusters= rowBytes=0; cachedCluster=-2;
		cameraAllVisible=true; visitGeneration=0;
	}
	static bool DecodeRow( const unsigned char *data, int bytes, int offset, unsigned char *out, int count )
	{
		if ( offset<0 || offset>=bytes || count<=0 ) return false;
		int written=0;
		while ( written<count )
		{
			if ( offset>=bytes ) return false;
			const unsigned char value=data[offset++];
			if ( value ) out[written++]=value;
			else
			{
				if ( offset>=bytes ) return false;
				const int run=data[offset++];
				if ( !run || run>count-written ) return false;
				memset(out+written,0,run); written+=run;
			}
		}
		return true;
	}
	bool Validate()
	{
		if ( !planes.Count() || !nodes.Count() || !leafClusters.Count() ||
			planes.Count()>MAX_MAP_PLANES || nodes.Count()>MAX_MAP_NODES || leafClusters.Count()>MAX_MAP_LEAFS ||
			visibility.Count()>MAX_MAP_VISIBILITY ) return false;
		for ( int i=0;i<planes.Count();++i )
		{
			const float normalLength=planes[i].normal.LengthSqr();
			if ( !planes[i].normal.IsValid() || !ShadowMap_IsFiniteFloat(planes[i].dist) ||
				!ShadowMap_IsFiniteFloat(normalLength) || normalLength<0.99f || normalLength>1.01f ) return false;
		}
		// Kahn's algorithm checks every node, including disconnected submodel trees.
		// Iterative walks below also mark nodes, bounding work even for shared DAGs.
		visits.SetCount(nodes.Count()); memset(visits.Base(),0,visits.Count()*sizeof(unsigned int));
		stack.EnsureCapacity(nodes.Count()); stack.RemoveAll();
		for ( int i=0;i<nodes.Count();++i )
		{
			if ( nodes[i].planenum<0 || nodes[i].planenum>=planes.Count() ) return false;
			for ( int side=0;side<2;++side )
			{
				const int child=nodes[i].children[side];
				if ( child>=0 ) { if ( child>=nodes.Count() ) return false; ++visits[child]; }
				else if ( -1-(int64)child>=leafClusters.Count() ) return false;
			}
		}
		for ( int i=0;i<nodes.Count();++i ) if ( !visits[i] ) stack.AddToTail(i);
		int processed=0;
		while ( stack.Count() )
		{
			const int node=stack.Tail(); stack.Remove(stack.Count()-1); ++processed;
			for ( int side=0;side<2;++side )
			{
				const int child=nodes[node].children[side];
				if ( child>=0 && !--visits[child] ) stack.AddToTail(child);
			}
		}
		if ( processed!=nodes.Count() ) return false;
		memset(visits.Base(),0,visits.Count()*sizeof(unsigned int)); visitGeneration=0;
		if ( !visibility.Count() )
		{
			// Standard no-vis maps are all visible, not malformed PVS.
			clusters=0; rowBytes=0; cameraAllVisible=true; return true;
		}
		if ( visibility.Count()<4 ) return false;
		memcpy(&clusters,visibility.Base(),4);
		if ( clusters<=0 || clusters>MAX_MAP_CLUSTERS || (int64)4+(int64)clusters*8>visibility.Count() ) return false;
		rowBytes=(clusters+7)/8;
		offsets.SetCount(clusters); decodedRow.SetCount(rowBytes); cameraRow.SetCount(rowBytes);
		cameraClusters.SetCount(rowBytes); cachedClusters.SetCount(rowBytes);
		memset(cachedClusters.Base(),0,rowBytes); cachedCluster=-2;
		for ( int leaf=0;leaf<leafClusters.Count();++leaf )
			if ( leafClusters[leaf]<-1 || leafClusters[leaf]>=clusters ) return false;
		for ( int i=0;i<clusters;++i )
		{
			memcpy(&offsets[i],visibility.Base()+4+i*8,4);
			if ( offsets[i]<4+clusters*8 ||
				!DecodeRow(visibility.Base(),visibility.Count(),offsets[i],decodedRow.Base(),rowBytes) ) return false;
		}
		return true;
	}
	void GatherBox( const Vector &mins, const Vector &maxs, unsigned char *bits )
	{
		memset(bits,0,rowBytes);
		if ( !++visitGeneration ) { memset(visits.Base(),0,visits.Count()*sizeof(unsigned int)); ++visitGeneration; }
		stack.RemoveAll(); stack.AddToTail(0); visits[0]=visitGeneration;
		const Vector center=(mins+maxs)*0.5f, half=(maxs-mins)*0.5f;
		while ( stack.Count() )
		{
			const int node=stack.Tail(); stack.Remove(stack.Count()-1);
			const dplane_t &plane=planes[nodes[node].planenum];
			const float distance=DotProduct(center,plane.normal)-plane.dist;
			const float extent=fabsf(plane.normal.x)*half.x+fabsf(plane.normal.y)*half.y+fabsf(plane.normal.z)*half.z;
			for ( int side=0;side<2;++side )
			{
				if ( side ? distance>extent : distance<-extent ) continue;
				const int child=nodes[node].children[side];
				if ( child>=0 )
				{
					if ( visits[child]!=visitGeneration ) { visits[child]=visitGeneration; stack.AddToTail(child); }
				}
				else SetCluster(bits,leafClusters[-1-child]);
			}
		}
	}
	void BuildLightClusters( const Vector &origin, int authoredCluster, const Vector &mins, const Vector &maxs, bool finite, CUtlVector<unsigned char> &out )
	{
		out.SetCount(rowBytes);
		if ( !rowBytes ) return;
		const Vector pad(16,16,16);
		GatherBox(finite ? mins : origin-pad,finite ? maxs : origin+pad,out.Base());
		SetCluster(out.Base(),authoredCluster);
	}
	void UpdateCamera( const Vector &origin )
	{
		if ( !rowBytes || !origin.IsValid() ) { cameraAllVisible=true; cachedCluster=-2; return; }
		int node=0; bool boundary=false;
		while ( node>=0 )
		{
			const dplane_t &plane=planes[nodes[node].planenum];
			const float distance=DotProduct(origin,plane.normal)-plane.dist;
			boundary=boundary || fabsf(distance)<=0.03125f;
			node=nodes[node].children[distance>=0 ? 0 : 1];
		}
		const int cluster=leafClusters[-1-node];
		if ( cluster>=0 && !boundary )
		{
			if ( cachedCluster!=cluster ) DecodeRow(visibility.Base(),visibility.Count(),offsets[cluster],cameraRow.Base(),rowBytes);
			cachedCluster=cluster; cameraAllVisible=false; return;
		}
		const float padding=cluster<0 ? 16.0f : 1.0f;
		const Vector pad(padding,padding,padding);
		GatherBox(origin-pad,origin+pad,cameraClusters.Base());
		if ( cachedCluster==-1 && !memcmp(cameraClusters.Base(),cachedClusters.Base(),rowBytes) ) return;
		memcpy(cachedClusters.Base(),cameraClusters.Base(),rowBytes); cachedCluster=-1;
		memset(cameraRow.Base(),0,rowBytes); cameraAllVisible=true;
		for ( int c=0;c<clusters;++c ) if ( cameraClusters[c/8]&(1u<<(c&7)) )
		{
			DecodeRow(visibility.Base(),visibility.Count(),offsets[c],decodedRow.Base(),rowBytes);
			for ( int b=0;b<rowBytes;++b ) cameraRow[b]|=decodedRow[b];
			cameraAllVisible=false;
		}
	}
	bool Eligible( const CUtlVector<unsigned char> &lightClusters ) const
	{
		if ( cameraAllVisible || !rowBytes ) return true;
		bool constrained=false;
		for ( int b=0;b<rowBytes;++b )
		{
			constrained=constrained || lightClusters[b]!=0;
			if ( lightClusters[b]&cameraRow[b] ) return true;
		}
		// Fully embedded emitters have no meaningful cluster, never silently drop them.
		return !constrained;
	}
private:
	void SetCluster( unsigned char *bits, int cluster ) const
	{ if ( cluster>=0 && cluster<clusters ) bits[cluster/8]|=(1u<<(cluster&7)); }
	CUtlVector<int> offsets, stack;
	CUtlVector<unsigned int> visits;
	CUtlVector<unsigned char> cameraRow, decodedRow, cameraClusters, cachedClusters;
	bool cameraAllVisible;
	int cachedCluster;
	unsigned int visitGeneration;
};

inline float ShadowLocalProximityScore( const Vector &camera, const Vector &emitter, bool eligible, float styledRadiance )
{
	if ( !eligible || !(styledRadiance>0) ) return 0;
	const float distance=(camera-emitter).Length();
	return ShadowMap_IsFiniteFloat(distance) ? 1.0f/MAX(distance,1.0f) : 0;
}

template<class Locals, class Ranks>
int ShadowHybridChoose( Locals &locals, const Ranks &ranks, int limit, bool instant )
{
	for ( int i=0;i<locals.Count();++i ) locals[i].desired=false;
	int selected=0;
	for ( int r=0;!instant && r<ranks.Count() && selected<limit;++r )
	{
		auto &local=locals[ranks[r].relevantIndex];
		if ( local.resident && local.score>0 ) { local.desired=true; ++selected; }
	}
	for ( int r=0;r<ranks.Count() && selected<limit;++r )
	{
		auto &local=locals[ranks[r].relevantIndex];
		if ( !local.desired && local.score>0 ) { local.desired=true; ++selected; }
	}
	for ( int r=0;!instant && r<ranks.Count();++r )
	{
		auto &challenger=locals[ranks[r].relevantIndex];
		if ( challenger.desired || challenger.score<=0 ) continue;
		int weakest=-1;
		for ( int w=ranks.Count()-1;w>=0;--w )
			if ( locals[ranks[w].relevantIndex].desired ) { weakest=ranks[w].relevantIndex; break; }
		if ( weakest<0 || !(challenger.score>locals[weakest].score*1.25f) ) break;
		locals[weakest].desired=false; challenger.desired=true;
	}
	return selected;
}

// A free slot carries only the unspent part of this frame. Thus a frame which
// crosses fade-out completion can begin fade-in without charging delta twice.
template<class Locals, class Ranks>
void ShadowHybridRetire( Locals &locals, const Ranks &ranks, int limit, bool instant, float halfSeconds, float delta,
	CUtlVector<float> &freeSlotSeconds, uint32 &demotions )
{
	int residents=0; freeSlotSeconds.RemoveAll();
	for ( int i=0;i<locals.Count();++i )
	{
		auto &local=locals[i];
		if ( instant && local.resident && !local.desired ) { local.resident=false; ++demotions; }
		if ( instant ) local.realtimeWeight=local.resident ? 1.0f : 0.0f;
		if ( local.resident ) ++residents;
	}
	for ( int r=ranks.Count()-1;r>=0 && residents>limit;--r )
	{
		auto &local=locals[ranks[r].relevantIndex];
		if ( !local.resident ) continue;
		local.resident=false; local.realtimeWeight=0; --residents; ++demotions;
	}
	for ( int slot=residents;slot<limit;++slot ) freeSlotSeconds.AddToTail(delta);
	for ( int i=0;i<locals.Count();++i )
	{
		auto &local=locals[i];
		if ( !local.resident ) continue;
		if ( local.desired ) local.realtimeWeight=halfSeconds>0 ? MIN(1.0f,local.realtimeWeight+delta/halfSeconds) : 1.0f;
		else
		{
			const float remaining=local.realtimeWeight*halfSeconds;
			local.realtimeWeight=halfSeconds>0 ? MAX(0.0f,local.realtimeWeight-delta/halfSeconds) : 0;
			if ( local.realtimeWeight==0 )
			{
				local.resident=false; ++demotions;
				freeSlotSeconds.AddToTail(MAX(0.0f,delta-remaining));
			}
		}
	}
}

template<class Locals, class Ranks>
void ShadowHybridPromote( Locals &locals, const Ranks &ranks, bool instant, float halfSeconds,
	const CUtlVector<float> &freeSlotSeconds, uint32 &promotions )
{
	int slot=0;
	for ( int r=0;r<ranks.Count() && slot<freeSlotSeconds.Count();++r )
	{
		auto &local=locals[ranks[r].relevantIndex];
		if ( !local.desired || local.resident ) continue;
		local.resident=true;
		local.realtimeWeight=instant || halfSeconds<=0 ? 1.0f : MIN(1.0f,freeSlotSeconds[slot]/halfSeconds);
		++slot; ++promotions;
	}
}
#endif
