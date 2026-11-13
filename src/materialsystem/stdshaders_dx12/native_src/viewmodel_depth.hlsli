// The first-person viewmodels are drawn through the front of the depth range (CViewRender, DepthRange( 0, 0.1 )), and
// their opaque depth is in the scene depth before the screen-space effects run (the viewmodel depth prepass,
// r_viewmodel_depth_prepass). That compressed depth is not a position in the scene, so the effects read native depth
// below this bound as "no geometry". World geometry lands below it only within 1.11 times the near plane.
#ifndef VIEWMODEL_DEPTH_HLSLI
#define VIEWMODEL_DEPTH_HLSLI
static const float DX12_VIEWMODEL_DEPTH_MAX = 0.1;
#endif
