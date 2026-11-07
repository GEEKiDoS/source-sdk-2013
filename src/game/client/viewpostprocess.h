//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================

#ifndef VIEWPOSTPROCESS_H
#define VIEWPOSTPROCESS_H

#if defined( _WIN32 )
#pragma once
#endif

void DoEnginePostProcessing( int x, int y, int w, int h, bool bFlashlightIsOn, bool bPostVGui = false );
void DoImageSpaceMotionBlur( const CViewSetup &view, int x, int y, int w, int h );
void DumpTGAofRenderTarget( const int width, const int height, const char *pFilename );

#if defined( L4D2 )
class IMatRenderContext;
void Terror_SetEnginePostFade( const byte *rgba, bool modulate );
void Terror_UpdateTonemapController();
void Terror_ApplyToneMapping( IMatRenderContext *context );
float Terror_GetCurrentExposure();
void Terror_SetExposureOverride( bool enabled, float value );
void Terror_RenderTransitionBlur( float alpha, float desaturation );
#endif

#endif // VIEWPOSTPROCESS_H
