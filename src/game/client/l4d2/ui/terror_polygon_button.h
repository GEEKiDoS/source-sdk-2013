// Copyright (c) 2006 Turtle Rock Studios, Inc.
// Public Alien Swarm SDK ancestry: swarm/vgui/polygonbutton.h (SDK license).
// Mirror: ReactiveDrop/reactivedrop_public_src, commit
// 9231d40422194dfecca8b698698df4af7e0d99f1, src/game/client/swarm/vgui.
#ifndef TERROR_POLYGON_BUTTON_H
#define TERROR_POLYGON_BUTTON_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/Button.h"
#include "vgui/ISurface.h"
#include "tier1/utlvector.h"
class CPolygonButton : public vgui::Button
{
 DECLARE_CLASS_SIMPLE(CPolygonButton,vgui::Button);
public:
 CPolygonButton(vgui::Panel *parent,const char *name);
 virtual void ApplySettings(KeyValues *data) OVERRIDE;
 virtual vgui::VPANEL IsWithinTraverse(int x,int y,bool traversePopups) OVERRIDE;
 virtual void PerformLayout() OVERRIDE;
 virtual void ComputeAlignment(int &tx0,int &ty0,int &tx1,int &ty1) OVERRIDE;
 virtual void PaintBackground() OVERRIDE;
 virtual void PaintBorder() OVERRIDE;
 virtual void ApplySchemeSettings(vgui::IScheme *scheme) OVERRIDE;
 virtual void UpdateHotspots(KeyValues *data);
protected:
 int m_nWhiteMaterial;
 CUtlVector<Vector2D> m_unscaledHotspotPoints,m_unscaledVisibleHotspotPoints;
 vgui::Vertex_t *m_hotspotPoints;
 int m_numHotspotPoints;
 vgui::Vertex_t *m_visibleHotspotPoints;
 int m_numVisibleHotspotPoints;
 Vector2D m_hotspotMins,m_hotspotMaxs;
};
#endif
