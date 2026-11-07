#ifndef TERROR_DEPTH_OF_FIELD_H
#define TERROR_DEPTH_OF_FIELD_H
#ifdef _WIN32
#pragma once
#endif
class CViewSetup;
struct TerrorDOFState_t
{
	bool enabled;
	float nearBlurDepth, nearFocusDepth, farFocusDepth, farBlurDepth;
	float nearBlurRadius, farBlurRadius;
};
extern TerrorDOFState_t g_TerrorDOFState;
bool Terror_IsDepthOfFieldEnabled( const CViewSetup *view );
void Terror_DrawDepthOfField( const CViewSetup &view );
void Terror_ClearSkyboxDepthAlpha( const CViewSetup *view );
#endif
