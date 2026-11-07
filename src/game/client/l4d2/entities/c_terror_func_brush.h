#ifndef C_TERROR_FUNC_BRUSH_H
#define C_TERROR_FUNC_BRUSH_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
class C_FuncBrush : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_FuncBrush, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C7A350, native primary17.
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
};
#endif
