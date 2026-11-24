#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <windows.h>
#include "shaderapi/ishaderapidx12lighting.h"
#include "tier1/utlvector.h"

// A cell is not a visibility answer: a wall can split even the smallest cell. Instead
// bound the entire pencil of segments joining this receiver box to the emitter. Mixed
// cells (including grazing faces) always use the normal, individually clipped ray.
namespace shaderapidx12
{
enum class ModelCellResult : char { Mixed, Clear, Blocked };
struct ModelRayInterval
{
	double lo, hi;
	void Clip( double a, double b ) // parameter range in which a + t * (b-a) >= 0
	{
		if ( a < 0.0 )
		{
			if ( b < 0.0 ) hi = -1.0;
			else lo = std::max( lo, a / ( a - b ) );
		}
		else if ( b < 0.0 ) hi = std::min( hi, a / ( a - b ) );
	}
};
inline ModelCellResult ClassifyModelLightCell( const CUtlVector<DX12ModelLightNode> &nodes, const Vector &center, const Vector &light )
{
	struct Part { int node; ModelRayInterval possible, certain; };
	CUtlVectorFixedGrowable<Part, 64> stack;
	stack.AddToTail( Part{0, {0,1}, {0,1}} );
	bool mixed = false;
	while ( stack.Count() )
	{
		const Part part = stack.Tail(); stack.RemoveMultipleFromTail(1);
		if ( part.node < 0 )
		{
			if ( part.node == -2 )
			{
				// At this t every ray is strictly inside the same solid leaf.
				if ( part.certain.lo < part.certain.hi ) return ModelCellResult::Blocked;
				mixed = true;
			}
			continue;
		}
		const auto &n = nodes[part.node];
		const auto distance = [&]( const Vector &p ) { return double(n.plane[0])*p.x + double(n.plane[1])*p.y + double(n.plane[2])*p.z - n.plane[3]; };
		const double a = distance(center), b = distance(light);
		// Support of an axis-aligned box on this plane. Both endpoints include headroom
		// for the 1/32-unit bias and floating-point BSP clipping at Source world scale.
		const double norm = fabs(n.plane[0]) + fabs(n.plane[1]) + fabs(n.plane[2]);
		const double r0 = 16.5 * norm, r1 = 0.5 * norm;
		for ( int side = 0; side < 2; ++side )
		{
			if ( n.children[side] == -1 ) continue;
			Part child = {n.children[side], part.possible, part.certain};
			const double sa = side ? -a : a, sb = side ? -b : b;
			child.possible.Clip(sa+r0,sb+r1);
			if ( child.possible.lo > child.possible.hi ) continue;
			child.certain.Clip(sa-r0,sb-r1);
			stack.AddToTail(child);
		}
	}
	return mixed ? ModelCellResult::Mixed : ModelCellResult::Clear;
}

// Recording-owner cache; workers write only the entry assigned to them. Neither
// draw nor submit waits for a cell. On a cold/busy/mixed cell use the ordinary ray.
class ModelLightCellsDX12
{
public:
	struct Cell
	{
		std::atomic<bool> ready{true};
		bool valid = false;
		Vector center;
		std::vector<ModelCellResult> result;
	};
	~ModelLightCellsDX12()
	{
		Clear();
		for ( auto &lane : m_Lanes ) if ( lane.work ) CloseThreadpoolWork(lane.work);
	}
	// Call before replacing either borrowed map span, including same-generation readmission.
	void Clear()
	{
		for ( auto &lane : m_Lanes )
			if ( lane.work ) WaitForThreadpoolWorkCallbacks(lane.work,FALSE);
		for ( auto &cell : m_Cells ) cell.valid = false;
	}
	const Cell *Request( const Vector &origin, const CUtlVector<DX12ModelLightNode> &nodes,
		const CUtlVector<DX12LightingSelectedLight> &lights )
	{
		if ( !nodes.Count() || !lights.Count() || !(fabsf(origin.x)<32768 && fabsf(origin.y)<32768 && fabsf(origin.z)<32768) ) return nullptr;
		const int x = int(floorf(origin.x/32)), y = int(floorf(origin.y/32)), z = int(floorf(origin.z/32));
		const Vector center( x*32+16, y*32+16, z*32+16 );
		const uint32 hash = uint32(x)*73856093u ^ uint32(y)*19349663u ^ uint32(z)*83492791u;
		Cell &cell = m_Cells[hash % m_Cells.size()];
		if ( !cell.ready.load(std::memory_order_acquire) ) return nullptr;
		if ( cell.valid && center == cell.center ) return &cell;
		for ( auto &lane : m_Lanes )
		{
			if ( lane.busy.load(std::memory_order_acquire) ) continue;
			if ( !lane.work ) lane.work = CreateThreadpoolWork(Build,&lane,nullptr);
			if ( !lane.work ) return nullptr;
			cell.center = center; cell.valid = true;
			// Bound both storage and cold work, even on very large editor light tables.
			cell.result.resize(std::min(lights.Count(),256));
			cell.ready = false;
			lane.cell = &cell; lane.nodes = &nodes; lane.lights = &lights;
			lane.busy = true;
			SubmitThreadpoolWork(lane.work);
			break;
		}
		return nullptr;
	}
private:
	struct Lane
	{
		std::atomic<bool> busy{false};
		PTP_WORK work = nullptr;
		Cell *cell = nullptr;
		const CUtlVector<DX12ModelLightNode> *nodes = nullptr;
		const CUtlVector<DX12LightingSelectedLight> *lights = nullptr;
	};
	static void CALLBACK Build( PTP_CALLBACK_INSTANCE, void *context, PTP_WORK )
	{
		Lane &lane = *static_cast<Lane *>(context);
		Cell &cell = *lane.cell;
		for ( size_t i=0; i<cell.result.size(); ++i )
		{
			const auto &light = (*lane.lights)[int(i)];
			const auto &p = light.origin;
			// Don't spend a BSP walk certifying an emitter outside this cell's
			// attenuation/fade reach. Mixed is not a visibility answer: a view
			// that supplies a longer reach still traces the exact segment.
			float reach = light.attenuationRadius;
			if ( light.endFade > light.startFade && ( reach <= 0 || light.endFade < reach ) )
				reach = light.endFade;
			if ( reach > 0 && ( fabsf(p[0]-cell.center.x) > reach+16.5f ||
				fabsf(p[1]-cell.center.y) > reach+16.5f || fabsf(p[2]-cell.center.z) > reach+16.5f ) )
			{
				cell.result[i] = ModelCellResult::Mixed;
				continue;
			}
			cell.result[i] = ClassifyModelLightCell(*lane.nodes,cell.center,Vector(p[0],p[1],p[2]));
		}
		cell.ready.store(true,std::memory_order_release);
		lane.busy.store(false,std::memory_order_release);
	}
	std::array<Cell,1024> m_Cells;
	Lane m_Lanes[2];
};
}
