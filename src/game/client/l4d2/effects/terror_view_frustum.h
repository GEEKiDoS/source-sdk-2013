#ifndef TERROR_VIEW_FRUSTUM_H
#define TERROR_VIEW_FRUSTUM_H
#ifdef _WIN32
#pragma once
#endif
class CViewSetup;
struct GameFrustumPlane_t { Vector normal; float distance; };
struct GameViewFrustum_t
{
	int m_nFrame;
	GameFrustumPlane_t m_Planes[6];
	bool CullBox( const Vector &mins, const Vector &maxs ) const;
};
// Native C1C9A20 always returns the one static cache, including stale/frame0.
const GameViewFrustum_t &GetGameViewFrustum();
void UpdateGameViewFrustum( const CViewSetup &view );
#endif
